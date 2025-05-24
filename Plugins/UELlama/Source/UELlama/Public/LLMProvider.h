#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "LLMContextManager.h"
#include "ContextVisualizationData.h"
#include "LLMProvider.generated.h"

UINTERFACE(MinimalAPI, meta=(CannotImplementInterfaceInBlueprint))
class ULLMProvider : public UInterface
{
    GENERATED_BODY()
};

class UELLAMA_API ILLMProvider
{
    GENERATED_BODY()

public:
    // Core LLM operations
    virtual void Initialize(const FString& ModelPath) = 0;
    virtual void ProcessInput(const FString& Input, const FString& HighFreqContext) = 0;
    virtual void Shutdown() = 0;
    
    // Context management
    virtual void UpdateContextBlock(ELLMContextBlockType BlockType, const FString& NewContent) = 0;
    virtual ULLMContextManager* GetContextManager() const = 0;
    virtual void SetContextManager(ULLMContextManager* InContextManager) = 0;
    
    // Visualization
    virtual FContextVisPayload GetContextVisualization() const = 0;
    virtual void BroadcastContextUpdate(const FContextVisPayload& Payload) = 0;
    
    // Delegates - these need to be accessible to implementers
    DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnTokenGenerated, const FString&, Token);
    virtual FOnTokenGenerated& GetOnTokenGenerated() = 0;
    
    DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnContextChanged, const FContextVisPayload&, Payload);
    virtual FOnContextChanged& GetOnContextChanged() = 0;
}; 
