// LlamaComponent.cpp
// ReSharper disable CppPrintfBadFormat
#include "LlamaComponent.h"
#include "llama.h"
#include <time.h>
#include "common.h"
#include "Misc/FileHelper.h"      // **** ADD THIS FOR FILE OPERATIONS ****
#include "Misc/Paths.h"           // **** ADD THIS FOR PROJECT PATHS ****
#include "RemoteLLMProvider.h"
#include "LocalLlamaProvider.h"  // Your existing local provider
#include "Llama.h"  // For LlamaInternal
#include "HAL/PlatformTime.h"
#include <thread>
#include <chrono>

#define GGML_CUDA_DMMV_X 64
#define GGML_CUDA_F16
#define GGML_CUDA_MMV_Y 2
#define GGML_USE_CUBLAS
#define GGML_USE_K_QUANTS
#define K_QUANTS_PER_ITERATION 2

//#define FULL_SYSTEMS_DESC_IN_CONTEXT

////////////////////////////////////////////////////////////////////////////////////////////////

static std::vector<llama_token> my_llama_tokenize(
    const llama_model* model,
    const std::string& text,
    bool add_bos,
    bool special
) {
    UE_LOG(LogTemp, Log, TEXT("my_llama_tokenize: %hs"), text.c_str());
    std::vector<llama_token> res;
    res.resize(text.length() + (add_bos ? 1 : 0)); // A reasonable initial size
    int n = llama_tokenize(
        llama_model_get_vocab(model),
        text.c_str(),
        static_cast<int>(text.length()),
        res.data(),
        static_cast<int>(res.size()),
        add_bos,
        special
    );
    if (n < 0) {
        // Negative n means buffer was too small, abs(n) is required size
        res.resize(-n);
        n = llama_tokenize(llama_model_get_vocab(model), text.c_str(), static_cast<int>(text.length()), res.data(), static_cast<int>(res.size()), add_bos, special);
    }
    if (n >= 0) {
        res.resize(n);
    } else {
        UE_LOG(LogTemp, Error, TEXT("Error tokenizing input in my_llama_tokenize."));
        res.clear(); // Return empty on error
    }
    return res;
}

////////////////////////////////////////////////////////////////////////////////////////////////

// Component implementation starts here
ULlamaComponent::ULlamaComponent(const FObjectInitializer& ObjectInitializer)
    : Super(ObjectInitializer)
    , bIsLlamaGenerating(false)
    , bIsLlamaCoreReady(false)
    , bPendingStaticWorldInfoUpdate(false)
    , bPendingLowFrequencyStateUpdate(false)
    , Provider(nullptr)
{
    PrimaryComponentTick.bCanEverTick = true;
}

void ULlamaComponent::BeginPlay()
{
    Super::BeginPlay();
    
    // Initialize the provider based on configuration
    InitializeProvider();
    
    if (Provider)
    {
        // Set up callbacks
        Provider->SetTokenCallback([this](const FString& Token) {
            OnTokenGenerated.Broadcast(Token);
            HandleTokenGenerated(Token);
        });
        
        Provider->SetContextChangedCallback([this](const FContextVisPayload& Payload) {
            ForwardContextUpdateToGameThread(Payload);
        });
        
        Provider->SetErrorCallback([this](const FString& ErrorMsg) {
            OnLlamaErrorOccurred.Broadcast(ErrorMsg);
        });
        
        Provider->SetProgressCallback([this](float Progress) {
            OnLlamaLoadingProgressDelegate.Broadcast(Progress);
        });
        
        Provider->SetReadyCallback([this](const FString& ReadyMessage) {
            HandleProviderReady(ReadyMessage);
        });
        
        Provider->SetToolCallCallback([this](const FString& ToolCall) {
            OnToolCallDetected.Broadcast(ToolCall);
        });
        
        Provider->SetIsGeneratingCallback([this](bool bIsGenerating) {
            bIsLlamaGenerating = bIsGenerating;
        });
        
        // Initialize with model and prompts
        FString SystemPrompt = LoadSystemPrompt();
        FString Systems = MakeSystemsBlock();
        FString LowFreq = MakeStatusBlock();
        
        Provider->Initialize(PathToModel, SystemPrompt, Systems, LowFreq);
    }
}

void ULlamaComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    if (Provider)
    {
        Provider->Shutdown();
    }
    Super::EndPlay(EndPlayReason);
}

void ULlamaComponent::TickComponent(float DeltaTime, enum ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
    Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
    
    if (Provider)
    {
        // Process any pending tasks
        Provider->ProcessInput(TEXT(""), TEXT(""), TEXT(""));
    }
}

void ULlamaComponent::InitializeProvider()
{
    if (ProviderType == ELlamaProviderType::Local)
    {
        Provider = NewObject<ULocalLlamaProvider>(this);
    }
    else if (ProviderType == ELlamaProviderType::Remote)
    {
        // Use default system prompt for now
        FString SystemPrompt = LoadSystemPrompt();
        
        Provider = NewObject<ULocalLlamaProvider>(this);  // Temporarily use LocalProvider until RemoteProvider is implemented
        if (Provider)
        {
            Provider->Initialize(PathToModel, SystemPrompt, MakeSystemsBlock(), MakeStatusBlock());
        }
    }
}

void ULlamaComponent::HandleProviderReady(const FString& ReadyMessage)
{
    bIsLlamaCoreReady = true;
    OnLlamaReady.Broadcast(ReadyMessage);
}

void ULlamaComponent::HandleTokenGenerated(const FString& Token)
{
    // Implement any game-specific token handling here
}

void ULlamaComponent::ForwardContextUpdateToGameThread(const FContextVisPayload& Payload)
{
    // Forward to both the delegate and the interface
    OnLlamaContextChangedDelegate.Broadcast(Payload);
    if (VisualizationInterface)
    {
        VisualizationInterface->UpdateVisualization(Payload);
    }
}

void ULlamaComponent::UpdateContextBlock(ELlamaContextBlockType BlockType, const FString& NewTextContent)
{
    if (Provider)
    {
        Provider->UpdateContextBlock(BlockType, NewTextContent);
    }
}

void ULlamaComponent::SetVisualizationInterface(TScriptInterface<ILLMVisualizationInterface> InVisualizationInterface)
{
    VisualizationInterface = InVisualizationInterface;
}

FString ULlamaComponent::LoadSystemPrompt() const
{
    // For now, return a default system prompt
    return TEXT("You are a helpful AI assistant.");
}

void ULlamaComponent::Initialize(const FString& ModelPath)
{
    PathToModel = ModelPath;
    InitializeProvider();
}

void ULlamaComponent::ProcessInput(const FString& InputText, const FString& HighFrequencyContextText, const FString& InputTypeHint)
{
    if (!Provider || !bIsLlamaCoreReady)
    {
        OnLlamaErrorOccurred.Broadcast(TEXT("Llama is not ready to process input"));
        return;
    }

    Provider->ProcessInput(InputText, HighFrequencyContextText, InputTypeHint);
}

void ULlamaComponent::SetGameInterface(TScriptInterface<ILLMGameInterface> InGameInterface)
{
    GameInterface = InGameInterface;
}

void ULlamaComponent::HandleContextChanged(const FContextVisPayload& Payload)
{
    OnContextChanged.Broadcast(Payload);
    OnLlamaContextChangedDelegate.Broadcast(Payload);
}

FString ULlamaComponent::MakeSystemsBlock() const
{
    FString SystemsBlock;
    if (GameInterface)
    {
        SystemsBlock = GameInterface->GetSystemsBlock();
    }
    return SystemsBlock;
}

FString ULlamaComponent::MakeStatusBlock() const
{
    FString StatusBlock;
    if (GameInterface)
    {
        StatusBlock = GameInterface->GetStatusBlock();
    }
    return StatusBlock;
}

void ULlamaComponent::HandleToolCall_GetSystemInfo(const FString& QueryString)
{
    if (!GameInterface)
    {
        return;
    }

    FString Response = GameInterface->GetSystemInfo(QueryString);
    SendToolResponseToLlama(TEXT("GetSystemInfo"), Response);
}

void ULlamaComponent::HandleToolCall_CommandSubmarineSystem(const FString& QueryString)
{
    if (!GameInterface)
    {
        return;
    }

    FString Response = GameInterface->CommandSubmarineSystem(QueryString);
    SendToolResponseToLlama(TEXT("CommandSubmarineSystem"), Response);
}

void ULlamaComponent::HandleToolCall_QuerySubmarineSystem(const FString& QueryString)
{
    if (!GameInterface)
    {
        return;
    }

    FString Response = GameInterface->QuerySubmarineSystem(QueryString);
    SendToolResponseToLlama(TEXT("QuerySubmarineSystem"), Response);
}

void ULlamaComponent::SendToolResponseToLlama(const FString& ToolName, const FString& JsonResponseContent)
{
    if (!Provider)
    {
        return;
    }

    // Format the tool response as a JSON string
    FString ToolResponse = FString::Printf(TEXT("{\"tool\": \"%s\", \"response\": %s}"), *ToolName, *JsonResponseContent);
    Provider->ProcessInput(ToolResponse, TEXT(""), TEXT("tool_response"));
}


