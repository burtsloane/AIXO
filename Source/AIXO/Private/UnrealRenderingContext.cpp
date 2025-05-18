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
    ResetVertexCount();
    ClearBuffers();
    return true;    // now purely a recorder
}

void UnrealRenderingContext::EndDrawing()
{
    // Hand off CPU‑built geometry to Slate for the next render pass.
    PresentedVertices = MoveTemp(WorkingVertices);
    PresentedIndices  = MoveTemp(WorkingIndices);

//    UE_LOG(LogTemp, Warning,
//           TEXT("UnrealRenderingContext::EndDrawing: %d vertices, %d indices (presented)"),
//           PresentedVertices.Num(), PresentedIndices.Num());
}

void UnrealRenderingContext::DrawLine(const FVector2D& Start, const FVector2D& End, const FLinearColor& Color, float Thickness)
{
    IncrementVertexCount(4);   // quad

    FVector2f A(Start);
    FVector2f B(End);
    FVector2f Dir = (B - A).GetSafeNormal();
    FVector2f N   = FVector2f(-Dir.Y, Dir.X) * 0.5f * Thickness;
    FVector2f M   = Dir * 0.5f * Thickness;

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

    FColor Col = To8bit(Color);

    if (bFill)
    {
        IncrementVertexCount(Segments + 2);
        uint32 CenterIdx = PushVertex(Center, WhiteUV, Col);

        float Step = 2.f * PI / Segments;
        uint32 PrevIdx = PushVertex(Center + FVector2D(Radius, 0), WhiteUV, Col);

        for (int i = 1; i <= Segments; ++i)
        {
            float Ang = i * Step;
            FVector2D P = Center + FVector2D(FMath::Cos(Ang), FMath::Sin(Ang)) * Radius;
            uint32 CurrIdx = PushVertex(P, WhiteUV, Col);
            WorkingIndices.Append({CenterIdx, PrevIdx, CurrIdx});
            PrevIdx = CurrIdx;
        }
    }
    else // outline: quad per segment
    {
        float T = 1.0f;
        IncrementVertexCount(Segments * 4);

        float Step = 2.f * PI / Segments;
        FVector2D PrevOuter = Center + FVector2D(Radius, 0);
        FVector2D PrevInner = Center + FVector2D(Radius - T, 0);

        for (int i = 1; i <= Segments; ++i)
        {
            float Ang = i * Step;
            FVector2D Outer = Center + FVector2D(FMath::Cos(Ang), FMath::Sin(Ang)) * Radius;
            FVector2D Inner = Center + FVector2D(FMath::Cos(Ang), FMath::Sin(Ang)) * (Radius - T);

            uint32 a = PushVertex(PrevInner, WhiteUV, Col);
            uint32 b = PushVertex(PrevOuter, WhiteUV, Col);
            uint32 c = PushVertex(Outer,      WhiteUV, Col);
            uint32 d = PushVertex(Inner,      WhiteUV, Col);
            WorkingIndices.Append({a,b,c, a,c,d});

            PrevOuter = Outer;
            PrevInner = Inner;
        }
    }
}

void UnrealRenderingContext::DrawRectangle(const FBox2D& Box, const FLinearColor& Color, bool bFilled)
{
    // Transform the box corners
    FVector2D Min = CurrentTransform.TransformPoint(Box.Min);
    FVector2D Max = CurrentTransform.TransformPoint(Box.Max);

    // Create vertices for transformed box
    FDiagramVertex Verts[4];
    Verts[0].Pos = FVector2f(Min.X, Min.Y);
    Verts[1].Pos = FVector2f(Max.X, Min.Y);
    Verts[2].Pos = FVector2f(Max.X, Max.Y);
    Verts[3].Pos = FVector2f(Min.X, Max.Y);

    FColor Color8Bit = To8bit(Color);
    for (int i = 0; i < 4; ++i)
    {
        Verts[i].Col = Color8Bit;
        Verts[i].UV = FVector2f(0, 0);
    }

    // Add vertices and indices
    uint32 BaseIndex = PresentedVertices.Num();
    for (int i = 0; i < 4; ++i)
    {
        PresentedVertices.Add(Verts[i]);
    }

    if (bFilled)
    {
        // Add two triangles for filled rectangle
        PresentedIndices.Add(BaseIndex);
        PresentedIndices.Add(BaseIndex + 1);
        PresentedIndices.Add(BaseIndex + 2);
        PresentedIndices.Add(BaseIndex);
        PresentedIndices.Add(BaseIndex + 2);
        PresentedIndices.Add(BaseIndex + 3);
    }
    else
    {
        // Add line segments for outline
        PresentedIndices.Add(BaseIndex);
        PresentedIndices.Add(BaseIndex + 1);
        PresentedIndices.Add(BaseIndex + 1);
        PresentedIndices.Add(BaseIndex + 2);
        PresentedIndices.Add(BaseIndex + 2);
        PresentedIndices.Add(BaseIndex + 3);
        PresentedIndices.Add(BaseIndex + 3);
        PresentedIndices.Add(BaseIndex);
    }
}

void UnrealRenderingContext::DrawTriangle(const FVector2D& P1,
                                          const FVector2D& P2,
                                          const FVector2D& P3,
                                          const FLinearColor& Color, bool bFill)
{
    IncrementVertexCount(3);
    FColor Col = To8bit(Color);
    uint32 a = PushVertex(P1, WhiteUV, Col);
    uint32 b = PushVertex(P2, WhiteUV, Col);
    uint32 c = PushVertex(P3, WhiteUV, Col);
    WorkingIndices.Append({a,b,c});
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
//	if (!Font)
//UE_LOG(LogTemp, Warning, TEXT("UnrealRenderingContext::DrawText %hs"), Font?"ok":"FONT NOT FOUND!!!");
	if (!Font) return;

    FColor Col = To8bit(Color);
    Col.A = 255;
    FVector2D Pen = Pos;

    for (TCHAR Ch : Text)
    {
        const FFontCharacter* Glyph = FindGlyph(Font, Ch);
//UE_LOG(LogTemp, Warning, TEXT("Glyph=%d %hs"), (int)(Ch), Glyph?"ok":"NOT FOUND!!!");
        if (!Glyph) continue;                          // should never hit

        uint8 Page = Glyph->TextureIndex;          // page 0,1,…
        if (!Font->Textures.IsValidIndex(Page)) continue;
        const FTextureResource* Res = Font->Textures[Page]->GetResource();
        if (!Res) continue;

        const float InvW = 1.f / Res->GetSizeX();
        const float InvH = 1.f / Res->GetSizeY();

        FVector2D TL = Pen + FVector2D(0.f, Glyph->VerticalOffset);
        FVector2D BR = TL  + FVector2D(Glyph->USize, Glyph->VSize);

        FVector2f UV0(Glyph->StartU * InvW,                  Glyph->StartV * InvH);
        FVector2f UV1((Glyph->StartU + Glyph->USize) * InvW,
                      (Glyph->StartV + Glyph->VSize) * InvH);

        uint32 v0 = PushVertex(TL, UV0, Col, Page);
        uint32 v1 = PushVertex({BR.X, TL.Y}, {UV1.X, UV0.Y}, Col, Page);
        uint32 v2 = PushVertex(BR, UV1, Col, Page);
        uint32 v3 = PushVertex({TL.X, BR.Y}, {UV0.X, UV1.Y}, Col, Page);
        WorkingIndices.Append({v0,v1,v2, v0,v2,v3});
        IncrementVertexCount(4);

        // Advance pen: glyph width + global kerning
        Pen.X += Glyph->USize + Font->Kerning;
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

