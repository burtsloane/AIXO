// SLLContextVisualizer.cpp
#include "SLLContextVisualizer.h"
#include "Rendering/DrawElements.h"
#include "Styling/CoreStyle.h" // For FCoreStyle
#include "Framework/Application/SlateApplication.h" // <-- For FSlateApplication and FontMeasureService
#include "Fonts/FontMeasure.h" // <-- For FSlateFontMeasure definition (often pulled in by SlateApplication)

void SLLContextVisualizer::Construct(const FArguments& InArgs)
{
    CurrentContextBlocks = InArgs._ContextBlocks;
    TotalTokenCapacityInternal = InArgs._TotalTokenCapacity;
    KvCacheDecodedTokenCountInternal = InArgs._KvCacheDecodedTokenCount;
    IsStaticWorldInfoUpToDateInternal = InArgs._IsStaticWorldInfoUpToDate;
    IsLowFrequencyStateUpToDateInternal = InArgs._IsLowFrequencyStateUpToDate;
    IsLlamaCoreActuallyReadyInternal = InArgs._IsLlamaCoreActuallyReady;
    IsLlamaCurrentlyIdleInternal = InArgs._IsLlamaCurrentlyIdle;
    FmsPerTokenDecode = InArgs._FmsPerTokenDecode;
    FmsPerTokenGenerate = InArgs._FmsPerTokenGenerate;
    SetCanTick(false);
}

void SLLContextVisualizer::SetContextBlocks(TAttribute<TArray<FContextVisBlock>> InBlocks)
{
    CurrentContextBlocks = InBlocks;
    Invalidate(EInvalidateWidget::Paint);
}

void SLLContextVisualizer::SetTotalTokenCapacity(TAttribute<int32> InCapacity)
{
    TotalTokenCapacityInternal = InCapacity;
    Invalidate(EInvalidateWidget::Paint);
}

void SLLContextVisualizer::SetKvCacheDecodedTokenCount(TAttribute<int32> InKvCacheCount)
{
    KvCacheDecodedTokenCountInternal = InKvCacheCount;
    Invalidate(EInvalidateWidget::Paint);
}

FVector2D SLLContextVisualizer::ComputeDesiredSize(float LayoutScaleMultiplier) const
{
    return FVector2D(10.0f * LayoutScaleMultiplier, 300.0f * LayoutScaleMultiplier);
}

int32 SLLContextVisualizer::OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const
{
//UE_LOG(LogTemp, Log, TEXT("SLLContextVisualizer::OnPaint - SWI UpToDate: %d, LFS UpToDate: %d, CoreReady: %d, LLM Idle: %d"),
//    IsStaticWorldInfoUpToDateInternal.Get(),
//    IsLowFrequencyStateUpToDateInternal.Get(),
//    IsLlamaCoreActuallyReadyInternal.Get(),
//    IsLlamaCurrentlyIdleInternal.Get());
//{
//const FPerformanceUIPayload& PerfData = PerformanceDataInternal.Get();
//UE_LOG(LogTemp, Warning, TEXT("SLLContextVisualizer::OnPaint - PerfData - Frame:%.2f, PromptEval:%.2f, TokenGen:%.2f, Avg/Tok:%.2f"),
//    PerfData.GameMetrics.FrameTimeMs,
//    PerfData.LlamaMetrics.PromptEvalTimeMs,
//    PerfData.LlamaMetrics.TokenGenerationTimeMs,
//    PerfData.LlamaMetrics.AvgTimePerGeneratedTokenMs);
//}
    
	const FSlateBrush* WhiteBrush = FCoreStyle::Get().GetBrush(TEXT("WhiteBrush"));
    const FVector2D FullLocalSize = AllottedGeometry.GetLocalSize();

    // --- Define areas ---
    const float HalfWidth = 20;//FullLocalSize.X * 0.5f;
    FGeometry BarGaugeGeometry = AllottedGeometry.MakeChild(
        FVector2D(HalfWidth, FullLocalSize.Y), // Size
        FSlateLayoutTransform() // Position (top-left of this child area)
    );
    FGeometry FlagsGeometry = AllottedGeometry.MakeChild(
        FVector2D(FullLocalSize.X - HalfWidth, FullLocalSize.Y),
        FSlateLayoutTransform(FVector2D(HalfWidth, 0.0f)) // Position (offset by half width)
    );

    int32 CurrentLayer = LayerId;

    // --- 1. Draw Bar Gauge on the Left Side ---
    const TArray<FContextVisBlock>& BlocksToDraw = CurrentContextBlocks.Get();
    const int32 CurrentTotalCapacity = TotalTokenCapacityInternal.Get();
    const int32 CurrentKvCacheCount = KvCacheDecodedTokenCountInternal.Get();
    const FVector2D BarGaugeLocalSize = BarGaugeGeometry.GetLocalSize();

    if (CurrentTotalCapacity > 0) {
        // Background for bar gauge area
        FSlateDrawElement::MakeBox(OutDrawElements, CurrentLayer, AllottedGeometry.ToPaintGeometry(), WhiteBrush, ESlateDrawEffect::None, FLinearColor(0.05f, 0.05f, 0.05f));

        for (const FContextVisBlock& Block : BlocksToDraw) {
            if (Block.NormalizedHeight <= 0.00001f) continue;
            float RectY_Visual = BarGaugeLocalSize.Y * (1.0f - (Block.NormalizedStart + Block.NormalizedHeight));
            float RectHeight_Visual = BarGaugeLocalSize.Y * Block.NormalizedHeight;
            if (RectHeight_Visual < 1.0f && RectHeight_Visual > 0.0f) RectHeight_Visual = 1.0f;
            if (RectHeight_Visual <= 0.0f) continue;

			FSlateLayoutTransform BoxTransform(FVector2D(0, RectY_Visual));
            FPaintGeometry BlockPaintGeometry = BarGaugeGeometry.ToPaintGeometry(FVector2D(BarGaugeLocalSize.X, RectHeight_Visual), BoxTransform);
            FSlateDrawElement::MakeBox(OutDrawElements, CurrentLayer + 1, BlockPaintGeometry, WhiteBrush, ESlateDrawEffect::None, Block.BlockColor);
        }

        // Draw KV Cache Cursor Line on bar gauge
        if (CurrentKvCacheCount > 0 && CurrentKvCacheCount <= CurrentTotalCapacity) {
            float NormalizedKvCachePos = static_cast<float>(CurrentKvCacheCount) / CurrentTotalCapacity;
            float LineY_Visual = BarGaugeLocalSize.Y * (1.0f - NormalizedKvCachePos);
            TArray<FVector2D> LinePoints;
            LinePoints.Add(FVector2D(0, LineY_Visual));
            LinePoints.Add(FVector2D(BarGaugeLocalSize.X, LineY_Visual));
            FSlateDrawElement::MakeLines(OutDrawElements, CurrentLayer + 2, BarGaugeGeometry.ToPaintGeometry(), LinePoints, ESlateDrawEffect::None, FLinearColor::White, true, 2.0f);
        }
    }
    CurrentLayer += 3; // Increment layer past bar gauge elements

    // --- 2. Draw Flags on the Right Side (Top Part of FlagsGeometry) ---
    const FSlateFontInfo PerfBarLabelFont = FCoreStyle::Get().GetFontStyle(TEXT("SmallFont")); // Declare font for perf bars
	TSharedRef<FSlateFontMeasure> FontMeasureService = FSlateApplication::Get().GetRenderer()->GetFontMeasureService();
	FVector2D ValueLabelSize = FontMeasureService->Measure(TEXT("Testing"), PerfBarLabelFont);

    const FVector2D FlagsAreaLocalSize = FlagsGeometry.GetLocalSize();
    const FSlateFontInfo FlagLabelFont = FCoreStyle::Get().GetFontStyle(TEXT("SmallFont")); // Font for flag labels
    const float FlagBoxSize = 16.0f;
    const float FlagPadding = 4.0f;
    const float TotalFlagHeightWithPadding = FlagBoxSize + FlagPadding; // This was TotalFlagHeight before
    float CurrentFlagY = FlagPadding;

    auto DrawFlag = [&](const FText& Label, bool bIsActive) {
//        if (CurrentFlagY + TotalFlagHeightWithPadding > FlagsAreaLocalSize.Y) return; 

		FSlateLayoutTransform BoxTransform(FVector2D(FlagPadding, CurrentFlagY+ValueLabelSize.Y+FlagPadding));
        FPaintGeometry BoxPaintGeometry = FlagsGeometry.ToPaintGeometry(FVector2D(FlagBoxSize, FlagBoxSize), BoxTransform);
        
        FSlateDrawElement::MakeBox(OutDrawElements, CurrentLayer, BoxPaintGeometry, WhiteBrush, ESlateDrawEffect::None, FLinearColor::White);
        
        FLinearColor FillColor = bIsActive ? FLinearColor::White : FLinearColor::Black;
        FVector2D BoxPosition = FVector2D(FlagPadding, CurrentFlagY+ValueLabelSize.Y+FlagPadding);
		FSlateLayoutTransform BoxTransform2(BoxPosition + FVector2D(1.0f, 1.0f));
        FPaintGeometry FillPaintGeometry = FlagsGeometry.ToPaintGeometry(FVector2D(FlagBoxSize - 2.0f, FlagBoxSize - 2.0f), BoxTransform2);
        FSlateDrawElement::MakeBox(OutDrawElements, CurrentLayer + 1, FillPaintGeometry, WhiteBrush, ESlateDrawEffect::None, FillColor);

        FVector2D TextPosition = FVector2D(FlagPadding, CurrentFlagY - 2 + (FlagBoxSize * 0.5f) - (FlagLabelFont.Size * 0.5f) ); // Use FlagLabelFont
		FSlateLayoutTransform BoxTransform3(TextPosition);
        FSlateDrawElement::MakeText(
            OutDrawElements,
            CurrentLayer + 2,
            FlagsGeometry.ToPaintGeometry(FVector2D(FlagsAreaLocalSize.X - (FlagBoxSize + 1 * FlagPadding), FlagBoxSize), BoxTransform3),
            Label,
            FlagLabelFont, // Use FlagLabelFont
            ESlateDrawEffect::None,
            FLinearColor::White
        );
        CurrentFlagY += TotalFlagHeightWithPadding + ValueLabelSize.Y+FlagPadding; // Use consistent variable name
    };

    DrawFlag(FText::FromString(TEXT("RDY")), IsLlamaCoreActuallyReadyInternal.Get());
    DrawFlag(FText::FromString(TEXT("SWI")), IsStaticWorldInfoUpToDateInternal.Get());
    DrawFlag(FText::FromString(TEXT("LFS")), IsLowFrequencyStateUpToDateInternal.Get());
    DrawFlag(FText::FromString(TEXT("IDL")), IsLlamaCurrentlyIdleInternal.Get());
    CurrentLayer += 3;

    // --- 3. Draw Vertical Performance Bars on the Right Side (Below Flags) ---    
    float PerfBarAreaStartY = CurrentFlagY + 40.0f; 
    float PerfBarWidth = 16.0f; 
    float PerfBarMaxHeight = FlagsAreaLocalSize.Y * 0.15f; // Adjusted max height slightly for labels
    float PerfBarSpacingX = 0.0f; 
    float PerfBarSpacingY = 10.0f; 
    float PerfBarPadding = 4.0f; 
    float CurrentPerfBarX = FlagPadding; 
    float CurrentPerfBarY = PerfBarAreaStartY; 

    auto DrawVerticalPerfBar = 
        [&](const FText& Label, float ValueMs, float MaxExpectedMs, const FLinearColor& BarColor) 
    {
//        if (CurrentPerfBarX + PerfBarWidth > FlagsAreaLocalSize.X) return; 
//        // Check for vertical space for bar + value label + descriptive label
//        if (CurrentPerfBarY + PerfBarMaxHeight + PerfBarLabelFont.Size * 2 + FlagPadding * 2 > FlagsAreaLocalSize.Y) return;

		FSlateLayoutTransform BoxTransform(FVector2D(CurrentPerfBarX, CurrentPerfBarY+2*ValueLabelSize.Y+2*PerfBarPadding));
        FPaintGeometry BarBgGeometry = FlagsGeometry.ToPaintGeometry(
            FVector2D(PerfBarWidth, PerfBarMaxHeight), BoxTransform
        );
        FSlateDrawElement::MakeBox(OutDrawElements, CurrentLayer, BarBgGeometry, WhiteBrush, ESlateDrawEffect::None, FLinearColor(0.2f, 0.2f, 0.2f));

        float NormalizedValue = (MaxExpectedMs > 0.001f) ? FMath::Clamp(ValueMs / MaxExpectedMs, 0.0f, 1.0f) : 0.0f;
        float ActualBarHeight = PerfBarMaxHeight * NormalizedValue;
        if (ActualBarHeight > 0) {
			FSlateLayoutTransform BoxTransform4(FVector2D(CurrentPerfBarX, CurrentPerfBarY+2*ValueLabelSize.Y+2*PerfBarPadding + (PerfBarMaxHeight - ActualBarHeight))); // Position is the translation
            FPaintGeometry BarFGGeometry = FlagsGeometry.ToPaintGeometry(
                FVector2D(PerfBarWidth, ActualBarHeight),
                BoxTransform4
            );
            FSlateDrawElement::MakeBox(OutDrawElements, CurrentLayer + 1, BarFGGeometry, WhiteBrush, ESlateDrawEffect::None, BarColor);
        }

        FString ValueTextStr = FString::Printf(TEXT("%gms"), ValueMs);
        FText ValueLabelText = FText::FromString(ValueTextStr);
//        float ValueLabelX = CurrentPerfBarX + (PerfBarWidth * 0.5f) - (ValueLabelSize.X * 0.5f);
//        float ValueLabelY = CurrentPerfBarY + PerfBarMaxHeight + FlagPadding * 0.5f;
        float ValueLabelX = CurrentPerfBarX;
        float ValueLabelY = CurrentPerfBarY + FlagPadding * 0.5f;

        // **** CORRECTED FOR ValueLabelText ****
        FSlateLayoutTransform ValueLabelTransform(FVector2D(ValueLabelX, ValueLabelY)); // Create transform from position
        FPaintGeometry ValueLabelPaintGeometry = FlagsGeometry.ToPaintGeometry(ValueLabelSize, ValueLabelTransform); // Use (Size, Transform) overload
        FSlateDrawElement::MakeText(OutDrawElements, CurrentLayer + 2,
            ValueLabelPaintGeometry, // Use the corrected geometry
            ValueLabelText, PerfBarLabelFont, ESlateDrawEffect::None, FLinearColor::White);

        FText DescriptiveLabel = Label;
        FVector2D DescriptiveLabelSize = FontMeasureService->Measure(DescriptiveLabel.ToString(), PerfBarLabelFont);
//        float DescriptiveLabelX = CurrentPerfBarX + (PerfBarWidth * 0.5f) - (DescriptiveLabelSize.X * 0.5f);
//        float DescriptiveLabelY = ValueLabelY + ValueLabelSize.Y + FlagPadding * 0.25f;
        float DescriptiveLabelX = CurrentPerfBarX;
        float DescriptiveLabelY = ValueLabelY + ValueLabelSize.Y + FlagPadding * 0.25f;

        // **** CORRECTED FOR DescriptiveLabel ****
        FSlateLayoutTransform DescriptiveLabelTransform(FVector2D(DescriptiveLabelX, DescriptiveLabelY)); // Create transform from position
        FPaintGeometry DescriptiveLabelPaintGeometry = FlagsGeometry.ToPaintGeometry(DescriptiveLabelSize, DescriptiveLabelTransform); // Use (Size, Transform) overload
         FSlateDrawElement::MakeText(OutDrawElements, CurrentLayer + 2, // Can use same layer or +1 if overlap is an issue
            DescriptiveLabelPaintGeometry, // Use the corrected geometry
            DescriptiveLabel, PerfBarLabelFont, ESlateDrawEffect::None, FLinearColor(0.7f,0.7f,0.7f));

        CurrentPerfBarX += PerfBarSpacingX;
        CurrentPerfBarY += PerfBarMaxHeight + PerfBarSpacingY + 2*ValueLabelSize.Y + 2*PerfBarPadding;
    };
    DrawVerticalPerfBar(FText::FromString(TEXT("DEC")), FmsPerTokenDecode.Get(), 10.0f, FLinearColor::Blue);
    DrawVerticalPerfBar(FText::FromString(TEXT("GEN")), FmsPerTokenGenerate.Get(), 100.0f, FLinearColor::Yellow);
//    DrawVerticalPerfBar(FText::FromString(TEXT("Frame")), PerfData.GameMetrics.FrameTimeMs, 33.3f, FLinearColor::Yellow);
//    DrawVerticalPerfBar(FText::FromString(TEXT("Prompt")), PerfData.LlamaMetrics.PromptEvalTimeMs, 1000.0f, FLinearColor::FromSRGBColor(FColor(0x41,0x69,0xE1)));
//    DrawVerticalPerfBar(FText::FromString(TEXT("Gen")), PerfData.LlamaMetrics.TokenGenerationTimeMs, 500.0f, FLinearColor::FromSRGBColor(FColor(0x3C,0xB3,0x71)));
////    DrawVerticalPerfBar(FText::FromString(TEXT("Avg/Tok")), PerfData.LlamaMetrics.AvgTimePerGeneratedTokenMs, 50.0f, FLinearColor::FromSRGBColor(FColor(0xBA,0x55,0xD3)));
//    DrawVerticalPerfBar(FText::FromString(TEXT("Avg/Tok")), 25.0f, 50.0f, FLinearColor::FromSRGBColor(FColor(0xBA,0x55,0xD3)));

    return CurrentLayer + 3;
}
