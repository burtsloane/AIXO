#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "ContextVisualizationData.h"
#include "ILLMVisualizationInterface.generated.h"

// This interface defines the contract for any class that wants to visualize LLM state
UINTERFACE(MinimalAPI, Blueprintable)
class ULLMVisualizationInterface : public UInterface
{
    GENERATED_BODY()
};

class UELLAMA_API ILLMVisualizationInterface
{
    GENERATED_BODY()

public:
    // Called when the context visualization needs to be updated
    UFUNCTION(BlueprintNativeEvent, Category = "LLM|Visualization")
    void UpdateVisualization(const FContextVisPayload& Payload);

    // Called when a new token is generated
    UFUNCTION(BlueprintNativeEvent, Category = "LLM|Visualization")
    void HandleTokenGenerated(const FString& Token);

    // Called when the LLM state changes (e.g., generating, ready, error)
    UFUNCTION(BlueprintNativeEvent, Category = "LLM|Visualization")
    void HandleLLMStateChanged(bool bIsGenerating, const FString& StatusMessage);

    // Called when a full context dump is requested
    UFUNCTION(BlueprintNativeEvent, Category = "LLM|Visualization")
    void HandleFullContextDump(const FString& ContextDump);
}; 
