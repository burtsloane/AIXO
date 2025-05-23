#include "LocalLlamaProvider.h"
#include "LlamaComponent.h"

ULocalLlamaProvider::ULocalLlamaProvider()
{
    // Create internal llama instance
    LlamaInternal = new Internal::Llama();
}

void ULocalLlamaProvider::Initialize(const FString& ModelPath)
{
    if (!LlamaInternal)
    {
        return;
    }

    // Set up callbacks
    LlamaInternal->tokenCb = [this](const FString& Token) {
        OnTokenGenerated.Broadcast(Token);
    };

    LlamaInternal->contextChangedCb = [this](const FContextVisPayload& Payload) {
        OnContextChanged.Broadcast(Payload);
    };

    LlamaInternal->errorCb = [this](const FString& ErrorMsg) {
        // Forward error to token callback for now
        OnTokenGenerated.Broadcast(FString::Printf(TEXT("Error: %s"), *ErrorMsg));
    };

    LlamaInternal->progressCb = [this](float Progress) {
        // Could add a progress delegate if needed
    };

    LlamaInternal->readyCb = [this](const FString& ReadyMsg) {
        // Could add a ready delegate if needed
    };

    LlamaInternal->setIsGeneratingCb = [this](bool bIsGenerating) {
        // Update visualization state
        FContextVisPayload Payload = GetContextVisualization();
        Payload.bIsLlamaCurrentlyIdle = !bIsGenerating;
        OnContextChanged.Broadcast(Payload);
    };

    // Initialize llama with empty context blocks for now
    LlamaInternal->InitializeLlama_LlamaThread(ModelPath, TEXT(""), TEXT(""), TEXT(""));
}

void ULocalLlamaProvider::ProcessInput(const FString& Input, const FString& HighFreqContext)
{
    if (!LlamaInternal)
    {
        return;
    }

    LlamaInternal->ProcessInputAndGenerate_LlamaThread(Input, HighFreqContext, TEXT("user"));
}

void ULocalLlamaProvider::Shutdown()
{
    if (LlamaInternal)
    {
        LlamaInternal->ShutdownLlama_LlamaThread();
        delete LlamaInternal;
        LlamaInternal = nullptr;
    }
}

void ULocalLlamaProvider::UpdateContextBlock(ELLMContextBlockType BlockType, const FString& NewContent)
{
    if (!LlamaInternal)
    {
        return;
    }

    // Convert from ILLMProvider's block type to Internal::Llama's block type
    ELlamaContextBlockType InternalBlockType;
    switch (BlockType)
    {
        case ELLMContextBlockType::SystemPrompt:
            InternalBlockType = ELlamaContextBlockType::SystemPrompt;
            break;
        case ELLMContextBlockType::StaticWorldInfo:
            InternalBlockType = ELlamaContextBlockType::StaticWorldInfo;
            break;
        case ELLMContextBlockType::LowFrequencyState:
            InternalBlockType = ELlamaContextBlockType::LowFrequencyState;
            break;
        default:
            return; // Other block types are managed internally
    }

    LlamaInternal->UpdateContextBlock_LlamaThread(InternalBlockType, NewContent);
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
    FContextVisPayload Payload;
    
    if (LlamaInternal)
    {
        // Request a context dump from the llama thread
        LlamaInternal->RequestFullContextDump_LlamaThread();
        
        // The actual visualization data will be sent via the contextChangedCb
        // For now, return a basic payload
        Payload.TotalTokenCapacity = 32768; // Example value
        Payload.KvCacheDecodedTokenCount = 0;
        Payload.bIsLlamaCoreActuallyReady = true;
        Payload.bIsLlamaCurrentlyIdle = true;
    }
    
    return Payload;
}

void ULocalLlamaProvider::BroadcastContextUpdate(const FContextVisPayload& Payload)
{
    OnContextChanged.Broadcast(Payload);
} 