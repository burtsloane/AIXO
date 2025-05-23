// LLamaComponent.h
#pragma once

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

#include <Components/ActorComponent.h>
#include <CoreMinimal.h>
#include <memory>
#include <atomic>
#include <deque>
#include <thread>
#include <functional>
#include <mutex>
#include "llama.h"
//#include "VisualTestHarnessActor.h"

#include "ContextVisualizationData.h"
#include "LLMProvider.h"
#include "ILLMVisualizationInterface.h"
#include "ILLMGameInterface.h"

#include "LlamaComponent.generated.h"

//#define TRACK_PARALLEL_CONTEXT_TOKENS

// Remove VisualTestHarnessActor forward declaration
// class AVisualTestHarnessActor;

using namespace std;

namespace
{
	class Q
	{
	public:
		void enqueue(function<void()>);
		bool processQ();

	private:
		deque<function<void()>> q;
		mutex mutex_;
	};

	void Q::enqueue(function<void()> v)
	{
		lock_guard l(mutex_);
		q.emplace_back(std::move(v));
	}

	bool Q::processQ() {
		function<void()> v;
		{
			lock_guard l(mutex_);
			if (q.empty()) {
				return false;
			}
			v = std::move(q.front());
			q.pop_front();
		}
		v();
		return true;
	}

	constexpr int n_threads = 4;

	struct Params
	{
		FString prompt;// = TEXT("You are AIXO.");

//  		FString pathToModel = "/Users/burt/Documents/Models/Qwen3-14B-Q4_K_M.gguf";
//  		FString pathToModel = "/Users/burt/Documents/Models/Qwen3-4B-Q4_K_M.gguf";
  		FString pathToModel;// = "/Users/burt/Documents/Models/Qwen_Qwen3-30B-A3B-Q3_K_S.gguf";
		std::vector<FString> stopSequences;
	};
} // namespace


// Enum for context block types
UENUM(BlueprintType)
enum class ELlamaContextBlockType : uint8
{
    SystemPrompt,        // Overall instructions, personality, tool definitions
    StaticWorldInfo,     // Grid topology, GetSystemInfo() notes, SOPs
    LowFrequencyState,   // Geopolitical updates, mission phase (changes rarely)
    // ConversationHistory is managed internally by ProcessInputAndGenerate
    // HighFrequencyState is passed directly with ProcessInputAndGenerate
    // UserInput and ToolResponse are also part of ProcessInputAndGenerate
    COUNT UMETA(Hidden) // For iterating if needed
};

namespace Internal
{
    class Llama
    {
    public:
	    Llama();
        ~Llama();

        // MODIFIED/NEW API for Llama thread
        void InitializeLlama_LlamaThread(const FString& ModelPath, const FString& InitialSystemPrompt, const FString& Systems, const FString& LowFreq /*, other params */);
        void ShutdownLlama_LlamaThread();
        void UpdateContextBlock_LlamaThread(ELlamaContextBlockType BlockType, const FString& NewTextContent);
        void ProcessInputAndGenerate_LlamaThread(const FString& InputText, const FString& HighFrequencyContextText, const FString& InputTypeHint);
        void RequestFullContextDump_LlamaThread(); // Renamed for clarity
		void SignalStopRunning() { bIsRunning = false; }

		// these generally send a broadcast to Unreal blueprints; only two do anything else
        std::function<void(FString)> tokenCb;
        std::function<void(FString)> fullContextDumpCb;
        std::function<void(FString)> errorCb;    // For errors
        std::function<void(float)> progressCb;   // For loading
        std::function<void(const FContextVisPayload&)> contextChangedCb;    // For context change update * also calls ForwardContextUpdateToGameThread
        std::function<void(FString)> readyCb;    // For ready, sets bIsLlamaCoreReady and broadcasts
        std::function<void(FString)> toolCallCb; // For tool calls, DO THE TOOL CALL PROCESS, call SendToolResponseToLlama (no broadcast)
        std::function<void(bool)> setIsGeneratingCb; // copy the busy flag up the chain

	private:
		std::string AssembleFullContextForDump();
		void DetokenizeAndAppend(std::string& TargetString, const std::vector<llama_token>& TokensToDetokenize, const llama_model* ModelHandle);
		std::string CleanString(std::string p_str);
		void RebuildFlatConversationHistoryTokensFromStructured();
		void BroadcastContextVisualUpdate_LlamaThread(int32 nTokens=0, float msDecode=0.0f, float msGenerate=0.0f);
		void LlamaLogContext(FString Label);

        // --- Threading & Queues ---
	public:
        Q qMainToLlama; // Renamed for clarity
        Q qLlamaToMain; // Renamed for clarity

	private:
        // --- Core Llama State ---
        llama_model* model = nullptr;
        llama_context* ctx = nullptr;
        llama_batch batch;
		int32_t batch_capacity; // store the capacity
        llama_sampler* sampler_chain_instance = nullptr;
        const llama_vocab* vocab = nullptr;
        int32_t n_ctx_from_model = 0; // Actual context window size

        // --- Context Block Management (Llama Thread Owned) ---
        struct FTokenizedContextBlock {
            std::vector<llama_token> Tokens;
            // FString OriginalText; // Optional: for debugging or re-tokenizing if vocab changes (rare)
        };
        TMap<ELlamaContextBlockType, FTokenizedContextBlock> FixedContextBlocks;
        std::vector<llama_token> ConversationHistoryTokens; // Single flat list of tokens for conversation
        std::vector<llama_token> CurrentHighFrequencyStateTokens; // Tokens for the HFS of the *current* turn

#ifdef TRACK_PARALLEL_CONTEXT_TOKENS
		void DebugContext(const FString &Message);
#endif // TRACK_PARALLEL_CONTEXT_TOKENS
		std::vector<llama_token> MirroredKvCacheTokens;			// for debugging

        int32_t kv_cache_token_cursor = 0; // Tracks how many tokens are currently valid in the KV cache from the start of the logical sequence.
                                           // This is our primary way to manage n_past effectively.

        // --- Conversation History Management ---
        struct FConversationTurn {
            FString Role; // e.g., "user", "assistant", "tool_response"
            std::vector<llama_token> Tokens;
        };
        std::deque<FConversationTurn> StructuredConversationHistory; // For logical turn management
        const int32 MAX_CONVERSATION_TOKENS_TARGET = 16384; // Target, will try to stay below this. Adjust based on n_ctx and fixed blocks.
                                                           // Example: if n_ctx=32k, fixed=4k, HFS=1k, AI response buffer=1k, then convo can be ~26k
                                                           // This should be calculated dynamically based on n_ctx and other blocks.

        // --- Generation State ---
        std::atomic<bool> eos_reached = false;
        std::vector<std::vector<llama_token>> stopSequencesTokens;
        std::atomic<bool> bIsGenerating = false; // True while actively sampling tokens

	private:
        std::atomic<bool> bIsRunning = true; // Renamed for clarity
        std::thread ThreadHandle; // Renamed for clarity

        // --- Helper Methods (Llama Thread) ---
        void ThreadRun_LlamaThread(); // Renamed for clarity
		void _TokenizeAndStoreFixedBlockInternal(ELlamaContextBlockType BlockType, const FString& Text, bool bAddBosForThisBlock);
        void AssembleFullPromptForTurn(const FString& CurrentInputOriginalTextFStr, const FString& InputTypeHint, std::vector<llama_token>& OutFullPromptTokens);
        void DecodeTokensAndSample(std::vector<llama_token>& TokensToDecode, bool bIsFinalPromptTokenLogits);
        void AppendTurnToStructuredHistory(const FString& Role, const std::vector<llama_token>& Tokens);
        void PruneConversationHistory(); // Manages StructuredConversationHistory and ConversationHistoryTokens
        bool CheckStopSequences();
        std::string AssembleFullContextForDump_LlamaThread(); // Renamed
        void StopSeqHelper(const FString& stopSeqFStr);
        void InvalidateKVCacheFromPosition(int32_t ValidTokenCount);

        // Temporary buffer for tokens generated in the current AI response
        std::vector<llama_token> CurrentTurnAIReplyTokens;
    };
} // namespace Internal


// Delegates
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnNewTokenGenerated, FString, NewToken);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnFullContextDumpReady, const FString&, ContextDump);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnToolCallDetected, const FString&, ToolCallJson);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnLlamaErrorOccurred, const FString&, ErrorMessage);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnLlamaLoadingProgressDelegate, float, Progress);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnLlamaReady, const FString&, ReadyMessage);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnContextChanged, const FContextVisPayload&, Payload);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnTokenGenerated, const FString&, Token);


UCLASS(Category = "LLM", BlueprintType, meta = (BlueprintSpawnableComponent))
class UELLAMA_API ULlamaComponent : public UActorComponent
{
    GENERATED_BODY()

public:
    ULlamaComponent(const FObjectInitializer& ObjectInitializer);
    ~ULlamaComponent();

    virtual void BeginPlay() override; // Changed from Activate for standard UE lifecycle
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override; // Changed from Deactivate
    virtual void TickComponent(float DeltaTime,
                               enum ELevelTick TickType,
                               FActorComponentTickFunction* ThisTickFunction) override;

    // Replace ActivateLlamaComponent with SetGameInterface
    void SetGameInterface(TScriptInterface<ILLMGameInterface> InGameInterface);

    // Delegates
    UPROPERTY(BlueprintAssignable)
    FOnNewTokenGenerated OnNewTokenGenerated;

    UPROPERTY(BlueprintAssignable, Category = "Llama|Debug")
    FOnFullContextDumpReady OnFullContextDumpReady;

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

	//

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Llama|Config")
    FString PathToModel;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Llama|Config", meta = (MultiLine = true))
    FString SystemPromptFileName;

    UFUNCTION(BlueprintCallable, Category = "Llama")
    void UpdateContextBlock(ELlamaContextBlockType BlockType, const FString& NewTextContent);

    UFUNCTION(BlueprintCallable, Category = "Llama")
    void ProcessInput(const FString& InputText, const FString& HighFrequencyContextText = TEXT(""), const FString& InputTypeHint = TEXT(""));

    UFUNCTION(BlueprintCallable, Category = "Llama|Debug")
    void TriggerFullContextDump();

    // Provider selection
    UPROPERTY(EditAnywhere, Category = "LLM")
    bool bUseLocalLlama = true;
    
    UPROPERTY(EditAnywhere, Category = "LLM", meta = (EditCondition = "bUseLocalLlama"))
    FString LocalModelPath;
    
    UPROPERTY(EditAnywhere, Category = "LLM", meta = (EditCondition = "!bUseLocalLlama"))
    FString RemoteEndpoint;
    
    UPROPERTY(EditAnywhere, Category = "LLM", meta = (EditCondition = "!bUseLocalLlama"))
    FString APIKey;

    // Set the visualization interface
    UFUNCTION(BlueprintCallable, Category = "LLM|Visualization")
    void SetVisualizationInterface(TScriptInterface<ILLMVisualizationInterface> InVisualizationInterface);

protected:
    // Provider management
    void InitializeProvider();
    void SwitchProvider(bool bUseLocal);
    
    // Provider instance
    UPROPERTY()
    TScriptInterface<ILLMProvider> Provider;
    
    // Context management
    UPROPERTY()
    ULLMContextManager* ContextManager;
    
    // Visualization
    void UpdateContextVisualization();
    void HandleContextChanged(const FContextVisPayload& Payload);
    
    // Token handling
    void HandleTokenGenerated(const FString& Token);
    
    // Provider switching
    UFUNCTION(BlueprintCallable, Category = "LLM")
    void ToggleProvider();
    
    UFUNCTION(BlueprintCallable, Category = "LLM")
    void SetProvider(bool bUseLocal);
    
    // Context updates
    UFUNCTION(BlueprintCallable, Category = "LLM")
    void UpdateSystemPrompt(const FString& NewPrompt);
    
    UFUNCTION(BlueprintCallable, Category = "LLM")
    void UpdateStaticWorldInfo(const FString& NewInfo);
    
    UFUNCTION(BlueprintCallable, Category = "LLM")
    void UpdateLowFrequencyState(const FString& NewState);
    
    // Visualization
    UPROPERTY(BlueprintReadOnly, Category = "LLM")
    FContextVisPayload CurrentVisualization;

    // The visualization interface
    UPROPERTY()
    TScriptInterface<ILLMVisualizationInterface> VisualizationInterface;

private:
    // Remove HarnessActor
    // AVisualTestHarnessActor* HarnessActor;
    
    // Add game interface
    UPROPERTY()
    TScriptInterface<ILLMGameInterface> GameInterface;

    // Update the stubbed functions to use GameInterface
    FString MakeSystemsBlock();
    FString MakeStatusBlock();
    void HandleToolCall_GetSystemInfo(const FString& QueryString);
    void HandleToolCall_CommandSubmarineSystem(const FString& QueryString);
    void HandleToolCall_QuerySubmarineSystem(const FString& QueryString);

    // ... rest of existing code ...

private:
    std::unique_ptr<Internal::Llama> LlamaInternal; // Renamed from llama
    bool bIsLlamaCoreReady = false; // Set by callback from Llama thread
	FString SystemsContextBlockRecent;
	FString LowFreqContextBlockRecent;

private:
    bool bPendingStaticWorldInfoUpdate = false;
    FString PendingStaticWorldInfoText;
    bool bPendingLowFrequencyStateUpdate = false;
    FString PendingLowFrequencyStateText;

public:
    UFUNCTION(BlueprintPure, Category = "Llama")
    bool IsLlamaBusy() const { return bIsLlamaGenerating.load(std::memory_order_acquire); }

    UFUNCTION(BlueprintPure, Category = "Llama")
    bool IsLlamaReady() const { return bIsLlamaCoreReady; } // You already have bIsLlamaCoreReady

private:
    std::atomic<bool> bIsLlamaGenerating; // This can be set by Llama thread via a callback when it starts/stops generation.
};

