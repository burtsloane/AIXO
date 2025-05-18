#include "SContextVisualizer.h"
#include "Rendering/DrawElements.h"
#include "Styling/CoreStyle.h" // For FCoreStyle

void SContextVisualizer::Construct(const FArguments& InArgs)
{
    CurrentContextBlocks = InArgs._ContextBlocks;
    TotalTokenCapacityInternal = InArgs._TotalTokenCapacity;
    KvCacheDecodedTokenCountInternal = InArgs._KvCacheDecodedTokenCount;
    SetCanTick(false);
}

void SContextVisualizer::SetContextBlocks(TAttribute<TArray<FContextVisBlock>> InBlocks)
{
    CurrentContextBlocks = InBlocks;
    Invalidate(EInvalidateWidget::Paint);
}

void SContextVisualizer::SetTotalTokenCapacity(TAttribute<int32> InCapacity)
{
    TotalTokenCapacityInternal = InCapacity;
    Invalidate(EInvalidateWidget::Paint);
}

void SContextVisualizer::SetKvCacheDecodedTokenCount(TAttribute<int32> InKvCacheCount)
{
    KvCacheDecodedTokenCountInternal = InKvCacheCount;
    Invalidate(EInvalidateWidget::Paint);
}

FVector2D SContextVisualizer::ComputeDesiredSize(float LayoutScaleMultiplier) const
{
    return FVector2D(10.0f * LayoutScaleMultiplier, 300.0f * LayoutScaleMultiplier);
}

int32 SContextVisualizer::OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const
{
    const FSlateBrush* WhiteBrush = FCoreStyle::Get().GetBrush(TEXT("WhiteBrush"));
    const TArray<FContextVisBlock>& BlocksToDraw = CurrentContextBlocks.Get();
    const int32 CurrentTotalCapacity = TotalTokenCapacityInternal.Get();
    const int32 CurrentKvCacheCount = KvCacheDecodedTokenCountInternal.Get();
    const FVector2D LocalSize = AllottedGeometry.GetLocalSize();

    if (CurrentTotalCapacity <= 0)
    {
        FSlateDrawElement::MakeBox(OutDrawElements, LayerId, AllottedGeometry.ToPaintGeometry(), WhiteBrush, ESlateDrawEffect::None, FLinearColor(0.1f, 0.0f, 0.0f, 0.0f));
        return LayerId;
    }
    int32 CurrentLayer = LayerId;

    // Draw Background for the whole bar
    FSlateDrawElement::MakeBox(
        OutDrawElements,
        CurrentLayer++,
        AllottedGeometry.ToPaintGeometry(),
        WhiteBrush,
        ESlateDrawEffect::None,
        FLinearColor(0.0f, 0.0f, 0.0f)
    );

    for (const FContextVisBlock& Block : BlocksToDraw)
    {
        if (Block.NormalizedHeight <= 0.00001f) continue;

        float RectY_Visual = LocalSize.Y * (1.0f - (Block.NormalizedStart + Block.NormalizedHeight));
        float RectHeight_Visual = LocalSize.Y * Block.NormalizedHeight;

        if (RectHeight_Visual < 1.0f && RectHeight_Visual > 0.0f) RectHeight_Visual = 1.0f;
        if (RectHeight_Visual <= 0.0f) continue;

        // Update to use AllottedGeometry instance
        FPaintGeometry BlockGeometry = AllottedGeometry.ToPaintGeometry(
            FVector2f(LocalSize.X, RectHeight_Visual),
            FSlateLayoutTransform(FVector2f(0, RectY_Visual))
        );

        FSlateDrawElement::MakeBox(
            OutDrawElements,
            CurrentLayer,
            BlockGeometry,
            WhiteBrush,
            ESlateDrawEffect::None,
            Block.BlockColor
        );
    }
    CurrentLayer++;

    // Draw KV Cache Cursor Line
    if (CurrentKvCacheCount > 0 && CurrentKvCacheCount <= CurrentTotalCapacity)
    {
        float NormalizedKvCachePos = static_cast<float>(CurrentKvCacheCount) / CurrentTotalCapacity;
        float LineY_Visual = LocalSize.Y * (1.0f - NormalizedKvCachePos);

        TArray<FVector2D> LinePoints;
        LinePoints.Add(FVector2D(0, LineY_Visual));
        LinePoints.Add(FVector2D(LocalSize.X, LineY_Visual));

        // Update to use AllottedGeometry instance
        FPaintGeometry LineGeometry = AllottedGeometry.ToPaintGeometry(
            FVector2f(LocalSize.X, LocalSize.Y),
            FSlateLayoutTransform()
        );

        FSlateDrawElement::MakeLines(
            OutDrawElements,
            CurrentLayer,
            LineGeometry,
            LinePoints,
            ESlateDrawEffect::None,
            FLinearColor::White,
            true,
            4.0f
        );
    }
    CurrentLayer++;

    return CurrentLayer;
} 