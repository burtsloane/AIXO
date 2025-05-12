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
		FString prompt = "You are AIXO, a helpful AI assistant for a submarine captain. You are currently in a test environment.";
  		FString pathToModel = "/Users/burt/Documents/Models/Qwen3-14B-Q4_K_M.gguf";
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

		std::function<void(FString)> tokenCb;					// called to return a string to Unreal main thread

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
		int current_sequence_pos; // (tracks the current position in the overall sequence for the KV cache)
		std::mutex prompt_mutex; // (to protect access to pending_prompt_tokens)
		bool new_prompt_ready = false; // (atomic or protected by mutex)

		std::atomic<bool> new_input_flag = false; // Simpler flag
		std::string new_input_buffer; // Store new FString input here, protected by mutex
		std::mutex input_mutex;

		Q qMainToThread;
		Q qThreadToMain;
		atomic_bool running = true;
		thread thread;
//		vector<vector<llama_token>> stopSequences;
//
		vector<llama_token> embd_inp;
		vector<llama_token> embd;
		vector<llama_token> res;
		int n_past = 0;
		vector<llama_token> last_n_tokens;
		int n_consumed = 0;
		bool eos = false;

		void threadRun();
		void unsafeActivate(bool bReset, Params);
		void unsafeDeactivate();
		void unsafeInsertPrompt(FString);
		bool CheckStopSequences(llama_token current_token);
	};
}


DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnNewTokenGenerated, FString, NewToken);

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

  UPROPERTY(EditAnywhere, BlueprintReadWrite)
  FString prompt = "Hello";

  UPROPERTY(EditAnywhere, BlueprintReadWrite)
  FString pathToModel = "/Users/burt/Documents/Models/Qwen3-14B-Q4_K_M.gguf";

  UPROPERTY(EditAnywhere, BlueprintReadWrite)
  TArray<FString> stopSequences;

  UFUNCTION(BlueprintCallable)
  void InsertPrompt(const FString &v);

private:
  std::unique_ptr<Internal::Llama> llama;
};
