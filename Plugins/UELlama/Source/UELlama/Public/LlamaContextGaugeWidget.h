// LlamaContextGaugeWidget.h
#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "ContextVisualizationData.h" // Your struct definitions (FContextVisPayload, etc.)
#include "LlamaContextGaugeWidget.generated.h" // Must be last include in .h

// Forward declarations
class SContextVisualizer; // The Slate widget it will host
class ULlamaComponent;    // The component it will get data from

/**
 * UMG Widget to display the LLM context visualization bar.
 * This widget hosts the SContextVisualizer Slate widget.
 */
UCLASS()
class UELLAMA_API ULlamaContextGaugeWidget : public UUserWidget
{
    GENERATED_BODY()

public:
    ULlamaContextGaugeWidget(const FObjectInitializer& ObjectInitializer);

    // Call this from your game logic (e.g., HUD Blueprint, PlayerController)
    // to link this UI widget to a specific LlamaComponent instance.
    UFUNCTION(BlueprintCallable, Category = "LlamaUI")
    void SetLlamaComponent(ULlamaComponent* InLlamaComponent);

    // UPROPERTY to allow setting the LlamaComponent target in Blueprint editor (optional)
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LlamaUI", meta = (ExposeOnSpawn = "true"))
    TObjectPtr<ULlamaComponent> TargetLlamaComponent;

protected:
    // UUserWidget Overrides
    virtual void NativeConstruct() override;
    virtual void NativeDestruct() override;

    // This is the key UUserWidget method for integrating custom Slate widgets.
    // It's called by the UMG system when it needs to build the Slate representation of this UMG widget.
    virtual TSharedRef<SWidget> RebuildWidget() override;

    virtual void ReleaseSlateResources(bool bReleaseChildren) override;

private:
    // Pointer to the actual Slate widget instance that does the drawing
    TSharedPtr<SContextVisualizer> SlateVisualizerWidget;

    // Cached data received from the LlamaComponent's delegate.
    // The Slate widget's attributes will bind to getters for these.
    UPROPERTY() // Keep UPROPERTY for GC
    TArray<FContextVisBlock> CachedBlocks;
    UPROPERTY()
    int32 CachedTotalCapacity = 32768;
    UPROPERTY()
    int32 CachedKvCacheCount = 0;
    bool bCachedStaticWorldInfoUpToDate = true;
    bool bCachedLowFrequencyStateUpToDate = false;
    bool bCachedIsLlamaCoreActuallyReady = false;
    bool bCachedIsLlamaCurrentlyIdle = false;
    float CachedmsPerTokenDecode = 0.0f;
    float CachedmsPerTokenGenerate = 0.0f;

    // Handler function for the LlamaComponent's delegate
    UFUNCTION() // Must be UFUNCTION to bind to a dynamic multicast delegate
    void HandleLlamaContextChanged(const FContextVisPayload& NewContextState);

    // Getter functions for Slate attribute binding
    TArray<FContextVisBlock> GetCachedBlocks() const;
    int32 GetCachedTotalCapacity() const;
    int32 GetCachedKvCacheCount() const;
    bool GetCachedStaticWorldInfoUpToDate() const;
    bool GetCachedLowFrequencyStateUpToDate() const;
    bool GetCachedIsLlamaCoreActuallyReady() const;
    bool GetCachedIsLlamaCurrentlyIdle() const;
    float GetCachedmsPerTokenDecode() const;
    float GetCachedmsPerTokenGenerate() const;
};
