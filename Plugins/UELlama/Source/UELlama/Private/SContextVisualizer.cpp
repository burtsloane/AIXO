// SContextVisualizer.cpp
#include "SContextVisualizer.h"
#include "Rendering/DrawElements.h"
#include "Styling/CoreStyle.h" // For FCoreStyle

void SContextVisualizer::Construct(const FArguments& InArgs)
{
    CurrentContextBlocks = InArgs._ContextBlocks;
    TotalTokenCapacityInternal = InArgs._TotalTokenCapacity;
    KvCacheDecodedTokenCountInternal = InArgs._KvCacheDecodedTokenCount; // New
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

void SContextVisualizer::SetKvCacheDecodedTokenCount(TAttribute<int32> InKvCacheCount) // New
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
    const int32 CurrentKvCacheCount = KvCacheDecodedTokenCountInternal.Get(); // New
    const FVector2D LocalSize = AllottedGeometry.GetLocalSize();

    if (CurrentTotalCapacity <= 0)
    {
        FSlateDrawElement::MakeBox(OutDrawElements, LayerId, AllottedGeometry.ToPaintGeometry(), WhiteBrush, ESlateDrawEffect::None, FLinearColor(0.1f, 0.0f, 0.0f, 0.0f));
        return LayerId;
    }
    int32 CurrentLayer = LayerId;

    // Draw Background for the whole bar (optional, if you want a border or different base)
    FSlateDrawElement::MakeBox(
        OutDrawElements,
        CurrentLayer++,
        AllottedGeometry.ToPaintGeometry(),
        WhiteBrush,
        ESlateDrawEffect::None,
        FLinearColor(0.0f, 0.0f, 0.0f) // Dark background for the bar itself
    );

    for (const FContextVisBlock& Block : BlocksToDraw)
    {
        if (Block.NormalizedHeight <= 0.00001f) continue; // Skip tiny or zero-height blocks

        float RectY_Visual = LocalSize.Y * (1.0f - (Block.NormalizedStart + Block.NormalizedHeight)); // Visual Y from top
        float RectHeight_Visual = LocalSize.Y * Block.NormalizedHeight;

        if (RectHeight_Visual < 1.0f && RectHeight_Visual > 0.0f) RectHeight_Visual = 1.0f;
        if (RectHeight_Visual <= 0.0f) continue;

        FPaintGeometry BlockGeometry = AllottedGeometry.ToPaintGeometry(
            FVector2D(0, RectY_Visual),
            FVector2D(LocalSize.X, RectHeight_Visual)
        );

        FSlateDrawElement::MakeBox(
            OutDrawElements,
            CurrentLayer, // Keep blocks on the same layer unless overlap is desired
            BlockGeometry,
            WhiteBrush,
            ESlateDrawEffect::None,
            Block.BlockColor
        );
    }
    CurrentLayer++; // Increment layer for elements on top of blocks

    // --- Draw KV Cache Cursor Line ---
    if (CurrentKvCacheCount > 0 && CurrentKvCacheCount <= CurrentTotalCapacity)
    {
        float NormalizedKvCachePos = static_cast<float>(CurrentKvCacheCount) / CurrentTotalCapacity;
        // Y position is from the bottom up, so invert for drawing from top down
        float LineY_Visual = LocalSize.Y * (1.0f - NormalizedKvCachePos);

        TArray<FVector2D> LinePoints;
        LinePoints.Add(FVector2D(0, LineY_Visual));
        LinePoints.Add(FVector2D(LocalSize.X, LineY_Visual));

        FSlateDrawElement::MakeLines(
            OutDrawElements,
            CurrentLayer, // Draw on top of the blocks
            AllottedGeometry.ToPaintGeometry(),
            LinePoints,
            ESlateDrawEffect::None,
            FLinearColor::White, // Line color
            true, // bAntialias
            4.0f  // Thickness
        );
    }
    CurrentLayer++;

    return CurrentLayer;
}
