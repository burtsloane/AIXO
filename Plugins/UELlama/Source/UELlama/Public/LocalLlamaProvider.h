#pragma once

#include "CoreMinimal.h"
#include "LLMProvider.h"
#include "LlamaComponent.h"
#include "LocalLlamaProvider.generated.h"

UCLASS()
class UELLAMA_API ULocalLlamaProvider : public UObject, public ILLMProvider
{
    GENERATED_BODY()

public:
    ULocalLlamaProvider();

    // ILLMProvider interface
    virtual void Initialize(const FString& ModelPath) override;
    virtual void ProcessInput(const FString& Input, const FString& HighFreqContext) override;
    virtual void Shutdown() override;
    virtual void UpdateContextBlock(ELLMContextBlockType BlockType, const FString& NewContent) override;
    virtual ULLMContextManager* GetContextManager() const override;
    virtual void SetContextManager(ULLMContextManager* InContextManager) override;
    virtual FContextVisPayload GetContextVisualization() const override;
    virtual void BroadcastContextUpdate(const FContextVisPayload& Payload) override;
    virtual FOnTokenGenerated& GetOnTokenGenerated() override { return OnTokenGenerated; }
    virtual FOnContextChanged& GetOnContextChanged() override { return OnContextChanged; }

private:
    // Context manager
    UPROPERTY()
    ULLMContextManager* ContextManager;

    // Internal llama instance
    class Internal::Llama* LlamaInternal;

    // Delegate instances
    FOnTokenGenerated OnTokenGenerated;
    FOnContextChanged OnContextChanged;
}; 