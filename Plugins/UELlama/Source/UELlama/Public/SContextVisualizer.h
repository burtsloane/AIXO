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
        , _KvCacheDecodedTokenCount(0)
        , _IsStaticWorldInfoUpToDate(true)
		, _IsLowFrequencyStateUpToDate(true)
		, _IsLlamaCoreActuallyReady(false)
		, _IsLlamaCurrentlyIdle(true)
		, _FmsPerTokenDecode(0.0f)
		, _FmsPerTokenGenerate(0.0f)
    {}
        SLATE_ATTRIBUTE(int32, TotalTokenCapacity)
        SLATE_ATTRIBUTE(TArray<FContextVisBlock>, ContextBlocks)
        SLATE_ATTRIBUTE(int32, KvCacheDecodedTokenCount)
		SLATE_ATTRIBUTE(bool, IsStaticWorldInfoUpToDate)
		SLATE_ATTRIBUTE(bool, IsLowFrequencyStateUpToDate)
		SLATE_ATTRIBUTE(bool, IsLlamaCoreActuallyReady)
		SLATE_ATTRIBUTE(bool, IsLlamaCurrentlyIdle)
		SLATE_ATTRIBUTE(float, FmsPerTokenDecode)
		SLATE_ATTRIBUTE(float, FmsPerTokenGenerate)
		// You can add more SLATE_ATTRIBUTE(bool, YourCustomFlagName) here
    SLATE_END_ARGS()

    void Construct(const FArguments& InArgs);

    virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override;
    virtual FVector2D ComputeDesiredSize(float LayoutScaleMultiplier) const override;

    // Attribute setters
    void SetContextBlocks(TAttribute<TArray<FContextVisBlock>> InBlocks);
    void SetTotalTokenCapacity(TAttribute<int32> InCapacity);
    void SetKvCacheDecodedTokenCount(TAttribute<int32> InKvCacheCount);

private:
    TAttribute<TArray<FContextVisBlock>> CurrentContextBlocks;
    TAttribute<int32> TotalTokenCapacityInternal;
    TAttribute<int32> KvCacheDecodedTokenCountInternal;

    TAttribute<bool> IsStaticWorldInfoUpToDateInternal;
    TAttribute<bool> IsLowFrequencyStateUpToDateInternal;
    TAttribute<bool> IsLlamaCoreActuallyReadyInternal;
    TAttribute<bool> IsLlamaCurrentlyIdleInternal;

    TAttribute<float> FmsPerTokenDecode;
    TAttribute<float> FmsPerTokenGenerate;

    // TArray<TTuple<FText, TAttribute<bool>>> CustomFlagsToDisplay; // For more dynamic flags
};
