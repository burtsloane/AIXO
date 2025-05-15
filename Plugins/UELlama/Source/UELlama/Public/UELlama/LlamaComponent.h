// 2023 (c) Mika Pi

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

#include "LlamaComponent.generated.h"

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
		TArray<FString> stopSequences;
	};
} // namespace



namespace Internal
{
	class Llama
	{
	public:
		Llama();
		~Llama();

		void activate(bool bReset, Params);
		void deactivate();
		void insertPrompt(FString v);
		void process();
		void RequestFullContextDump();
		std::string AssembleFullContextForDump();

		std::function<void(FString)> tokenCb;					// called to return a string to Unreal main thread
	    std::function<void(FString)> fullContextDumpCb; // Callback for sending the dump

	private:
		std::atomic<bool> eos_reached = false; // End Of Sequence for current generation
		std::vector<llama_token> last_n_tokens_for_penalty; // Sized to sampling_params.penalty_last_n or n_ctx
		std::vector<std::vector<llama_token>> stopSequencesTokens; // Tokenized stop sequences

		llama_model* model = nullptr;
		llama_context* ctx = nullptr;
		llama_batch batch;									//+BAS
		llama_sampler * sampler_chain_instance = nullptr;	//+BAS
		const llama_vocab * vocab = nullptr;				//+BAS
		int current_kv_pos = 0;								//+BAS

		std::vector<llama_token> current_conversation_tokens; // Stores ALL tokens of the current conversation (prompt + user + AI)
		int32_t current_eval_pos = 0; // Tracks how many tokens from current_conversation_tokens have already been evaluated and are in the KV cache.

		std::vector<llama_token> pending_prompt_tokens; // (to store tokens from new prompts)
		int current_sequence_pos = 0; // (tracks the current position in the overall sequence for the KV cache)
		bool new_prompt_ready = false; // (atomic or protected by mutex)

		std::atomic<bool> new_input_flag = false; // Simpler flag
		std::string new_input_buffer; // Store new FString input here, protected by mutex
		std::mutex input_mutex;

		Q qMainToThread;
		Q qThreadToMain;
		atomic_bool running = true;
		thread thread;

		enum class LlamaState { IDLE, PROCESSING_INPUT, GENERATING_RESPONSE };
		std::atomic<LlamaState> current_ai_state = LlamaState::IDLE;
		bool initial_context_processed = false; // Tracks if static prefix + initial status is done
		int32_t N_STATIC_END_POS = 0; // Position after static prefix
		int32_t N_CONVO_END_POS_BEFORE_STATUS = 0; // Position after conversation, before last status block
		std::deque<std::vector<llama_token>> conversation_history_turns; // Stores tokenized turns
		const int MAX_CONVO_HISTORY_TURNS = 20; // Max user/AI turn pairs to keep in active context
		bool pending_status_update_flag = false; // Set by main thread or system events
		std::vector<llama_token> current_ai_response_buffer; // Temporarily stores tokens of AI's current response
		bool is_generating_response_internally = false; // True while AI is outputting tokens for a single turn
		int32_t kv_pos_before_current_ai_response = 0; // KV pos before AI started its current stream of tokens

		void threadRun();
		void unsafeActivate(bool bReset, Params);
		void unsafeDeactivate();
		void unsafeInsertPrompt(FString);
		bool CheckStopSequences(llama_token current_token);
		//
		std::vector<llama_token> GetCurrentSubmarineStatusTokens();
		bool ConversationHistoryNeedsPruning();
		void PruneConversationHistoryAndUpdateContext();
		void AddToConversationHistoryDeque(const std::string& role, const std::vector<llama_token>& tokens);
//		std::vector<llama_token> GetPrunedConversationTokenVectorFromHistory();
		void ProcessTokenVectorChunked(const std::vector<llama_token>& tokens_to_process, int32_t start_offset_in_vector, bool last_chunk_gets_logits);
		void InvalidateKVCacheAndTokensFrom(int32_t position_to_keep_kv_until);
		void StopSeqHelper(const FString& stopSeqFStr);
	};
}


DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnNewTokenGenerated, FString, NewToken);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnFullContextDumpReady, const FString&, ContextDump);

UCLASS(Category = "LLM", BlueprintType, meta = (BlueprintSpawnableComponent))
class UELLAMA_API ULlamaComponent : public UActorComponent
{
  GENERATED_BODY()
public:
  ULlamaComponent(const FObjectInitializer &ObjectInitializer);
  ~ULlamaComponent();

  virtual void Activate(bool bReset) override;
  virtual void Deactivate() override;
  virtual void TickComponent(float DeltaTime,
                             ELevelTick TickType,
                             FActorComponentTickFunction* ThisTickFunction) override;

  UPROPERTY(BlueprintAssignable)
  FOnNewTokenGenerated OnNewTokenGenerated;

  UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(MultiLine=true))
  FString prompt = "Hello";

  UPROPERTY(EditAnywhere, BlueprintReadWrite)
//  FString pathToModel = "/Users/burt/Documents/Models/Qwen3-14B-Q4_K_M.gguf";
//  FString pathToModel = "/Users/burt/Documents/Models/Qwen3-4B-Q4_K_M.gguf";
  FString pathToModel = "/Users/burt/Documents/Models/Qwen_Qwen3-30B-A3B-Q3_K_S.gguf";

  UPROPERTY(EditAnywhere, BlueprintReadWrite)
  TArray<FString> stopSequences;

  UFUNCTION(BlueprintCallable)
  void InsertPrompt(const FString &v);

    UFUNCTION(BlueprintCallable, Category = "Llama|Debug")
    void TriggerFullContextDump();

    UPROPERTY(BlueprintAssignable, Category = "Llama|Debug")
    FOnFullContextDumpReady OnFullContextDumpReady;

private:
  std::unique_ptr<Internal::Llama> llama;
};
