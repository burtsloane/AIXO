// 2023 (c) Mika Pi

// ReSharper disable CppPrintfBadFormat
#include "UELlama/LlamaComponent.h"

#define GGML_CUDA_DMMV_X 64
#define GGML_CUDA_F16
#define GGML_CUDA_MMV_Y 2
#define GGML_USE_CUBLAS
#define GGML_USE_K_QUANTS
#define K_QUANTS_PER_ITERATION 2

////////////////////////////////////////////////////////////////////////////////////////////////

 static std::vector<llama_token> my_llama_tokenize(
     const llama_model* model,
     const std::string& text,
     bool add_bos,
     bool special
 ) {
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
// звукоряд
         res.resize(n);
     } else {
         UE_LOG(LogTemp, Error, TEXT("Error tokenizing input in my_llama_tokenize."));
         res.clear(); // Return empty on error
     }
     return res;
 }


////////////////////////////////////////////////////////////////////////////////////////////////


namespace Internal
{

  void Llama::insertPrompt(FString v)
  {
    qMainToThread.enqueue([this, v = std::move(v)]() mutable {
    	unsafeInsertPrompt(std::move(v));
	});
  }

void Llama::unsafeInsertPrompt(FString UserInputFStr) {
    if (!ctx || !model) {
        UE_LOG(LogTemp, Error, TEXT("Llama not activated for unsafeInsertPrompt."));
        return;
    }
    std::string stdUserInput = TCHAR_TO_UTF8(*UserInputFStr);
    // Add leading space if model expects it for user turns, e.g., " User: "
    // This depends on your prompt templating. For now, assume it's added by the prompt structure.

    {
        std::lock_guard<std::mutex> lock(input_mutex);
        new_input_buffer += stdUserInput; // Append to a buffer
        new_input_flag = true;
        eos_reached = false; // New input, so not EOS
    }
    UE_LOG(LogTemp, Log, TEXT("Llama::unsafeInsertPrompt: '%s'"), *UserInputFStr);
}

bool Llama::CheckStopSequences(llama_token current_token) {
    if (stopSequencesTokens.empty()) return false;

    // A temporary buffer that includes the current token with the end of last_n_tokens_for_penalty
    std::vector<llama_token> current_sequence_tail = last_n_tokens_for_penalty;
    int32_t	penalty_last_n = 8;		// used to be in sampling_params.penalty_last_n
    if (!current_sequence_tail.empty() && current_sequence_tail.size() >= penalty_last_n) {
         current_sequence_tail.erase(current_sequence_tail.begin());
    }
    current_sequence_tail.push_back(current_token);


    for (const auto& stop_seq : stopSequencesTokens) {
        if (stop_seq.empty()) continue;
        if (current_sequence_tail.size() >= stop_seq.size()) {
            // Check if the tail of current_sequence_tail matches stop_seq
            if (std::equal(stop_seq.begin(), stop_seq.end(),
                           current_sequence_tail.end() - stop_seq.size())) {
                UE_LOG(LogTemp, Log, TEXT("Llama thread %p: Stop sequence matched."), this);
                return true;
            }
        }
    }
    return false;
}

Llama::Llama() : thread([this]() { threadRun(); }) {}


void Llama::threadRun() {
    UE_LOG(LogTemp, Log, TEXT("Llama thread %p: Starting."), this);
    batch = llama_batch_init(512, 0, 1); // Max tokens in one batch, 0 embd, 1 seq_id
                                                    // Size this appropriately (e.g., n_batch from llama.cpp examples)
    while (running) {
		while (qMainToThread.processQ()) {}

        if (!ctx || !model || !running) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            continue;
        }

        std::string current_input_text_chunk;
        bool has_new_input = false;
        {
            std::lock_guard<std::mutex> lock(input_mutex);
            if (new_input_flag) {
                current_input_text_chunk.swap(new_input_buffer); // Take the buffered input
                new_input_flag = false;
                has_new_input = true;
            }
        }

        if (has_new_input && !current_input_text_chunk.empty()) {
            std::vector<llama_token> new_tokens = my_llama_tokenize(
                this->model, 
                current_input_text_chunk, 
                false, // No BOS for follow-up
                false  // No special tokens for user input typically
            );

            // Append to the main conversation history
            current_conversation_tokens.insert(current_conversation_tokens.end(), new_tokens.begin(), new_tokens.end());
            UE_LOG(LogTemp, Log, TEXT("Llama thread %p: Processing %d new input tokens. Total convo tokens: %d"), 
                this, new_tokens.size(), current_conversation_tokens.size());
        }

        // If there are tokens to evaluate (either from new input or because we are generating)
        // and we are not at EOS for generation.
        if (!eos_reached && current_eval_pos < static_cast<int32_t>(current_conversation_tokens.size())) {
            
            // KV Cache Management (Context Shifting)
            const int32_t n_ctx = llama_n_ctx(ctx);
            if (static_cast<int32_t>(current_conversation_tokens.size()) > n_ctx) {
                // Evict oldest tokens from current_conversation_tokens AND the KV cache
                int32_t n_left    = current_eval_pos; // Tokens already in KV
                int32_t n_discard = n_left / 2;       // Discard half of the past
                
                // llama_kv_cache_seq_rm(ctx, 0, 0, n_discard); // Remove first n_discard tokens from seq 0
                // llama_kv_cache_seq_shift(ctx, 0, n_discard, n_left, -n_discard); // Shift remaining
                
                // More robustly, from common.cpp logic:
                const int n_keep = n_discard;//llama_keep_n(ctx); // Or a user-defined value
                llama_kv_self_seq_rm(ctx, 0, n_keep, current_eval_pos); // Remove tokens from n_keep to current_eval_pos
//???                llama_kv_cache_seq_shift(ctx, 0, current_eval_pos, n_keep, -(current_eval_pos - n_keep));


                // Remove from our C++ side conversation history too
                current_conversation_tokens.erase(current_conversation_tokens.begin() + n_keep, 
                                                  current_conversation_tokens.begin() + current_eval_pos);
                
                current_eval_pos = n_keep; // Reset eval_pos to where the valid KV cache now starts
                UE_LOG(LogTemp, Warning, TEXT("Llama thread %p: KV Cache Shifted. current_eval_pos now %d. Total convo tokens: %d"), 
                    this, current_eval_pos, current_conversation_tokens.size());
            }

            // Prepare batch for tokens that haven't been evaluated yet
            int32_t num_tokens_to_eval = current_conversation_tokens.size() - current_eval_pos;
            if (num_tokens_to_eval > 0) {
                // Ensure batch size is not exceeded for a single decode call
                // llama.cpp examples often process in chunks of `n_batch` (e.g., 512)
                int32_t n_eval_chunk = num_tokens_to_eval;	//std::min(num_tokens_to_eval, (int32_t)batch.n_tokens_max); // batch.n_tokens_max is your init size

                batch = llama_batch_get_one(
                    current_conversation_tokens.data() + current_eval_pos, // Pointer to the unevaluated tokens
                    n_eval_chunk
                );
                // llama_batch_get_one sets logits=true for the last token in this chunk

                if (llama_decode(ctx, batch) != 0) {
                    UE_LOG(LogTemp, Error, TEXT("Llama thread %p: llama_decode failed."), this);
                    eos_reached = true; // Stop on error
                    continue;
                }
                current_eval_pos += n_eval_chunk;
            }
        }


        // If all current input is processed (current_eval_pos == current_conversation_tokens.size())
        // and we are not at EOS, then sample the next token
        if (!eos_reached && current_eval_pos == static_cast<int32_t>(current_conversation_tokens.size()) && model) {
            if (current_eval_pos == 0) { // Should not happen if initial prompt was processed
                UE_LOG(LogTemp, Warning, TEXT("Llama thread %p: No tokens evaluated, cannot sample."), this);
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
                continue;
            }

            // --- SAMPLING ---
            // Logits are from the last token of the *previously decoded batch*
            float* logits = llama_get_logits_ith(ctx, batch.n_tokens - 1);
            int n_vocab = llama_vocab_n_tokens(llama_model_get_vocab(model));

            std::vector<llama_token_data> candidates_data_vec(n_vocab);
            for (int token_id = 0; token_id < n_vocab; ++token_id) {
                candidates_data_vec[token_id] = llama_token_data{ (llama_token)token_id, logits[token_id], 0.0f };
            }
            llama_token_data_array candidates_p = { candidates_data_vec.data(), candidates_data_vec.size(), false };

			llama_token new_id = llama_sampler_sample(sampler_chain_instance, ctx, batch.n_tokens - 1); // Get final token

//const char* piece_str_debug = llama_vocab_get_text(llama_model_get_vocab(model), new_id);
//UE_LOG(LogTemp, Warning, TEXT("Generated Token ID: %d, Piece: '%s'"), new_id, UTF8_TO_TCHAR(piece_str_debug ? piece_str_debug : "NULL"));

			llama_sampler_accept(sampler_chain_instance, new_id); // Accept token

            if (new_id == llama_vocab_eos(llama_model_get_vocab(model)) || CheckStopSequences(new_id)) { // CheckStopSequences needs new_id too
                UE_LOG(LogTemp, Log, TEXT("Llama thread %p: EOS or stop sequence."), this);
                eos_reached = true;
            } else {
                // Send token to main thread
                const char* piece_str = llama_vocab_get_text(llama_model_get_vocab(model), new_id);
                if (piece_str) {




//    FString HexBytes;
//    for (int i = 0; piece_str[i] != '\0'; ++i) {
//        HexBytes += FString::Printf(TEXT("%02X "), (unsigned char)piece_str[i]);
//    }
//    UE_LOG(LogTemp, Warning, TEXT("Token ID: %d, Piece: '%s', Bytes: %s"), new_id, UTF8_TO_TCHAR(piece_str), *HexBytes);



const char* piece_str_raw = llama_vocab_get_text(llama_model_get_vocab(model), new_id);
std::string piece_std_str = (piece_str_raw ? piece_str_raw : "");

// Check for UTF-8 sequence for Ġ (U+0120) -> C4 A0
if (piece_std_str.rfind("\xC4\xA0", 0) == 0) { // Efficiently checks if string starts with "\xC4\xA0"
    piece_std_str.replace(0, 2, " "); // Replace the 2 bytes of Ġ with 1 byte of space
}





                    FString token_fstring = UTF8_TO_TCHAR(piece_std_str.c_str());
                    qThreadToMain.enqueue([token_fstring, this]() mutable {
                        if (tokenCb) tokenCb(std::move(token_fstring));
                    });
                }

                // Add the generated token to our conversation history to be processed in the next iteration
                current_conversation_tokens.push_back(new_id);
                // The next iteration will pick up this new token at current_eval_pos
                // and decode it to get logits for the *next* prediction.
            }
        } else if (eos_reached || !running) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100)); // Longer sleep if EOS or stopping
        } else {
            std::this_thread::sleep_for(std::chrono::milliseconds(10)); // Idle
        }
    } // end while(running)

//    llama_batch_free(batch);
    unsafeDeactivate();
    UE_LOG(LogTemp, Log, TEXT("Llama thread %p: Stopped."), this);
}

  Llama::~Llama()
  {
    running = false;
    thread.join();
  }

  void Llama::process()
  {
    while (qThreadToMain.processQ()) ;
  }

  void Llama::activate(bool bReset, Params params)
  {
    qMainToThread.enqueue([bReset, params = std::move(params), this]() mutable {
      unsafeActivate(bReset, std::move(params));
    });
  }

  void Llama::deactivate()
  {
    qMainToThread.enqueue([this]() { unsafeDeactivate(); });
  }

void Llama::unsafeActivate(bool bReset, Params params_editor) {
    UE_LOG(LogTemp, Warning, TEXT("%p Loading LLM model %p bReset: %d"), this, model, bReset);
    if (bReset)
      unsafeDeactivate();
    if (model)
      return;

	llama_model_params model_params = llama_model_default_params();
//	model_params.n_gpu_layers = 50;

	model = llama_model_load_from_file(TCHAR_TO_UTF8(*params_editor.pathToModel), model_params);

    if (!model)
    {
      UE_LOG(LogTemp, Error, TEXT("%p unable to load model"), this);
      unsafeDeactivate();
      return;
    }
	vocab = llama_model_get_vocab(model);

    // initialize the context

    llama_context_params ctx_params = llama_context_default_params();
    // n_ctx is the context size
    int n_prompt = 8192 - 1024;
    int n_predict = 1024;
    ctx_params.n_ctx = n_prompt + n_predict - 1;
    // n_batch is the maximum number of tokens that can be processed in a single call to llama_decode
    ctx_params.n_batch = n_prompt;
    // enable performance counters
    ctx_params.no_perf = false;

    ctx = llama_init_from_model(model, ctx_params);

    if (ctx == NULL) {
        fprintf(stderr , "%s: error: failed to create the llama_context\n" , __func__);
        return;
    }

    // initialize the sampler

    auto sparams = llama_sampler_chain_default_params();
    sparams.no_perf = false;
	sampler_chain_instance = llama_sampler_chain_init(sparams);

//    llama_sampler_chain_add(sampler_chain_instance, llama_sampler_init_top_k(50));
//    llama_sampler_chain_add(sampler_chain_instance, llama_sampler_init_top_p(0.9, 1));
//    llama_sampler_chain_add(sampler_chain_instance, llama_sampler_init_temp (0.8));
//
//    // typically, the chain should end with a sampler such as "greedy", "dist" or "mirostat"
//    // this sampler will be responsible to select the actual token
//    llama_sampler_chain_add(sampler_chain_instance, llama_sampler_init_dist(seed));
    llama_sampler_chain_add(sampler_chain_instance, llama_sampler_init_greedy());

/*
auto sparams = llama_sampler_chain_default_params();
// sparams.no_perf = false; // This field might not exist or be relevant anymore.
// Populate sparams with your UProperty values (temp, top_k, top_p, penalties etc.)
// sparams.temp = params_editor.Temperature; // Example, check llama.h for exact field names
// sparams.top_k = params_editor.TopK;
// sparams.top_p = params_editor.TopP;
// sparams.penalty_repeat = params_editor.RepeatPenalty;
// sparams.penalty_last_n = params_editor.RepeatLastN; // Make sure UELlama has this UProperty
// sparams.penalty_toks = last_n_tokens_for_penalty.data(); // Needs to be updated each sample
// sparams.penalty_toks_size = last_n_tokens_for_penalty.size();

sampler_chain_instance = llama_sampler_chain_init(&sparams); // Pass pointer to params

// Add the samplers you want. For greedy, it's simple:
llama_sampler_chain_add(sampler_chain_instance, llama_sampler_init_greedy(ctx));
// For more complex sampling, you'd add other samplers like:
// llama_sampler_chain_add(sampler_chain_instance, llama_sampler_init_top_k(ctx, &candidates_p, top_k_value, 1));
// llama_sampler_chain_add(sampler_chain_instance, llama_sampler_init_temperature(ctx, &candidates_p, temp_value));
// etc.
// The order matters.
*/
    //

    current_conversation_tokens.clear();
    current_eval_pos = 0;
    eos_reached = false;

    // Tokenize initial prompt
    std::string initial_prompt_str = TCHAR_TO_UTF8(*params_editor.prompt);
    if (!initial_prompt_str.empty() && initial_prompt_str[0] != ' ') { // Llama 2 often expects leading space for sys prompt
        // Or handle BOS token explicitly
        initial_prompt_str = " " + initial_prompt_str;
    }
    
    std::vector<llama_token> initial_tokens = my_llama_tokenize(
        this->model, 
        initial_prompt_str, 
        true, // Add BOS for the very first prompt
        true  // Allow special tokens in system prompt
    );

    current_conversation_tokens = initial_tokens;
    // last_n_tokens_for_penalty might be initialized here too, or sized to n_ctx and filled with padding.

    // No initial batch creation here; threadRun will handle it.
    // The old UELlama did a warm-up eval with BOS. This can still be done in threadRun
    // before the main loop if desired, by creating a batch with BOS and decoding it.
    UE_LOG(LogTemp, Log, TEXT("Llama activated. Initial prompt tokenized to %d tokens."), current_conversation_tokens.size());
}

  void Llama::unsafeDeactivate()
  {
    UE_LOG(LogTemp, Warning, TEXT("%p Unloading LLM model %p"), this, model);
    if (!model) return;
    llama_sampler_free(sampler_chain_instance);
    sampler_chain_instance = nullptr;
    llama_free(ctx);
    ctx = nullptr;
    llama_model_free(model);
    model = nullptr;
  }
} // namespace Internal

ULlamaComponent::ULlamaComponent(const FObjectInitializer &ObjectInitializer)
  : UActorComponent(ObjectInitializer), llama(make_unique<Internal::Llama>())
{
  PrimaryComponentTick.bCanEverTick = true;
  PrimaryComponentTick.bStartWithTickEnabled = true;
  llama->tokenCb = [this](FString NewToken) {
	OnNewTokenGenerated.Broadcast(std::move(NewToken));
  };
}

ULlamaComponent::~ULlamaComponent() = default;

void ULlamaComponent::Activate(bool bReset)
{
  Super::Activate(bReset);
  Params params;
  params.pathToModel = pathToModel;
  params.prompt = prompt;
  params.stopSequences = stopSequences;
  llama->activate(bReset, std::move(params));
}

void ULlamaComponent::Deactivate()
{
  llama->deactivate();
  Super::Deactivate();
}

void ULlamaComponent::TickComponent(float DeltaTime,
                                    ELevelTick TickType,
                                    FActorComponentTickFunction* ThisTickFunction)
{
  Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
  llama->process();
}

auto ULlamaComponent::InsertPrompt(const FString& v) -> void
{
  llama->insertPrompt(v);
}
