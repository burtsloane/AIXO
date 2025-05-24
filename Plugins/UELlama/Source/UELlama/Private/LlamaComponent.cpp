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


namespace Internal
{
    // --- Llama Constructor/Destructor ---
    Llama::Llama() : ThreadHandle([this]() { 
        UE_LOG(LogTemp, Log, TEXT("Llama::Constructor - Creating Llama thread"));
        ThreadRun_LlamaThread(); 
    }) {
        UE_LOG(LogTemp, Log, TEXT("Llama::Constructor - Thread created"));
    }

    Llama::~Llama()
    {
        bIsRunning = false;
        if (ThreadHandle.joinable())
        {
            ThreadHandle.join();
        }
        // Ensure llama_free is called if model/ctx were loaded
        if (ctx) llama_free(ctx);
        if (model) llama_model_free(model);
        if (batch.token) llama_batch_free(batch); // Check if initialized before freeing
        if (sampler_chain_instance) llama_sampler_free(sampler_chain_instance);
    }
    
#ifdef TRACK_PARALLEL_CONTEXT_TOKENS
    void Llama::DebugContext(const FString &Message)
    {
		std::string CTX;
		for (llama_token tk : MirroredKvCacheTokens) {
			const char* piece = llama_vocab_get_text(vocab, tk);
			if (piece) CTX += piece;
		}
		std::string ss = CleanString(CTX);
		FString fs = UTF8_TO_TCHAR(ss.c_str());
        UE_LOG(LogTemp, Log, TEXT("DebugContext: %s\n%s"), *Message, *fs);
//        std::string os;
//        for (int i=0; i<20; i++) {
//        	char sss[200];
//        	snprintf(sss, 200, "%02x", 0x0ff & ss[i]);
//        	if (i > 0) os += ".";
//        	os += sss;
//        }        
//		fs = UTF8_TO_TCHAR(os.c_str());
//        UE_LOG(LogTemp, Log, TEXT("              %s"), *fs);
    }
#endif // TRACK_PARALLEL_CONTEXT_TOKENS

    // --- Llama Thread Initialization ---
    void Llama::InitializeLlama_LlamaThread(const FString& ModelPathFStr, const FString& InitialSystemPromptFStr, const FString& Systems, const FString& LowFreq /*, other params */)
    {
        UE_LOG(LogTemp, Log, TEXT("LlamaThread: Starting initialization... Model: %s"), *ModelPathFStr);
        if (model) { // Already initialized
            UE_LOG(LogTemp, Warning, TEXT("LlamaThread: Already initialized."));
            return;
        }

        llama_model_params model_params = llama_model_default_params();
        // model_params.n_gpu_layers = 50; // Configure as needed

        UE_LOG(LogTemp, Log, TEXT("LlamaThread: Loading model from file..."));
        model = llama_model_load_from_file(TCHAR_TO_UTF8(*ModelPathFStr), model_params);
        if (!model) {
            FString ErrorMsg = FString::Printf(TEXT("LlamaThread: Unable to load model from %s"), *ModelPathFStr);
            UE_LOG(LogTemp, Error, TEXT("%s"), *ErrorMsg);
            qLlamaToMain.enqueue([this, ErrorMsg]() { if (errorCb) errorCb(ErrorMsg); });
            return;
        }
        UE_LOG(LogTemp, Log, TEXT("LlamaThread: Model loaded successfully"));
        vocab = llama_model_get_vocab(model);
        n_ctx_from_model = llama_model_n_ctx_train(model); // Or llama_n_ctx if using that for actual context size

        llama_context_params ctx_params = llama_context_default_params();
        ctx_params.n_ctx = n_ctx_from_model; // Use model's training context or a configured value
        ctx_params.n_batch = 2048; // Or llama_n_batch(ctx) from model if available, or a sensible default. Max tokens per llama_decode call.
        ctx_params.n_threads = n_threads; // From your global namespace
        ctx_params.n_threads_batch = n_threads; // For batch processing
        ctx_params.no_perf = false;
		this->batch_capacity = ctx_params.n_batch; // Store the capacity you used for context

        ctx = llama_init_from_model(model, ctx_params);
        if (!ctx) {
            FString ErrorMsg = TEXT("LlamaThread: Failed to create llama_context.");
            UE_LOG(LogTemp, Error, TEXT("%s"), *ErrorMsg);
            qLlamaToMain.enqueue([this, ErrorMsg]() { if (errorCb) errorCb(ErrorMsg); });
            llama_model_free(model); model = nullptr;
            return;
        }

        batch = llama_batch_init(ctx_params.n_batch, 0, 1); // n_tokens, embd, n_seq_max

        // Initialize Sampler Chain

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

		llama_token id = 0;

		auto sparams = llama_sampler_chain_default_params();
		sparams.no_perf = false;
		sampler_chain_instance = llama_sampler_chain_init(sparams);

		/// NOTE: Avoid using on the full vocabulary as searching for repeated tokens can become slow. For example, apply top-k or top-p sampling first.
		llama_sampler_chain_add(sampler_chain_instance, llama_sampler_init_penalties(64, 1.10f, 0.0f, 0.0f));

		uint32_t time_based_seed = 4242;//static_cast<uint32_t>(time(NULL));

		if (temp <= 0)
		{
			// Greedy sampling
			llama_sampler_chain_add(sampler_chain_instance, llama_sampler_init_greedy());
		}
		else
		{
			if (mirostat == 1)
			{
				static float mirostat_mu = 2.0f * mirostat_tau;
				const int mirostat_m = 100;
				llama_sampler_chain_add(sampler_chain_instance, llama_sampler_init_temp (temp));
				llama_sampler_chain_add(sampler_chain_instance, llama_sampler_init_mirostat (llama_vocab_n_tokens(vocab), time_based_seed, mirostat_tau, mirostat_eta, mirostat_m));
			}
			else if (mirostat == 2)
			{
				static float mirostat_mu = 2.0f * mirostat_tau;
				llama_sampler_chain_add(sampler_chain_instance, llama_sampler_init_temp (temp));
				llama_sampler_chain_add(sampler_chain_instance, llama_sampler_init_mirostat_v2 (time_based_seed, mirostat_tau, mirostat_eta));
			}
			else
			{
				// Temperature sampling
				llama_sampler_chain_add(sampler_chain_instance, llama_sampler_init_top_k(top_k));
				//              llama_sample_tail_free(ctx, &candidates_p, tfs_z, 1);
				llama_sampler_chain_add(sampler_chain_instance, llama_sampler_init_typical (typical_p, 1));
				llama_sampler_chain_add(sampler_chain_instance, llama_sampler_init_top_p (top_p, 1));
				llama_sampler_chain_add(sampler_chain_instance, llama_sampler_init_temp (temp));
				llama_sampler_chain_add(sampler_chain_instance, llama_sampler_init_dist(time_based_seed));
			}
		}
//        // Example simplified:
//        auto sparams = llama_sampler_chain_default_params();
//        sampler_chain_instance = llama_sampler_chain_init(sparams);
//        llama_sampler_chain_add(sampler_chain_instance, llama_sampler_init_greedy()); // Simplest for now

        // Initialize Stop Sequences
        stopSequencesTokens.clear();
        StopSeqHelper(TEXT("<|im_end|>")); // Qwen specific or your chosen EOS
        StopSeqHelper(TEXT("~~~END_AIXO_TURN~~~"));
        // Add other critical stop sequences (e.g., "Captain:", "TOOL_RESPONSE:")
        // if you want the AI to robustly stop before generating these roles.

        // Initialize Context Blocks
        FixedContextBlocks.Empty();
        ConversationHistoryTokens.clear();
        StructuredConversationHistory.clear();
        CurrentHighFrequencyStateTokens.clear();
        kv_cache_token_cursor = 0; // Fresh start for KV cache
		llama_kv_self_clear(ctx); // Explicitly clear KV cache on full init
MirroredKvCacheTokens.clear();

		UE_LOG(LogTemp, Log, TEXT("LlamaThread: Initialization tokenizing fixed blocks."));

		// Just tokenize and store. DO NOT call InvalidateKVCacheFromPosition here.
		_TokenizeAndStoreFixedBlockInternal(ELlamaContextBlockType::SystemPrompt, InitialSystemPromptFStr, true); // true for BOS
		_TokenizeAndStoreFixedBlockInternal(ELlamaContextBlockType::StaticWorldInfo, Systems, false);
		_TokenizeAndStoreFixedBlockInternal(ELlamaContextBlockType::LowFrequencyState, LowFreq, false);

		UE_LOG(LogTemp, Log, TEXT("LlamaThread: Initialization complete. KV cursor at %d. Fixed blocks tokenized."), kv_cache_token_cursor);
		// At this point, kv_cache_token_cursor is STILL 0. No decoding has happened.
    
		// 2. **** NEW: Pre-decode Fixed Blocks into KV Cache ****
		UE_LOG(LogTemp, Log, TEXT("LlamaThread: Starting pre-decoding of fixed context blocks..."));
		std::vector<llama_token> FixedBlocksCombinedTokens;
		// Assemble tokens from FixedContextBlocks in order
		for (uint8 i = 0; i < (uint8)ELlamaContextBlockType::COUNT; ++i) {
			if (const FTokenizedContextBlock* Block = FixedContextBlocks.Find((ELlamaContextBlockType)i)) {
				FixedBlocksCombinedTokens.insert(FixedBlocksCombinedTokens.end(), Block->Tokens.begin(), Block->Tokens.end());
			}
		}

		UE_LOG(LogTemp, Log, TEXT("LlamaThread: Initialization about to send context visual update."));
		BroadcastContextVisualUpdate_LlamaThread();

		int32_t current_kv_pos_for_predecode = 0; // Start from beginning of cache
	    double TokenStartTime = FPlatformTime::Seconds();
		if (!FixedBlocksCombinedTokens.empty()) {
			int batch_size_override = 64;
			// Use a simplified version of DecodeTokensAndSample's prompt processing part
			// Or a dedicated helper function for this.
			// For now, let's conceptualize it:
			for (int32_t i = 0; i < FixedBlocksCombinedTokens.size(); /* i advanced by batch */ ) {
				common_batch_clear(batch);
				int32_t n_batch_tokens = 0;
				for (int32_t j = 0; j < batch_size_override/*this->batch_capacity*/ && i + j < FixedBlocksCombinedTokens.size(); ++j) {
					common_batch_add(batch, FixedBlocksCombinedTokens[i + j], current_kv_pos_for_predecode + j, {0}, false /* no logits needed */);
					n_batch_tokens++;
				}

				if (batch.n_tokens == 0) break;

				if (llama_decode(ctx, batch) != 0) {
					FString ErrorMsg = TEXT("LlamaThread: llama_decode failed during fixed blocks pre-decoding.");
					UE_LOG(LogTemp, Error, TEXT("%s"), *ErrorMsg);
					qLlamaToMain.enqueue([this, ErrorMsg]() { if (errorCb) errorCb(ErrorMsg); });
					// Handle error: maybe shutdown or mark as not ready
					return;
				}
				// Append successfully decoded tokens to MirroredKvCacheTokens
//				for (int32_t k = 0; k < batch.n_tokens; ++k) {
//					 MirroredKvCacheTokens.push_back(FixedBlocksCombinedTokens[i + k]); // Add the token that was processed
//				}
				MirroredKvCacheTokens.insert(MirroredKvCacheTokens.end(), FixedBlocksCombinedTokens.begin()+i, FixedBlocksCombinedTokens.begin()+i+batch.n_tokens);
				current_kv_pos_for_predecode += batch.n_tokens;
				i += batch.n_tokens;

				// Optional: Send progress update to main thread for UI
				float Progress = static_cast<float>(current_kv_pos_for_predecode) / FixedBlocksCombinedTokens.size();
				qLlamaToMain.enqueue([this, Progress]() { if (progressCb) progressCb(Progress); });
				if (i >= FixedBlocksCombinedTokens.size()) {
					BroadcastContextVisualUpdate_LlamaThread();
				} else {
					BroadcastContextVisualUpdate_LlamaThread(batch.n_tokens, FPlatformTime::Seconds()-TokenStartTime);
				}
				TokenStartTime = FPlatformTime::Seconds();
				if (!bIsRunning) return;
			}
			kv_cache_token_cursor = MirroredKvCacheTokens.size();
			UE_LOG(LogTemp, Log, TEXT("LlamaThread: Fixed context blocks pre-decoded. KV cache populated with %d tokens."), MirroredKvCacheTokens.size());
		}
		// --- End of Pre-decoding ---

		// Signal main thread that AIXO is now ready (or after pre-decode)
		UE_LOG(LogTemp, Log, TEXT("LlamaThread: Initialization complete, signaling ready"));
		FString msg = "Ready";
		qLlamaToMain.enqueue([this, msg]() { if (readyCb) readyCb(msg); });

		BroadcastContextVisualUpdate_LlamaThread();

		UE_LOG(LogTemp, Log, TEXT("LlamaThread: Initialization complete. KV cursor at %d. Fixed blocks tokenized and pre-decoded."), MirroredKvCacheTokens.size());
    }

	// Renamed helper to avoid confusion with the public UpdateContextBlock
	void Llama::_TokenizeAndStoreFixedBlockInternal(ELlamaContextBlockType BlockType, const FString& Text, bool bAddBosForThisBlock) {
		if (!model) return;
		std::string StdText = TCHAR_TO_UTF8(*Text);

		FTokenizedContextBlock Block;
		std::vector<llama_token> StdTokens = my_llama_tokenize(model, StdText, bAddBosForThisBlock, (BlockType == ELlamaContextBlockType::SystemPrompt)); // Special for system prompt
		Block.Tokens.insert(Block.Tokens.end(), StdTokens.begin(), StdTokens.end());
		FixedContextBlocks.FindOrAdd(BlockType) = Block;
		UE_LOG(LogTemp, Log, TEXT("LlamaThread: Tokenized (internal store) block %d, %d tokens."), (int)BlockType, Block.Tokens.size());
	}

    void Llama::ShutdownLlama_LlamaThread()
    {
        UE_LOG(LogTemp, Log, TEXT("LlamaThread: Shutting down..."));
        if (sampler_chain_instance) { llama_sampler_free(sampler_chain_instance); sampler_chain_instance = nullptr; }
        if (batch.token) { /*llama_batch_free(batch);*/ batch.token = nullptr; /* or however you check init */ }
        if (ctx) { llama_free(ctx); ctx = nullptr; }
        if (model) { llama_model_free(model); model = nullptr; }
//        FixedContextBlocks.Clear();
        ConversationHistoryTokens.clear();
        StructuredConversationHistory.clear();
        UE_LOG(LogTemp, Log, TEXT("LlamaThread: Shutdown complete."));
    }

	void Llama::UpdateContextBlock_LlamaThread(ELlamaContextBlockType BlockTypeToUpdate, const FString& NewTextContent)
	{
		if (!ctx || !model) {
			UE_LOG(LogTemp, Error, TEXT("LlamaThread: UpdateContextBlockAndKV called but Llama not ready."));
			return;
		}
		if (bIsGenerating.load(std::memory_order_acquire)) {
			UE_LOG(LogTemp, Error, TEXT("LlamaThread: UpdateContextBlockAndKV for %d called while Llama is generating. Update deferred or ignored for now."), (int)BlockTypeToUpdate);
			// Ideally, you'd queue this update to happen after generation.
			// For now, we'll just skip if busy to prevent interference.
			FString fs = "AIXO is currently thinking.\nPlease update shortly.\n\n";
			qLlamaToMain.enqueue([this, fs]() mutable { if (tokenCb) tokenCb(MoveTemp(fs)); });
			return;
		}

		UE_LOG(LogTemp, Log, TEXT("LlamaThread: Updating KV for changed BlockType %d."), (int)BlockTypeToUpdate);

		bIsGenerating = true; // Acquire "generation lock" for this entire operation
		qLlamaToMain.enqueue([this]() { if (setIsGeneratingCb) setIsGeneratingCb(true); });

		// --- 1. Retokenize the changed fixed block ---
		// _TokenizeAndStoreFixedBlockInternal just updates the FTokenizedContextBlock in FixedContextBlocks
		_TokenizeAndStoreFixedBlockInternal(BlockTypeToUpdate, NewTextContent, (BlockTypeToUpdate == ELlamaContextBlockType::SystemPrompt));

		// --- 2. Determine KV Invalidation Point & Roll Back KV Cache and MirroredTokens ---
		// This calculates the sum of token lengths of fixed blocks *before* BlockTypeToUpdate.
		int32_t ValidPrefixTokenCount = 0;
		for (uint8 i = 0; i < (uint8)BlockTypeToUpdate; ++i) {
			if (const FTokenizedContextBlock* PrevBlock = FixedContextBlocks.Find((ELlamaContextBlockType)i)) {
				ValidPrefixTokenCount += PrevBlock->Tokens.size();
			}
		}
		InvalidateKVCacheFromPosition(ValidPrefixTokenCount); // Rolls back MirroredKvCacheTokens and llama_kv_cache_seq_rm
															  // kv_cache_token_cursor is now ValidPrefixTokenCount (via MirroredKvCacheTokens.size())

		UE_LOG(LogTemp, Log, TEXT("LlamaThread: KV cache rolled back to %d tokens for BlockType %d update."), MirroredKvCacheTokens.size(), (int)BlockTypeToUpdate);

		BroadcastContextVisualUpdate_LlamaThread();

		// --- 3. Assemble the sequence of ALL tokens that need to be re-decoded to update the KV cache ---
		// This includes the updated block, all subsequent fixed blocks, the full conversation history, and current HFS.
		std::vector<llama_token> TokensToReDecodeNow;
		TokensToReDecodeNow.reserve(n_ctx_from_model); // Pre-allocate

		// Add the updated block and all subsequent fixed blocks
		bool bPastUpdatedBlock = false;
		for (uint8 i = 0; i < (uint8)ELlamaContextBlockType::COUNT; ++i) {
			ELlamaContextBlockType currentEnumBlock = (ELlamaContextBlockType)i;
			if (currentEnumBlock == BlockTypeToUpdate) {
				bPastUpdatedBlock = true;
			}
			if (bPastUpdatedBlock) { // Add this block (which is the newly tokenized one) and all subsequent fixed blocks
				if (const FTokenizedContextBlock* Block = FixedContextBlocks.Find(currentEnumBlock)) {
					TokensToReDecodeNow.insert(TokensToReDecodeNow.end(), Block->Tokens.begin(), Block->Tokens.end());
				}
			}
		}

		// Add current full ConversationHistoryTokens (already templated)
		TokensToReDecodeNow.insert(TokensToReDecodeNow.end(), ConversationHistoryTokens.begin(), ConversationHistoryTokens.end());

		// Add current CurrentHighFrequencyStateTokens (formatted as it would be for a prompt)
		if (!CurrentHighFrequencyStateTokens.empty()) {
			std::string hfs_prefix_str = "<|im_start|>system\n[Submarine Status:]\n"; // Match your prompt assembly
			std::string hfs_suffix_str = "\n<|im_end|>\n";
			std::vector<llama_token> hfs_p_tok = my_llama_tokenize(model, hfs_prefix_str, false, true);
			std::vector<llama_token> hfs_s_tok = my_llama_tokenize(model, hfs_suffix_str, false, true);

			TokensToReDecodeNow.insert(TokensToReDecodeNow.end(), hfs_p_tok.begin(), hfs_p_tok.end());
			TokensToReDecodeNow.insert(TokensToReDecodeNow.end(), CurrentHighFrequencyStateTokens.begin(), CurrentHighFrequencyStateTokens.end());
			TokensToReDecodeNow.insert(TokensToReDecodeNow.end(), hfs_s_tok.begin(), hfs_s_tok.end());
		}

		// --- 4. Decode `TokensToReDecodeNow` into KV Cache (logits=false for all) ---
		if (!TokensToReDecodeNow.empty()) {
			UE_LOG(LogTemp, Log, TEXT("LlamaThread: Background KV Update: Decoding %d tokens to refresh cache starting from (current) KV cursor %d."),
				TokensToReDecodeNow.size(), MirroredKvCacheTokens.size());

			// The kv_cache_token_cursor (from MirroredKvCacheTokens.size()) is already at ValidPrefixTokenCount.
			// The token positions for llama_batch_add will start from this current MirroredKvCacheTokens.size().
			int32_t kv_pos_for_this_decode_pass = MirroredKvCacheTokens.size();
		    double TokenStartTime = FPlatformTime::Seconds();

			for (int32_t i = 0; i < TokensToReDecodeNow.size(); /* i advanced by batch.n_tokens */ ) {
				int batch_size_override = 64;
				common_batch_clear(batch);
				int32_t n_batch_fill = 0;
				for (int32_t j = 0; j < batch_size_override/*this->batch_capacity*/ && i + j < TokensToReDecodeNow.size(); ++j) {
					common_batch_add(batch, TokensToReDecodeNow[i + j], kv_pos_for_this_decode_pass + j, {0}, false); // Logits = false
					n_batch_fill++;
				}

				if (batch.n_tokens == 0) break;

				if (llama_decode(ctx, batch) != 0) {
					FString ErrorMsg = FString::Printf(TEXT("LlamaThread: llama_decode failed during background KV update for BlockType %d."), (int)BlockTypeToUpdate);
					UE_LOG(LogTemp, Error, TEXT("%s"), *ErrorMsg);
					qLlamaToMain.enqueue([this, ErrorMsg]() { if (errorCb) errorCb(ErrorMsg); });
					// KV cache might be in a partial state. A full reset might be needed on next ProcessInput.
					// For now, just stop this background update.
					return;
				}

				// Append successfully decoded tokens to MirroredKvCacheTokens
//				for (int32_t k = 0; k < batch.n_tokens; ++k) {
//					 MirroredKvCacheTokens.push_back(TokensToReDecodeNow[i + k]);
//				}
				MirroredKvCacheTokens.insert(MirroredKvCacheTokens.end(), TokensToReDecodeNow.begin()+i, TokensToReDecodeNow.begin()+i+batch.n_tokens);
				kv_pos_for_this_decode_pass += batch.n_tokens;
				i += batch.n_tokens;

				// Optional: Send progress if this is a very long background update
				// float Progress = static_cast<float>(i) / TokensToReDecodeNow.size();
				// qLlamaToMain.enqueue([this, Progress]() { /* Call OnLlamaLoadingProgress or similar */ });
				if (i >= TokensToReDecodeNow.size()) {
					BroadcastContextVisualUpdate_LlamaThread();
				} else {
					BroadcastContextVisualUpdate_LlamaThread(batch.n_tokens, FPlatformTime::Seconds()-TokenStartTime);
				}
				TokenStartTime = FPlatformTime::Seconds();
			}
			kv_cache_token_cursor = MirroredKvCacheTokens.size();
			BroadcastContextVisualUpdate_LlamaThread();
			UE_LOG(LogTemp, Log, TEXT("LlamaThread: Background KV Update for BlockType %d complete. KV cursor (MirroredKvCacheTokens.size) now at %d."),
				(int)BlockTypeToUpdate, MirroredKvCacheTokens.size());
		} else {
			UE_LOG(LogTemp, Log, TEXT("LlamaThread: Background KV Update for BlockType %d - no tokens needed re-decoding after invalidation (e.g., last block changed or history empty). KV cursor at %d."),
				(int)BlockTypeToUpdate, MirroredKvCacheTokens.size());
		}

		bIsGenerating = false; // Acquire "generation lock" for this entire operation
		qLlamaToMain.enqueue([this]() { if (setIsGeneratingCb) setIsGeneratingCb(false); });

		// --- 5. Broadcast Context Visual Update ---
		// It's good to update the visualizer after the KV cache is refreshed.
		BroadcastContextVisualUpdate_LlamaThread();
	}

    // Invalidates KV cache from a certain token position onwards in the logical sequence
    void Llama::InvalidateKVCacheFromPosition(int32_t ValidTokenCountBeforeInvalidation) {
        if (!ctx) return;
        if (ValidTokenCountBeforeInvalidation < kv_cache_token_cursor) {
            UE_LOG(LogTemp, Log, TEXT("LlamaThread: KV Cache: Removing tokens from pos %d to %d."), ValidTokenCountBeforeInvalidation, kv_cache_token_cursor);
            llama_kv_self_seq_rm(ctx, 0, ValidTokenCountBeforeInvalidation, kv_cache_token_cursor); // seq_id 0, from pos, to pos (-1 means end)
            kv_cache_token_cursor = ValidTokenCountBeforeInvalidation;
        }
MirroredKvCacheTokens.resize(ValidTokenCountBeforeInvalidation);
         // If ValidTokenCountBeforeInvalidation >= kv_cache_token_cursor, cache is already valid up to that point or beyond.
    }

	void Llama::AssembleFullPromptForTurn(
		const FString& CurrentInputOriginalTextFStr, // Raw text of the current user/tool input for focus
		const FString& CurrentInputTypeHintFStr,     // "user" or "tool"
		std::vector<llama_token>& OutFullPromptTokens)
	{
		OutFullPromptTokens.clear();
		OutFullPromptTokens.reserve(n_ctx_from_model); // Pre-allocate

		// 1. Fixed Prefix Blocks (SystemPrompt, StaticWorldInfo, LowFrequencyState)
		for (uint8 i = 0; i < (uint8)ELlamaContextBlockType::COUNT; ++i) {
			if (const FTokenizedContextBlock* Block = FixedContextBlocks.Find((ELlamaContextBlockType)i)) {
				OutFullPromptTokens.insert(OutFullPromptTokens.end(), Block->Tokens.begin(), Block->Tokens.end());
			}
		}

		// 2. Conversation History (already fully formatted with roles and includes the current user/tool input)
		OutFullPromptTokens.insert(OutFullPromptTokens.end(), ConversationHistoryTokens.begin(), ConversationHistoryTokens.end());

		// 3. Current High Frequency State (Formatted as a system message for this turn)
		if (!CurrentHighFrequencyStateTokens.empty()) {
			// Ensure HFS is wrapped in the model's expected system message format
			// Example for Qwen: <|im_start|>system\n[HFS_CONTENT]<|im_end|>\n
			// The CurrentHighFrequencyStateTokens should be just the content.
			std::string hfs_prefix_str = "<|im_start|>system\n[Submarine Status:]\n"; // Be explicit
			std::string hfs_suffix_str = "\n<|im_end|>\n";
			
			std::vector<llama_token> hfs_p_tok = my_llama_tokenize(model, hfs_prefix_str, false, true);
			std::vector<llama_token> hfs_s_tok = my_llama_tokenize(model, hfs_suffix_str, false, true);

			OutFullPromptTokens.insert(OutFullPromptTokens.end(), hfs_p_tok.begin(), hfs_p_tok.end());
			OutFullPromptTokens.insert(OutFullPromptTokens.end(), CurrentHighFrequencyStateTokens.begin(), CurrentHighFrequencyStateTokens.end());
			OutFullPromptTokens.insert(OutFullPromptTokens.end(), hfs_s_tok.begin(), hfs_s_tok.end());
		}

		// 4. **** ADD FOCUS INSTRUCTION (if current input was from user) ****
//		if (CurrentInputTypeHintFStr.Equals(TEXT("user"), ESearchCase::IgnoreCase) && !CurrentInputOriginalTextFStr.IsEmpty()) {
//			std::string clean_user_query_std_str = TCHAR_TO_UTF8(*CurrentInputOriginalTextFStr);
//			// Basic sanitization for embedding in a string, though LLMs are usually robust.
//			// Could replace internal quotes if necessary, but often not needed.
//			std::string focus_instr_str = "<|im_start|>system\nInstruction: Your primary task is to address the last user query: \"" + clean_user_query_std_str + "\". Use all available information to formulate your response or action for this specific query.\n<|im_end|>\n";
//			std::vector<llama_token> focus_tokens = my_llama_tokenize(model, focus_instr_str, false, true);
//			OutFullPromptTokens.insert(OutFullPromptTokens.end(), focus_tokens.begin(), focus_tokens.end());
//			UE_LOG(LogTemp, Log, TEXT("LlamaThread: Added focus instruction for query: %s"), *CurrentInputOriginalTextFStr);
//		}
if (0)    if (CurrentInputTypeHintFStr.Equals(TEXT("user"), ESearchCase::IgnoreCase) && !CurrentInputOriginalTextFStr.IsEmpty()) {
        // Only add focus instruction if the current input is from the USER, not a tool response.
        std::string clean_user_query_std_str = TCHAR_TO_UTF8(*CurrentInputOriginalTextFStr);
        std::string focus_instr_str = "<|im_start|>system\nInstruction: Your primary task is to address the last user query: \"" + clean_user_query_std_str + "\". Use all available information, including any recent tool responses, to formulate your response or action for this specific query.\n<|im_end|>\n";
        // Added "including any recent tool responses" to the focus instruction itself.
        std::vector<llama_token> focus_tokens = my_llama_tokenize(model, focus_instr_str, false, true);
        OutFullPromptTokens.insert(OutFullPromptTokens.end(), focus_tokens.begin(), focus_tokens.end());
        UE_LOG(LogTemp, Log, TEXT("LlamaThread: Added focus instruction for user query: %s"), *CurrentInputOriginalTextFStr);
    }
		
		// 5. AI Assistant Prompt Prefix
		// For Qwen, the assistant turn starts immediately after the last user/system/tool <|im_end|>.
		// The actual prompt for the assistant to start generating is just "<|im_start|>assistant\n"
		std::string assistant_prefix_str = "<|im_start|>assistant\n";
		// Qwen sometimes expects no newline after "<|im_start|>assistant" if it's immediately generating.
		// Check Qwen's specific examples. If it's just "<|im_start|>assistant", adjust.
		// The newline is usually good practice as models are often trained with content on the next line.

		std::vector<llama_token> assistant_prefix_tokens = my_llama_tokenize(model, assistant_prefix_str, false, true);
		OutFullPromptTokens.insert(OutFullPromptTokens.end(), assistant_prefix_tokens.begin(), assistant_prefix_tokens.end());
	}
	
	void Llama::LlamaLogContext(FString Label) {
		int32_t StablePrefixLength = 0;
		for (uint8 j = 0; j < (uint8)ELlamaContextBlockType::COUNT; ++j) {
			if (const FTokenizedContextBlock* Block = FixedContextBlocks.Find((ELlamaContextBlockType)j)) {
				StablePrefixLength += Block->Tokens.size();
			}
		}
		StablePrefixLength += ConversationHistoryTokens.size();
		int32 CurrentFixedTokens = 0;
		for (uint8 i = 0; i < (uint8)ELlamaContextBlockType::COUNT; ++i) {
			if (const FTokenizedContextBlock* Block = FixedContextBlocks.Find((ELlamaContextBlockType)i)) {
				CurrentFixedTokens += Block->Tokens.size();
			}
		}
		std::string cs;
		int lim = MirroredKvCacheTokens.size();
		int i = lim - 1200;
		if (i < CurrentFixedTokens) i = CurrentFixedTokens;
		for	(; i<lim; i++) {
			char num[250];
			snprintf(num, 250, " (%d)", i);
			if (i == (StablePrefixLength-ConversationHistoryTokens.size())) {
				cs += "\n-----Start of convo-----";
				cs += num;
				cs += "\n";
			}
			const char* piece = llama_vocab_get_text(vocab, MirroredKvCacheTokens[i]);
			if (piece && piece[0] == '\n') cs += num;
			if (piece) cs += piece;
			//
			snprintf(num, 250, " (%d)", i+1);
			if ((i+1) == StablePrefixLength) {
				cs += "\n-----End of convo-----";
				cs += num;
				cs += "\n";
			}
			if ((i+1) == kv_cache_token_cursor) {
				cs += "\n-----Token cursor-----";
				cs += num;
				cs += "\n";
			}
		}
		std::string ss = CleanString(cs);
		//FString str = UTF8_TO_TCHAR(CleanString(CurrentReplyStr).c_str());
		FString fs = UTF8_TO_TCHAR(ss.c_str());
		UE_LOG(LogTemp, Log, TEXT("LlamaLogContext %s: \n--------------------------------------- CONTEXT DUMP STARTS ---------------------------------------\n\n%s\n\n--------------------------------------- CONTEXT DUMP ENDS ---------------------------------------\n"), *Label, *fs);
		
		cs = "";
		for (llama_token t: ConversationHistoryTokens) {
			const char* piece = llama_vocab_get_text(vocab, t);
			if (piece) cs += piece;
		}
		ss = CleanString(cs);
		//FString str = UTF8_TO_TCHAR(CleanString(CurrentReplyStr).c_str());
		fs = UTF8_TO_TCHAR(ss.c_str());
		UE_LOG(LogTemp, Log, TEXT("                 : \n--------------------------------------- CONVERSATION HISTORY STARTS ---------------------------------------\n\n%s\n\n--------------------------------------- CONVERSATION HISTORY ENDS ---------------------------------------\n"), *fs);

		cs = "";
		int idx=0;
        for (const auto& turn : StructuredConversationHistory) {
        	char num[250];
        	snprintf(num, 250, "%d", idx);
        	cs += num;
        	cs += ":";
        	cs += TCHAR_TO_UTF8(*turn.Role);
        	cs += "-'";
			for (llama_token t: turn.Tokens) {
				const char* piece = llama_vocab_get_text(vocab, t);
				if (piece) cs += piece;
			}
			cs += "'\n";
        	idx++;
        }
		ss = CleanString(cs);
		//FString str = UTF8_TO_TCHAR(CleanString(CurrentReplyStr).c_str());
		fs = UTF8_TO_TCHAR(ss.c_str());
		UE_LOG(LogTemp, Log, TEXT("                 : \n--------------------------------------- STRUCTURED CONVERSATION HISTORY STARTS ---------------------------------------\n\n%s\n\n--------------------------------------- STRUCTURED CONVERSATION HISTORY ENDS ---------------------------------------\n"), *fs);
	}

	
    void Llama::DecodeTokensAndSample(std::vector<llama_token>& FullPromptTokensForThisTurn, bool bIsFinalPromptTokenLogits)
    {
        if (!ctx || !model || (FullPromptTokensForThisTurn.size() == 0)) return;

//        LlamaLogContext("DecodeTokensAndSample top");

        eos_reached = false;
        CurrentTurnAIReplyTokens.clear();

        // Determine how many tokens from FullPromptTokensForThisTurn are already in KV cache
        int32_t n_tokens_to_eval_from_prompt = 0;
//        int32_t prompt_eval_start_index = 0;
    	int32_t prompt_eval_start_index_in_vector = 0; // Index within FullPromptTokensForThisTurn

    // This logic determines how much of FullPromptTokensForThisTurn is "new" compared to what's in KV cache
        if (kv_cache_token_cursor < FullPromptTokensForThisTurn.size()) {
			// We expect to append to the existing KV cache.
			// The first token from FullPromptTokensForThisTurn that needs decoding is at this index in the vector:
			prompt_eval_start_index_in_vector = kv_cache_token_cursor;
			n_tokens_to_eval_from_prompt = FullPromptTokensForThisTurn.size() - kv_cache_token_cursor;
		} else if (kv_cache_token_cursor > FullPromptTokensForThisTurn.size()) {
			// DESYNC! KV cache thinks it has more than the current total prompt.
			// This means InvalidateKVCacheFromPosition was not called correctly after a shortening operation.
			UE_LOG(LogTemp, Error, TEXT("LlamaThread: KV CACHE DESYNC! kv_cursor (%d) > FullPrompt.Num() (%d). Forcing full re-eval."),
				kv_cache_token_cursor, FullPromptTokensForThisTurn.size());
			
			InvalidateKVCacheFromPosition(0); // Resets kv_cache_token_cursor to 0
			
			prompt_eval_start_index_in_vector = 0; // Start from the beginning of the vector
			n_tokens_to_eval_from_prompt = FullPromptTokensForThisTurn.size();
		}
        // If kv_cache_token_cursor == FullPromptTokensForThisTurn.size(), all prompt tokens are in cache.

		UE_LOG(LogTemp, Log, TEXT("LlamaThread: Decoding prompt. Total prompt tokens: %d. Current KV cursor: %d. Tokens to eval from prompt: %d, starting at vector index %d."),
			FullPromptTokensForThisTurn.size(), kv_cache_token_cursor, n_tokens_to_eval_from_prompt, prompt_eval_start_index_in_vector);
//        UE_LOG(LogTemp, Log, TEXT("LlamaThread: Decoding prompt. Total prompt tokens: %d. KV cursor: %d. Tokens to eval from prompt: %d at index %d."),
//            FullPromptTokensForThisTurn.size(), kv_cache_token_cursor, n_tokens_to_eval_from_prompt, prompt_eval_start_index);

//        LlamaLogContext("DecodeTokensAndSample decoding");

        // Decode the new part of the prompt
#ifdef TRACK_PARALLEL_CONTEXT_TOKENS
DebugContext("DecodeTokensAndSample: Before Prompt Decoded, decoding");
#endif // TRACK_PARALLEL_CONTEXT_TOKENS
		if (n_tokens_to_eval_from_prompt > 0) {
		    double TokenStartTime = FPlatformTime::Seconds();
			for (int32_t i = 0; i < n_tokens_to_eval_from_prompt;  ) { //i advanced by batch size
				int batch_size_override = 64;
				common_batch_clear(batch);
				int32_t current_batch_fill_count = 0;
				for (int32_t j = 0; j < batch_size_override/*this->batch_capacity*/ && i + j < n_tokens_to_eval_from_prompt; ++j) {
					// Token from the vector to be decoded
					llama_token token_to_decode = FullPromptTokensForThisTurn[prompt_eval_start_index_in_vector + i + j];
					
					// Position in the KV cache for this token:
					// It's the current end of the valid KV cache (kv_cache_token_cursor) + offset within this new chunk (j)
					int32_t token_kv_pos = kv_cache_token_cursor + j; // THIS IS THE CRITICAL 'pos'

					bool bLogits = bIsFinalPromptTokenLogits && (prompt_eval_start_index_in_vector + i + j == FullPromptTokensForThisTurn.size() - 1);
					
					common_batch_add(batch, token_to_decode, token_kv_pos, {0}, bLogits);
MirroredKvCacheTokens.push_back(token_to_decode);
					current_batch_fill_count++;
				}

				if (batch.n_tokens == 0) break; // Should only happen if n_tokens_to_eval_from_prompt was 0 initially

				if (llama_decode(ctx, batch) != 0) {
					FString ErrorMsg = TEXT("LlamaThread: llama_decode failed during generation.");
					UE_LOG(LogTemp, Error, TEXT("%s"), *ErrorMsg);
					qLlamaToMain.enqueue([this, ErrorMsg]() { if (errorCb) errorCb(ErrorMsg); });
					eos_reached = true;
					return;
				}
				// IMPORTANT: Advance kv_cache_token_cursor by the number of tokens *successfully decoded and added to KV cache*
				kv_cache_token_cursor += batch.n_tokens;
				i += batch.n_tokens; // Advance the loop for the source tokens
				if (i >= n_tokens_to_eval_from_prompt) {
					BroadcastContextVisualUpdate_LlamaThread();
				} else {
					BroadcastContextVisualUpdate_LlamaThread(batch.n_tokens, FPlatformTime::Seconds()-TokenStartTime);
				}
				TokenStartTime = FPlatformTime::Seconds();
			}
			kv_cache_token_cursor = MirroredKvCacheTokens.size();
		}
		UE_LOG(LogTemp, Log, TEXT("LlamaThread: Prompt decoded. KV cursor now: %d."), kv_cache_token_cursor);
#ifdef TRACK_PARALLEL_CONTEXT_TOKENS
DebugContext("DecodeTokensAndSample: Prompt Decoded, starting generation");
#endif // TRACK_PARALLEL_CONTEXT_TOKENS

        // Start Generation Loop
//        LlamaLogContext("DecodeTokensAndSample start generation");
		double TokenStartTime = FPlatformTime::Seconds();
		int32 kv_cache_token_cursor_at_gen_start = kv_cache_token_cursor;
        while (kv_cache_token_cursor < n_ctx_from_model && !eos_reached) { // Check against actual context window
            if (!bIsRunning) { eos_reached = true; break; } // Check if shutdown requested
//if ((kv_cache_token_cursor%10) == 0) UE_LOG(LogTemp, Log, TEXT("*"))
//            float* logits = llama_get_logits_ith(ctx, batch.n_tokens - 1); // Get logits from the last token processed
            llama_token new_token_id = llama_sampler_sample(sampler_chain_instance, ctx, -1/*logits, nullptr candidates */); // Pass logits explicitly
            llama_sampler_accept(sampler_chain_instance, new_token_id);

            CurrentTurnAIReplyTokens.push_back(new_token_id);

            if (new_token_id == llama_vocab_eos(llama_model_get_vocab(model)) || CheckStopSequences()) {
                eos_reached = true;
                // Check for tool call before finalizing EOS
                std::string CurrentReplyStr;
                for(llama_token tk : CurrentTurnAIReplyTokens) {
                    const char* piece = llama_vocab_get_text(vocab, tk);
                    if (piece) CurrentReplyStr += piece;
                }
//                // TODO: Proper Tool Call Parsing Here based on your chosen format (e.g. ```tool_call...``` or JSON)
//                // For now, simple placeholder:
//				if (CurrentReplyStr.find("<tool_call>") != std::string::npos) {
//                    qLlamaToMain.enqueue([this, CurrentReplyStr]() {
//                    	FString str = UTF8_TO_TCHAR(CleanString(CurrentReplyStr).c_str());
//                    	if (toolCallCb) toolCallCb(str);
//					});
//                    // Don't send EOS yet, wait for tool response to continue generation
//                    // Or, if tool call is the *end* of the turn, then set eos_reached.
//                    // This depends on your AIXO's turn structure. Assuming tool call ends AI's immediate output:
//                    UE_LOG(LogTemp, Log, TEXT("LlamaThread: Tool call detected. '%hs'"), CleanString(CurrentReplyStr).c_str());
//                } else {
//					UE_LOG(LogTemp, Log, TEXT("LlamaThread: EOS or Stop Sequence reached."));
//                }
                break; // Exit generation loop
            }

            // Send token to main thread
            std::string piece_str_raw = CleanString(llama_vocab_get_text(vocab, new_token_id));
            {
                // (Your UTF-8 char replacement logic for Ġ and Ċ)
                FString token_fstring = UTF8_TO_TCHAR(piece_str_raw.c_str());
                qLlamaToMain.enqueue([this, token_fstring]() mutable { if (tokenCb) tokenCb(MoveTemp(token_fstring)); });
            }

            // Prepare for next token: decode the just generated token
            common_batch_clear(batch);
            common_batch_add(batch, new_token_id, kv_cache_token_cursor, {0}, true); // Logits for this new token for next prediction
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
        } // End of generation loop
		BroadcastContextVisualUpdate_LlamaThread(kv_cache_token_cursor - kv_cache_token_cursor_at_gen_start, 0.0f, FPlatformTime::Seconds() - TokenStartTime);
		TokenStartTime = FPlatformTime::Seconds();

#ifdef TRACK_PARALLEL_CONTEXT_TOKENS
DebugContext("DecodeTokensAndSample: Ending generation");
#endif // TRACK_PARALLEL_CONTEXT_TOKENS

//        LlamaLogContext("DecodeTokensAndSample end generation");

// performance data
if (eos_reached) {
    struct llama_perf_context_data PerfCtxData = llama_perf_context(ctx);
    struct llama_perf_sampler_data PerfSamplerData = llama_perf_sampler(sampler_chain_instance);

    UE_LOG(LogTemp, Warning, TEXT("LlamaThread: Raw Perf - PromptEvalT:%.2fms, PromptEvalN:%d, GenEvalT:%.2fms, GenEvalN:%d, SampleT:%.2fms, SampleN:%d"),
        (float)PerfCtxData.t_p_eval_ms, PerfCtxData.n_p_eval,
        (float)PerfCtxData.t_eval_ms, PerfCtxData.n_eval,
        (float)PerfSamplerData.t_sample_ms, PerfSamplerData.n_sample);
    
    llama_perf_context_reset(ctx);
    llama_perf_sampler_reset(sampler_chain_instance);
}

        if (eos_reached) { // eos_reached is true if llama_token_eos or your stop sequence was hit
            FString RawAIOutputThisTurnStr;
            for(llama_token tk : CurrentTurnAIReplyTokens) { // CurrentTurnAIReplyTokens contains all tokens generated by AI this turn
                const char* piece = llama_vocab_get_text(vocab, tk);
                if(piece) RawAIOutputThisTurnStr += UTF8_TO_TCHAR(piece);
            }
            // Clean the raw output string if necessary (your CleanString function)
            RawAIOutputThisTurnStr = UTF8_TO_TCHAR(CleanString(TCHAR_TO_UTF8(*RawAIOutputThisTurnStr)).c_str());


            // Trim at ~~~END_AIXO_TURN~~~ if it's present, as that's our logical end.
            int32 EndTurnMarkerPos = RawAIOutputThisTurnStr.Find(TEXT("~~~END_AIXO_TURN~~~"));
            FString ProcessedAIOutputStr = (EndTurnMarkerPos != INDEX_NONE) ? RawAIOutputThisTurnStr.Left(EndTurnMarkerPos) : RawAIOutputThisTurnStr;
            ProcessedAIOutputStr = ProcessedAIOutputStr.TrimStartAndEnd(); // Trim any leading/trailing whitespace after marker removal


            // 1. Parse ProcessedAIOutputStr to find <think>, <tool_call>, CAP:
            FString ThinkContent; // You might log this
            FString ToolCallPayloadForMainThread; // The JSON part of the tool call
            FString ToolCallFullTagForHistory;    // The full <tool_call>...</tool_call> for history
            FString CaptainDialogueForMainThread; // The text after "CAP: " for TTS/UI
            bool bToolCallMadeThisTurn = false;

            // --- Robust Parsing Logic (Example - refine this) ---
            int32 ThinkStart = ProcessedAIOutputStr.Find(TEXT("<think>"));
            int32 ThinkEnd = ProcessedAIOutputStr.Find(TEXT("</think>"));
            if (ThinkStart != INDEX_NONE && ThinkEnd != INDEX_NONE && ThinkEnd > ThinkStart) {
                ThinkContent = ProcessedAIOutputStr.Mid(ThinkStart + FString(TEXT("<think>")).Len(), ThinkEnd - (ThinkStart + FString(TEXT("<think>")).Len()));
				ProcessedAIOutputStr.RemoveAt(ThinkStart, ThinkEnd + FString(TEXT("</think>")).Len() - ThinkStart);
                UE_LOG(LogTemp, Log, TEXT("LlamaThread: Think content: '%s'"), *ThinkContent);
            }

            int32 ToolCallStartTagPos = ProcessedAIOutputStr.Find(TEXT("<tool_call>"));
            int32 ToolCallEndTagPos = ProcessedAIOutputStr.Find(TEXT("</tool_call>"));
            if (ToolCallStartTagPos != INDEX_NONE && ToolCallEndTagPos != INDEX_NONE && ToolCallEndTagPos > ToolCallStartTagPos) {
                ToolCallFullTagForHistory = ProcessedAIOutputStr.Mid(ToolCallStartTagPos, ToolCallEndTagPos + FString(TEXT("</tool_call>")).Len() - ToolCallStartTagPos);
                ToolCallPayloadForMainThread = ToolCallFullTagForHistory.Mid(FString(TEXT("<tool_call>")).Len());
                ToolCallPayloadForMainThread.LeftChopInline(FString(TEXT("</tool_call>")).Len());
				ProcessedAIOutputStr.RemoveAt(ToolCallStartTagPos, ToolCallEndTagPos + FString(TEXT("</tool_call>")).Len() - ToolCallStartTagPos);

                bToolCallMadeThisTurn = true;
                qLlamaToMain.enqueue([this, ToolCallPayloadForMainThread]() {
                	if (toolCallCb) toolCallCb(ToolCallPayloadForMainThread);
                	if (tokenCb) {
                		tokenCb(TEXT("\n"));
                		tokenCb(ToolCallPayloadForMainThread);
                		tokenCb(TEXT("\n"));
					}
				});

                UE_LOG(LogTemp, Log, TEXT("LlamaThread: Tool call detected: '%s'"), *ToolCallFullTagForHistory);
            }

            // Only look for CAP: if not primarily a tool call, or if AI can do both (depends on your prompting)
            // Assuming for now, if a tool call is made, that's the primary action for this step.
            // If AI can also speak *before* a tool call, adjust parsing.
            if (!bToolCallMadeThisTurn) { // Or if you allow dialogue AND tool call, parse independently
//                int32 CapStart = ProcessedAIOutputStr.Find(TEXT("CAP:"));
//                if (CapStart != INDEX_NONE)
                {
                    // End of CAP dialogue is before any other tags or end of string
                    int32 EndOfCap = ProcessedAIOutputStr.Len(); // Default to end of string
                    // You might want to find the next <tag> if any, to not include it in dialogue.
                    
                    CaptainDialogueForMainThread = ProcessedAIOutputStr;//.Mid(CapStart + FString(TEXT("CAP:")).Len()).TrimStartAndEnd();
                    // Send CaptainDialogueForMainThread to main thread for TTS/display
                    // (e.g., using a dedicated FOnDialogueGenerated delegate or OnGenerationComplete)
                    UE_LOG(LogTemp, Log, TEXT("LlamaThread: Captain dialogue: '%s'"), *CaptainDialogueForMainThread);
                }
            }
            // --- End of Parsing ---

//        	LlamaLogContext("DecodeTokensAndSample end of parsing");

            // 2. Construct the *actual* assistant message TOKENS to store in history
            std::vector<llama_token> AssistantMessageTokensForStorageInHistory;
            if (bToolCallMadeThisTurn && !ToolCallFullTagForHistory.IsEmpty()) {
                // If a tool call was made, that's the assistant's "utterance" for history
                std::string tool_call_std_str = TCHAR_TO_UTF8(*ToolCallFullTagForHistory);
                std::vector<llama_token> tc_std = my_llama_tokenize(model, tool_call_std_str, false, true); // true for special tags
                AssistantMessageTokensForStorageInHistory.insert(AssistantMessageTokensForStorageInHistory.end(), tc_std.begin(), tc_std.end());
            } else if (!CaptainDialogueForMainThread.IsEmpty()) {
                // If no tool call, but there was dialogue, that's the utterance
                std::string cap_dialogue_std_str = TCHAR_TO_UTF8(*CaptainDialogueForMainThread);
                std::vector<llama_token> cd_std = my_llama_tokenize(model, cap_dialogue_std_str, false, false); // false for plain text
                AssistantMessageTokensForStorageInHistory.insert(AssistantMessageTokensForStorageInHistory.end(), cd_std.begin(), cd_std.end());
            }

//LlamaLogContext("DecodeTokensAndSample 2");

            // If AssistantMessageTokensForStorageInHistory is empty here, it means the AI generated
            // something that wasn't a recognized tool call or CAP: dialogue (e.g., only a <think> block and then stopped).
            // You might decide to store an empty assistant turn or a placeholder.

            // 3. Append the assistant's processed turn to structured history
            //    Only do this if there's something substantive to add.
            if (!AssistantMessageTokensForStorageInHistory.empty()) {
				{
					std::string s;
					for (llama_token tk : AssistantMessageTokensForStorageInHistory) {
						const char* piece = llama_vocab_get_text(vocab, tk);
						if (piece) s += piece;
					}
					s = CleanString(s);
					UE_LOG(LogTemp, Log, TEXT("LlamaThread: DecodeTokensAndSample KV cursor: %d. send to assistant history '%hs'."), kv_cache_token_cursor, s.c_str());
				}
				int32_t StablePrefixLength = 0;
				for (uint8 i = 0; i < (uint8)ELlamaContextBlockType::COUNT; ++i) {
					if (const FTokenizedContextBlock* Block = FixedContextBlocks.Find((ELlamaContextBlockType)i)) {
						StablePrefixLength += Block->Tokens.size();
					}
				}
				StablePrefixLength += ConversationHistoryTokens.size(); // ConversationHistoryTokens now includes the latest input with its role tags
//LlamaLogContext("DecodeTokensAndSample 3");

				if (kv_cache_token_cursor > StablePrefixLength) {
					UE_LOG(LogTemp, Warning, TEXT("LlamaThread: KV cursor (%d) was beyond current fixed+convo length (%d). Adjusting."),
						kv_cache_token_cursor, StablePrefixLength);
					InvalidateKVCacheFromPosition(StablePrefixLength); // This sets kv_cache_token_cursor = StablePrefixLength
				}
                AppendTurnToStructuredHistory(TEXT("assistant"), AssistantMessageTokensForStorageInHistory);
std::string assistant_prefix_str = "<|im_start|>assistant\n";
std::vector<llama_token> assistant_prefix_tokens = my_llama_tokenize(model, assistant_prefix_str, false, true);
MirroredKvCacheTokens.insert(MirroredKvCacheTokens.end(), assistant_prefix_tokens.begin(), assistant_prefix_tokens.end());
kv_cache_token_cursor += assistant_prefix_tokens.size();
                MirroredKvCacheTokens.insert(MirroredKvCacheTokens.end(), AssistantMessageTokensForStorageInHistory.begin(), AssistantMessageTokensForStorageInHistory.end());
                kv_cache_token_cursor += AssistantMessageTokensForStorageInHistory.size();
//LlamaLogContext("DecodeTokensAndSample 3.1");
                // Note: AppendTurnToStructuredHistory should internally rebuild the flat ConversationHistoryTokens
                // and PruneConversationHistory (if called from there) should handle KV cache invalidation
                // if the total length changes significantly or pruning occurs.
                // The kv_cache_token_cursor *already* reflects the end of the AI's generated tokens.
                // The main thing is that ConversationHistoryTokens is now longer for the *next* turn.
            } else if (bToolCallMadeThisTurn && ToolCallFullTagForHistory.IsEmpty()) {
                 UE_LOG(LogTemp, Warning, TEXT("LlamaThread: Tool call detected but content for history was empty."));
            } else if (!CaptainDialogueForMainThread.IsEmpty() && AssistantMessageTokensForStorageInHistory.empty()){
                 UE_LOG(LogTemp, Warning, TEXT("LlamaThread: Captain dialogue detected but content for history was empty."));
            } else {
                 UE_LOG(LogTemp, Log, TEXT("LlamaThread: AI turn ended with no substantive tool call or CAP dialogue to add to history."));
            }
        } // End if (eos_reached)

        UE_LOG(LogTemp, Log, TEXT("LlamaThread: DecodeTokensAndSample finished. KV cursor: %d. EOS: %d. bIsGenerating: %d"),
            kv_cache_token_cursor, eos_reached.load(), bIsGenerating.load());
//        LlamaLogContext("DecodeTokensAndSample finished");
    } // End of DecodeTokensAndSample

    void Llama::AppendTurnToStructuredHistory(const FString& Role, const std::vector<llama_token>& Tokens) {
        StructuredConversationHistory.push_back({Role, Tokens});
        // Rebuild the flat ConversationHistoryTokens from StructuredConversationHistory
        ConversationHistoryTokens.clear();
        for (const auto& turn : StructuredConversationHistory) {
            // Here you would inject role tokens like "<|im_start|>user\n" before turn.Tokens
            // and "<|im_end|>\n" after, according to your model's chat template.
            // This is CRITICAL for the LLM to understand the conversation flow.
            // Example for Qwen (simplified):
            std::string role_prefix = "<|im_start|>" + std::string(TCHAR_TO_UTF8(*turn.Role)) + "\n";
            std::string role_suffix = "<|im_end|>\n";		// ??? had to remove <|im_end|> or it would wind up twice in convo

            std::vector<llama_token> role_prefix_t = my_llama_tokenize(model, role_prefix, false, true);
            std::vector<llama_token> role_suffix_t = my_llama_tokenize(model, role_suffix, false, true);

			ConversationHistoryTokens.insert(ConversationHistoryTokens.end(), role_prefix_t.begin(), role_prefix_t.end());
			ConversationHistoryTokens.insert(ConversationHistoryTokens.end(), turn.Tokens.begin(), turn.Tokens.end());
			ConversationHistoryTokens.insert(ConversationHistoryTokens.end(), role_suffix_t.begin(), role_suffix_t.end());
        }
    }

    void Llama::PruneConversationHistory() {
        if (!model || !ctx) return;

        int32 CurrentFixedTokens = 0;
        for (uint8 i = 0; i < (uint8)ELlamaContextBlockType::COUNT; ++i) {
            if (const FTokenizedContextBlock* Block = FixedContextBlocks.Find((ELlamaContextBlockType)i)) {
                CurrentFixedTokens += Block->Tokens.size();
            }
        }
        // Add current HFS + buffer for new input & AI response
        int32 OverheadTokens = CurrentFixedTokens + CurrentHighFrequencyStateTokens.size() + 512; // 512 is a guess for input+output
        int32 TargetMaxConvoTokens = n_ctx_from_model - OverheadTokens;

        if (TargetMaxConvoTokens < 0) TargetMaxConvoTokens = 0; // Safety

        int32 CurrentTotalConvoTokens = ConversationHistoryTokens.size(); // Based on flat token list

        if (CurrentTotalConvoTokens > TargetMaxConvoTokens) {
            UE_LOG(LogTemp, Warning, TEXT("LlamaThread: Pruning conversation. Current: %d, Target Max: %d"), CurrentTotalConvoTokens, TargetMaxConvoTokens);
            int32 TokensToTrimCount = 0;
            while (CurrentTotalConvoTokens > TargetMaxConvoTokens && !StructuredConversationHistory.empty()) {
                const FConversationTurn& oldest_turn = StructuredConversationHistory.front();
                // Calculate tokens for this turn including role markers
                std::string role_prefix = "<|im_start|>" + std::string(TCHAR_TO_UTF8(*oldest_turn.Role)) + "\n";
                std::string role_suffix = "<|im_end|>\n";
                std::vector<llama_token> r_p_t = my_llama_tokenize(model, role_prefix, false, true);
                std::vector<llama_token> r_s_t = my_llama_tokenize(model, role_suffix, false, true);
                
                int32 TurnTokenLength = r_p_t.size() + oldest_turn.Tokens.size() + r_s_t.size();
                TokensToTrimCount += TurnTokenLength;
                CurrentTotalConvoTokens -= TurnTokenLength;
                StructuredConversationHistory.pop_front();
            }

            // Rebuild ConversationHistoryTokens from the pruned StructuredConversationHistory
            ConversationHistoryTokens.clear(); // Clear it first
            for (const auto& turn : StructuredConversationHistory) {
				std::string role_prefix = "<|im_start|>" + std::string(TCHAR_TO_UTF8(*turn.Role)) + "\n";
				std::string role_suffix = "<|im_end|>\n";
				std::vector<llama_token> r_p_t = my_llama_tokenize(model, role_prefix, false, true);
				std::vector<llama_token> r_s_t = my_llama_tokenize(model, role_suffix, false, true);

				ConversationHistoryTokens.insert(ConversationHistoryTokens.end(), r_p_t.begin(), r_p_t.end());
				ConversationHistoryTokens.insert(ConversationHistoryTokens.end(), turn.Tokens.begin(), turn.Tokens.end());
				ConversationHistoryTokens.insert(ConversationHistoryTokens.end(), r_s_t.begin(), r_s_t.end());
            }
            
            // Invalidate KV cache from the start of the conversation history block
            InvalidateKVCacheFromPosition(CurrentFixedTokens);
            UE_LOG(LogTemp, Log, TEXT("LlamaThread: Conversation pruned. New convo token count: %d. KV invalidated from: %d"), ConversationHistoryTokens.size(), CurrentFixedTokens);
        }
    }

	void Llama::RebuildFlatConversationHistoryTokensFromStructured() {
		ConversationHistoryTokens.clear();
		for (const FConversationTurn& turn : StructuredConversationHistory) {
			std::string role_prefix_str = "<|im_start|>" + std::string(TCHAR_TO_UTF8(*turn.Role)) + "\n";
			std::string role_suffix_str = "<|im_end|>\n";
			// ... tokenize prefix, turn.Tokens, suffix and append to ConversationHistoryTokens ...
		}
	}
	void Llama::StopSeqHelper(const FString& stopSeqFStr)
	{
		std::string stopSeqStdStr = TCHAR_TO_UTF8(*stopSeqFStr);
		
		// Tokenize the stop sequence string.
		// - add_bos should be false.
		// - 'special' flag: This is tricky.
		//   - If your stop string IS a special token itself (e.g., "<|im_end|>" might be a single token ID),
		//     then 'special' might need to be true for the tokenizer to recognize it as such.
		//   - If your stop string is plain text (e.g., "Captain:"), 'special' should be false.
		//   - For "<|im_end|>", it's often a single special token. Test with special = true first.
		//     If it tokenizes into multiple tokens, try special = false.
		bool is_special_token_sequence = stopSeqStdStr.rfind("<|", 0) == 0 && stopSeqStdStr.rfind("|>", stopSeqStdStr.length() - 2) != std::string::npos;


		std::vector<llama_token> tokenized_stop_seq = my_llama_tokenize( // Your helper function
			this->model,
			stopSeqStdStr,
			false, // Never add BOS to a stop sequence itself
			is_special_token_sequence // True if it looks like a special token format
		);

		if (!tokenized_stop_seq.empty()) {
			this->stopSequencesTokens.push_back(tokenized_stop_seq);
			
			FString token_ids_str;
			for(llama_token t : tokenized_stop_seq) { token_ids_str += FString::Printf(TEXT("%d "), t); }
			UE_LOG(LogTemp, Log, TEXT("Tokenized stop sequence: '%s' -> Tokens: [ %s]"), *stopSeqFStr, *token_ids_str);
		} else {
			UE_LOG(LogTemp, Warning, TEXT("Failed to tokenize stop sequence (or it was empty): '%s'"), *stopSeqFStr);
		}
	}

	bool Llama::CheckStopSequences() {
		if (stopSequencesTokens.empty()) return false;

		for (const auto& stop_seq : stopSequencesTokens) {
			if (stop_seq.empty()) continue;
			if (CurrentTurnAIReplyTokens.size() >= stop_seq.size()) {
				// Check if the tail of current_conversation_tokens matches stop_seq
				if (std::equal(stop_seq.begin(), stop_seq.end(),
							   CurrentTurnAIReplyTokens.end() - stop_seq.size())) {
					// For debugging which stop sequence matched:
//					std::string matched_stop_str;
//					for(llama_token t : stop_seq) {
//						const char* piece = llama_vocab_get_text(llama_model_get_vocab(model), t);
//						if (piece) matched_stop_str += piece;
//					}
//					UE_LOG(LogTemp, Log, TEXT("Llama thread %p: Stop sequence matched: '%s'"), this, UTF8_TO_TCHAR(matched_stop_str.c_str()));
					UE_LOG(LogTemp, Log, TEXT("Llama thread %p: Stop sequence matched."), this);
					return true;
				}
			}
		}
		return false;
	}
	
	std::string Llama::CleanString(std::string p_str) {
		// Add more replacements if needed, e.g., for C_dot (Ċ) if it's a newline token
		// The original code had C4 8A for newline, which is Ċ (U+010A).
		// If your model uses a different newline token, adjust accordingly.
		// Or, if specific tokens are *meant* to be newlines (like llama_token_nl()),
		// you might handle them differently.
		// For now, let's assume your specific replacement for Ċ is desired.
		size_t start_pos = 0;
		while((start_pos = p_str.find("\xC4\xA0", start_pos)) != std::string::npos) { // Check for C_dot (Ċ) -> newline
			 p_str.replace(start_pos, 2, " ");
			 start_pos++; // Move past the replaced character to avoid infinite loop if replacement contains original
		}
		start_pos = 0;
		while((start_pos = p_str.find("\xC4\x8A", start_pos)) != std::string::npos) { // Check for C_dot (Ċ) -> newline
			 p_str.replace(start_pos, 2, "\n");
			 start_pos++; // Move past the replaced character to avoid infinite loop if replacement contains original
		}
		start_pos = 0;
		while((start_pos = p_str.find("\xC4\x83", start_pos)) != std::string::npos) { // Check for C_dot (Äł) -> newline
			 p_str.replace(start_pos, 2, " ");
			 start_pos++; // Move past the replaced character to avoid infinite loop if replacement contains original
		}
		start_pos = 0;
		while((start_pos = p_str.find("\xC4\x80", start_pos)) != std::string::npos) { // Check for C_dot (ÄĬ) -> newline
			 p_str.replace(start_pos, 2, "\n");
			 start_pos++; // Move past the replaced character to avoid infinite loop if replacement contains original
		}
//The Äł is C4 B3 in UTF-8 (for Ł) or C4 B2 for L with stroke, not C4 A0 or C4 8A.
//The ÄĬ is C4 B0 in UTF-8 (for Ī) or similar.
		return p_str;
	}

	// Helper function to de-tokenize a std::vector<llama_token> and append to a string
	void Llama::DetokenizeAndAppend(std::string& TargetString, const std::vector<llama_token>& TokensToDetokenize, const llama_model* ModelHandle) {
		if (!ModelHandle) return;
		const llama_vocab* CurrentVocab = llama_model_get_vocab(ModelHandle);
		if (!CurrentVocab) return;

		for (llama_token token_id : TokensToDetokenize) {
			const char* piece = llama_vocab_get_text(CurrentVocab, token_id);
			if (piece) {
				TargetString += CleanString(piece);
			}
		}
	}

	std::string Llama::AssembleFullContextForDump_LlamaThread() {
		if (!model || !ctx) {
			return "[Llama not initialized or model/context not available for dump]";
		}

		std::string full_context_str;
		full_context_str.reserve(n_ctx_from_model * 6); // Increased pre-allocation a bit

//		UE_LOG(LogTemp, Log, TEXT("LlamaThread: Assembling context dump..."));

		// --- This order MUST EXACTLY MATCH how AssembleFullPromptForTurn builds the prompt ---

		full_context_str += "========== START OF FULL CONTEXT DUMP ==========\n";

		// 1. Fixed Prefix Blocks (System, StaticWorld, LowFreq)
		full_context_str += "\n---------- FIXED CONTEXT BLOCKS ----------\n";
		for (uint8 i = 0; i < (uint8)ELlamaContextBlockType::COUNT; ++i) {
			ELlamaContextBlockType currentBlockType = (ELlamaContextBlockType)i;
			// Construct block name string for logging
			FString BlockNameFStr = UEnum::GetValueAsString(currentBlockType);
			std::string BlockNameStdStr = "UNKNOWN_BLOCK";
			if (!BlockNameFStr.IsEmpty()) {
				BlockNameStdStr = TCHAR_TO_UTF8(*BlockNameFStr);
			}
			
			if (const FTokenizedContextBlock* Block = FixedContextBlocks.Find(currentBlockType)) {
				full_context_str += "--- START BLOCK: " + BlockNameStdStr + " ---\n";
				DetokenizeAndAppend(full_context_str, Block->Tokens, model);
				full_context_str += "\n--- END BLOCK: " + BlockNameStdStr + " ---\n";
			}
		}
		full_context_str += "---------- END OF FIXED CONTEXT BLOCKS ----------\n";

		// 2. Conversation History (This is the flat ConversationHistoryTokens array)
		//    This array should have been built by your history management logic
		//    to include all turns (user and assistant) with proper chat templating.
		full_context_str += "\n---------- FLATTENED CONVERSATION HISTORY (from ConversationHistoryTokens) ----------\n";
		DetokenizeAndAppend(full_context_str, ConversationHistoryTokens, model);
		full_context_str += "\n---------- END OF FLATTENED CONVERSATION HISTORY ----------\n";

		// 3. Current High Frequency State (as it was prepared for the *last* turn or *would be* for the next)
		full_context_str += "\n---------- CURRENT HIGH FREQUENCY STATE (CurrentHighFrequencyStateTokens) ----------\n";
		// You need to decide how HFS is formatted for the prompt. If it's wrapped in role tags,
		// the dump should reflect that. For now, just detokenizing its content.
		// Example: if HFS is presented as a system message:
		// if (!CurrentHighFrequencyStateTokens.IsEmpty()) {
		//    full_context_str += std::string(TCHAR_TO_UTF8(*FString::Printf(TEXT("<|im_start|>system\n[HFS_CONTENT]\n<|im_end|>\n")))).replace(
		//        std::string(TCHAR_TO_UTF8(TEXT("[HFS_CONTENT]"))).size(), DetokenizeToString(CurrentHighFrequencyStateTokens, model));
		// }
		DetokenizeAndAppend(full_context_str, CurrentHighFrequencyStateTokens, model); // Assuming this holds the latest HFS tokens
		full_context_str += "\n---------- END OF CURRENT HIGH FREQUENCY STATE ----------\n";

		// 4. AI Assistant Prefix (The prompt that cues the AI to start its response)
		//    This should be the exact same prefix used in AssembleFullPromptForTurn.
		std::string assistant_prefix_str = "\n<|im_start|>assistant\n"; // Example for Qwen
		full_context_str += "\n---------- AI PROMPT PREFIX ----------\n";
		full_context_str += assistant_prefix_str;
		full_context_str += "\n---------- END OF AI PROMPT PREFIX ----------\n";


		// Optional: Add KV Cache Status
		full_context_str += TCHAR_TO_UTF8(*FString::Printf(TEXT("\n\n--- KV Cache Status ---\nValid Tokens (kv_cache_token_cursor): %d / %d (n_ctx_from_model)\n"), 
											kv_cache_token_cursor, n_ctx_from_model));
		
		full_context_str += "\n========== END OF FULL CONTEXT DUMP ==========\n";


//		UE_LOG(LogTemp, Log, TEXT("LlamaThread: Context dump assembled. Length: %d chars (approx)."), full_context_str.length());
		return full_context_str;
	}

	// And ensure RequestFullContextDump_LlamaThread calls this correctly:
	void Llama::RequestFullContextDump_LlamaThread() { // Renamed
		std::string context_dump_std_str = AssembleFullContextForDump_LlamaThread();
		FString context_dump_fstr = UTF8_TO_TCHAR(context_dump_std_str.c_str());

		// Your existing FString post-processing for special characters can go here if needed,
		// though the DetokenizeAndAppend helper now handles some of it.

		qLlamaToMain.enqueue([this, context_dump_fstr]() mutable {
			if (fullContextDumpCb) {
				fullContextDumpCb(MoveTemp(context_dump_fstr));
			}
		});
	}

	void Llama::BroadcastContextVisualUpdate_LlamaThread(int32 nTokens, float sDecode, float sGenerate)
	{
		if (!model || !ctx) {
			UE_LOG(LogTemp, Warning, TEXT("LlamaThread: Cannot broadcast context visual update, model/ctx/owner not ready."));
			return;
		}

//UE_LOG(LogTemp, Warning, TEXT("BroadcastContextVisualUpdate_LlamaThread: %d tokens, %.fms decode, %.fms generate."), nTokens, 1000*sDecode, 1000*sGenerate);

		FContextVisPayload Payload;
		Payload.TotalTokenCapacity = n_ctx_from_model; // The full capacity of the context window
		Payload.KvCacheDecodedTokenCount = MirroredKvCacheTokens.size(); // How much is ACTUALLY in KV cache
		if (nTokens == 0) {
			// don't consume msDecode or msGenerate
			Payload.fFmsPerTokenDecode = 0.0f;
			Payload.fFmsPerTokenGenerate = 0.0f;
		} else {
			if (sDecode > 0) Payload.fFmsPerTokenDecode = 1000.0f*sDecode / nTokens;
			if (sGenerate > 0) Payload.fFmsPerTokenGenerate = 1000.0f*sGenerate / nTokens;
		}

		// This will sum the tokens of the blocks we *intend* to visualize as part of the current logical prompt structure
		int32 AccumulatedTokensForVisualizedPromptStructure = 0;
		float CurrentNormalizedPosition = 0.0f; // Tracks the bottom of the next block to draw

		// Helper lambda to add blocks to the payload
		auto AddBlockToPayload = 
			[&](EContextVisBlockType Type, const std::vector<llama_token>& Tokens, const FString& TooltipPrefix = TEXT(""))
		{
			if (Tokens.empty() || Payload.TotalTokenCapacity == 0) return;

			float NormalizedHeight = static_cast<float>(Tokens.size()) / static_cast<float>(Payload.TotalTokenCapacity);
			// Ensure even very small blocks are minimally visible if desired, or just let them be tiny
			if (NormalizedHeight < 0.0005f && NormalizedHeight > 0.0f) NormalizedHeight = 0.0005f; 
			if (NormalizedHeight <= 0.0f) return; // Don't draw zero-height blocks

			FLinearColor Color = FLinearColor::Black;
			FString TypeName = UEnum::GetValueAsString(Type);
			if (TypeName.IsEmpty()) TypeName = TEXT("UnknownBlockType");

			// --- Color Assignments ---
			if (Type == EContextVisBlockType::SystemPrompt) Color = FLinearColor::FromSRGBColor(FColor(0xFF,0x80,0x80));
			else if (Type == EContextVisBlockType::StaticWorldInfo) Color = FLinearColor::FromSRGBColor(FColor(0x80,0x80,0xFF));
			else if (Type == EContextVisBlockType::LowFrequencyState) Color = FLinearColor::FromSRGBColor(FColor(0x00,0x00,0xFF));
			else if (Type == EContextVisBlockType::ConversationTurnUser) Color = FLinearColor::FromSRGBColor(FColor(0xFF,0xFF,0x00));
			else if (Type == EContextVisBlockType::ConversationTurnAssistant) Color = FLinearColor::FromSRGBColor(FColor(0x46,0x82,0xB4));
			else if (Type == EContextVisBlockType::ConversationTurnToolResponse) Color = FLinearColor::FromSRGBColor(FColor(0x8A,0x2B,0xE2));
			else if (Type == EContextVisBlockType::HighFrequencyState) Color = FLinearColor::FromSRGBColor(FColor(0xFF,0x80,0x00));
			// Add color for FocusInstruction if you make it a distinct EContextVisBlockType
			// else if (Type == EContextVisBlockType::FocusInstruction) Color = FLinearColor::FromSRGBColor(FColor(0xFF,0xFF,0x00)); // Yellow (example)

			FText Tooltip = FText::FromString(FString::Printf(TEXT("%s%s (%d tokens)"), *TooltipPrefix, *TypeName, Tokens.size()));
			Payload.Blocks.Add(FContextVisBlock(Type, CurrentNormalizedPosition, NormalizedHeight, Color, Tooltip));
			
			CurrentNormalizedPosition += NormalizedHeight;
			AccumulatedTokensForVisualizedPromptStructure += Tokens.size();
		};

		// --- Populate Payload.Blocks based on the current logical prompt structure ---

		// 1. Fixed Blocks
		for (uint8 i = 0; i < (uint8)ELlamaContextBlockType::COUNT; ++i) {
			ELlamaContextBlockType InternalBlockType = (ELlamaContextBlockType)i;
			if (const FTokenizedContextBlock* Block = FixedContextBlocks.Find(InternalBlockType)) {
				EContextVisBlockType VisBlockType = EContextVisBlockType::Unknown; // Default
				switch (InternalBlockType) {
					case ELlamaContextBlockType::SystemPrompt:      VisBlockType = EContextVisBlockType::SystemPrompt; break;
					case ELlamaContextBlockType::StaticWorldInfo:   VisBlockType = EContextVisBlockType::StaticWorldInfo; break;
					case ELlamaContextBlockType::LowFrequencyState: VisBlockType = EContextVisBlockType::LowFrequencyState; break;
					// Add cases for any other ELlamaContextBlockType members if they exist and need visualization
					default:
						UE_LOG(LogTemp, Warning, TEXT("BroadcastContextVisualUpdate: Unmapped ELlamaContextBlockType %d"), (int)InternalBlockType);
						break; 
				}

				if (VisBlockType != EContextVisBlockType::Unknown) { // Only call if we have a valid mapping
					AddBlockToPayload(VisBlockType, Block->Tokens); // Use the mapped VisBlockType
				}
			}
		}

		// 2. Conversation History (visualize each turn from StructuredConversationHistory)
		//    This shows the logical turns. The flat ConversationHistoryTokens (which includes template markers)
		//    is what's actually in MirroredKvCacheTokens for the conversation part.
		//    For visualization, showing distinct turns is clearer.
		int TurnCounter = 0;
		for (const FConversationTurn& Turn : StructuredConversationHistory) {
			EContextVisBlockType VisTurnType = EContextVisBlockType::Unknown;
			FString RoleDisplayName = Turn.Role; // Default to actual role

			if (Turn.Role.Equals(TEXT("user"), ESearchCase::IgnoreCase)) VisTurnType = EContextVisBlockType::ConversationTurnUser;
			else if (Turn.Role.Equals(TEXT("assistant"), ESearchCase::IgnoreCase)) VisTurnType = EContextVisBlockType::ConversationTurnAssistant;
			else if (Turn.Role.Equals(TEXT("tool"), ESearchCase::IgnoreCase)) {
				VisTurnType = EContextVisBlockType::ConversationTurnToolResponse; // Or a specific "ToolCall" type
				RoleDisplayName = TEXT("Tool Call (by Assistant)"); // Clarify for tooltip
			} else if (Turn.Role.Equals(TEXT("tool_response"), ESearchCase::IgnoreCase)) { // use this role for system's tool response
				VisTurnType = EContextVisBlockType::ConversationTurnToolResponse;
				RoleDisplayName = TEXT("Tool Response (from System)");
			}
			
			// We are visualizing the *content* tokens of each turn.
			// The chat template tokens (<|im_start|>, etc.) are part of MirroredKvCacheTokens
			// but we might not draw them as separate tiny blocks in the visualizer for clarity.
			// The AddBlockToPayload will use Turn.Tokens.size().
			AddBlockToPayload(VisTurnType, Turn.Tokens, FString::Printf(TEXT("Turn %d (%s): "), TurnCounter, *RoleDisplayName));
			TurnCounter++;
		}

		// 3. Current High Frequency State (visualize its content tokens)
		//    This represents the HFS that *would be* added to the next prompt.
		AddBlockToPayload(EContextVisBlockType::HighFrequencyState, CurrentHighFrequencyStateTokens, TEXT("Next HFS: "));

		// 4. (Optional) Focus Instruction - if you want to visualize it
		//    If the focus instruction is generated dynamically and tokenized just before prompt assembly,
		//    you might not have its tokens readily available here unless you pass it or re-tokenize.
		//    For now, skipping explicit visualization of focus instruction as a separate block,
		//    as it's transient and part of the prompt assembly for a specific turn.
		//    If it were a persistent block, you'd add it like other fixed blocks.

		// Set the total usage based on what we've decided to visualize
		// Payload.CurrentTotalTokenUsage = AccumulatedTokensForVisualizedPromptStructure; // This was removed from FContextVisPayload

		// 5. Free Space (remainder of the bar)
		if (CurrentNormalizedPosition < 1.0f && CurrentNormalizedPosition >= 0.0f) {
			float FreeHeight = 1.0f - CurrentNormalizedPosition;
			if (FreeHeight > 0.00001f) { // Only add if there's actual space
				 Payload.Blocks.Add(FContextVisBlock(
					EContextVisBlockType::FreeSpace, 
					CurrentNormalizedPosition, 
					FreeHeight, 
					FLinearColor(0.1f, 0.1f, 0.1f, 0.5f), // Dark semi-transparent gray
					FText::FromString(FString::Printf(TEXT("Free Space (%.2f%%)"), FreeHeight * 100.0f))
				));
			}
		}

		// Marshal to main thread
		FContextVisPayload PayloadCopy = Payload;
		qLlamaToMain.enqueue([this, PayloadCopy]() { if (contextChangedCb) contextChangedCb(PayloadCopy); });

//		// Marshal to main thread
//		if (OwningLlamaComponentPtr) {
//			// Create a copy of Payload specifically for the lambda to capture.
//			// This ensures the data persists until the lambda executes.
//			FContextVisPayload PayloadCopy = Payload;
//
////			UE_LOG(LogTemp, Warning, TEXT("LlamaComponent's marshal lambda queued."));
//
//			qLlamaToMain.enqueue([this, PayloadCopy]() { if (contextChangedCb) contextChangedCb(PayloadCopy); });
////			qLlamaToMain.enqueue([OwnerPtr = OwningLlamaComponentPtr, CapturedPayload = PayloadCopy]() {
////				// This lambda now runs on the Main Thread.
//////				UE_LOG(LogTemp, Warning, TEXT("LlamaComponent's marshal lambda starts on main thread."));
////				// OwnerPtr and CapturedPayload are copies held by the lambda.
////				if (OwnerPtr && OwnerPtr->IsValidLowLevel()) { // Good practice to check validity on main thread
////					OwnerPtr->ForwardContextUpdateToGameThread(CapturedPayload);
//////					UE_LOG(LogTemp, Warning, TEXT("ForwardContextUpdateToGameThread lambda executed. dec ms=%.f gen ms=%.f"), CapturedPayload.fFmsPerTokenDecode, CapturedPayload.fFmsPerTokenGenerate);
////				} else {
////					UE_LOG(LogTemp, Warning, TEXT("LlamaComponent's OwningLlamaComponentPtr was null or invalid when ForwardContextUpdateToGameThread lambda executed."));
////				}
////			});
//		} else {
//			UE_LOG(LogTemp, Error, TEXT("LlamaThread: OwningLlamaComponentPtr is null in BroadcastContextVisualUpdate. Cannot send update."));
//		}

//  UE_LOG(LogTemp, Warning, TEXT("LlamaThread: Broadcasting Context Update. TotalTokenCapacity: %d, KvCacheDecodedTokenCount: %d, NumVisualBlocks: %d, decodems: %g, genms: %g"), Payload.TotalTokenCapacity, Payload.KvCacheDecodedTokenCount, Payload.Blocks.Num(), Payload.fFmsPerTokenDecode, Payload.fFmsPerTokenGenerate);
/*
  for (int32 i = 0; i < Payload.Blocks.Num(); ++i) {
      const FContextVisBlock& Block = Payload.Blocks[i];
      FString BlockTypeName = UEnum::GetValueAsString(Block.BlockType);
//      UE_LOG(LogTemp, Log, TEXT("  Block %d: Type=%s, Start=%.2f, Height=%.4f, Color=(R=%.1f,G=%.1f,B=%.1f)"), i, *BlockTypeName, Block.NormalizedStart, Block.NormalizedHeight, Block.BlockColor.R, Block.BlockColor.G, Block.BlockColor.B);
  }

  if (OwningLlamaComponentPtr) { // Ensure OwningLlamaComponentPtr is valid
       FContextVisPayload PayloadCopy = Payload;
       qLlamaToMain.enqueue([this, PayloadCopy]() mutable {
          if (OwningLlamaComponentPtr && OwningLlamaComponentPtr->IsValidLowLevel()) {
//				UE_LOG(LogTemp, Warning, TEXT("LlamaComponent's second marshal lambda starts on main thread."));
              OwningLlamaComponentPtr->ForwardContextUpdateToGameThread(PayloadCopy);
          }
       });
  }
*/
  
  	}

	void Llama::ProcessInputAndGenerate_LlamaThread(
		const FString& InputTextFStr,             // The raw text from the user or tool response
		const FString& HighFrequencyContextTextFStr, // Raw HFS text
		const FString& InputTypeHintFStr          // "user", "tool" (for formatting the InputTextFStr)
	) {
		UE_LOG(LogTemp, Log, TEXT("LlamaThread: BEGIN ProcessInput: '%s', HFS: '%s', Hint: '%s'"), 
			*InputTextFStr, *HighFrequencyContextTextFStr, *InputTypeHintFStr);

		if (!ctx || !model) {
			UE_LOG(LogTemp, Error, TEXT("LlamaThread: ProcessInput called but Llama not ready."));
			FString ErrorMsg = TEXT("Llama model or context not initialized.");
			qLlamaToMain.enqueue([this, ErrorMsg]() { if (errorCb) errorCb(ErrorMsg); });
			return;
		}

		if (bIsGenerating.load(std::memory_order_acquire)) {
			UE_LOG(LogTemp, Warning, TEXT("LlamaThread: ProcessInput called while already generating. Input '%s' ignored."), *InputTextFStr);
			FString fs = "AIXO is currently generating.\nPlease try again shortly.\n\n";
			qLlamaToMain.enqueue([this, fs]() mutable { if (tokenCb) tokenCb(MoveTemp(fs)); });
			return;
		}

		// Set generating flag
		bIsGenerating = true;
		qLlamaToMain.enqueue([this]() { if (setIsGeneratingCb) setIsGeneratingCb(true); });

		// 1. Update High Frequency State if provided
		if (!HighFrequencyContextTextFStr.IsEmpty()) {
			std::string hfs_prefix_str = "<|im_start|>system\n[Submarine Status:]\n";
			std::string hfs_suffix_str = "\n<|im_end|>\n";
			std::string hfs_content = TCHAR_TO_UTF8(*HighFrequencyContextTextFStr);
			std::string full_hfs = hfs_prefix_str + hfs_content + hfs_suffix_str;
			CurrentHighFrequencyStateTokens = my_llama_tokenize(model, full_hfs, false, true);
		}

		// 2. Tokenize and add the input to conversation history
		std::string role_prefix = "<|im_start|>" + std::string(TCHAR_TO_UTF8(*InputTypeHintFStr)) + "\n";
		std::string role_suffix = "<|im_end|>\n";
		std::string input_content = TCHAR_TO_UTF8(*InputTextFStr);
		std::string full_input = role_prefix + input_content + role_suffix;
		std::vector<llama_token> input_tokens = my_llama_tokenize(model, full_input, false, true);
		AppendTurnToStructuredHistory(InputTypeHintFStr, input_tokens);

		// 3. Assemble the full prompt for this turn
		std::vector<llama_token> full_prompt_tokens;
		AssembleFullPromptForTurn(InputTextFStr, InputTypeHintFStr, full_prompt_tokens);

		// 4. Decode and generate
		DecodeTokensAndSample(full_prompt_tokens, true);

		// Clear generating flag
		bIsGenerating = false;
		qLlamaToMain.enqueue([this]() { if (setIsGeneratingCb) setIsGeneratingCb(false); });

		UE_LOG(LogTemp, Log, TEXT("LlamaThread: END ProcessInput: '%s'. kv_cache_token_cursor at %d"), 
			*InputTextFStr, kv_cache_token_cursor);
	}

    // --- Main Llama Thread Loop ---
    void Llama::ThreadRun_LlamaThread()
    {
        UE_LOG(LogTemp, Log, TEXT("LlamaThread: Starting thread run loop"));
        while (bIsRunning)
        {
            // Process any pending tasks from the main thread
            if (qMainToLlama.processQ()) {
                continue; // Keep processing if there are more items
            }

            // If no tasks, sleep briefly to avoid busy-waiting
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        UE_LOG(LogTemp, Log, TEXT("LlamaThread: Thread run loop ending"));
    }


    // ... (StopSeqHelper, AssembleFullContextForDump_LlamaThread, RequestFullContextDump_LlamaThread - adapt as needed) ...
    // Make sure AssembleFullContextForDump_LlamaThread uses the new block structure.
    // Your existing unsafeActivate/Deactivate logic will be largely replaced by InitializeLlama_LlamaThread/ShutdownLlama_LlamaThread.
    // Your existing unsafeInsertPrompt is replaced by ProcessInputAndGenerate_LlamaThread.

} // namespace Internal


// --- ULlamaComponent Method Implementations ---
ULlamaComponent::ULlamaComponent(const FObjectInitializer& ObjectInitializer)
    : Super(ObjectInitializer)
{
    UE_LOG(LogTemp, Log, TEXT("ULlamaComponent::Constructor - Component created in Blueprint"));
    PrimaryComponentTick.bCanEverTick = true;
    PrimaryComponentTick.TickInterval = 0.016f; // ~60 Hz
}

ULlamaComponent::~ULlamaComponent()
{
    if (LlamaInternal)
    {
        LlamaInternal->SignalStopRunning();
        delete LlamaInternal;
        LlamaInternal = nullptr;
    }
}

void ULlamaComponent::BeginPlay()
{
    Super::BeginPlay();

    // Only create the provider instance, but don't initialize it yet
    if (GetWorld()->IsGameWorld())
    {
        CreateProviderInstance();
    }
    else
    {
        UE_LOG(LogTemp, Log, TEXT("ULlamaComponent::BeginPlay - Skipping provider creation in editor"));
    }
}

void ULlamaComponent::CreateProviderInstance()
{
    if (Provider)
    {
        UE_LOG(LogTemp, Warning, TEXT("ULlamaComponent::CreateProviderInstance - Provider already exists!"));
        return;
    }

    // Create context manager if needed
    if (!ContextManager)
    {
        ContextManager = NewObject<ULLMContextManager>(this);
        UE_LOG(LogTemp, Log, TEXT("ULlamaComponent::CreateProviderInstance - Created ContextManager"));
    }

    // Create the appropriate provider based on configuration
    if (bUseLocalLlama)
    {
        Provider = NewObject<ULocalLlamaProvider>(this);
        if (ULocalLlamaProvider* LocalProvider = Cast<ULocalLlamaProvider>(Provider))
        {
            LocalProvider->SetContextManager(ContextManager);
            UE_LOG(LogTemp, Log, TEXT("ULlamaComponent::CreateProviderInstance - Created LocalLlamaProvider"));
        }
    }
    else
    {
        Provider = NewObject<URemoteLLMProvider>(this);
        if (URemoteLLMProvider* RemoteProvider = Cast<URemoteLLMProvider>(Provider))
        {
            RemoteProvider->SetContextManager(ContextManager);
            if (!APIKey.IsEmpty())
            {
                RemoteProvider->SetAPIKey(APIKey);
            }
            UE_LOG(LogTemp, Log, TEXT("ULlamaComponent::CreateProviderInstance - Created RemoteLLMProvider"));
        }
    }

    // Set up provider callbacks
    if (Provider)
    {
        Provider->GetOnTokenGenerated().AddDynamic(this, &ULlamaComponent::HandleTokenGenerated);
        Provider->GetOnContextChanged().AddDynamic(this, &ULlamaComponent::HandleContextChanged);
        
        // Bind to the provider's ready state
        if (ULocalLlamaProvider* LocalProvider = Cast<ULocalLlamaProvider>(Provider))
        {
            LocalProvider->OnReady.AddDynamic(this, &ULlamaComponent::HandleProviderReady);
        }

        // Load and set initial system prompt
        if (!SystemPromptFileName.IsEmpty())
        {
            FString LoadedSystemPrompt;
            FString SystemPromptFilePath = FPaths::ProjectContentDir() / SystemPromptFileName;
            
            if (FFileHelper::LoadFileToString(LoadedSystemPrompt, *SystemPromptFilePath))
            {
                UE_LOG(LogTemp, Log, TEXT("ULlamaComponent: Successfully loaded system prompt from: %s"), *SystemPromptFilePath);
                ContextManager->UpdateBlock(ELLMContextBlockType::SystemPrompt, LoadedSystemPrompt);
            }
            else
            {
                UE_LOG(LogTemp, Error, TEXT("ULlamaComponent: FAILED to load system prompt from: %s"), *SystemPromptFilePath);
            }
        }

        // Initialize provider with model path
        if (!PathToModel.IsEmpty())
        {
            Provider->Initialize(PathToModel);
        }
        else
        {
            UE_LOG(LogTemp, Error, TEXT("ULlamaComponent::CreateProviderInstance - PathToModel is empty!"));
        }
    }
    else
    {
        UE_LOG(LogTemp, Error, TEXT("ULlamaComponent::CreateProviderInstance - Failed to create provider!"));
    }
}

void ULlamaComponent::InitializeProvider()
{
    if (!Provider)
    {
        UE_LOG(LogTemp, Error, TEXT("ULlamaComponent::InitializeProvider - Provider not created! Call CreateProviderInstance first."));
        return;
    }

    if (bUseLocalLlama && PathToModel.IsEmpty())
    {
        UE_LOG(LogTemp, Warning, TEXT("ULlamaComponent::InitializeProvider - PathToModel is empty! Provider will not initialize."));
        return;
    }

    // Initialize the provider with the appropriate path
    const FString ModelPath = bUseLocalLlama ? PathToModel : RemoteEndpoint;
    Provider->Initialize(ModelPath);
}

FString ULlamaComponent::MakeSystemsBlock()
{
    if (!GameInterface)
    {
        return TEXT("// STUBBED OUT: No game interface available");
    }
    return GameInterface->GetSystemsContextBlock();
}

FString ULlamaComponent::MakeStatusBlock()
{
    if (!GameInterface)
    {
        return TEXT("// STUBBED OUT: No game interface available");
    }
    return GameInterface->GetLowFrequencyStateBlock();
}

#ifdef never
void ULlamaComponent::ActivateLlamaComponent(AVisualTestHarnessActor* InHarnessActor)		// called by VisualTestHarnessActor::BeginPlay because it is first
{
	GameInterface = InGameInterface;

    // Initialize the provider if not already done
    if (!Provider)
    {
        InitializeProvider();
    }

//    // Update the system prompt with the harness actor's command handlers
//    if (HarnessActor)
//    {
        FString SystemsBlock = MakeSystemsBlock();
        UpdateContextBlock(ELlamaContextBlockType::SystemPrompt, SystemsBlock);
//    }
//
//	HarnessActor = InHarnessActor;
//	if (HarnessActor)
//	{
//	}
//	else UE_LOG(LogTemp, Error, TEXT("ULlamaComponent: HarnessActor not found. Llama not initialized."));

	UE_LOG(LogTemp, Log, TEXT("ULlamaComponent::ActivateLlamaComponent."));

    if (LlamaInternal && !PathToModel.IsEmpty() && !SystemPromptFileName.IsEmpty()) {
        FString LoadedSystemPrompt;
        // FString SystemPromptFilePath = FPaths::ProjectContentDir() / TEXT("AIXO_Prompts/SystemPrompt_AIXO.txt");
        // Or, if you want to make the filename a UPROPERTY:
        FString SystemPromptFilePath = FPaths::ProjectContentDir() / SystemPromptFileName; // Where SystemPromptFileName is an FString UPROPERTY

        if (FFileHelper::LoadFileToString(LoadedSystemPrompt, *SystemPromptFilePath))
        {
            UE_LOG(LogTemp, Log, TEXT("ULlamaComponent: Successfully loaded system prompt from: %s"), *SystemPromptFilePath);
        }
        else
        {
            UE_LOG(LogTemp, Error, TEXT("ULlamaComponent: FAILED to load system prompt from: %s. Using default or empty."), *SystemPromptFilePath);
            // LoadedSystemPrompt will be empty or you can set a fallback default here:
            // LoadedSystemPrompt = TEXT("<|im_start|>system\nDefault fallback prompt if file fails to load.<|im_end|>\n");
        }

        // Marshal the initialization call to the Llama thread
        FString ModelPathCopy = PathToModel;
        FString SystemPromptCopy = LoadedSystemPrompt;
		FString SystemsContextBlock;
		FString LowFreqContextBlock;
        // TODO: this is where to add to the static system prompt, need access to the VisualTestHarnessActor and SubmarineState
        //        but also can change any block by calling UpdateContextBlock() from the main thread
// & per-junction name, power source flag, GetSystemInfo(), GetAvailableCommands/Queries
// & & connectivity == segment name xn
// & per-segment name
// & & connectivity == junction name and pin # x2

        if (!HarnessActor) UE_LOG(LogTemp, Error, TEXT("ULlamaComponent: HarnessActor is NULL!!!!!!!!!!"));
        if (HarnessActor) {
			SystemsContextBlock = MakeSystemsBlock();
			LowFreqContextBlock = MakeStatusBlock();
//UE_LOG(LogTemp, Error, TEXT("ULlamaComponent: LowFreqContextBlock: %s"), *str);

//        	ASubmarineState *ss = HarnessActor->SubmarineState;
		}
		
//SystemPromptCopy = "System Prompt\n";//You are AIXO, operating a submarine with Captain. This is the System Prompt. It's not very long.\n";
//SystemsContextBlock = "Static World Info\n";//"Systems string\nalso not long\n";
//LowFreqContextBlock = "Low Frequency\n";//"LowFreq string\nalso not very long\n";
LowFreqContextBlock = "";		// TESTING because this changes quickly, as soon as the propagator runs

		SystemsContextBlockRecent = SystemsContextBlock;
		LowFreqContextBlockRecent = LowFreqContextBlock;

        LlamaInternal->qMainToLlama.enqueue([this, ModelPathCopy, SystemPromptCopy, SystemsContextBlock, LowFreqContextBlock]() {
	UE_LOG(LogTemp, Log, TEXT("ULlamaComponent::ActivateLlamaComponent calling InitializeLlama_LlamaThread."));
            LlamaInternal->InitializeLlama_LlamaThread(ModelPathCopy, SystemPromptCopy, SystemsContextBlock, LowFreqContextBlock /*, other params */);
        });
    } else {
        UE_LOG(LogTemp, Error, TEXT("ULlamaComponent: PathToModel or SystemPromptFileName is empty. Llama not initialized."));
    }
    
//    UE_LOG(LogTemp, Log, TEXT("ULlamaComponent: [0]\n%s\n"), *MakeCommandHandlerString(HarnessActor->CmdDistributor.CommandHandlers[0]));
//    UE_LOG(LogTemp, Log, TEXT("ULlamaComponent: [1]\n%s\n"), *MakeCommandHandlerString(HarnessActor->CmdDistributor.CommandHandlers[1]));

}
#endif // never

void ULlamaComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
//    if (LlamaInternal) {
//        LlamaInternal->SignalStopRunning(); // Signal thread to stop
//        // The LlamaInternal destructor will join the thread.
//        // If you need to explicitly queue a shutdown command:
//        // LlamaInternal->qMainToLlama.enqueue([this]() {
//        //     LlamaInternal->ShutdownLlama_LlamaThread();
//        // });
//    }
    if (Provider)
    {
        Provider->Shutdown();
    }
    
    Super::EndPlay(EndPlayReason);
}

void ULlamaComponent::SetGameInterface(TScriptInterface<ILLMGameInterface> InGameInterface)
{
    GameInterface = InGameInterface;
    
    // Create provider if not already done
    if (!Provider)
    {
        CreateProviderInstance();
    }
    
    // Only update system prompt if we're in a game world
    if (GameInterface && GetWorld()->IsGameWorld())
    {
        FString SystemsBlock = MakeSystemsBlock();
        UpdateContextBlock(ELlamaContextBlockType::SystemPrompt, SystemsBlock);
    }
}

void ULlamaComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
    Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

    if (LlamaInternal) {
        // Process just one callback per tick to avoid blocking the main thread
        ProcessResponseQueue();
    }

    if (bIsLlamaCoreReady) {
        FString CurrentSystemsBlock = MakeSystemsBlock();
        if (SystemsContextBlockRecent != CurrentSystemsBlock) {
            bPendingStaticWorldInfoUpdate = true;
            PendingStaticWorldInfoText = CurrentSystemsBlock;
        }

        FString CurrentLowFreqBlock = MakeStatusBlock();
        if (LowFreqContextBlockRecent != CurrentLowFreqBlock) {
            bPendingLowFrequencyStateUpdate = true;
            PendingLowFrequencyStateText = CurrentLowFreqBlock;
        }

        // Attempt to send pending updates if Llama is not busy
        if (!bIsLlamaGenerating.load(std::memory_order_acquire)) { // Check the flag
            if (bPendingStaticWorldInfoUpdate) {
                UE_LOG(LogTemp, Log, TEXT("ULlamaComponent: StaticWorldInfo changed. Queuing update NOW."));
                UpdateContextBlock(ELlamaContextBlockType::StaticWorldInfo, PendingStaticWorldInfoText);
                SystemsContextBlockRecent = PendingStaticWorldInfoText; // Mark as sent
                bPendingStaticWorldInfoUpdate = false;
            }
            if (bPendingLowFrequencyStateUpdate) {
                UE_LOG(LogTemp, Log, TEXT("ULlamaComponent: LowFrequencyState changed. Queuing update NOW."));
                UpdateContextBlock(ELlamaContextBlockType::LowFrequencyState, PendingLowFrequencyStateText);
                LowFreqContextBlockRecent = PendingLowFrequencyStateText; // Mark as sent
                bPendingLowFrequencyStateUpdate = false;
            }
        } else {
            if (bPendingStaticWorldInfoUpdate || bPendingLowFrequencyStateUpdate) {
                UE_LOG(LogTemp, Log, TEXT("ULlamaComponent: Llama is busy, context updates deferred."));
            }
        }
    }
}

void ULlamaComponent::ProcessInput(const FString& InputText, const FString& HighFrequencyContextText, const FString& InputTypeHint)
{
    if (Provider)
    {
        Provider->ProcessInput(InputText, HighFrequencyContextText);
    }
}

void ULlamaComponent::HandleTokenGenerated_Implementation(const FString& Token)
{
    UE_LOG(LogTemp, Log, TEXT("ULlamaComponent::HandleTokenGenerated - Received token: %s"), *Token);
    // Forward to both the delegate and the interface
    OnTokenGenerated.Broadcast(Token);
    if (VisualizationInterface)
    {
        VisualizationInterface->HandleTokenGenerated(Token);
    }
}

// This function is called by the lambda queued from Internal::Llama::toolCallCb
void ULlamaComponent::HandleToolCall_GetSystemInfo(const FString& QueryString)
{
    if (!GameInterface)
    {
        SendToolResponseToLlama(TEXT("get_system_info"), TEXT("{\"error\": \"No game interface available\"}"));
        return;
    }
    FString Response = GameInterface->HandleGetSystemInfo(QueryString);
    SendToolResponseToLlama(TEXT("get_system_info"), Response);
}

void ULlamaComponent::HandleToolCall_QuerySubmarineSystem(const FString& QueryString)
{
    if (!GameInterface)
    {
        SendToolResponseToLlama(TEXT("query_submarine_system"), TEXT("{\"error\": \"No game interface available\"}"));
        return;
    }
    FString Response = GameInterface->HandleQuerySubmarineSystem(QueryString);
    SendToolResponseToLlama(TEXT("query_submarine_system"), Response);
}

void ULlamaComponent::HandleToolCall_CommandSubmarineSystem(const FString& QueryString)
{
    if (!GameInterface)
    {
        SendToolResponseToLlama(TEXT("command_submarine_system"), TEXT("{\"error\": \"No game interface available\"}"));
        return;
    }
    FString Response = GameInterface->HandleCommandSubmarineSystem(QueryString);
    SendToolResponseToLlama(TEXT("command_submarine_system"), Response);
}

void ULlamaComponent::SendToolResponseToLlama(const FString& ToolName, const FString& JsonResponseContent)
{
    // Format according to how your LLM expects tool responses (e.g., Qwen's <|im_start|>tool...)
    // This is the CONTENT that goes inside the tool role tags.
    // The ProcessInputAndGenerate_LlamaThread will wrap it with <|im_start|>tool ... <|im_end|>
    UE_LOG(LogTemp, Log, TEXT("Sending Tool Response for '%s': %s"), *ToolName, *JsonResponseContent);
    ProcessInput(JsonResponseContent, TEXT(""), TEXT("tool"));
}

UFUNCTION(Category = "Llama|Context")
void ULlamaComponent::HandleContextChanged(const FContextVisPayload& Payload)
{
    UE_LOG(LogTemp, Log, TEXT("ULlamaComponent::HandleContextChanged - Received context update. Blocks: %d, KvCacheCount: %d"), 
        Payload.Blocks.Num(), Payload.KvCacheDecodedTokenCount);
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
        // Convert from LlamaComponent's block type to ILLMProvider's block type
        ELLMContextBlockType ProviderBlockType;
        switch (BlockType)
        {
            case ELlamaContextBlockType::SystemPrompt:
                ProviderBlockType = ELLMContextBlockType::SystemPrompt;
                break;
            case ELlamaContextBlockType::StaticWorldInfo:
                ProviderBlockType = ELLMContextBlockType::StaticWorldInfo;
                break;
            case ELlamaContextBlockType::LowFrequencyState:
                ProviderBlockType = ELLMContextBlockType::LowFrequencyState;
                break;
            default:
                return; // Other block types are managed internally
        }
        
        Provider->UpdateContextBlock(ProviderBlockType, NewTextContent);
    }
}

void ULlamaComponent::ForwardContextUpdateToGameThread(const FContextVisPayload& LlamaThreadPayload)
{
//	UE_LOG(LogTemp, Warning, TEXT("ForwardContextUpdateToGameThread was called."));
    // This function is now executing on the Game Thread.
    // It's safe to broadcast a delegate that UMG widgets are bound to.
    if (OnLlamaContextChangedDelegate.IsBound())
    {
        FContextVisPayload FinalPayload = LlamaThreadPayload; // Copy

//    UE_LOG(LogTemp, Log, TEXT("ULlamaComponent: Forwarding to UI. PendingSWI: %d (UpToDate: %d), PendingLFS: %d (UpToDate: %d), CoreReady: %d, IsGenerating: %d (Idle: %d)"),
//        bPendingStaticWorldInfoUpdate, FinalPayload.bIsStaticWorldInfoUpToDate,
//        bPendingLowFrequencyStateUpdate, FinalPayload.bIsLowFrequencyStateUpToDate,
//        FinalPayload.bIsLlamaCoreActuallyReady,
//        this->bIsLlamaGenerating.load(std::memory_order_acquire), FinalPayload.bIsLlamaCurrentlyIdle
//    );

        OnLlamaContextChangedDelegate.Broadcast(FinalPayload);
//		UE_LOG(LogTemp, Warning, TEXT("ForwardContextUpdateToGameThread OnLlamaContextChangedDelegate."));
    }
    else
    {
        // Optional: Log if no one is listening, though often widgets might bind/unbind dynamically.
        // UE_LOG(LogTemp, Verbose, TEXT("ULlamaComponent: OnLlamaContextChangedDelegate broadcast, but no listeners."));
    }
}

void ULlamaComponent::SetVisualizationInterface(TScriptInterface<ILLMVisualizationInterface> InVisualizationInterface)
{
    VisualizationInterface = InVisualizationInterface;
}

// Add these implementations after the existing ULlamaComponent methods

void ULlamaComponent::Initialize(const FString& ModelPath)
{
    if (!Provider)
    {
        UE_LOG(LogTemp, Error, TEXT("ULlamaComponent::Initialize - Provider is null!"));
        return;
    }

    UE_LOG(LogTemp, Log, TEXT("ULlamaComponent::Initialize - Initializing with path: %s"), *ModelPath);
    Provider->Initialize(ModelPath);
}

bool ULlamaComponent::IsLlamaBusy() const
{
    return bIsLlamaGenerating.load(std::memory_order_acquire);
}

bool ULlamaComponent::IsLlamaReady() const
{
    return bIsLlamaCoreReady;
}

void ULlamaComponent::HandleProviderReady(const FString& ReadyMessage)
{
    UE_LOG(LogTemp, Log, TEXT("LlamaComponent: Provider is ready: %s"), *ReadyMessage);
    bIsLlamaCoreReady = true;
    OnLlamaReady.Broadcast(ReadyMessage);
}


