#pragma once

#include "CoreMinimal.h"
#include "UObject/NoExportTypes.h"
#include "ILlamaCore.h"
#include "LlamaTypes.h"
#include "LlamaCore.generated.h"

namespace Internal {
    class LlamaInternal;
}

UCLASS()
class UELLAMA_API ULlamaCore : public UObject, public ILlamaCoreInterface
{
    GENERATED_BODY()

public:
    ULlamaCore();
    virtual ~ULlamaCore();

    // ILlamaCoreInterface implementation
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

private:
    std::unique_ptr<Internal::LlamaInternal> LlamaInternal;
}; 
