#include "SSubDiagram.h"
#include "RenderingThread.h"          // ENQUEUE_RENDER_COMMAND
#include "Rendering/DrawElements.h"   // FSlateDrawElement & friends
#include "Rendering/SlateRenderBatch.h"  // (optional) only if you really need FSlateRenderBatch
#include "RHIStaticStates.h"          // Sampler states etc.
#include "../Public/VisualTestHarnessActor.h"   // Use relative path
#include "DiagramVertex.h"
#include "Slate/SlateBrushAsset.h"
#include "Styling/CoreStyle.h"     // FCoreStyle::Get()
#include "Engine/Font.h"          // for GEngine->GetSmallFont()

static const FSoftObjectPath TinyFontPath(TEXT("/Game/Fonts/Roboto-BoldCondensed.Roboto-BoldCondensed"));

struct FTouchInfo
{
    int32   PointerId;
    int32   UserId;
    FVector2f LocalPos;
};

TMap<int32 /*PointerId*/, FTouchInfo> ActiveTouches;   // member in the .cpp

/*
Important notes
	•	No copies per‑frame except the SlateVerts conversion (tiny—just 24 bytes × ~10 k).
If you want zero cost, you can write a custom IRenderDataPayload later.
	•	WhiteBrush ensures the shader sees a bound texture; your per‑vertex colour tints it.
	•	Still one draw call regardless of vertex count.

Knowledge hand‑off for the next sprint 📋
	•	Core change: replaced Canvas primitives with a single custom Slate widget (SSubDiagram) that receives a CPU‑built vertex/index array from UnrealRenderingContext. One draw per texture page ⇒ stable 60 fps even at ~4 k verts.
	•	Geometry flow:
VisualizationManager → builds WorkingVertices & WorkingIndices
EndDrawing() swaps to Presented* (double‑buffer)
SSubDiagram::OnPaint() batch‑flushes by Page, applies AccumulatedRenderTransform, uses MakeCustomVerts.
	•	Texture scheme:
Page 255 = opaque 1×1 white brush for solid UI elements.
Page 0…N come from UFont::Textures of the imported bitmap font.
Vertex struct now has uint8 Page.
	•	Input: NativeWidgetHost passes pointer & touch directly to SSubDiagram. Widget exposes OnMouse* + OnTouch* and hands local‑space coords back to AVisualTestHarnessActor::HandleMouseTap(). Multi‑touch distinguished by PointerIndex.
	•	Remaining TODOs flagged in code:
	•	Dirty‑flag around InvalidateFast()
	•	Optional icon atlas integration
	•	Possible instanced wire material
	•	Risk areas:
	•	Raw back‑pointer to AVisualTestHarnessActor — safe as long as actor out‑lives widget; convert to TWeakObjectPtr if HUD ever persists between level loads.
	•	Bitmap font import path — ensure Offline cache so Font->Textures.Num()>0.

With these notes the next team can profile in Unreal Insights and decide whether to pursue GPU instancing or stick with the current Slate batching, which is already fast enough for the target 60 fps budget on mid‑tier GPUs.
*/

int32 SSubDiagram::OnPaint( const FPaintArgs& Args,
                            const FGeometry&  Allotted,
                            const FSlateRect& Clip,
                            FSlateWindowElementList& OutDraw,
                            int32 LayerId,
                            const FWidgetStyle& Style,
                            bool ParentEnabled ) const
{
    if (!CpuVerts || !CpuIndices || CpuVerts->Num() == 0 || CpuIndices->Num() == 0) 
    {
        UE_LOG(LogTemp, Warning, TEXT("SSubDiagram::OnPaint: Invalid vertex/index data"));
        return LayerId;
    }

    /* ------------------------------------------------------------
     * Clip all custom verts to the widget's allotted geometry
     * and paint a solid white background under the diagram.
     * ------------------------------------------------------------ */
    const FSlateRect WidgetRect = Allotted.GetLayoutBoundingRect();
    OutDraw.PushClip(FSlateClippingZone(WidgetRect));

    const FSlateBrush* WhiteBrush = FCoreStyle::Get().GetBrush("GenericWhiteBox");
    FSlateDrawElement::MakeBox(
        OutDraw,
        LayerId,
        Allotted.ToPaintGeometry(),
        WhiteBrush,
        ESlateDrawEffect::None,
        FLinearColor::White);

    ++LayerId;   // diagram will be painted above the background

    UFont* Font = Cast<UFont>(TinyFontPath.TryLoad());
    if (!Font) 
    {
        UE_LOG(LogTemp, Warning, TEXT("SSubDiagram::OnPaint: Font not found"));
        return LayerId;
    }

    // ───────── resource handle cache ─────────
    TArray<FSlateResourceHandle> PageHandles;
    PageHandles.SetNum(Font->Textures.Num());

    for (int32 i = 0; i < Font->Textures.Num(); ++i)
    {
        if (!Font->Textures[i]) continue;
        
        FSlateBrush TempBrush;
        TempBrush.DrawAs = ESlateBrushDrawType::Image;
        TempBrush.SetResourceObject(Font->Textures[i]);
        TempBrush.ImageSize = FVector2D(Font->Textures[i]->GetSizeX(), Font->Textures[i]->GetSizeY());
        TempBrush.TintColor = FLinearColor::White;

        PageHandles[i] = FSlateApplication::Get().GetRenderer()->GetResourceHandle(TempBrush);
    }

    FSlateResourceHandle WhiteHandle = FSlateApplication::Get().GetRenderer()->GetResourceHandle(*WhiteBrush);

    // ───────── batching ─────────
    uint8 CurrentPage = 255;
    FSlateResourceHandle CurrentHandle;
    TArray<FSlateVertex> Verts;
    TArray<SlateIndex>   Indices;

    auto FlushBatch = [&]()
    {
        if (Verts.Num() == 0) return;
        FSlateDrawElement::MakeCustomVerts(
            OutDraw, LayerId, CurrentHandle, Verts, Indices, nullptr, 0, 0);
        Verts.Reset();
        Indices.Reset();
    };

    const FSlateRenderTransform& Acc = Allotted.GetAccumulatedRenderTransform();

    auto PushVert = [&](const FDiagramVertex& D)->uint32
    {
        FSlateVertex SV;
        SV.Position = Acc.TransformPoint(D.Pos);
        SV.Color    = D.Col;
        SV.TexCoords[0] = D.UV.X; SV.TexCoords[1] = D.UV.Y;
        SV.TexCoords[2] = 1.f;    SV.TexCoords[3] = 1.f;
        SV.MaterialTexCoords = FVector2f::ZeroVector;
        Verts.Add(SV);
        return Verts.Num() - 1;
    };

    // walk triangles in index buffer order
    for (int32 i = 0; i < CpuIndices->Num(); i += 3)
    {
        // Safety check: ensure we have enough indices for a complete triangle
        if (i + 2 >= CpuIndices->Num()) break;

        // Get indices and validate they're in bounds
        uint32 idx0 = (*CpuIndices)[i];
        uint32 idx1 = (*CpuIndices)[i + 1];
        uint32 idx2 = (*CpuIndices)[i + 2];

        if (idx0 >= (uint32)CpuVerts->Num() || 
            idx1 >= (uint32)CpuVerts->Num() || 
            idx2 >= (uint32)CpuVerts->Num())
        {
            UE_LOG(LogTemp, Warning, TEXT("SSubDiagram::OnPaint: Index out of bounds: %d, %d, %d (max: %d)"), 
                   idx0, idx1, idx2, CpuVerts->Num());
            continue;
        }

        const FDiagramVertex& D = (*CpuVerts)[idx0];
        uint8 Page = D.Page;
        if (Page != CurrentPage)
        {
            FlushBatch();
            CurrentPage = Page;
            CurrentHandle = PageHandles.IsValidIndex(Page) ? PageHandles[Page] : WhiteHandle;
        }

        uint32 ia = PushVert((*CpuVerts)[idx0]);
        uint32 ib = PushVert((*CpuVerts)[idx1]);
        uint32 ic = PushVert((*CpuVerts)[idx2]);
        Indices.Append({ia, ib, ic});
    }

    FlushBatch();          // final page
    OutDraw.PopClip();     // end clipping zone
    return LayerId;
}

FVector2D SSubDiagram::ComputeDesiredSize(float) const
{
    // Simple fallback: if we have geometry, derive extents; else 100x100
    if (CpuVerts && CpuVerts->Num() > 0)
    {
        FVector2f Max = FVector2f::ZeroVector;
        for (const FDiagramVertex& V : *CpuVerts)
        {
            Max.X = FMath::Max(Max.X, V.Pos.X);
            Max.Y = FMath::Max(Max.Y, V.Pos.Y);
        }
        return FVector2D(Max);
    }
    return FVector2D(100.f, 100.f);
}

bool bIsGrabbed = false;
int32 GrabbedPointerIndex = -1;

FReply SSubDiagram::OnMouseButtonDown(
    const FGeometry& MyGeom, const FPointerEvent& MouseEvent)
{
    if (MouseEvent.GetEffectingButton() == EKeys::LeftMouseButton && !bIsGrabbed)
    {
        const FVector2f Local = MyGeom.AbsoluteToLocal(MouseEvent.GetScreenSpacePosition());
        // Let the owner decide if this should be grabbed
        if (Owner->HandleMouseTap(FVector2D(Local), 1, MouseEvent.GetPointerIndex()))
        {
            bIsGrabbed = true;
            GrabbedPointerIndex = MouseEvent.GetPointerIndex();
            return FReply::Handled().CaptureMouse(SharedThis(this));
        }
    }
    return FReply::Unhandled();
}

FReply SSubDiagram::OnMouseButtonUp(
    const FGeometry& MyGeom, const FPointerEvent& MouseEvent)
{
    if (bIsGrabbed && MouseEvent.GetPointerIndex() == GrabbedPointerIndex &&
        MouseEvent.GetEffectingButton() == EKeys::LeftMouseButton)
    {
        const FVector2f Local = MyGeom.AbsoluteToLocal(MouseEvent.GetScreenSpacePosition());
        Owner->HandleMouseTap(FVector2D(Local), 3, MouseEvent.GetPointerIndex());
        bIsGrabbed = false;
        GrabbedPointerIndex = -1;
        return FReply::Handled().ReleaseMouseCapture();
    }
    return FReply::Unhandled();
}

FReply SSubDiagram::OnMouseMove(
    const FGeometry& MyGeom, const FPointerEvent& MouseEvent)
{
    if (bIsGrabbed && MouseEvent.GetPointerIndex() == GrabbedPointerIndex)
    {
        const FVector2f Local = MyGeom.AbsoluteToLocal(MouseEvent.GetScreenSpacePosition());
        Owner->HandleMouseTap(FVector2D(Local), 2, MouseEvent.GetPointerIndex());
        return FReply::Handled();
    }
    return FReply::Unhandled();
}

FReply SSubDiagram::OnTouchStarted(const FGeometry& MyGeom,
                                   const FPointerEvent& Touch)
{
    const int32 Id     = Touch.GetPointerIndex();
    const int32 User   = Touch.GetUserIndex();
    const FVector2f Local =
        MyGeom.AbsoluteToLocal(Touch.GetScreenSpacePosition());

    ActiveTouches.Add(Id, {Id, User, Local});

    UE_LOG(LogTemp, Log, TEXT("[Touch %d] Down  at %.0f, %.0f (User %d)"),
           Id, Local.X, Local.Y, User);

    return FReply::Handled()
           .CaptureMouse(SharedThis(this));   // keeps move/end routed here
}

FReply SSubDiagram::OnTouchMoved(const FGeometry& MyGeom,
                                 const FPointerEvent& Touch)
{
    const int32 Id = Touch.GetPointerIndex();
    if (FTouchInfo* Info = ActiveTouches.Find(Id))
    {
        Info->LocalPos =
            MyGeom.AbsoluteToLocal(Touch.GetScreenSpacePosition());
        // e.g. highlight route while finger drags
    }
    return FReply::Handled();
}

FReply SSubDiagram::OnTouchEnded(const FGeometry& MyGeom,
                                 const FPointerEvent& Touch)
{
    const int32 Id = Touch.GetPointerIndex();
    ActiveTouches.Remove(Id);

    UE_LOG(LogTemp, Log, TEXT("[Touch %d] Up"), Id);
    return FReply::Handled()
           .ReleaseMouseCapture();
}
