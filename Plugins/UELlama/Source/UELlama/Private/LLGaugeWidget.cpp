// LLGaugeWidget.cpp
#include "LLGaugeWidget.h"
#include "SLLContextVisualizer.h"   // The Slate widget header
#include "LlamaComponent.h"       // The LlamaComponent header
#include "Components/Border.h"    // Example: If you use a UBorder in BP as a host
#include "Components/SizeBox.h"   // Example: If you use a USizeBox in BP as a host
#include "Slate/SObjectWidget.h"  // For SNew(SObjectWidget).TakeWidget() if needed

ULLGaugeWidget::ULLGaugeWidget(const FObjectInitializer& ObjectInitializer)
    : Super(ObjectInitializer)
{
    // Default values if needed
    CachedTotalCapacity = 32768;
    CachedKvCacheCount = 0;
}

// This is where UMG asks our UUserWidget to provide its Slate widget counterpart.
TSharedRef<SWidget> ULLGaugeWidget::RebuildWidget()
{
    // If we haven't created our Slate visualizer yet, or if it became invalid, create it now.
    if (!SlateVisualizerWidget.IsValid())
    {
        SAssignNew(SlateVisualizerWidget, SLLContextVisualizer)
            .TotalTokenCapacity(TAttribute<int32>::Create(TAttribute<int32>::FGetter::CreateUObject(this, &ULLGaugeWidget::GetCachedTotalCapacity)))
            .ContextBlocks(TAttribute<TArray<FContextVisBlock>>::Create(TAttribute<TArray<FContextVisBlock>>::FGetter::CreateUObject(this, &ULLGaugeWidget::GetCachedBlocks)))
            .KvCacheDecodedTokenCount(TAttribute<int32>::Create(TAttribute<int32>::FGetter::CreateUObject(this, &ULLGaugeWidget::GetCachedKvCacheCount)))
			.IsStaticWorldInfoUpToDate(TAttribute<bool>::Create(TAttribute<bool>::FGetter::CreateUObject(this, &ULLGaugeWidget::GetCachedStaticWorldInfoUpToDate)))
			.IsLowFrequencyStateUpToDate(TAttribute<bool>::Create(TAttribute<bool>::FGetter::CreateUObject(this, &ULLGaugeWidget::GetCachedLowFrequencyStateUpToDate)))
			.IsLlamaCoreActuallyReady(TAttribute<bool>::Create(TAttribute<bool>::FGetter::CreateUObject(this, &ULLGaugeWidget::GetCachedIsLlamaCoreActuallyReady)))
			.IsLlamaCurrentlyIdle(TAttribute<bool>::Create(TAttribute<bool>::FGetter::CreateUObject(this, &ULLGaugeWidget::GetCachedIsLlamaCurrentlyIdle)))
			.FmsPerTokenDecode(TAttribute<float>::Create(TAttribute<float>::FGetter::CreateUObject(this, &ULLGaugeWidget::GetCachedmsPerTokenDecode)))
			.FmsPerTokenGenerate(TAttribute<float>::Create(TAttribute<float>::FGetter::CreateUObject(this, &ULLGaugeWidget::GetCachedmsPerTokenGenerate)));
//		    .PerformanceData(TAttribute<FPerformanceUIPayload>::Create(TAttribute<FPerformanceUIPayload>::FGetter::CreateUObject(this, &ULLGaugeWidget::GetCachedPerformanceData)));
    }

    // Important: UUserWidget needs to return a UWidget.
    // SNew(SObjectWidget).TakeWidget() is a common way to wrap a TSharedRef<SWidget> for UMG.
    // However, if this UMG widget's *entire purpose* is to BE this Slate widget,
    // and its Blueprint has NO other UMG elements, you can often just return the Slate widget.
    // The UMG system is usually smart enough to wrap it.
    // If you have a UMG hierarchy designed in the Blueprint editor (e.g., this widget is inside a Border),
    // then you would add SlateVisualizerWidget to a named slot in NativeConstruct instead, and
    // RebuildWidget might just return Super::RebuildWidget() or a simple SNullWidget if the root is BP-defined.

    // For a UUserWidget that *is* essentially just a Slate widget:
    return SlateVisualizerWidget.ToSharedRef();
    // If problems, try: return SNew(SObjectWidget).TakeWidget(SlateVisualizerWidget.ToSharedRef());
}

void ULLGaugeWidget::NativeConstruct()
{
    Super::NativeConstruct();

    // RebuildWidget() will be called by the system to get the Slate widget.
    // If TargetLlamaComponent was set as an exposed property on spawn, try to bind now.
//    if (TargetLlamaComponent)
//    {
//        SetLlamaComponent(TargetLlamaComponent);
//    }
}

void ULLGaugeWidget::NativeDestruct()
{
    // Unbind from the delegate when the widget is destroyed
//	if (TargetLlamaComponent->OnLlamaContextChangedDelegate.IsBound()) {
//		TargetLlamaComponent->OnLlamaContextChangedDelegate.RemoveDynamic(this, &ULLGaugeWidget::HandleLlamaContextChanged);
//	}
    Super::NativeDestruct();
}

//void ULLGaugeWidget::SetLlamaComponent(ULlamaComponent* InLlamaComponent)
//{
//    // Unbind from old component, if any
////    if (TargetLlamaComponent && TargetLlamaComponent->OnLlamaContextChangedDelegate.IsBound())
////    {
////        TargetLlamaComponent->OnLlamaContextChangedDelegate.RemoveDynamic(this, &ULLGaugeWidget::HandleLlamaContextChanged);
////    }
//
////    TargetLlamaComponent = InLlamaComponent;
//
////    // Bind to new component's delegate
////    if (TargetLlamaComponent)
////    {
////        TargetLlamaComponent->OnLlamaContextChangedDelegate.AddDynamic(this, &ULLGaugeWidget::HandleLlamaContextChanged);
////        UE_LOG(LogTemp, Log, TEXT("LLGaugeWidget: Bound to LlamaComponent %s"), *TargetLlamaComponent->GetName());
////
////        // Optional: Request an initial update if the LlamaComponent supports it
////        // This would require LlamaComponent to have a method that broadcasts its current state.
////        // For example:
////        // if (TargetLlamaComponent->IsLlamaCoreReady()) // Assuming LlamaComponent has such a flag
////        // {
////        //    TargetLlamaComponent->TriggerContextVisualUpdateBroadcast(); // You'd add this method to LlamaComponent
////        // }
////    }
////    else
////    {
////        UE_LOG(LogTemp, Warning, TEXT("LLGaugeWidget: SetLlamaComponent called with null. Unbound."));
////    }
//}

void ULLGaugeWidget::HandleLlamaContextChanged(const FContextVisPayload& NewContextState)
{
    // UE_LOG(LogTemp, Log, TEXT("LLGaugeWidget: HandleLlamaContextChanged received. Blocks: %d"), NewContextState.Blocks.Num());

    // Update cached values that the Slate widget's attributes are bound to
    CachedBlocks = NewContextState.Blocks;
    CachedTotalCapacity = NewContextState.TotalTokenCapacity;
    CachedKvCacheCount = NewContextState.KvCacheDecodedTokenCount;
    bCachedStaticWorldInfoUpToDate = NewContextState.bIsStaticWorldInfoUpToDate;
    bCachedLowFrequencyStateUpToDate = NewContextState.bIsLowFrequencyStateUpToDate;
    bCachedIsLlamaCoreActuallyReady = NewContextState.bIsLlamaCoreActuallyReady;
    bCachedIsLlamaCurrentlyIdle = NewContextState.bIsLlamaCurrentlyIdle;
    if (NewContextState.fFmsPerTokenDecode > 0) CachedmsPerTokenDecode = NewContextState.fFmsPerTokenDecode;
    if (NewContextState.fFmsPerTokenGenerate > 0) CachedmsPerTokenGenerate = NewContextState.fFmsPerTokenGenerate;

//UE_LOG(LogTemp, Log, TEXT("GaugeWidget: HandleContextChanged - Cached: %d %g %g"), bCachedStaticWorldInfoUpToDate, CachedmsPerTokenDecode, CachedmsPerTokenGenerate);

    // The Slate widget (SLLContextVisualizer) uses TAttributes that poll these GetCached... functions.
    // To ensure it repaints with the new data, we might need to tell it its underlying data changed.
    // However, often just changing the UPROPERTY members that TAttributes point to is enough
    // if the TAttributes are set up with FGetter::CreateUObject.
    // If SLLContextVisualizer itself has Set... methods that call Invalidate, that's cleaner.
    // Let's assume SLLContextVisualizer's SetContextBlocks etc. call Invalidate(EInvalidateWidget::Paint)
    if (SlateVisualizerWidget.IsValid())
    {
        // This direct setting is an alternative to attribute polling if you prefer push over pull
        // SlateVisualizerWidget->SetContextBlocks(CachedBlocks);
        // SlateVisualizerWidget->SetTotalTokenCapacity(CachedTotalCapacity);
        // SlateVisualizerWidget->SetKvCacheDecodedTokenCount(CachedKvCacheCount);
        // If using attributes, the getters will provide the new values.
        // We just need to ensure Slate knows to repaint.
        SlateVisualizerWidget->Invalidate(EInvalidateWidget::Paint);
    }
}

// Getter functions for Slate TAttribute binding
TArray<FContextVisBlock> ULLGaugeWidget::GetCachedBlocks() const
{
    return CachedBlocks;
}

int32 ULLGaugeWidget::GetCachedTotalCapacity() const
{
    return CachedTotalCapacity;
}

int32 ULLGaugeWidget::GetCachedKvCacheCount() const
{
    return CachedKvCacheCount;
}

bool ULLGaugeWidget::GetCachedStaticWorldInfoUpToDate() const
{
    UE_LOG(LogTemp, Verbose, TEXT("GaugeWidget: GetCachedStaticWorldInfoUpToDate() returning %d"), bCachedStaticWorldInfoUpToDate);
    return bCachedStaticWorldInfoUpToDate;
}

bool ULLGaugeWidget::GetCachedLowFrequencyStateUpToDate() const
{
    UE_LOG(LogTemp, Verbose, TEXT("GaugeWidget: GetCachedLowFrequencyStateUpToDate() returning %d"), bCachedLowFrequencyStateUpToDate);
    return bCachedLowFrequencyStateUpToDate;
}

bool ULLGaugeWidget::GetCachedIsLlamaCoreActuallyReady() const
{
    UE_LOG(LogTemp, Verbose, TEXT("GaugeWidget: GetCachedIsLlamaCoreActuallyReady() returning %d"), bCachedIsLlamaCoreActuallyReady);
    return bCachedIsLlamaCoreActuallyReady;
}

bool ULLGaugeWidget::GetCachedIsLlamaCurrentlyIdle() const
{
    UE_LOG(LogTemp, Verbose, TEXT("GaugeWidget: GetCachedIsLlamaCurrentlyIdle() returning %d"), bCachedIsLlamaCurrentlyIdle);
    return bCachedIsLlamaCurrentlyIdle;
}

float ULLGaugeWidget::GetCachedmsPerTokenDecode() const
{
    UE_LOG(LogTemp, Verbose, TEXT("GaugeWidget: GetCachedmsPerTokenDecode() returning %g"), CachedmsPerTokenDecode);
    return CachedmsPerTokenDecode;
}

float ULLGaugeWidget::GetCachedmsPerTokenGenerate() const
{
    UE_LOG(LogTemp, Verbose, TEXT("GaugeWidget: GetCachedmsPerTokenGenerate() returning %g"), CachedmsPerTokenGenerate);
    return CachedmsPerTokenGenerate;
}

void ULLGaugeWidget::ReleaseSlateResources(bool bReleaseChildren)
{
    Super::ReleaseSlateResources(bReleaseChildren); // Always call Super

    // Release our TSharedPtr to the Slate widget.
    // This allows the Slate widget to be garbage collected if nothing else holds a reference.
    SlateVisualizerWidget.Reset();

//    UE_LOG(LogTemp, Log, TEXT("LLGaugeWidget: Slate resources released."));
}
