#include "LocalLlamaProvider.h"
#include "LlamaCore.h"

ULocalLlamaProvider::ULocalLlamaProvider(const FObjectInitializer& ObjectInitializer)
{
    // Create LlamaCore instance as a default subobject with a proper name
    LlamaCore = ObjectInitializer.CreateDefaultSubobject<ULlamaCore>(this, TEXT("LlamaCore"));
}

ULocalLlamaProvider::~ULocalLlamaProvider()
{
    if (LlamaCore)
    {
        LlamaCore->Shutdown();
    }
}

void ULocalLlamaProvider::Initialize(const FString& ModelPath)
{
    if (!LlamaCore)
    {
        UE_LOG(LogTemp, Error, TEXT("ULocalLlamaProvider::Initialize - LlamaCore is null!"));
        return;
    }

    // Set up callbacks
    LlamaCore->SetTokenCallback([this](FString Token) {
        UE_LOG(LogTemp, Log, TEXT("LocalLlamaProvider: Token callback received: %s"), *Token);
        // Broadcast token to any listeners
        OnTokenGenerated.Broadcast(Token);
    });

    LlamaCore->SetContextChangedCallback([this](const FContextVisPayload& Payload) {
        UE_LOG(LogTemp, Log, TEXT("LocalLlamaProvider: Context changed callback received"));
        OnContextChanged.Broadcast(Payload);
    });

    LlamaCore->SetErrorCallback([this](FString Error) {
        UE_LOG(LogTemp, Error, TEXT("LocalLlamaProvider: LlamaCore error: %s"), *Error);
    });

    LlamaCore->SetProgressCallback([this](float Progress) {
        UE_LOG(LogTemp, Log, TEXT("LocalLlamaProvider: Progress update: %.2f"), Progress);
    });

    LlamaCore->SetReadyCallback([this](FString Message) {
        UE_LOG(LogTemp, Log, TEXT("LocalLlamaProvider: Llama is ready: %s"), *Message);
        OnReady.Broadcast(Message);
    });

    LlamaCore->SetToolCallCallback([this](FString ToolCall) {
        UE_LOG(LogTemp, Log, TEXT("LocalLlamaProvider: Tool call received: %s"), *ToolCall);
    });

    LlamaCore->SetIsGeneratingCallback([this](bool bIsGenerating) {
        UE_LOG(LogTemp, Log, TEXT("LocalLlamaProvider: Generation state changed: %s"), bIsGenerating ? TEXT("Generating") : TEXT("Idle"));
    });

    // Get context blocks from the context manager if available
    FString SystemPrompt = TEXT("");
    FString SystemsInfo = TEXT("");
    FString LowFreqState = TEXT("");

    if (ContextManager)
    {
        SystemPrompt = ContextManager->GetSystemPrompt();
        SystemsInfo = ContextManager->GetSystemsInfo();
        LowFreqState = ContextManager->GetLowFreqState();
        UE_LOG(LogTemp, Log, TEXT("LocalLlamaProvider: Retrieved context blocks from manager - SystemPrompt: %d chars, SystemsInfo: %d chars, LowFreqState: %d chars"),
            SystemPrompt.Len(), SystemsInfo.Len(), LowFreqState.Len());
    }
    else
    {
        UE_LOG(LogTemp, Warning, TEXT("LocalLlamaProvider: No context manager available"));
    }

    // Initialize with context blocks
    LlamaCore->Initialize(ModelPath, SystemPrompt, SystemsInfo, LowFreqState);
}

void ULocalLlamaProvider::ProcessInput(const FString& Input, const FString& HighFreqContext)
{
    if (!LlamaCore)
    {
        UE_LOG(LogTemp, Error, TEXT("ULocalLlamaProvider::ProcessInput - LlamaCore is null!"));
        return;
    }

    LlamaCore->ProcessInput(Input, HighFreqContext, TEXT(""));
}

void ULocalLlamaProvider::Shutdown()
{
    if (LlamaCore)
    {
        LlamaCore->Shutdown();
    }
}

void ULocalLlamaProvider::UpdateContextBlock(ELLMContextBlockType BlockType, const FString& NewContent)
{
    if (!LlamaCore)
    {
        UE_LOG(LogTemp, Error, TEXT("ULocalLlamaProvider::UpdateContextBlock - LlamaCore is null!"));
        return;
    }

    // Convert from LLMContextBlockType to LlamaContextBlockType
    ELlamaContextBlockType LlamaBlockType;
    switch (BlockType)
    {
        case ELLMContextBlockType::SystemPrompt:
            LlamaBlockType = ELlamaContextBlockType::SystemPrompt;
            break;
        case ELLMContextBlockType::StaticWorldInfo:
            LlamaBlockType = ELlamaContextBlockType::StaticWorldInfo;
            break;
        case ELLMContextBlockType::LowFrequencyState:
            LlamaBlockType = ELlamaContextBlockType::LowFrequencyState;
            break;
        default:
            UE_LOG(LogTemp, Warning, TEXT("ULocalLlamaProvider::UpdateContextBlock - Unsupported block type"));
            return;
    }

    LlamaCore->UpdateContextBlock(LlamaBlockType, NewContent);
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
