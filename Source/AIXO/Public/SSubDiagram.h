#pragma once
#include "CoreMinimal.h"
#include "Widgets/SLeafWidget.h"
#include "Rendering/DrawElements.h"      // MakeCustomVerts
#include "Slate/SlateBrushAsset.h"
#include "DiagramVertex.h"

// The widget just keeps pointers to the CPU arrays living in UnrealRenderingContext.
// No copying, no locks.

class AVisualTestHarnessActor;

class SSubDiagram : public SLeafWidget
{
public:
    SLATE_BEGIN_ARGS(SSubDiagram) {}
        /** Pointer to the CPU‑side geometry recorder */
        SLATE_ARGUMENT(AVisualTestHarnessActor*, OwnerActor)
        SLATE_ARGUMENT(const TArray<FDiagramVertex>*, Verts)
        SLATE_ARGUMENT(const TArray<uint32>*,        Indices)
    SLATE_END_ARGS()

    void Construct(const FArguments& InArgs)
    {
        Owner      = InArgs._OwnerActor;
        CpuVerts   = InArgs._Verts;
        CpuIndices = InArgs._Indices;
    }

    // --- public API: call when CPU mesh changes ---
    void InvalidateFast() {
//UE_LOG(LogTemp, Log, TEXT("SSubDiagram:InvalidateFast called %hs"), CpuVerts?"":"CpuVerts is null!!!!!!!!");
    	Invalidate(EInvalidateWidgetReason::Paint);
    }

    // --- SWidget override ---
    virtual int32 OnPaint( const FPaintArgs& Args,
                           const FGeometry&  AllottedGeometry,
                           const FSlateRect& MyCullingRect,
                           FSlateWindowElementList& OutDrawElements,
                           int32 LayerId,
                           const FWidgetStyle& InWidgetStyle,
                           bool bParentEnabled ) const override;

    // --- SWidget required override ---
    virtual FVector2D ComputeDesiredSize(float) const override;

    /** Update CPU array pointers after the widget has been constructed */
    void SetGeometryPointers(const TArray<FDiagramVertex>* InVerts,
                             const TArray<uint32>*        InIndices)
    {
        CpuVerts   = InVerts;
        CpuIndices = InIndices;
//UE_LOG(LogTemp, Log, TEXT("SSubDiagram:SetGeometryPointers called %hs"), CpuVerts?"":"CpuVerts is null!!!!!!!!");
    }

	virtual bool SupportsKeyboardFocus() const override { return true; }

	virtual FReply OnMouseButtonDown(
		const FGeometry& MyGeom, const FPointerEvent& MouseEvent) override;

	virtual FReply OnMouseButtonUp(
		const FGeometry& MyGeom, const FPointerEvent& MouseEvent) override;

	virtual FReply OnMouseMove(
		const FGeometry& MyGeom, const FPointerEvent& MouseEvent) override;

	virtual FReply OnTouchStarted(const FGeometry& MyGeom,
								  const FPointerEvent& TouchEvent) override;

	virtual FReply OnTouchMoved  (const FGeometry& MyGeom,
								  const FPointerEvent& TouchEvent) override;

	virtual FReply OnTouchEnded  (const FGeometry& MyGeom,
								  const FPointerEvent& TouchEvent) override;
private:
    bool bMouseDownInside = false;   // track drag / clickprivate:
    const TArray<FDiagramVertex>* CpuVerts   = nullptr;
    const TArray<uint32>*        CpuIndices = nullptr;
    AVisualTestHarnessActor* Owner = nullptr;    // raw but safe
};

