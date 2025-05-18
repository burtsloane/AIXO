// LLamaComponent.h
#pragma once

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

#include "LlamaComponent.generated.h"

//#define TRACK_PARALLEL_CONTEXT_TOKENS

class AVisualTestHarnessActor;

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
	    Llama(ULlamaComponent* InOwningComponent); // NEW: Constructor takes owner
        ~Llama();

        // MODIFIED/NEW API for Llama thread
        void InitializeLlama_LlamaThread(const FString& ModelPath, const FString& InitialSystemPrompt, const FString& Systems, const FString& LowFreq /*, other params */);
        void ShutdownLlama_LlamaThread();
        void UpdateContextBlock_LlamaThread(ELlamaContextBlockType BlockType, const FString& NewTextContent);
        void ProcessInputAndGenerate_LlamaThread(const FString& InputText, const FString& HighFrequencyContextText, const FString& InputTypeHint);
        void RequestFullContextDump_LlamaThread(); // Renamed for clarity
		std::string AssembleFullContextForDump();
		void DetokenizeAndAppend(std::string& TargetString, const std::vector<llama_token>& TokensToDetokenize, const llama_model* ModelHandle);
		std::string CleanString(std::string p_str);
		void RebuildFlatConversationHistoryTokensFromStructured();
		void BroadcastContextVisualUpdate_LlamaThread();

        std::function<void(FString)> tokenCb;
        std::function<void(FString)> fullContextDumpCb;
        std::function<void(FString)> toolCallCb; // For tool calls
        std::function<void(FString)> errorCb;    // For errors
        std::function<void(float)> progressCb;   // For loading
        std::function<void(FString)> readyCb;    // For ready
        std::function<void(const FContextVisPayload&)> contextChangedCb;    // For context change update

    private:
        // --- Core Llama State ---
        llama_model* model = nullptr;
        llama_context* ctx = nullptr;
        llama_batch batch;
		int32_t batch_capacity; // store the capacity
        llama_sampler* sampler_chain_instance = nullptr;
        const llama_vocab* vocab = nullptr;
        int32_t n_ctx_from_model = 0; // Actual context window size
	    ULlamaComponent* OwningLlamaComponentPtr; // Member to store the owner

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

        // --- Threading & Queues ---
public:
        Q qMainToLlama; // Renamed for clarity
        Q qLlamaToMain; // Renamed for clarity
        std::atomic<bool> bIsRunning = true; // Renamed for clarity
private:
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
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnLlamaContextChangedDelegate, const FContextVisPayload&, ContextMessage);


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
                               ELevelTick TickType,
                               FActorComponentTickFunction* ThisTickFunction) override;

	void ActivateLlamaComponent(AVisualTestHarnessActor* InHarnessActor);
	void ForwardContextUpdateToGameThread(const FContextVisPayload& Payload);

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
    FOnLlamaContextChangedDelegate OnLlamaContextChangedDelegate;

	//

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Llama|Config")
    FString PathToModel; // Renamed from pathToModel

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Llama|Config", meta = (MultiLine = true))
    FString SystemPromptText; // Renamed from prompt

    // UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Llama|Config")
    // std::vector<FString> StopSequences; // Renamed from stopSequences - this will be handled by system prompt now

    // --- NEW API ---
    UFUNCTION(BlueprintCallable, Category = "Llama")
    void UpdateContextBlock(ELlamaContextBlockType BlockType, const FString& NewTextContent);

    UFUNCTION(BlueprintCallable, Category = "Llama")
    void ProcessInput(const FString& InputText, const FString& HighFrequencyContextText, const FString& InputTypeHint);

    UFUNCTION(BlueprintCallable, Category = "Llama|Debug")
    void TriggerFullContextDump();

private:
    std::unique_ptr<Internal::Llama> LlamaInternal; // Renamed from llama
	AVisualTestHarnessActor* HarnessActor;
    bool bIsLlamaCoreReady = false; // Set by callback from Llama thread
	FString SystemsContextBlockRecent;
	FString LowFreqContextBlockRecent;
	FString MakeSystemsBlock();
	FString MakeStatusBlock();
};
