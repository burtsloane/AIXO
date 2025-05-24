#pragma once

#include "CoreMinimal.h"
#include "UObject/NoExportTypes.h"
#include "Templates/UniquePtr.h"
#include "ILlamaProvider.h"
#include "Llama.h"
#include "LlamaTypes.h"
#include "LocalLlamaProvider.generated.h"

class ULLMContextManager;

namespace Internal {
    class LlamaInternal;
}

UCLASS()
class UELLAMA_API ULocalLlamaProvider : public UObject, public ILlamaProvider
{
    GENERATED_BODY()

public:
    ULocalLlamaProvider(const FObjectInitializer& ObjectInitializer);
    virtual ~ULocalLlamaProvider();

    // ILlamaProvider implementation
    virtual void Initialize(const FString& ModelPath, const FString& InitialSystemPrompt, const FString& Systems, const FString& LowFreq) override;
    virtual void Shutdown() override;
    virtual void ProcessInput(const FString& InputText, const FString& HighFrequencyContextText = TEXT(""), const FString& InputTypeHint = TEXT("")) override;
    virtual void UpdateContextBlock(ELlamaContextBlockType BlockType, const FString& NewContent) override;
    virtual void RequestFullContextDump() override;
    virtual bool IsGenerating() const override;
    virtual void SetTokenCallback(std::function<void(FString)> Callback) override;
    virtual void SetContextChangedCallback(std::function<void(const FContextVisPayload&)> Callback) override;
    virtual void SetErrorCallback(std::function<void(FString)> Callback) override;
    virtual void SetProgressCallback(std::function<void(float)> Callback) override;
    virtual void SetReadyCallback(std::function<void(FString)> Callback) override;
    virtual void SetToolCallCallback(std::function<void(FString)> Callback) override;
    virtual void SetIsGeneratingCallback(std::function<void(bool)> Callback) override;

    // Context management
    ULLMContextManager* GetContextManager() const;
    void SetContextManager(ULLMContextManager* InContextManager);
    FContextVisPayload GetContextVisualization() const;
    void BroadcastContextUpdate(const FContextVisPayload& Payload);

private:
    TUniquePtr<Internal::LlamaInternal> LlamaInternal;
    UPROPERTY()
    ULLMContextManager* ContextManager;

    DECLARE_MULTICAST_DELEGATE_OneParam(FOnContextChanged, const FContextVisPayload&);
    FOnContextChanged OnContextChanged;
}; 
