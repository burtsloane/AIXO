#pragma once

#include "CoreMinimal.h"
#include "IVisualElement.h" // Base RenderingContext definition
#include "Engine/Canvas.h" // For UCanvas
#include "Kismet/KismetRenderingLibrary.h" // Include for FDrawToRenderTargetContext
#include "RHI.h" // Added include for rendering hardware interface
#include "DiagramVertex.h"

// Forward Declarations
class UTextureRenderTarget2D;
class UWorld;
class UTexture2D; // Forward declare

FORCEINLINE static FColor To8bit(const FLinearColor& L)
{
    return L.ToFColor(true);          // sRGB → 0‑255
}

/**
 * Unreal Engine specific implementation of RenderingContext.
 * Uses UCanvas drawing functions targeting a UTextureRenderTarget2D.
 */
class UnrealRenderingContext : public RenderingContext
{
private:
    UTextureRenderTarget2D* RenderTarget = nullptr;
    UCanvas* Canvas = nullptr;
    FVector2D CanvasSize = FVector2D::ZeroVector;
    FDrawToRenderTargetContext RenderTargetContext;
    UWorld* World = nullptr; // Needed for drawing functions
    UTexture2D* SolidColorTexture = nullptr; // Store the texture to use for solid colors
    int32 VertexCount;

    /** Dynamic geometry recorded this frame (CPU‑side) */
    // Removed local FDiagramVertex struct

    TArray<FDiagramVertex> WorkingVertices;
    TArray<uint32>         WorkingIndices;
    TArray<FDiagramVertex> PresentedVertices;
    TArray<uint32>         PresentedIndices;

    void        ClearBuffers();
	uint32 PushVertex(const FVector2D& Pos,
					  const FVector2f& UV,
					  const FColor&    Col,
                      uint8 Page = 255);

public:
    /** 
     * Constructor.
     * @param InWorld The world context, needed for Kismet drawing functions.
     * @param InRenderTarget The target texture to draw onto.
     * @param InSolidColorTexture The texture to use for drawing solid colors (e.g., WhiteSquareTexture).
     */
    UnrealRenderingContext(UWorld* InWorld, UTextureRenderTarget2D* InRenderTarget, UTexture2D* InSolidColorTexture);
    virtual ~UnrealRenderingContext() override;

    /** Prepares the canvas for drawing. Call before any Draw operations. */
    bool BeginDrawing();

    /** Finalizes drawing to the render target. Call after all Draw operations. */
    void EndDrawing();

    // --- RenderingContext Interface Implementation ---
    virtual void DrawLine(const FVector2D& Start, const FVector2D& End, const FLinearColor& Color, float Thickness) override;
    virtual void DrawCircle(const FVector2D& Center, float Radius, const FLinearColor& Color, bool bFill = false, int Segments = 16) override;
    virtual void DrawRectangle(const FBox2D& Rect, const FLinearColor& Color, bool bFill = false) override;
    virtual void DrawTriangle(const FVector2D& P1, const FVector2D& P2, const FVector2D& P3, const FLinearColor& Color, bool bFill = false) override;
    virtual void DrawText(const FVector2D& Position, const FString& Text, const FLinearColor& Color /* Font parameters? */) override;
    virtual void DrawTinyText(const FVector2D& Position, const FString& Text, const FLinearColor& Color /* Font parameters? */) override;
    // Add DrawTexture if needed later

public:
	void ResetVertexCount();
	void IncrementVertexCount(int32 Amount);

public:
    /** Read‑only access for UI widgets */
	const TArray<FDiagramVertex>* GetPresentedVertices() const { return &PresentedVertices; }
	const TArray<uint32>*         GetPresentedIndices()  const { return &PresentedIndices; }
}; 
