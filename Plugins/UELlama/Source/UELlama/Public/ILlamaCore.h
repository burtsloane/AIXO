#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "ContextVisualizationData.h"
#include "LlamaTypes.h"
#include "ILlamaCore.generated.h"

// Forward declarations
namespace Internal {
    class LlamaInternal;
}

UINTERFACE(MinimalAPI, meta=(CannotImplementInterfaceInBlueprint))
class ULlamaCoreInterface : public UInterface
{
    GENERATED_BODY()
};

class UELLAMA_API ILlamaCoreInterface
{
    GENERATED_BODY()

public:
    // Core operations
    virtual void Initialize(const FString& ModelPath, const FString& InitialSystemPrompt, const FString& Systems, const FString& LowFreq) = 0;
    virtual void Shutdown() = 0;
    virtual void ProcessInput(const FString& InputText, const FString& HighFrequencyContextText = TEXT(""), const FString& InputTypeHint = TEXT("")) = 0;
    
    // Context management
    virtual void UpdateContextBlock(ELlamaContextBlockType BlockType, const FString& NewContent) = 0;
    virtual void RequestFullContextDump() = 0;
    
    // State queries
    virtual bool IsGenerating() const = 0;
    
    // Callback setters
    virtual void SetTokenCallback(std::function<void(FString)> Callback) = 0;
    virtual void SetContextChangedCallback(std::function<void(const FContextVisPayload&)> Callback) = 0;
    virtual void SetErrorCallback(std::function<void(FString)> Callback) = 0;
    virtual void SetProgressCallback(std::function<void(float)> Callback) = 0;
    virtual void SetReadyCallback(std::function<void(FString)> Callback) = 0;
    virtual void SetToolCallCallback(std::function<void(FString)> Callback) = 0;
    virtual void SetIsGeneratingCallback(std::function<void(bool)> Callback) = 0;
}; 