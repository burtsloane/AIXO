// 2023 (c) Mika Pi
// ReSharper disable CppPrintfBadFormat
#include "UELlama/LlamaComponent.h"
#include <time.h>
#include "common.h"

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
    Llama::Llama() : ThreadHandle([this]() { ThreadRun_LlamaThread(); }) {}

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

    // --- Llama Thread Initialization ---
    void Llama::InitializeLlama_LlamaThread(const FString& ModelPathFStr, const FString& InitialSystemPromptFStr /*, other params */)
    {
        UE_LOG(LogTemp, Log, TEXT("LlamaThread: Initializing Llama... Model: %s"), *ModelPathFStr);
        if (model) { // Already initialized
            UE_LOG(LogTemp, Warning, TEXT("LlamaThread: Already initialized."));
            return;
        }

        llama_model_params model_params = llama_model_default_params();
        // model_params.n_gpu_layers = 50; // Configure as needed

        model = llama_model_load_from_file(TCHAR_TO_UTF8(*ModelPathFStr), model_params);
        if (!model) {
            FString ErrorMsg = FString::Printf(TEXT("LlamaThread: Unable to load model from %s"), *ModelPathFStr);
            UE_LOG(LogTemp, Error, TEXT("%s"), *ErrorMsg);
            qLlamaToMain.enqueue([this, ErrorMsg]() { if (errorCb) errorCb(ErrorMsg); });
            return;
        }
        vocab = llama_model_get_vocab(model);
        n_ctx_from_model = llama_model_n_ctx_train(model); // Or llama_n_ctx if using that for actual context size

        llama_context_params ctx_params = llama_context_default_params();
        ctx_params.n_ctx = n_ctx_from_model; // Use model's training context or a configured value
        ctx_params.n_batch = 2048; // Or llama_n_batch(ctx) from model if available, or a sensible default. Max tokens per llama_decode call.
        ctx_params.n_threads = n_threads; // From your global namespace
        ctx_params.n_threads_batch = n_threads; // For batch processing
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
        kv_cache_token_cursor = 0;

        // Tokenize and store initial System Prompt
        TokenizeAndStoreFixedBlock(ELlamaContextBlockType::SystemPrompt, InitialSystemPromptFStr, true);

        // TODO: Initialize other fixed blocks if they have initial content
        // TokenizeAndStoreFixedBlock(ELlamaContextBlockType::StaticWorldInfo, GetInitialStaticWorldInfo(), false);

        UE_LOG(LogTemp, Log, TEXT("LlamaThread: Initialization complete. Context size: %d"), n_ctx_from_model);
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

    void Llama::TokenizeAndStoreFixedBlock(ELlamaContextBlockType BlockType, const FString& Text, bool bIsSystemPrompt) {
        if (!model) return;
        std::string StdText = TCHAR_TO_UTF8(*Text);
        // System prompt might need specific BOS handling based on model
        bool bAddBos = (BlockType == ELlamaContextBlockType::SystemPrompt && bIsSystemPrompt);

        FTokenizedContextBlock Block;
        std::vector<llama_token> TokensTArray; // Use TArray for easier integration with UE types if needed later
        std::vector<llama_token> StdTokens = my_llama_tokenize(model, StdText, bAddBos, true /* allow special for system prompt */);
        TokensTArray.insert(TokensTArray.end(), StdTokens.begin(), StdTokens.end());
        Block.Tokens = TokensTArray;

        FixedContextBlocks.FindOrAdd(BlockType) = Block;

        // TODO: serious optimization possible here
        // This invalidates KV cache from this block onwards.
        // For simplicity during init, we'll just let the first ProcessInputAndGenerate build the full cache.
        // More advanced: track kv_cache_token_cursor precisely.
        InvalidateKVCacheFromPosition(0); // Simplest: force full rebuild on next generation
        UE_LOG(LogTemp, Log, TEXT("LlamaThread: Tokenized and stored block %d, %d tokens."), (int)BlockType, Block.Tokens.size());
    }


    void Llama::UpdateContextBlock_LlamaThread(ELlamaContextBlockType BlockType, const FString& NewTextContent)
    {
        if (!model || BlockType == ELlamaContextBlockType::COUNT /* Invalid */) {
             UE_LOG(LogTemp, Error, TEXT("LlamaThread: Cannot update context block, model not loaded or invalid block type."));
            return;
        }
        UE_LOG(LogTemp, Log, TEXT("LlamaThread: Updating context block %d."), (int)BlockType);

        // For now, only allow updating fixed blocks. Conversation/HFS are handled differently.
        if (FixedContextBlocks.Contains(BlockType)) {
            TokenizeAndStoreFixedBlock(BlockType, NewTextContent, (BlockType == ELlamaContextBlockType::SystemPrompt));

            // Determine the start position of this block in the logical sequence to invalidate KV cache
            int32_t InvalidationStartPos = 0;
            for (uint8 i = 0; i < (uint8)BlockType; ++i) {
                if (const FTokenizedContextBlock* PrevBlock = FixedContextBlocks.Find((ELlamaContextBlockType)i)) {
                    InvalidationStartPos += PrevBlock->Tokens.size();
                }
            }
            InvalidateKVCacheFromPosition(InvalidationStartPos);
            UE_LOG(LogTemp, Log, TEXT("LlamaThread: Block %d updated. KV cache invalidated from token pos %d."), (int)BlockType, InvalidationStartPos);
        } else {
            UE_LOG(LogTemp, Warning, TEXT("LlamaThread: Attempted to update non-fixed block type %d via UpdateContextBlock."), (int)BlockType);
        }
    }

    // Invalidates KV cache from a certain token position onwards in the logical sequence
    void Llama::InvalidateKVCacheFromPosition(int32_t ValidTokenCountBeforeInvalidation) {
        if (!ctx) return;
        if (ValidTokenCountBeforeInvalidation < kv_cache_token_cursor) {
            UE_LOG(LogTemp, Log, TEXT("LlamaThread: KV Cache: Removing tokens from pos %d to %d."), ValidTokenCountBeforeInvalidation, kv_cache_token_cursor);
            llama_kv_self_seq_rm(ctx, 0, ValidTokenCountBeforeInvalidation, kv_cache_token_cursor); // seq_id 0, from pos, to pos (-1 means end)
            kv_cache_token_cursor = ValidTokenCountBeforeInvalidation;
        }
         // If ValidTokenCountBeforeInvalidation >= kv_cache_token_cursor, cache is already valid up to that point or beyond.
    }


    void Llama::AssembleFullPromptForTurn(const std::vector<llama_token>& NewInputTokens, std::vector<llama_token>& OutFullPromptTokens)
    {
        OutFullPromptTokens.clear();
        int32_t CurrentTokenPosition = 0;

        // 1. Fixed Prefix Blocks (System, StaticWorld, LowFreq)
        for (uint8 i = 0; i < (uint8)ELlamaContextBlockType::COUNT; ++i) {
            if (const FTokenizedContextBlock* Block = FixedContextBlocks.Find((ELlamaContextBlockType)i)) {
		        OutFullPromptTokens.insert(OutFullPromptTokens.end(), Block->Tokens.begin(), Block->Tokens.end());
                CurrentTokenPosition += Block->Tokens.size();
            }
        }
        int32 FixedBlockEndPos = CurrentTokenPosition;

        // 2. Conversation History
        OutFullPromptTokens.insert(OutFullPromptTokens.end(), ConversationHistoryTokens.begin(), ConversationHistoryTokens.end());
        CurrentTokenPosition += ConversationHistoryTokens.size();
        int32 ConversationEndPos = CurrentTokenPosition;

        // 3. Current High Frequency State
        std::string hfs_prefix_str = "\n<|im_start|>high_frequency_state\n"; // Example for Qwen, model-specific
        std::vector<llama_token> hfs_prefix_tokens = my_llama_tokenize(model, hfs_prefix_str, false, true);
        OutFullPromptTokens.insert(OutFullPromptTokens.end(), hfs_prefix_tokens.begin(), hfs_prefix_tokens.end());
        CurrentTokenPosition += hfs_prefix_tokens.size();
        //
        OutFullPromptTokens.insert(OutFullPromptTokens.end(), CurrentHighFrequencyStateTokens.begin(), CurrentHighFrequencyStateTokens.end());
        CurrentTokenPosition += CurrentHighFrequencyStateTokens.size();

        // 4. New Input for this turn
        std::string newinput_prefix_str = "\n<|im_start|>new_input\n"; // Example for Qwen, model-specific
        std::vector<llama_token> newinput_prefix_tokens = my_llama_tokenize(model, newinput_prefix_str, false, true);
        OutFullPromptTokens.insert(OutFullPromptTokens.end(), newinput_prefix_tokens.begin(), newinput_prefix_tokens.end());
        CurrentTokenPosition += newinput_prefix_tokens.size();
        //
        OutFullPromptTokens.insert(OutFullPromptTokens.end(), NewInputTokens.begin(), NewInputTokens.end());
        CurrentTokenPosition += NewInputTokens.size();

        // 5. AI Assistant Prefix (e.g., "<|im_start|>assistant\n" for Qwen)
        // This should be part of your prompt templating, tokenize and append it.
        // For Qwen, it's important.
        std::string assistant_prefix_str = "\n<|im_start|>assistant\n"; // Example for Qwen, model-specific
        std::vector<llama_token> assistant_prefix_tokens = my_llama_tokenize(model, assistant_prefix_str, false, true);
        OutFullPromptTokens.insert(OutFullPromptTokens.end(), assistant_prefix_tokens.begin(), assistant_prefix_tokens.end());

        // --- KV Cache Management ---
        // Compare OutFullPromptTokens with what's in kv_cache_token_cursor
        // For simplicity here, we'll assume kv_cache_token_cursor tracks the valid prefix.
        // The DecodeTokensAndSample will handle feeding only the new part.

        // If Fixed Blocks changed, kv_cache_token_cursor would have been reset by InvalidateKVCacheFromPosition.
        // If ConversationHistory was pruned, it also would have called InvalidateKVCacheFromPosition.
    }

    void Llama::DecodeTokensAndSample(std::vector<llama_token>& FullPromptTokensForThisTurn, bool bIsFinalPromptTokenLogits)
    {
        if (!ctx || !model || (FullPromptTokensForThisTurn.size() == 0)) return;

        bIsGenerating = true;
        eos_reached = false;
        CurrentTurnAIReplyTokens.clear();

        // Determine how many tokens from FullPromptTokensForThisTurn are already in KV cache
        int32_t n_tokens_to_eval_from_prompt = 0;
        int32_t prompt_eval_start_index = 0;

        if (kv_cache_token_cursor < FullPromptTokensForThisTurn.size()) {
            // Check for matching prefix; kv_cache_token_cursor should be the length of the matching prefix
            // This simple check assumes the prefix *is* matching if kv_cache_token_cursor > 0.
            // A more robust check would compare tokens.
            prompt_eval_start_index = kv_cache_token_cursor;
            n_tokens_to_eval_from_prompt = FullPromptTokensForThisTurn.size() - kv_cache_token_cursor;
        } else if (kv_cache_token_cursor > FullPromptTokensForThisTurn.size()) {
            // Current prompt is shorter than what's in cache - this means a significant rollback happened.
            // InvalidateKVCacheFromPosition should have handled this.
            // For safety, re-evaluate all.
            UE_LOG(LogTemp, Warning, TEXT("LlamaThread: Prompt shorter than KV cache cursor. Forcing full re-eval."));
            InvalidateKVCacheFromPosition(0);
            prompt_eval_start_index = 0;
            n_tokens_to_eval_from_prompt = FullPromptTokensForThisTurn.size();
        }
        // If kv_cache_token_cursor == FullPromptTokensForThisTurn.size(), all prompt tokens are in cache.

        UE_LOG(LogTemp, Log, TEXT("LlamaThread: Decoding prompt. Total prompt tokens: %d. KV cursor: %d. Tokens to eval from prompt: %d at index %d."),
            FullPromptTokensForThisTurn.size(), kv_cache_token_cursor, n_tokens_to_eval_from_prompt, prompt_eval_start_index);

        // Decode the new part of the prompt
        if (n_tokens_to_eval_from_prompt > 0) {
            for (int32_t i = 0; i < n_tokens_to_eval_from_prompt; i += this->batch_capacity) {
                common_batch_clear(batch);
                int32_t n_batch_tokens = FMath::Min((int32_t)this->batch_capacity, n_tokens_to_eval_from_prompt - i);
                for (int32_t j = 0; j < n_batch_tokens; ++j) {
                    // Add token to batch: token_id, position in sequence, seq_id_list, logits_flag
                    bool bLogits = bIsFinalPromptTokenLogits && (prompt_eval_start_index + i + j == FullPromptTokensForThisTurn.size() - 1);
                    common_batch_add(batch, FullPromptTokensForThisTurn[prompt_eval_start_index + i + j], kv_cache_token_cursor + j, {0}, bLogits);
                }
                if (batch.n_tokens == 0) break;

                if (llama_decode(ctx, batch) != 0) {
                    FString ErrorMsg = TEXT("LlamaThread: llama_decode failed during prompt processing.");
                    UE_LOG(LogTemp, Error, TEXT("%s"), *ErrorMsg);
                    qLlamaToMain.enqueue([this, ErrorMsg]() { if (errorCb) errorCb(ErrorMsg); });
                    eos_reached = true; bIsGenerating = false; return;
                }
                kv_cache_token_cursor += batch.n_tokens;
            }
        }
		UE_LOG(LogTemp, Log, TEXT("LlamaThread: Prompt decoded. KV cursor now: %d."), kv_cache_token_cursor);


        // Start Generation Loop
        while (kv_cache_token_cursor < n_ctx_from_model && !eos_reached) { // Check against actual context window
            if (!bIsRunning) { eos_reached = true; break;} // Check if shutdown requested

            float* logits = llama_get_logits_ith(ctx, batch.n_tokens - 1); // Get logits from the last token processed
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
                // TODO: Proper Tool Call Parsing Here based on your chosen format (e.g. ```tool_call...``` or JSON)
                // For now, simple placeholder:
				if (CurrentReplyStr.find("<tool_call>") != std::string::npos) {
                    qLlamaToMain.enqueue([this, CurrentReplyStr]() {
                    	FString str = UTF8_TO_TCHAR(CleanString(CurrentReplyStr).c_str());
                    	if (toolCallCb) toolCallCb(str);
					});
                    // Don't send EOS yet, wait for tool response to continue generation
                    // Or, if tool call is the *end* of the turn, then set eos_reached.
                    // This depends on your AIXO's turn structure. Assuming tool call ends AI's immediate output:
                    UE_LOG(LogTemp, Log, TEXT("LlamaThread: Tool call detected."));
                } else {
					UE_LOG(LogTemp, Log, TEXT("LlamaThread: EOS or Stop Sequence reached."));
                }
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
            if (llama_decode(ctx, batch) != 0) {
                FString ErrorMsg = TEXT("LlamaThread: llama_decode failed during generation.");
                UE_LOG(LogTemp, Error, TEXT("%s"), *ErrorMsg);
                qLlamaToMain.enqueue([this, ErrorMsg]() { if (errorCb) errorCb(ErrorMsg); });
                eos_reached = true; break;
            }
            kv_cache_token_cursor++;
        } // End of generation loop

		// generation loop ends
		bIsGenerating = false;
		if (eos_reached) { // eos_reached is true if llama_token_eos or your stop sequence (like ~~~END_AIXO_TURN~~~) was hit
			FString RawAIOutputThisTurnStr; // The text generated between <|im_start|>assistant and ~~~END_AIXO_TURN~~~
			for(llama_token tk : CurrentTurnAIReplyTokens) { // CurrentTurnAIReplyTokens should ONLY contain tokens from THIS assistant generation
				const char* piece = llama_vocab_get_text(vocab, tk);
				if(piece) RawAIOutputThisTurnStr += UTF8_TO_TCHAR(piece);
			}
			{
				std::string sss = TCHAR_TO_UTF8(*RawAIOutputThisTurnStr);
				sss = CleanString(sss);
				FString fs = UTF8_TO_TCHAR(sss.c_str());
				RawAIOutputThisTurnStr = fs;
			}

			// Trim at ~~~END_AIXO_TURN~~~ if it's present, as that's our logical end.
			// The model might try to generate <|im_end|> after it, which we don't want in the stored content.
			int32 EndTurnMarkerPos = RawAIOutputThisTurnStr.Find(TEXT("~~~END_AIXO_TURN~~~"));
			FString ProcessedAIOutputStr = (EndTurnMarkerPos != INDEX_NONE) ? RawAIOutputThisTurnStr.Left(EndTurnMarkerPos) : RawAIOutputThisTurnStr;

			// 1. Parse ProcessedAIOutputStr to find <think>, <tool_call>, CAP:
			FString ThinkContent;
			FString ToolCallContentForHistory;
			FString CaptainDialogueContentForHistory;
			bool bToolCallMadeThisTurn = false;

			// Simple parsing (you'll need more robust regex or string manipulation)
			int32 ThinkStart = ProcessedAIOutputStr.Find(TEXT("<think>"));
			int32 ThinkEnd = ProcessedAIOutputStr.Find(TEXT("</think>"));
			if (ThinkStart != INDEX_NONE && ThinkEnd != INDEX_NONE && ThinkEnd > ThinkStart) {
				ThinkContent = ProcessedAIOutputStr.Mid(ThinkStart + FString(TEXT("<think>")).Len(), ThinkEnd - (ThinkStart + FString(TEXT("<think>")).Len()));
				// TODO: Log ThinkContent if desired
				{
					UE_LOG(LogTemp, Log, TEXT("LlamaThread: DecodeTokensAndSample KV cursor: %d. think content '%s'."), kv_cache_token_cursor, *ThinkContent);
				}
			}

			int32 ToolCallStart = ProcessedAIOutputStr.Find(TEXT("<tool_call>"));
			int32 ToolCallEnd = ProcessedAIOutputStr.Find(TEXT("</tool_call>"));
			if (ToolCallStart != INDEX_NONE && ToolCallEnd != INDEX_NONE && ToolCallEnd > ToolCallStart) {
				ToolCallContentForHistory = ProcessedAIOutputStr.Mid(ToolCallStart, ToolCallEnd + FString(TEXT("</tool_call>")).Len() - ToolCallStart); // Include tags
				bToolCallMadeThisTurn = true;
				std::string ss = TCHAR_TO_UTF8(*ToolCallContentForHistory);
				ss = CleanString(ss);
				FString fs = UTF8_TO_TCHAR(ss.c_str());
				qLlamaToMain.enqueue([this, fs]() { if (toolCallCb) toolCallCb(fs); });
				ToolCallContentForHistory = fs;
			}

			int32 CapStart = ProcessedAIOutputStr.Find(TEXT("CAP:"));
			if (CapStart != INDEX_NONE) {
				// Find end of CAP dialogue (e.g., before ~~~END_AIXO_TURN~~~ or next tag)
				int32 EndOfCap = ProcessedAIOutputStr.Find(TEXT("~~~END_AIXO_TURN~~~"), ESearchCase::CaseSensitive, ESearchDir::FromStart, CapStart);
				if (EndOfCap == INDEX_NONE && ToolCallStart > CapStart) EndOfCap = ToolCallStart; // If tool call comes after CAP
				if (EndOfCap == INDEX_NONE) EndOfCap = ProcessedAIOutputStr.Len();

				CaptainDialogueContentForHistory = ProcessedAIOutputStr.Mid(CapStart + FString(TEXT("CAP:")).Len(), EndOfCap - (CapStart + FString(TEXT("CAP:")).Len())).TrimStartAndEnd();
				// Send CaptainDialogueContentForHistory to main thread for TTS/display
				qLlamaToMain.enqueue([this, CaptainDialogueContentForHistory]() { 
					// Assuming OnGenerationCompleteDelegate is for final dialogue
					// if (OnGenerationCompleteDelegate) OnGenerationCompleteDelegate.ExecuteIfBound(CaptainDialogueContent); 
				});
				// TODO: Or a new delegate for just dialogue
				{
					UE_LOG(LogTemp, Log, TEXT("LlamaThread: DecodeTokensAndSample KV cursor: %d. dialogue '%s'."), kv_cache_token_cursor, *CaptainDialogueContentForHistory);
				}
			}

			std::vector<llama_token> AssistantMessageTokensForStorageInHistory;
			if (!ToolCallContentForHistory.IsEmpty()) {
				bToolCallMadeThisTurn = true;
				// Tokenize ToolCallContentForHistory (which includes the tags)
				std::vector<llama_token> tc_std = my_llama_tokenize(model, TCHAR_TO_UTF8(*ToolCallContentForHistory), false, true);
				AssistantMessageTokensForStorageInHistory.insert(AssistantMessageTokensForStorageInHistory.end(), tc_std.begin(), tc_std.end());
			} else if (!CaptainDialogueContentForHistory.IsEmpty()) {
				// Tokenize just the dialogue content
				std::vector<llama_token> cd_std = my_llama_tokenize(model, TCHAR_TO_UTF8(*CaptainDialogueContentForHistory), false, false);
				AssistantMessageTokensForStorageInHistory.insert(AssistantMessageTokensForStorageInHistory.end(), cd_std.begin(), cd_std.end());
			}
			// If both are empty, but AI generated something (e.g. just <think> then EOS), AssistantMessageTokensForStorageInHistory might be empty.

			if ((AssistantMessageTokensForStorageInHistory.size() != 0) || bToolCallMadeThisTurn) {
				{
					std::string s;
					for (llama_token tk : AssistantMessageTokensForStorageInHistory) {
						const char* piece = llama_vocab_get_text(vocab, tk);
						if (piece) s += piece;
					}
					UE_LOG(LogTemp, Log, TEXT("LlamaThread: DecodeTokensAndSample KV cursor: %d. send to history '%hs'."), kv_cache_token_cursor, s.c_str());
				}
				AppendTurnToStructuredHistory(TEXT("assistant"), AssistantMessageTokensForStorageInHistory);
			}
		}
    }


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
            std::string role_suffix = "<|im_end|>\n";

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

		UE_LOG(LogTemp, Log, TEXT("LlamaThread: Assembling context dump..."));

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


		UE_LOG(LogTemp, Log, TEXT("LlamaThread: Context dump assembled. Length: %d chars (approx)."), full_context_str.length());
		return full_context_str;
	}
/*
	std::string Llama::AssembleFullContextForDump_LlamaThread() {
		if (!model || !ctx) {
			return "[Llama not initialized or model/context not available for dump]";
		}

		std::string full_context_str;
		full_context_str.reserve(n_ctx_from_model * 5); // Pre-allocate roughly

		UE_LOG(LogTemp, Log, TEXT("LlamaThread: Assembling context dump..."));

		// --- This order MUST EXACTLY MATCH how AssembleFullPromptForTurn builds the prompt ---

		// 1. Fixed Prefix Blocks (System, StaticWorld, LowFreq)
		//    Iterate in the defined order of ELlamaContextBlockType
		for (uint8 i = 0; i < (uint8)ELlamaContextBlockType::COUNT; ++i) {
			ELlamaContextBlockType currentBlockType = (ELlamaContextBlockType)i;
			if (const FTokenizedContextBlock* Block = FixedContextBlocks.Find(currentBlockType)) {
				// Optional: Add a marker for debugging which block this is
				full_context_str += "\n--- START BLOCK: " + std::string(TCHAR_TO_UTF8(*UEnum::GetValueAsString(currentBlockType))) + " ---\n";
				DetokenizeAndAppend(full_context_str, Block->Tokens, model);
				full_context_str += "\n--- END BLOCK: " + std::string(TCHAR_TO_UTF8(*UEnum::GetValueAsString(currentBlockType))) + " ---\n";
			}
		}

		// 2. Conversation History (Rebuilt from StructuredConversationHistory with role markers)
		//    This is the most complex part to get right for the dump because it involves
		//    re-applying the chat template. The `ConversationHistoryTokens` flat array
		//    already has this applied.
		// full_context_str += "\n--- START CONVERSATION HISTORY (Formatted) ---\n";
		DetokenizeAndAppend(full_context_str, ConversationHistoryTokens, model);
		// full_context_str += "\n--- END CONVERSATION HISTORY (Formatted) ---\n";


		// 3. Current High Frequency State (as it was prepared for the *last* turn or *would be* for the next)
		//    This reflects the HFS tokens that were (or would be) appended.
		// full_context_str += "\n--- START CURRENT HIGH FREQUENCY STATE ---\n";
		DetokenizeAndAppend(full_context_str, CurrentHighFrequencyStateTokens, model); // Assuming this holds the latest HFS tokens
		// full_context_str += "\n--- END CURRENT HIGH FREQUENCY STATE ---\n";


		// 4. New Input for the current turn (if any was being processed)
		//    This is tricky because "New Input" is transient; it gets merged into conversation history.
		//    If you want to see what the *last* input was, you might need to fetch it from the
		//    last turn in StructuredConversationHistory if it's a user/tool_response turn.
		//    For a general "what the AI would see *next*", this might be empty until new input arrives.
		//    Let's assume for now this dump reflects the state *before* a new input is fully integrated
		//    or *after* a generation, so the "new input" is already part of ConversationHistoryTokens.

		// 5. AI Assistant Prefix (The prompt that cues the AI to start generating its response)
		//    This should be the exact same prefix used in AssembleFullPromptForTurn.
		std::string assistant_prefix_str = "\n<|im_start|>assistant\n"; // Example for Qwen, ensure this matches your actual prefix
		// No need to tokenize and de-tokenize this if it's a fixed string; just append.
		full_context_str += assistant_prefix_str;


		// Optional: Add KV Cache Status
		// full_context_str += FString::Printf(TEXT("\n\n--- KV Cache Status ---\nValid Tokens: %d / %d (Context Window)\n"), kv_cache_token_cursor, n_ctx_from_model).ToString();

		UE_LOG(LogTemp, Log, TEXT("LlamaThread: Context dump assembled. Length: %d chars (approx)."), full_context_str.length());
		return full_context_str;
	}
*/

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

    void Llama::ProcessInputAndGenerate_LlamaThread(const FString& InputTextFStr, const FString& HighFrequencyContextTextFStr, const FString& InputTypeHintFStr)
    {
        if (!ctx || !model || bIsGenerating) { // Don't interrupt ongoing generation
            if (bIsGenerating) {
            	UE_LOG(LogTemp, Warning, TEXT("LlamaThread: ProcessInput called while already generating. Input ignored."));
            } else {
            	UE_LOG(LogTemp, Error, TEXT("LlamaThread: ProcessInput called but Llama not ready."));
			}
            return;
        }
        UE_LOG(LogTemp, Log, TEXT("LlamaThread: Processing Input: '%s', HFS: '%s', Hint: '%s'"), *InputTextFStr, *HighFrequencyContextTextFStr, *InputTypeHintFStr);

        // 1. Tokenize new HFS and Input
        std::string hfs_std = TCHAR_TO_UTF8(*HighFrequencyContextTextFStr);
        // HFS should be formatted with any necessary prefixes/suffixes for the prompt template
        // e.g., "\n[Submarine Status:]\n" + hfs_std + "\n"
        // For Qwen, it might be part of the user message or a system message before user.
        // Let's assume HFS is just text to be appended before user input for now.
        std::vector<llama_token> hfs_tokens_tarray;
        std::vector<llama_token> hfs_tokens_std = my_llama_tokenize(model, hfs_std, false, false);
        hfs_tokens_tarray.insert(hfs_tokens_tarray.end(), hfs_tokens_std.begin(), hfs_tokens_std.end());
        CurrentHighFrequencyStateTokens = hfs_tokens_tarray;

        std::string input_std = TCHAR_TO_UTF8(*InputTextFStr);
        // Format input_std according to role (user, tool_response) and chat template
        // e.g., for Qwen user: "<|im_start|>user\n" + input_std + "<|im_end|>\n"
        // e.g., for Qwen tool response: "<|im_start|>tool\n" + input_std + "<|im_end|>\n<|im_start|>assistant\n" (to prompt AI)
        // This formatting is CRITICAL. For now, let's assume InputTextFStr is ALREADY formatted with role markers.
        std::vector<llama_token> input_tokens_tarray;
        std::vector<llama_token> input_tokens_std = my_llama_tokenize(model, input_std, false, true /* allow special for role/tool tags */);
        input_tokens_tarray.insert(input_tokens_tarray.end(), input_tokens_std.begin(), input_tokens_std.end());

        // 2. Add input to structured history (before pruning, so it's considered for what to keep)
        // The Role here should be "user" or "tool" based on InputTypeHintFStr
        AppendTurnToStructuredHistory(InputTypeHintFStr, input_tokens_tarray); // This rebuilds ConversationHistoryTokens

        // 3. Prune conversation history (this will also update flat ConversationHistoryTokens and invalidate KV if needed)
        PruneConversationHistory();

        // 4. Assemble the full prompt for this turn
        std::vector<llama_token> FullPromptForTurn;
        // NewInputTokens for AssembleFullPromptForTurn will be empty, as the input is now part of ConversationHistoryTokens
        AssembleFullPromptForTurn(std::vector<llama_token>() /* Empty NewInput */, FullPromptForTurn);

        // 5. Decode and Generate
        DecodeTokensAndSample(FullPromptForTurn, true /* Logits for last token of prompt */);
    }


    // --- Main Llama Thread Loop ---
    void Llama::ThreadRun_LlamaThread()
    {
        // Initial wait for InitializeLlama_LlamaThread to be called
        while (bIsRunning && (!model || !ctx)) {
            qMainToLlama.processQ(); // Process init command
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }

        UE_LOG(LogTemp, Log, TEXT("LlamaThread %p: Starting main loop."), this);

        while (bIsRunning)
        {
            // Process any pending commands from the main thread
            // This is where UpdateContextBlock, ProcessInputAndGenerate_LlamaThread, etc., are actually invoked.
            qMainToLlama.processQ();

            // If not actively generating, sleep a bit to yield CPU
            if (!bIsGenerating.load()) {
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
            // The actual generation logic is now part of DecodeTokensAndSample,
            // which is called by ProcessInputAndGenerate_LlamaThread.
        }
        UE_LOG(LogTemp, Log, TEXT("LlamaThread %p: Exiting main loop."), this);
        ShutdownLlama_LlamaThread(); // Clean up llama resources
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
    PrimaryComponentTick.bCanEverTick = true;
    PrimaryComponentTick.bStartWithTickEnabled = true;
    LlamaInternal = std::make_unique<Internal::Llama>();

    // Setup delegates to marshal callbacks from LlamaInternal to ULlamaComponent's Blueprint delegates
    LlamaInternal->tokenCb = [this](FString NewToken) {
        LlamaInternal->qLlamaToMain.enqueue([this, NewToken]() { // Ensure BP delegate is called on game thread
            OnNewTokenGenerated.Broadcast(NewToken);
        });
    };
    LlamaInternal->fullContextDumpCb = [this](FString ContextDump) {
        LlamaInternal->qLlamaToMain.enqueue([this, ContextDump]() {
            OnFullContextDumpReady.Broadcast(ContextDump);
        });
    };
    LlamaInternal->toolCallCb = [this](FString ToolCallJson) {
        LlamaInternal->qLlamaToMain.enqueue([this, ToolCallJson]() {
        	// TODO: this is where to handle tool calls instead of telling the blueprints 
            OnToolCallDetected.Broadcast(ToolCallJson);
        });
    };
    LlamaInternal->errorCb = [this](FString ErrorMessage) {
        LlamaInternal->qLlamaToMain.enqueue([this, ErrorMessage]() {
            OnLlamaErrorOccurred.Broadcast(ErrorMessage);
        });
    };
}

ULlamaComponent::~ULlamaComponent()
{
    // LlamaInternal unique_ptr will handle its own destruction, which calls LlamaInternal->bIsRunning = false and joins thread.
}

void ULlamaComponent::BeginPlay() // Changed from Activate
{
    Super::BeginPlay();
    if (LlamaInternal && !PathToModel.IsEmpty() && !SystemPromptText.IsEmpty()) {
        // Marshal the initialization call to the Llama thread
        FString ModelPathCopy = PathToModel;
        FString SystemPromptCopy = SystemPromptText;
        // TODO: this is where to add to the static system prompt, need access to the VisualTestHarnessActor and SubmarineState
        //        but also can change any block by calling UpdateContextBlock() from the main thread
        LlamaInternal->qMainToLlama.enqueue([this, ModelPathCopy, SystemPromptCopy]() {
            LlamaInternal->InitializeLlama_LlamaThread(ModelPathCopy, SystemPromptCopy /*, other params */);
        });
    } else {
        UE_LOG(LogTemp, Error, TEXT("ULlamaComponent: PathToModel or SystemPromptText is empty. Llama not initialized."));
    }

	AVisualTestHarnessActor* HarnessActor = Cast<AVisualTestHarnessActor>(GetOwner());
	if (HarnessActor)
	{
	}
}

void ULlamaComponent::EndPlay(const EEndPlayReason::Type EndPlayReason) // Changed from Deactivate
{
    if (LlamaInternal) {
        LlamaInternal->bIsRunning = false; // Signal thread to stop
        // The LlamaInternal destructor will join the thread.
        // If you need to explicitly queue a shutdown command:
        // LlamaInternal->qMainToLlama.enqueue([this]() {
        //     LlamaInternal->ShutdownLlama_LlamaThread();
        // });
    }
    Super::EndPlay(EndPlayReason);
}

void ULlamaComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
    Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
    if (LlamaInternal) {
        // Process any callbacks from the Llama thread that are pending for the main thread
        while(LlamaInternal->qLlamaToMain.processQ());
    }
}

void ULlamaComponent::UpdateContextBlock(ELlamaContextBlockType BlockType, const FString& NewTextContent)
{
    if (LlamaInternal) {
        FString TextCopy = NewTextContent; // Ensure lifetime for lambda
        LlamaInternal->qMainToLlama.enqueue([this, BlockType, TextCopy]() {
            LlamaInternal->UpdateContextBlock_LlamaThread(BlockType, TextCopy);
        });
    }
}

void ULlamaComponent::ProcessInput(const FString& InputText, const FString& HighFrequencyContextText, const FString& InputTypeHint)
{
    if (LlamaInternal) {
        FString InputCopy = InputText;
        FString HFSCopy = HighFrequencyContextText;
        // TODO: this is where to add to the high frequency context, need access to the VisualTestHarnessActor and SubmarineState
        FString HintCopy = InputTypeHint;
        LlamaInternal->qMainToLlama.enqueue([this, InputCopy, HFSCopy, HintCopy]() {
            LlamaInternal->ProcessInputAndGenerate_LlamaThread(InputCopy, HFSCopy, HintCopy);
        });
    }
}

void ULlamaComponent::TriggerFullContextDump()
{
    if (LlamaInternal) {
        LlamaInternal->qMainToLlama.enqueue([this]() {
            LlamaInternal->RequestFullContextDump_LlamaThread();
        });
    }
}

