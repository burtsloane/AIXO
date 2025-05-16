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

UnrealRenderingContext::UnrealRenderingContext(UWorld* InWorld, UTextureRenderTarget2D* InRenderTarget, UTexture2D* InSolidColorTexture)
    : RenderTarget(InRenderTarget),
      Canvas(nullptr),
      World(InWorld),
      SolidColorTexture(InSolidColorTexture)
{
    if (RenderTarget)
    {
        CanvasSize = FVector2D(RenderTarget->SizeX, RenderTarget->SizeY);
    }
    VertexCount = 0;
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

void UnrealRenderingContext::DrawRectangle(const FBox2D& Rect,
                                           const FLinearColor& Color,
                                           bool bFill)
{
    IncrementVertexCount(bFill ? 4 : 8);
    const FColor Col = To8bit(Color);
	FVector2f TL(Rect.Min);
	FVector2f BR(Rect.Max);
	FVector2f TR(BR.X, TL.Y);
	FVector2f BL(TL.X, BR.Y);

    if (bFill)
    {
        uint32 v0 = PushVertex(FVector2D(TL), WhiteUV, Col);
        uint32 v1 = PushVertex(FVector2D(TR), WhiteUV, Col);
        uint32 v2 = PushVertex(FVector2D(BL), WhiteUV, Col);
        uint32 v3 = PushVertex(FVector2D(BR), WhiteUV, Col);
        WorkingIndices.Append({v0,v1,v2, v2,v3,v1});
    }
    else // outline: 4 thin quads
    {
        float T = 1.0f;                              // outline thickness in px

        auto AddQuad = [&](FVector2f A, FVector2f B, FVector2f C, FVector2f D)
        {
            uint32 a = PushVertex(FVector2D(FMath::RoundToFloat(A.X), FMath::RoundToFloat(A.Y)), WhiteUV, Col);
            uint32 b = PushVertex(FVector2D(FMath::RoundToFloat(B.X), FMath::RoundToFloat(B.Y)), WhiteUV, Col);
            uint32 c = PushVertex(FVector2D(FMath::RoundToFloat(C.X), FMath::RoundToFloat(C.Y)), WhiteUV, Col);
            uint32 d = PushVertex(FVector2D(FMath::RoundToFloat(D.X), FMath::RoundToFloat(D.Y)), WhiteUV, Col);
            WorkingIndices.Append({a,b,c, a,c,d});
        };

        // top
        AddQuad(TL, TR, TR + FVector2f(0,T), TL + FVector2f(0,T));
        // bottom
        AddQuad(BL - FVector2f(0,T), BR - FVector2f(0,T), BR, BL);
        // left
        AddQuad(TL, TL + FVector2f(T,0), BL + FVector2f(T,0), BL);
        // right
        AddQuad(TR - FVector2f(T,0), TR, BR, BR - FVector2f(T,0));
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

