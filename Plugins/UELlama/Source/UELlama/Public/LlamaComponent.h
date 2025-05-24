// LLamaComponent.h
#pragma once

#include <Components/ActorComponent.h>
#include <CoreMinimal.h>
#include "LLInternal.h"
#include "LLContextVisualizationData.h"
#include "LlamaComponent.generated.h"

// Delegates
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnNewTokenGenerated, FString, NewToken);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnFullContextDumpReady, const FString&, ContextDump);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnToolCallDetected, const FString&, ToolCallJson);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnLlamaErrorOccurred, const FString&, ErrorMessage);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnLlamaLoadingProgressDelegate, float, Progress);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnLlamaReady, const FString&, ReadyMessage);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnLlamaContextChangedDelegate, const FContextVisPayload&, ContextMessage);

UCLASS(Category = "LLM", BlueprintType, meta = (BlueprintSpawnableComponent))
class UELLAMA_API ULlamaComponent : public UActorComponent
{
    GENERATED_BODY()
public:
    ULlamaComponent(const FObjectInitializer& ObjectInitializer);
    ~ULlamaComponent();

    virtual void BeginPlay() override; // Changed from Activate for standard UE lifecycle
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override; // Changed from Deactivate
    virtual void TickComponent(float DeltaTime,
                               enum ELevelTick TickType,
                               FActorComponentTickFunction* ThisTickFunction) override;

	void ActivateLlamaComponent(FString SystemsContextBlock, FString LowFreqContextBlock);

public:		// setup in blueprints
	// setup variables, don't change these while running
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Llama|Config")
    FString PathToModel;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Llama|Config", meta = (MultiLine = true))
    FString SystemPromptFileName;

public:		// Delegates
    UPROPERTY(BlueprintAssignable)
    FOnNewTokenGenerated OnNewTokenGenerated;

    UPROPERTY(BlueprintAssignable, Category = "Llama|Debug")
    FOnFullContextDumpReady OnFullContextDumpReady;

    UPROPERTY(BlueprintAssignable, Category = "Llama")
    FOnToolCallDetected OnToolCallDetected;

    UPROPERTY(BlueprintAssignable, Category = "Llama")
    FOnLlamaErrorOccurred OnLlamaErrorOccurred;

    UPROPERTY(BlueprintAssignable, Category = "Llama")
    FOnLlamaLoadingProgressDelegate OnLlamaLoadingProgressDelegate;

    UPROPERTY(BlueprintAssignable, Category = "Llama")
    FOnLlamaReady OnLlamaReady;

    UPROPERTY(BlueprintAssignable, Category = "Llama")
    FOnLlamaContextChangedDelegate OnLlamaContextChangedDelegate;

public:	// main LLM control functions
    UFUNCTION(BlueprintCallable, Category = "Llama")
    void UpdateContextBlock(ELlamaContextBlockType BlockType, const FString& NewTextContent);

    UFUNCTION(BlueprintCallable, Category = "Llama")
    void ProcessInput(const FString& InputText, const FString& HighFrequencyContextText, const FString& InputTypeHint);

    UFUNCTION(BlueprintCallable, Category = "Llama|Debug")
    void TriggerFullContextDump();

public:
    UFUNCTION(BlueprintPure, Category = "Llama")
    bool IsLlamaBusy() const { return bIsLlamaGenerating; }

    UFUNCTION(BlueprintPure, Category = "Llama")
    bool IsLlamaReady() const { return bIsLlamaCoreReady; }

private:
    LLInternal* LlamaImpl;
    bool bIsLlamaGenerating; // This is set by Llama thread via a callback when it starts/stops generation.
    bool bIsLlamaCoreReady = false; // Set by callback from Llama thread
};

