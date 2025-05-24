// LLamaComponent.h
#pragma once

// Engine includes first
#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Templates/SharedPointer.h"
#include "Templates/UniquePtr.h"

// Plugin includes
#include "LlamaTypes.h"
#include "ContextVisualizationData.h"
#include "ILlamaProvider.h"
#include "ILLMVisualizationInterface.h"
#include "ILLMGameInterface.h"
#include "ILlamaCore.h"

// Third-party includes
#include "llama-cpp.h"
#include "ConcurrentQueue.h"
#include "Llama.h"  // Contains LlamaInternal definition

// STL includes last
#include <memory>
#include <atomic>
#include <deque>
#include <thread>
#include <functional>
#include <mutex>

#include "LlamaComponent.generated.h"

// ./llama-server -m /path/to/model.gguf --host 0.0.0.0 --port 8080 --n-predict 2048 --ctx-size 4096 --streaming
//The key parameters are:
//-m: Path to your model file
//--host: Set to 0.0.0.0 to accept connections from other machines
//--port: Port to listen on (default is 8080)
//--n-predict: Maximum tokens to generate
//--ctx-size: Context window size
//--streaming: Enable streaming responses
//To use this with our remote provider:
//Start the server on your GPU machine
//In the Unreal Editor:
//Set bUseLocalLlama = false on your LlamaComponent
//Set the RemoteEndpoint to http://your.gpu.machine.ip:8080/completion

//#define TRACK_PARALLEL_CONTEXT_TOKENS

// Remove VisualTestHarnessActor forward declaration
// class AVisualTestHarnessActor;

namespace
{
    constexpr int n_threads = 4;

    struct Params
    {
        FString prompt;
        FString pathToModel;
        std::vector<FString> stopSequences;
    };
} // namespace

// Remove the LlamaInternal class definition since it's now in Llama.h

// Delegates
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnNewTokenGenerated, const FString&, Token);
//DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnFullContextDumpReady, const FString&, ContextDump);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnToolCallDetected, const FString&, ToolCall);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnLlamaErrorOccurred, const FString&, ErrorMessage);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnLlamaLoadingProgressDelegate, float, Progress);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnLlamaReady, const FString&, ReadyMessage);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnContextChanged, const FContextVisPayload&, Payload);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnTokenGenerated, const FString&, Token);

// Forward declarations
namespace Internal {
    class LlamaInternal;
}

UENUM(BlueprintType)
enum class ELlamaProviderType : uint8
{
    Local,
    Remote
};

UCLASS(Category = "LLM", BlueprintType, meta = (BlueprintSpawnableComponent))
class UELLAMA_API ULlamaComponent : public UActorComponent
{
    GENERATED_BODY()

public:
    ULlamaComponent(const FObjectInitializer& ObjectInitializer);
    virtual ~ULlamaComponent();

    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
    virtual void TickComponent(float DeltaTime, enum ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

    // Core functionality
    UFUNCTION(BlueprintCallable, Category = "Llama")
    void Initialize(const FString& ModelPath);

    UFUNCTION(BlueprintCallable, Category = "Llama")
    void ProcessInput(const FString& InputText, const FString& HighFrequencyContextText = TEXT(""), const FString& InputTypeHint = TEXT(""));

    UFUNCTION(BlueprintCallable, Category = "Llama")
    void UpdateContextBlock(ELlamaContextBlockType BlockType, const FString& NewTextContent);

    // Interface setters
    UFUNCTION(BlueprintCallable, Category = "LLM")
    void SetGameInterface(TScriptInterface<ILLMGameInterface> InGameInterface);

    UFUNCTION(BlueprintCallable, Category = "LLM|Visualization")
    void SetVisualizationInterface(TScriptInterface<ILLMVisualizationInterface> InVisualizationInterface);

    // State queries
    UFUNCTION(BlueprintPure, Category = "Llama")
    bool IsLlamaBusy() const { return bIsLlamaGenerating; }

    UFUNCTION(BlueprintPure, Category = "Llama")
    bool IsLlamaReady() const { return bIsLlamaCoreReady; }

    // Delegates
    UPROPERTY(BlueprintAssignable)
    FOnNewTokenGenerated OnNewTokenGenerated;

//    UPROPERTY(BlueprintAssignable, Category = "Llama|Debug")
//    FOnFullContextDumpReady OnFullContextDumpReady;

    UPROPERTY(BlueprintAssignable, Category = "Llama")
    FOnToolCallDetected OnToolCallDetected;

    UPROPERTY(BlueprintAssignable, Category = "Llama")
    FOnLlamaErrorOccurred OnLlamaErrorOccurred;

    UPROPERTY(BlueprintAssignable, Category = "Llama")
    FOnLlamaLoadingProgressDelegate OnLlamaLoadingProgressDelegate;

    UPROPERTY(BlueprintAssignable, Category = "Llama")
    FOnLlamaReady OnLlamaReady;

    UPROPERTY(BlueprintAssignable, Category = "Llama")
    FOnContextChanged OnContextChanged;

    UPROPERTY(BlueprintAssignable, Category = "Llama")
    FOnTokenGenerated OnTokenGenerated;

    UPROPERTY(BlueprintAssignable, Category = "Llama")
    FOnContextChanged OnLlamaContextChangedDelegate;

    // Configuration
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Llama|Config")
    FString PathToModel;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Llama|Config", meta = (MultiLine = true))
    FString SystemPromptFileName;

    UPROPERTY(EditAnywhere, Category = "LLM")
    bool bUseLocalLlama = true;
    
    UPROPERTY(EditAnywhere, Category = "LLM", meta = (EditCondition = "!bUseLocalLlama"))
    FString RemoteEndpoint;
    
    UPROPERTY(EditAnywhere, Category = "LLM", meta = (EditCondition = "!bUseLocalLlama"))
    FString APIKey;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Llama")
    ELlamaProviderType ProviderType = ELlamaProviderType::Local;

protected:
    // Internal state
    TUniquePtr<Internal::LlamaInternal> LlamaInternal;  // Use Unreal's TUniquePtr
    std::atomic<bool> bIsLlamaGenerating;
    bool bIsLlamaCoreReady;
    FString SystemsContextBlockRecent;
    FString LowFreqContextBlockRecent;
    bool bPendingStaticWorldInfoUpdate;
    FString PendingStaticWorldInfoText;
    bool bPendingLowFrequencyStateUpdate;
    FString PendingLowFrequencyStateText;
    ILlamaProvider* Provider;  // Fixed typo from ILLMProvider to ILlamaProvider

    // Context management
    void UpdateSystemPrompt(const FString& NewPrompt);
    void UpdateStaticWorldInfo(const FString& NewInfo);
    void UpdateLowFrequencyState(const FString& NewState);
    
    // Token handling
    UFUNCTION(BlueprintNativeEvent, Category = "LLM")
    void HandleTokenGenerated(const FString& Token);
    
    // Context updates
    UFUNCTION(Category = "Llama|Context")
    void HandleContextChanged(const FContextVisPayload& Payload);
    void ForwardContextUpdateToGameThread(const FContextVisPayload& LlamaThreadPayload);

private:
    // Game interface helpers
    void InitializeProvider();  // Only declare once
    void CreateProviderInstance(); // Creates and returns the appropriate provider instance based on configuration
    FString MakeSystemsBlock();  // Only declare once
    FString MakeStatusBlock();  // Only declare once
    FString LoadSystemPrompt() const;  // Add declaration here
    void HandleToolCall_GetSystemInfo(const FString& QueryString);
    void HandleToolCall_CommandSubmarineSystem(const FString& QueryString);
    void HandleToolCall_QuerySubmarineSystem(const FString& QueryString);
    void SendToolResponseToLlama(const FString& ToolName, const FString& JsonResponseContent);
    UFUNCTION()
    void HandleProviderReady(const FString& ReadyMessage);

    // Queue processing
    bool ProcessQueue() { return LlamaInternal ? LlamaInternal->qMainToLlama.processQ() : false; }
    bool ProcessResponseQueue() { return LlamaInternal ? LlamaInternal->qLlamaToMain.processQ() : false; }

    // Game interface
    UPROPERTY()
    TScriptInterface<ILLMGameInterface> GameInterface;

    UPROPERTY()
    TScriptInterface<ILLMVisualizationInterface> VisualizationInterface;
};

