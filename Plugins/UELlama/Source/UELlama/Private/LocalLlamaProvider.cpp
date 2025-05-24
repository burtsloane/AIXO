#include "LocalLlamaProvider.h"

ULocalLlamaProvider::ULocalLlamaProvider(const FObjectInitializer& ObjectInitializer)
    : Super(ObjectInitializer)
    , ContextManager(nullptr)
{
    LlamaInternal = MakeUnique<Internal::LlamaInternal>();
}

ULocalLlamaProvider::~ULocalLlamaProvider()
{
    if (LlamaInternal)
    {
        LlamaInternal->SignalStopRunning();
        LlamaInternal.Reset();
    }
}

void ULocalLlamaProvider::Initialize(const FString& ModelPath, const FString& InitialSystemPrompt, const FString& Systems, const FString& LowFreq)
{
    if (LlamaInternal)
    {
        LlamaInternal->InitializeLlama_LlamaThread(ModelPath, InitialSystemPrompt, Systems, LowFreq);
    }
}

void ULocalLlamaProvider::Shutdown()
{
    if (LlamaInternal)
    {
        LlamaInternal->SignalStopRunning();
        LlamaInternal.Reset();
    }
}

void ULocalLlamaProvider::ProcessInput(const FString& InputText, const FString& HighFrequencyContextText, const FString& InputTypeHint)
{
    if (LlamaInternal)
    {
        LlamaInternal->ProcessInputAndGenerate_LlamaThread(InputText, HighFrequencyContextText, InputTypeHint);
    }
}

void ULocalLlamaProvider::UpdateContextBlock(ELlamaContextBlockType BlockType, const FString& NewContent)
{
    if (LlamaInternal)
    {
        LlamaInternal->UpdateContextBlock_LlamaThread(BlockType, NewContent);
    }
}

void ULocalLlamaProvider::RequestFullContextDump()
{
    if (LlamaInternal)
    {
        LlamaInternal->RequestFullContextDump_LlamaThread();
    }
}

bool ULocalLlamaProvider::IsGenerating() const
{
    return LlamaInternal && LlamaInternal->bIsGenerating;
}

void ULocalLlamaProvider::SetTokenCallback(std::function<void(FString)> Callback)
{
    if (LlamaInternal)
    {
        LlamaInternal->tokenCb = Callback;
    }
}

void ULocalLlamaProvider::SetContextChangedCallback(std::function<void(const FContextVisPayload&)> Callback)
{
    if (LlamaInternal)
    {
        LlamaInternal->contextChangedCb = Callback;
    }
}

void ULocalLlamaProvider::SetErrorCallback(std::function<void(FString)> Callback)
{
    if (LlamaInternal)
    {
        LlamaInternal->errorCb = Callback;
    }
}

void ULocalLlamaProvider::SetProgressCallback(std::function<void(float)> Callback)
{
    if (LlamaInternal)
    {
        LlamaInternal->progressCb = Callback;
    }
}

void ULocalLlamaProvider::SetReadyCallback(std::function<void(FString)> Callback)
{
    if (LlamaInternal)
    {
        LlamaInternal->readyCb = Callback;
    }
}

void ULocalLlamaProvider::SetToolCallCallback(std::function<void(FString)> Callback)
{
    if (LlamaInternal)
    {
        LlamaInternal->toolCallCb = Callback;
    }
}

void ULocalLlamaProvider::SetIsGeneratingCallback(std::function<void(bool)> Callback)
{
    if (LlamaInternal)
    {
        LlamaInternal->setIsGeneratingCb = Callback;
    }
}

ULLMContextManager* ULocalLlamaProvider::GetContextManager() const
{
    return ContextManager;
}

void ULocalLlamaProvider::SetContextManager(ULLMContextManager* InContextManager)
{
    ContextManager = InContextManager;
}

FContextVisPayload ULocalLlamaProvider::GetContextVisualization() const
{
    // Return empty payload for now - this should be implemented to return actual context state
    return FContextVisPayload();
}

void ULocalLlamaProvider::BroadcastContextUpdate(const FContextVisPayload& Payload)
{
    OnContextChanged.Broadcast(Payload);
} 
