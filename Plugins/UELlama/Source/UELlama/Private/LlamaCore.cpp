#include "LlamaCore.h"
#include "Llama.h"  // Include the full definition of LlamaInternal
#include "LlamaComponent.h"

ULlamaCore::ULlamaCore()
{
    LlamaInternal = std::make_unique<Internal::LlamaInternal>();
}

ULlamaCore::~ULlamaCore()
{
    Shutdown();
}

void ULlamaCore::Initialize(const FString& ModelPath, const FString& InitialSystemPrompt, const FString& Systems, const FString& LowFreq)
{
    if (LlamaInternal)
    {
        LlamaInternal->InitializeLlama_LlamaThread(ModelPath, InitialSystemPrompt, Systems, LowFreq);
    }
}

void ULlamaCore::Shutdown()
{
    if (LlamaInternal)
    {
        LlamaInternal->ShutdownLlama_LlamaThread();
    }
}

void ULlamaCore::ProcessInput(const FString& InputText, const FString& HighFrequencyContextText, const FString& InputTypeHint)
{
    if (LlamaInternal)
    {
        LlamaInternal->ProcessInputAndGenerate_LlamaThread(InputText, HighFrequencyContextText, InputTypeHint);
    }
}

void ULlamaCore::UpdateContextBlock(ELlamaContextBlockType BlockType, const FString& NewContent)
{
    if (LlamaInternal)
    {
        LlamaInternal->UpdateContextBlock_LlamaThread(BlockType, NewContent);
    }
}

void ULlamaCore::RequestFullContextDump()
{
    if (LlamaInternal)
    {
        LlamaInternal->RequestFullContextDump_LlamaThread();
    }
}

bool ULlamaCore::IsGenerating() const
{
    return LlamaInternal && LlamaInternal->bIsGenerating;
}

void ULlamaCore::SetTokenCallback(std::function<void(FString)> Callback)
{
    if (LlamaInternal)
    {
        LlamaInternal->tokenCb = Callback;
    }
}

void ULlamaCore::SetContextChangedCallback(std::function<void(const FContextVisPayload&)> Callback)
{
    if (LlamaInternal)
    {
        LlamaInternal->contextChangedCb = Callback;
    }
}

void ULlamaCore::SetErrorCallback(std::function<void(FString)> Callback)
{
    if (LlamaInternal)
    {
        LlamaInternal->errorCb = Callback;
    }
}

void ULlamaCore::SetProgressCallback(std::function<void(float)> Callback)
{
    if (LlamaInternal)
    {
        LlamaInternal->progressCb = Callback;
    }
}

void ULlamaCore::SetReadyCallback(std::function<void(FString)> Callback)
{
    if (LlamaInternal)
    {
        LlamaInternal->readyCb = Callback;
    }
}

void ULlamaCore::SetToolCallCallback(std::function<void(FString)> Callback)
{
    if (LlamaInternal)
    {
        LlamaInternal->toolCallCb = Callback;
    }
}

void ULlamaCore::SetIsGeneratingCallback(std::function<void(bool)> Callback)
{
    if (LlamaInternal)
    {
        LlamaInternal->setIsGeneratingCb = Callback;
    }
}

// ... rest of file ... 
