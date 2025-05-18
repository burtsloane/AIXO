#include "UnrealRenderingContext.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Kismet/KismetRenderingLibrary.h"
#include "Engine/Canvas.h" // Included via header, but good practice
#include "Engine/Engine.h" // For GEngine access (Font)
#include "Engine/Texture2D.h"
#include "Engine/Font.h"
#include "CanvasItem.h" // Include for FCanvas definitions
#include "CanvasTypes.h" // Include for FCanvas::StaticWhiteTexture
#include "RHI.h"
#include "RenderResource.h"

FVector2f WhiteUV(0.5f, 0.5f);
static const FSoftObjectPath TinyFontPath(TEXT("/Game/Fonts/Roboto-BoldCondensed.Roboto-BoldCondensed"));

UnrealRenderingContext::UnrealRenderingContext(UWorld* InWorld, UTextureRenderTarget2D* InRenderTarget, UTexture2D* InWhiteSquareTexture)
    : RenderTarget(InRenderTarget)
    , World(InWorld)
    , SolidColorTexture(InWhiteSquareTexture)
    , VertexCount(0)
{
    // Initialize transform stack with identity transform
    FTransform2D IdentityTransform(1.0f, FVector2D::ZeroVector);  // Scale=1, Translation=0
    TransformStack.Add(IdentityTransform);
    CurrentTransform = IdentityTransform;

    if (RenderTarget)
    {
        CanvasSize = FVector2D(RenderTarget->SizeX, RenderTarget->SizeY);
    }
}

void UnrealRenderingContext::ResetVertexCount()
{
    VertexCount = 0;
}

void UnrealRenderingContext::IncrementVertexCount(int32 Amount)
{
    VertexCount += Amount;
}

void UnrealRenderingContext::ClearBuffers()
{
    // Only clear working buffers, not presented buffers
    WorkingVertices.Reset();
    WorkingIndices.Reset();
}

uint32 UnrealRenderingContext::PushVertex(const FVector2D& Pos,
                                          const FVector2f& UV,
                                          const FColor&    Col,
                                          uint8 Page)
{
    uint32 Index = WorkingVertices.Num();
    WorkingVertices.Add({ FVector2f(Pos), UV, Col, Page });
    return Index;
}

UnrealRenderingContext::~UnrealRenderingContext()
{
    // Ensure EndDrawing was called if BeginDrawing was successful
    if (Canvas)
    {
        EndDrawing(); 
    }
}

bool UnrealRenderingContext::BeginDrawing()
{
    // Ensure we start with clean working buffers
    WorkingVertices.Reset();
    WorkingIndices.Reset();
    ResetVertexCount();
    return true;
}

void UnrealRenderingContext::EndDrawing()
{
    // Copy the working buffers to presented buffers instead of moving
    PresentedVertices = WorkingVertices;
    PresentedIndices = WorkingIndices;

    // Clear working buffers for next frame
    WorkingVertices.Reset();
    WorkingIndices.Reset();

//    UE_LOG(LogTemp, Log, TEXT("UnrealRenderingContext::EndDrawing: %d vertices, %d indices (presented)"), PresentedVertices.Num(), PresentedIndices.Num());
}

void UnrealRenderingContext::DrawLine(const FVector2D& Start, const FVector2D& End, const FLinearColor& Color, float Thickness)
{
    // Transform the line endpoints
    FVector2D TransformedStart = CurrentTransform.TransformPoint(Start);
    FVector2D TransformedEnd = CurrentTransform.TransformPoint(End);

    // Calculate scaled thickness - get max scale from transform matrix
    TScale2<float> Scale = CurrentTransform.GetMatrix().GetScale();
    float MaxScale = FMath::Max(FMath::Abs(Scale.GetVector().X), FMath::Abs(Scale.GetVector().Y));
    float ScaledThickness = FMath::Max(1.0f, Thickness * MaxScale);  // Scale thickness with zoom

    FVector2f A(TransformedStart);
    FVector2f B(TransformedEnd);
    FVector2f Dir = (B - A).GetSafeNormal();
    FVector2f N = FVector2f(-Dir.Y, Dir.X) * 0.5f * ScaledThickness;
    FVector2f M = Dir * 0.5f * ScaledThickness;

    A -= M;
    B += M;

    uint32 v0 = PushVertex(FVector2D(A - N), WhiteUV, Color.ToFColor(true));
    uint32 v1 = PushVertex(FVector2D(B - N), WhiteUV, Color.ToFColor(true));
    uint32 v2 = PushVertex(FVector2D(B + N), WhiteUV, Color.ToFColor(true));
    uint32 v3 = PushVertex(FVector2D(A + N), WhiteUV, Color.ToFColor(true));

    WorkingIndices.Append({v0, v1, v2, v0, v2, v3});
}

void UnrealRenderingContext::DrawCircle(const FVector2D& Center,
                                        float Radius,
                                        const FLinearColor& Color,
                                        bool bFill,
                                        int Segments)
{
    if (Radius <= 0 || Segments < 3) return;

    // Transform the center and scale the radius
    FVector2D TransformedCenter = CurrentTransform.TransformPoint(Center);
    TScale2<float> Scale = CurrentTransform.GetMatrix().GetScale();
    float MaxScale = FMath::Max(FMath::Abs(Scale.GetVector().X), FMath::Abs(Scale.GetVector().Y));
    float ScaledRadius = Radius * MaxScale;
    float LineWidth = FMath::Max(1.0f, 1.0f * MaxScale);  // Scale line width with zoom

    FColor Col = To8bit(Color);

    if (bFill)
    {
        IncrementVertexCount(Segments + 2);
        uint32 CenterIdx = PushVertex(TransformedCenter, WhiteUV, Col);

        float Step = 2.f * PI / Segments;
        uint32 PrevIdx = PushVertex(TransformedCenter + FVector2D(ScaledRadius, 0), WhiteUV, Col);

        for (int i = 1; i <= Segments; ++i)
        {
            float Ang = i * Step;
            FVector2D P = TransformedCenter + FVector2D(FMath::Cos(Ang), FMath::Sin(Ang)) * ScaledRadius;
            uint32 CurrIdx = PushVertex(P, WhiteUV, Col);
            WorkingIndices.Append({CenterIdx, PrevIdx, CurrIdx});
            PrevIdx = CurrIdx;
        }
    }
    else // outline: thin rectangles for each segment
    {
        float InnerRadius = ScaledRadius - LineWidth/2;
        float OuterRadius = ScaledRadius + LineWidth/2;

        float Step = 2.f * PI / Segments;
        for (int i = 0; i < Segments; ++i)
        {
            float Ang1 = i * Step;
            float Ang2 = (i + 1) * Step;
            
            // Calculate inner and outer points for this segment
            FVector2D Inner1 = TransformedCenter + FVector2D(FMath::Cos(Ang1), FMath::Sin(Ang1)) * InnerRadius;
            FVector2D Outer1 = TransformedCenter + FVector2D(FMath::Cos(Ang1), FMath::Sin(Ang1)) * OuterRadius;
            FVector2D Inner2 = TransformedCenter + FVector2D(FMath::Cos(Ang2), FMath::Sin(Ang2)) * InnerRadius;
            FVector2D Outer2 = TransformedCenter + FVector2D(FMath::Cos(Ang2), FMath::Sin(Ang2)) * OuterRadius;

            // Create a thin rectangle for this segment
            uint32 v0 = PushVertex(Inner1, WhiteUV, Col);
            uint32 v1 = PushVertex(Outer1, WhiteUV, Col);
            uint32 v2 = PushVertex(Outer2, WhiteUV, Col);
            uint32 v3 = PushVertex(Inner2, WhiteUV, Col);
            WorkingIndices.Append({v0, v1, v2, v0, v2, v3});
        }
    }
}

void UnrealRenderingContext::DrawRectangle(const FBox2D& Box, const FLinearColor& Color, bool bFilled)
{
    // Transform the box corners
    FVector2D Min = CurrentTransform.TransformPoint(Box.Min);
    FVector2D Max = CurrentTransform.TransformPoint(Box.Max);

    // Get scale for line thickness
    TScale2<float> Scale = CurrentTransform.GetMatrix().GetScale();
    float MaxScale = FMath::Max(FMath::Abs(Scale.GetVector().X), FMath::Abs(Scale.GetVector().Y));
    float LineWidth = FMath::Max(1.0f, 1.0f * MaxScale);  // Scale line width with zoom

    // Ensure minimum size of 1 pixel after scaling
    if (Max.X - Min.X < LineWidth) {
        float CenterX = (Min.X + Max.X) * 0.5f;
        Min.X = CenterX - LineWidth/2;
        Max.X = CenterX + LineWidth/2;
    }
    if (Max.Y - Min.Y < LineWidth) {
        float CenterY = (Min.Y + Max.Y) * 0.5f;
        Min.Y = CenterY - LineWidth/2;
        Max.Y = CenterY + LineWidth/2;
    }

    FColor Color8Bit = To8bit(Color);

    if (bFilled)
    {
        // Create vertices for filled rectangle in clockwise order
        uint32 v0 = PushVertex(Min, WhiteUV, Color8Bit);                    // Bottom-left
        uint32 v1 = PushVertex(FVector2D(Max.X, Min.Y), WhiteUV, Color8Bit); // Bottom-right
        uint32 v2 = PushVertex(Max, WhiteUV, Color8Bit);                    // Top-right
        uint32 v3 = PushVertex(FVector2D(Min.X, Max.Y), WhiteUV, Color8Bit); // Top-left

        // Add two triangles for filled rectangle (both clockwise)
        WorkingIndices.Append({v0, v1, v2, v0, v2, v3});
    }
    else
    {
        // For outline, create thin rectangles for each edge
        float HalfWidth = LineWidth/2;

        // Bottom edge
        FVector2D BottomMin(Min.X, Min.Y - HalfWidth);
        FVector2D BottomMax(Max.X, Min.Y + HalfWidth);
        uint32 b0 = PushVertex(BottomMin, WhiteUV, Color8Bit);
        uint32 b1 = PushVertex(FVector2D(BottomMax.X, BottomMin.Y), WhiteUV, Color8Bit);
        uint32 b2 = PushVertex(BottomMax, WhiteUV, Color8Bit);
        uint32 b3 = PushVertex(FVector2D(BottomMin.X, BottomMax.Y), WhiteUV, Color8Bit);
        WorkingIndices.Append({b0, b1, b2, b0, b2, b3});

        // Right edge
        FVector2D RightMin(Max.X - HalfWidth, Min.Y);
        FVector2D RightMax(Max.X + HalfWidth, Max.Y);
        uint32 r0 = PushVertex(RightMin, WhiteUV, Color8Bit);
        uint32 r1 = PushVertex(FVector2D(RightMax.X, RightMin.Y), WhiteUV, Color8Bit);
        uint32 r2 = PushVertex(RightMax, WhiteUV, Color8Bit);
        uint32 r3 = PushVertex(FVector2D(RightMin.X, RightMax.Y), WhiteUV, Color8Bit);
        WorkingIndices.Append({r0, r1, r2, r0, r2, r3});

        // Top edge
        FVector2D TopMin(Min.X, Max.Y - HalfWidth);
        FVector2D TopMax(Max.X, Max.Y + HalfWidth);
        uint32 t0 = PushVertex(TopMin, WhiteUV, Color8Bit);
        uint32 t1 = PushVertex(FVector2D(TopMax.X, TopMin.Y), WhiteUV, Color8Bit);
        uint32 t2 = PushVertex(TopMax, WhiteUV, Color8Bit);
        uint32 t3 = PushVertex(FVector2D(TopMin.X, TopMax.Y), WhiteUV, Color8Bit);
        WorkingIndices.Append({t0, t1, t2, t0, t2, t3});

        // Left edge
        FVector2D LeftMin(Min.X - HalfWidth, Min.Y);
        FVector2D LeftMax(Min.X + HalfWidth, Max.Y);
        uint32 l0 = PushVertex(LeftMin, WhiteUV, Color8Bit);
        uint32 l1 = PushVertex(FVector2D(LeftMax.X, LeftMin.Y), WhiteUV, Color8Bit);
        uint32 l2 = PushVertex(LeftMax, WhiteUV, Color8Bit);
        uint32 l3 = PushVertex(FVector2D(LeftMin.X, LeftMax.Y), WhiteUV, Color8Bit);
        WorkingIndices.Append({l0, l1, l2, l0, l2, l3});
    }
}

void UnrealRenderingContext::DrawTriangle(const FVector2D& P1,
                                          const FVector2D& P2,
                                          const FVector2D& P3,
                                          const FLinearColor& Color, bool bFill)
{
    // Transform the points
    FVector2D TransformedP1 = CurrentTransform.TransformPoint(P1);
    FVector2D TransformedP2 = CurrentTransform.TransformPoint(P2);
    FVector2D TransformedP3 = CurrentTransform.TransformPoint(P3);

    FColor Col = To8bit(Color);

    if (bFill)
    {
        // For filled triangle, use a single triangle
        uint32 a = PushVertex(TransformedP1, WhiteUV, Col);
        uint32 b = PushVertex(TransformedP2, WhiteUV, Col);
        uint32 c = PushVertex(TransformedP3, WhiteUV, Col);
        WorkingIndices.Append({a, b, c});
    }
    else
    {
        // For outline, create thin rectangles for each edge
        TScale2<float> Scale = CurrentTransform.GetMatrix().GetScale();
        float MaxScale = FMath::Max(FMath::Abs(Scale.GetVector().X), FMath::Abs(Scale.GetVector().Y));
        float LineWidth = FMath::Max(1.0f, 1.0f * MaxScale);  // Scale line width with zoom

        // Helper function to create a thin rectangle for a line segment
        auto CreateEdgeRect = [&](const FVector2D& Start, const FVector2D& End) {
            FVector2D Dir = (End - Start).GetSafeNormal();
            FVector2D Perp = FVector2D(-Dir.Y, Dir.X) * (LineWidth/2);
            
            FVector2D TL = Start - Perp;
            FVector2D TR = Start + Perp;
            FVector2D BL = End - Perp;
            FVector2D BR = End + Perp;

            uint32 v0 = PushVertex(TL, WhiteUV, Col);
            uint32 v1 = PushVertex(TR, WhiteUV, Col);
            uint32 v2 = PushVertex(BR, WhiteUV, Col);
            uint32 v3 = PushVertex(BL, WhiteUV, Col);
            WorkingIndices.Append({v0, v1, v2, v0, v2, v3});
        };

        // Create rectangles for each edge
        CreateEdgeRect(TransformedP1, TransformedP2);
        CreateEdgeRect(TransformedP2, TransformedP3);
        CreateEdgeRect(TransformedP3, TransformedP1);
    }
}

static const FFontCharacter* FindGlyph(const UFont* Font, TCHAR CodePoint)
{
    uint16 Index16 = Font->IsRemapped
                     ? Font->CharRemap.FindRef((uint16)CodePoint)   // 0 if missing
                     : (uint16)CodePoint;

    int32 Index = (int32)Index16;
    return Font->Characters.IsValidIndex(Index) ? &Font->Characters[Index] : nullptr;
}

void UnrealRenderingContext::DrawText(const FVector2D& Pos,
                                      const FString&   Text,
                                      const FLinearColor& Color)
{
    UFont* Font = Cast<UFont>(TinyFontPath.TryLoad());
    if (!Font) return;

    FColor Col = To8bit(Color);
    Col.A = 255;

    // Transform the base position
    FVector2D TransformedPos = CurrentTransform.TransformPoint(Pos);

    // Get the scale from the transform matrix
    TScale2<float> Scale = CurrentTransform.GetMatrix().GetScale();
    float MaxScale = FMath::Max(FMath::Abs(Scale.GetVector().X), FMath::Abs(Scale.GetVector().Y));

    FVector2D Pen = TransformedPos;

    for (TCHAR Ch : Text)
    {
        const FFontCharacter* Glyph = FindGlyph(Font, Ch);
        if (!Glyph) continue;

        uint8 Page = Glyph->TextureIndex;
        if (!Font->Textures.IsValidIndex(Page)) continue;
        const FTextureResource* Res = Font->Textures[Page]->GetResource();
        if (!Res) continue;

        const float InvW = 1.f / Res->GetSizeX();
        const float InvH = 1.f / Res->GetSizeY();

        // Scale the glyph size by the transform
        float ScaledUSize = Glyph->USize * MaxScale;
        float ScaledVSize = Glyph->VSize * MaxScale;
        float ScaledVerticalOffset = Glyph->VerticalOffset * MaxScale;

        FVector2D TL = Pen + FVector2D(0.f, ScaledVerticalOffset);
        FVector2D BR = TL + FVector2D(ScaledUSize, ScaledVSize);

        FVector2f UV0(Glyph->StartU * InvW,                  Glyph->StartV * InvH);
        FVector2f UV1((Glyph->StartU + Glyph->USize) * InvW,
                      (Glyph->StartV + Glyph->VSize) * InvH);

        uint32 v0 = PushVertex(TL, UV0, Col, Page);
        uint32 v1 = PushVertex({BR.X, TL.Y}, {UV1.X, UV0.Y}, Col, Page);
        uint32 v2 = PushVertex(BR, UV1, Col, Page);
        uint32 v3 = PushVertex({TL.X, BR.Y}, {UV0.X, UV1.Y}, Col, Page);
        WorkingIndices.Append({v0,v1,v2, v0,v2,v3});
        IncrementVertexCount(4);

        // Advance pen: scaled glyph width + scaled kerning
        Pen.X += (Glyph->USize + Font->Kerning) * MaxScale;
    }
}

void UnrealRenderingContext::DrawTinyText(const FVector2D& Pos,
                                      const FString&   Text,
                                      const FLinearColor& Color)
{
	DrawText(Pos, Text, Color);
}

void UnrealRenderingContext::PushTransform(const FTransform2D& Transform)
{
    // Multiply new transform with current transform
    CurrentTransform = Transform.Concatenate(CurrentTransform);
    TransformStack.Add(CurrentTransform);
}

void UnrealRenderingContext::PopTransform()
{
    if (TransformStack.Num() > 1)
    {
        TransformStack.Pop();
        CurrentTransform = TransformStack.Last();
    }
}

