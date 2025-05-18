#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"

struct FContextVisBlock
{
    float NormalizedStart;
    float NormalizedHeight;
    FLinearColor BlockColor;
};

class SContextVisualizer : public SCompoundWidget
{
public:
    SLATE_BEGIN_ARGS(SContextVisualizer)
        : _ContextBlocks()
        , _TotalTokenCapacity(0)
        , _KvCacheDecodedTokenCount(0)
    {}
        SLATE_ATTRIBUTE(TArray<FContextVisBlock>, ContextBlocks)
        SLATE_ATTRIBUTE(int32, TotalTokenCapacity)
        SLATE_ATTRIBUTE(int32, KvCacheDecodedTokenCount)
    SLATE_END_ARGS()

    void Construct(const FArguments& InArgs);
    void SetContextBlocks(TAttribute<TArray<FContextVisBlock>> InBlocks);
    void SetTotalTokenCapacity(TAttribute<int32> InCapacity);
    void SetKvCacheDecodedTokenCount(TAttribute<int32> InKvCacheCount);

    virtual FVector2D ComputeDesiredSize(float LayoutScaleMultiplier) const override;
    virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override;

private:
    TAttribute<TArray<FContextVisBlock>> CurrentContextBlocks;
    TAttribute<int32> TotalTokenCapacityInternal;
    TAttribute<int32> KvCacheDecodedTokenCountInternal;
}; 