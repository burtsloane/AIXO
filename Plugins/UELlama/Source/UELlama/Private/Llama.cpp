#include "Llama.h"
#include "LlamaComponent.h"
#include "HAL/PlatformTime.h"
#include <thread>
#include <chrono>

namespace Internal
{
    // Helper functions for batch operations
    inline void common_batch_clear(llama_batch& batch) {
        batch.n_tokens = 0;
    }

    inline void common_batch_add(llama_batch& batch, llama_token id, int32_t pos, const std::vector<llama_seq_id>& seq_ids, bool logits) {
        batch.token[batch.n_tokens] = id;
        batch.pos[batch.n_tokens] = pos;
        // Create a single-element array for seq_id
        llama_seq_id seq_id = seq_ids.empty() ? 0 : seq_ids[0];
        batch.seq_id[batch.n_tokens] = &seq_id;  // Assign pointer to seq_id
        batch.logits[batch.n_tokens] = logits;
        batch.n_tokens++;
    }

    // --- LlamaInternal Constructor/Destructor ---
    LlamaInternal::LlamaInternal() noexcept 
    {
        UE_LOG(LogTemp, Log, TEXT("LlamaInternal::Constructor - Creating Llama thread"));
        ThreadHandle = std::thread([this]() { 
            ThreadRun_LlamaThread(); 
        });
        UE_LOG(LogTemp, Log, TEXT("LlamaInternal::Constructor - Thread created"));
        current_kv_pos_for_predecode = 0;
    }

    LlamaInternal::~LlamaInternal()
    {
        bIsRunning = false;
        if (ThreadHandle.joinable())
        {
            ThreadHandle.join();
        }
        if (ctx) llama_free(ctx);
        if (model) llama_model_free(model);
        if (batch.token) llama_batch_free(batch);
        if (sampler_chain_instance) llama_sampler_free(sampler_chain_instance);
    }

    // --- Llama Thread Initialization ---
    void LlamaInternal::InitializeLlama_LlamaThread(const FString& ModelPathFStr, const FString& InitialSystemPromptFStr, const FString& Systems, const FString& LowFreq)
    {
        UE_LOG(LogTemp, Log, TEXT("LlamaThread: Starting initialization... Model: %s"), *ModelPathFStr);
        if (model) { // Already initialized
            UE_LOG(LogTemp, Warning, TEXT("LlamaThread: Already initialized."));
            return;
        }

        // Tokenize and store fixed blocks first
        _TokenizeAndStoreFixedBlockInternal(ELlamaContextBlockType::SystemPrompt, InitialSystemPromptFStr, true);
        _TokenizeAndStoreFixedBlockInternal(ELlamaContextBlockType::Systems, Systems, false);
        _TokenizeAndStoreFixedBlockInternal(ELlamaContextBlockType::LowFrequencyState, LowFreq, false);

        // Combine all fixed blocks into a single token sequence
        std::vector<llama_token> FixedBlocksCombinedTokens;
        for (const auto& [BlockType, Block] : FixedContextBlocks) {
            FixedBlocksCombinedTokens.insert(FixedBlocksCombinedTokens.end(), Block.Tokens.begin(), Block.Tokens.end());
        }

        // Model loading with periodic queue check
        llama_model_params model_params = llama_model_default_params();
        UE_LOG(LogTemp, Log, TEXT("LlamaThread: Loading model from file..."));
        model = llama_model_load_from_file(TCHAR_TO_UTF8(*ModelPathFStr), model_params);
        if (!model) {
            FString ErrorMsg = FString::Printf(TEXT("LlamaThread: Unable to load model from %s"), *ModelPathFStr);
            UE_LOG(LogTemp, Error, TEXT("%s"), *ErrorMsg);
            qLlamaToMain.enqueue([this, ErrorMsg]() { if (errorCb) errorCb(ErrorMsg); });
            return;
        }
        // Yield to queue processing after model load
        if (!qMainToLlama.processQ()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }

        UE_LOG(LogTemp, Log, TEXT("LlamaThread: Model loaded successfully"));
        vocab = llama_model_get_vocab(model);
        n_ctx_from_model = llama_model_n_ctx_train(model);

        // Context creation with periodic queue check
        llama_context_params ctx_params = llama_context_default_params();
        ctx_params.n_ctx = n_ctx_from_model;
        ctx_params.n_batch = 2048;
        ctx_params.n_threads = n_threads;
        ctx_params.n_threads_batch = n_threads;
        ctx_params.no_perf = false;
        this->batch_capacity = ctx_params.n_batch;

        ctx = llama_init_from_model(model, ctx_params);
        if (!ctx) {
            FString ErrorMsg = TEXT("LlamaThread: Failed to create llama_context.");
            UE_LOG(LogTemp, Error, TEXT("%s"), *ErrorMsg);
            qLlamaToMain.enqueue([this, ErrorMsg]() { if (errorCb) errorCb(ErrorMsg); });
            llama_model_free(model); model = nullptr;
            return;
        }
        // Yield to queue processing after context creation
        if (!qMainToLlama.processQ()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }

        batch = llama_batch_init(ctx_params.n_batch, 0, 1);

        // Initialize sampler chain with periodic queue check
        const float temp = 0.80f;
        const int32_t top_k = 40;
        const float top_p = 0.95f;
        const float tfs_z = 1.00f;
        const float typical_p = 1.00f;
        const int32_t repeat_last_n = 64;
        const float repeat_penalty = 1.10f;
        const float alpha_presence = 0.00f;
        const float alpha_frequency = 0.00f;
        const int mirostat = 0;
        const float mirostat_tau = 5.f;
        const float mirostat_eta = 0.1f;
        const bool penalize_nl = true;

        auto sparams = llama_sampler_chain_default_params();
        sparams.no_perf = false;
        sampler_chain_instance = llama_sampler_chain_init(sparams);
        llama_sampler_chain_add(sampler_chain_instance, llama_sampler_init_penalties(64, 1.10f, 0.0f, 0.0f));
        // Yield to queue processing after sampler initialization
        if (!qMainToLlama.processQ()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }

        // Pre-decoding fixed blocks with periodic queue checks
        if (!FixedBlocksCombinedTokens.empty()) {
            int batch_size_override = 64;
            for (int32_t i = 0; i < FixedBlocksCombinedTokens.size(); ) {
                common_batch_clear(batch);
                int32_t n_batch_tokens = 0;
                for (int32_t j = 0; j < batch_size_override && i + j < FixedBlocksCombinedTokens.size(); ++j) {
                    common_batch_add(batch, FixedBlocksCombinedTokens[i + j], current_kv_pos_for_predecode + j, {0}, false);
                    n_batch_tokens++;
                }

                if (batch.n_tokens == 0) break;

                if (llama_decode(ctx, batch) != 0) {
                    FString ErrorMsg = TEXT("LlamaThread: llama_decode failed during fixed blocks pre-decoding.");
                    UE_LOG(LogTemp, Error, TEXT("%s"), *ErrorMsg);
                    qLlamaToMain.enqueue([this, ErrorMsg]() { if (errorCb) errorCb(ErrorMsg); });
                    return;
                }

                // Yield to queue processing every 10 batches
                if (i % (batch_size_override * 10) == 0) {
                    if (!qMainToLlama.processQ()) {
                        std::this_thread::sleep_for(std::chrono::milliseconds(1));
                    }
                }

                i += n_batch_tokens;
                current_kv_pos_for_predecode += n_batch_tokens;
            }
        }

        // Signal ready state
        UE_LOG(LogTemp, Log, TEXT("LlamaThread: Initialization complete, signaling ready"));
        FString msg = "Ready";
        qLlamaToMain.enqueue([this, msg]() { if (readyCb) readyCb(msg); });
    }

    void LlamaInternal::DecodeTokensAndSample(std::vector<llama_token>& TokensToDecode, bool bIsFinalPromptTokenLogits)
    {
        // Start Generation Loop with periodic queue checks
        double TokenStartTime = FPlatformTime::Seconds();
        int32 kv_cache_token_cursor_at_gen_start = kv_cache_token_cursor;
        int32 tokens_since_last_yield = 0;
        
        while (kv_cache_token_cursor < n_ctx_from_model && !eos_reached) {
            if (!bIsRunning) { eos_reached = true; break; }

            // Generate token
            llama_token new_token_id = llama_sampler_sample(sampler_chain_instance, ctx, -1);
            llama_sampler_accept(sampler_chain_instance, new_token_id);
            CurrentTurnAIReplyTokens.push_back(new_token_id);

            // Yield to queue processing every 10 tokens
            tokens_since_last_yield++;
            if (tokens_since_last_yield >= 10) {
                if (!qMainToLlama.processQ()) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(1));
                }
                tokens_since_last_yield = 0;
            }

            // Prepare for next token
            common_batch_clear(batch);
            common_batch_add(batch, new_token_id, kv_cache_token_cursor, {0}, true);
            MirroredKvCacheTokens.push_back(new_token_id);
            
            if (llama_decode(ctx, batch) != 0) {
                FString ErrorMsg = TEXT("LlamaThread: llama_decode failed during generation.");
                UE_LOG(LogTemp, Error, TEXT("%s"), *ErrorMsg);
                qLlamaToMain.enqueue([this, ErrorMsg]() { if (errorCb) errorCb(ErrorMsg); });
                eos_reached = true;
                break;
            }

            kv_cache_token_cursor++;
            BroadcastContextVisualUpdate_LlamaThread(kv_cache_token_cursor - kv_cache_token_cursor_at_gen_start, 0.0f, FPlatformTime::Seconds() - TokenStartTime);
        }
    }

    // --- Main Llama Thread Loop ---
    void LlamaInternal::ThreadRun_LlamaThread()
    {
        UE_LOG(LogTemp, Log, TEXT("LlamaThread: Starting thread run loop"));
        while (bIsRunning)
        {
            // Process any pending tasks from the main thread
            if (qMainToLlama.processQ()) {
                continue; // Keep processing if there are more items
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        UE_LOG(LogTemp, Log, TEXT("LlamaThread: Thread run loop ending"));
    }

    void LlamaInternal::ShutdownLlama_LlamaThread()
    {
        if (ctx)
        {
            llama_free(ctx);
            ctx = nullptr;
        }
        if (model)
        {
            llama_model_free(model);
            model = nullptr;
        }
    }

    void LlamaInternal::UpdateContextBlock_LlamaThread(ELlamaContextBlockType BlockType, const FString& NewContent)
    {
        if (!ctx)
        {
            return;
        }

        _TokenizeAndStoreFixedBlockInternal(BlockType, NewContent, true);
    }

    void LlamaInternal::RequestFullContextDump_LlamaThread()
    {
        if (!ctx)
        {
            return;
        }

        FContextVisPayload Payload;
        // TODO: Implement context dump collection
        qLlamaToMain.enqueue([this, Payload]() { 
            if (contextChangedCb) contextChangedCb(Payload); 
        });
    }

    void LlamaInternal::ProcessInputAndGenerate_LlamaThread(const FString& InputText, const FString& HighFrequencyContextText, const FString& InputTypeHint)
    {
        if (!ctx)
        {
            return;
        }

        // TODO: Implement input processing and generation
        // This is a placeholder that should be replaced with actual llama.cpp implementation
        FString Response = TEXT("Response placeholder");
        qLlamaToMain.enqueue([this, Response]() { 
            if (tokenCb) tokenCb(Response); 
        });
    }

    void LlamaInternal::_TokenizeAndStoreFixedBlockInternal(ELlamaContextBlockType BlockType, const FString& Content, bool bClearExisting)
    {
        if (!ctx)
        {
            return;
        }

        // TODO: Implement tokenization and storage
        // This is a placeholder that should be replaced with actual llama.cpp implementation
    }
} // namespace Internal 