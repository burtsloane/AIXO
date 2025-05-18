// SContextVisualizer.h
#pragma once

#include "CoreMinimal.h"
#include "Widgets/SLeafWidget.h"
#include "ContextVisualizationData.h"

class UELLAMA_API SContextVisualizer : public SLeafWidget
{
public:
    SLATE_BEGIN_ARGS(SContextVisualizer)
        : _TotalTokenCapacity(32768)
        , _KvCacheDecodedTokenCount(0) // New
    {}
        SLATE_ATTRIBUTE(int32, TotalTokenCapacity)
        SLATE_ATTRIBUTE(TArray<FContextVisBlock>, ContextBlocks)
        SLATE_ATTRIBUTE(int32, KvCacheDecodedTokenCount) // New
    SLATE_END_ARGS()

    void Construct(const FArguments& InArgs);

    virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override;
    virtual FVector2D ComputeDesiredSize(float LayoutScaleMultiplier) const override;

    // Attribute setters
    void SetContextBlocks(TAttribute<TArray<FContextVisBlock>> InBlocks);
    void SetTotalTokenCapacity(TAttribute<int32> InCapacity);
    void SetKvCacheDecodedTokenCount(TAttribute<int32> InKvCacheCount); // New

private:
    TAttribute<TArray<FContextVisBlock>> CurrentContextBlocks;
    TAttribute<int32> TotalTokenCapacityInternal;
    TAttribute<int32> KvCacheDecodedTokenCountInternal; // New
};
