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
                // For debugging which stop sequence matched:
				std::string matched_stop_str;
				for(llama_token t : stop_seq) {
					const char* piece = llama_vocab_get_text(llama_model_get_vocab(model), t);
					if (piece) matched_stop_str += piece;
				}
				UE_LOG(LogTemp, Log, TEXT("Llama thread %p: Stop sequence matched: '%s'"), this, UTF8_TO_TCHAR(matched_stop_str.c_str()));
//                UE_LOG(LogTemp, Log, TEXT("Llama thread %p: Stop sequence matched."), this);
                return true;
            }
        }
    }
    return false;
}

Llama::Llama() : thread([this]() { threadRun(); }) {}

// In Internal::Llama class

// Tokenizes and returns the current submarine status
std::vector<llama_token> Llama::GetCurrentSubmarineStatusTokens() {
    // FString status_FString = OuterLlamaComponent->GetFormattedSubmarineStatus(); // Call a UFUNCTION on ULlamaComponent
    // std::string status_std_str = "SubmarineStatus:\nDepth: 100m\nSpeed: 5kts\nBattery1: 80%\n"; // Example
    // For now, a placeholder:
    std::string status_std_str = "\n[Submarine Status: Depth=" + std::to_string(rand()%200) + "m, Battery=" + std::to_string(rand()%100) + "%]\n";
    return my_llama_tokenize(this->model, status_std_str, false, false);
}

// Checks if conversation history needs pruning based on token count or turn count
bool Llama::ConversationHistoryNeedsPruning() {
    int total_convo_tokens = 0;
    for (const auto& turn : conversation_history_turns) {
        total_convo_tokens += turn.size();
    }
    // Example: Prune if convo history itself exceeds half the context, or too many turns
    return (conversation_history_turns.size() > MAX_CONVO_HISTORY_TURNS) ||
           (N_STATIC_END_POS + total_convo_tokens + GetCurrentSubmarineStatusTokens().size() > llama_n_ctx(ctx) - 256 /* buffer for AI response */);
}

// Prunes conversation_history_turns (deque) and updates current_conversation_tokens and KV cache
void Llama::PruneConversationHistoryAndUpdateContext() {
    UE_LOG(LogTemp, Warning, TEXT("Pruning conversation history. Current turns: %d"), conversation_history_turns.size());
    while (conversation_history_turns.size() > MAX_CONVO_HISTORY_TURNS / 2) { // Keep newest half
        conversation_history_turns.pop_front();
    }

    // Invalidate KV cache from after static prefix up to current end
    InvalidateKVCacheAndTokensFrom(N_STATIC_END_POS); // Rolls back KV and current_conversation_tokens

    // Re-append the pruned conversation history to current_conversation_tokens
    for (const auto& turn_tokens : conversation_history_turns) {
        current_conversation_tokens.insert(current_conversation_tokens.end(), turn_tokens.begin(), turn_tokens.end());
    }
    // N_CONVO_END_POS_BEFORE_STATUS will be set after this, before new status is added
    // current_eval_pos is already N_STATIC_END_POS, so the re-appended convo will be decoded
}

// Adds a turn to the structured conversation history deque
void Llama::AddToConversationHistoryDeque(const std::string& role, const std::vector<llama_token>& tokens) {
    // You'd likely have a struct for turns, or just store the tokens.
    // For simplicity, let's assume you have a way to format them with role later.
    // This example just adds the tokens. You'll need to prepend role tokens when rebuilding the flat vector.
    conversation_history_turns.push_back(tokens);
    if (conversation_history_turns.size() > MAX_CONVO_HISTORY_TURNS * 2) { // Keep a bit more than strictly needed for pruning
        conversation_history_turns.pop_front();
    }
}

// Gets a flat vector of tokens from the pruned conversation_history_turns (deque)
std::vector<llama_token> Llama::GetPrunedConversationTokenVectorFromHistory() {
    std::vector<llama_token> flat_convo;
    // This needs to correctly prepend role tokens like "Captain: ", "AIXO_CAP: "
    // based on your chat template.
    for (const auto& turn_tokens : conversation_history_turns) {
        // Example: if (role == "user") flat_convo.insert(flat_convo.end(), user_prefix_tokens.begin(), user_prefix_tokens.end());
        flat_convo.insert(flat_convo.end(), turn_tokens.begin(), turn_tokens.end());
        // flat_convo.push_back(llama_token_nl(model)); // Add newlines between turns
    }
    return flat_convo;
}

// Helper to process a vector of tokens in chunks
// tokens_to_process is LIKELY current_conversation_tokens
// start_offset_in_vector is where in current_conversation_tokens we begin processing (this should be current_eval_pos before this call)
void Llama::ProcessTokenVectorChunked(const std::vector<llama_token>& tokens_to_process, 
                                      int32_t start_offset_in_vector, 
                                      bool last_chunk_gets_logits) {
    if (start_offset_in_vector >= static_cast<int32_t>(tokens_to_process.size())) {
        // If current_eval_pos was already at the end, nothing to do.
        // However, if we just added new tokens, start_offset_in_vector should be less than tokens_to_process.size().
        UE_LOG(LogTemp, Log, TEXT("ProcessTokenVectorChunked: No new tokens to process. Start offset: %d, Total size: %d"), 
            start_offset_in_vector, tokens_to_process.size());
        return;
    }

    int32_t num_tokens_overall_to_eval = tokens_to_process.size() - start_offset_in_vector;
    UE_LOG(LogTemp, Log, TEXT("ProcessTokenVectorChunked: Processing %d tokens, starting from vector index %d. Current KV pos: %d"), 
        num_tokens_overall_to_eval, start_offset_in_vector, this->current_sequence_pos);

    for (int32_t chunk_offset = 0; chunk_offset < num_tokens_overall_to_eval; /* chunk_offset incremented by batch.n_tokens */) {
        int32_t n_eval_this_chunk = std::min((int32_t)llama_n_batch(ctx), num_tokens_overall_to_eval - chunk_offset);
        
        common_batch_clear(batch);
        for (int32_t i = 0; i < n_eval_this_chunk; ++i) {
            int32_t token_vector_idx = start_offset_in_vector + chunk_offset + i;
            bool produce_logits_for_this_token = last_chunk_gets_logits && (chunk_offset + i == num_tokens_overall_to_eval - 1);

            // llama_batch_add is simpler and safer than direct array manipulation
            common_batch_add(this->batch, tokens_to_process[token_vector_idx], 
                            this->current_sequence_pos + i, // This is the critical part for KV cache position
                            {0}, // Sequence ID array
                            produce_logits_for_this_token);
        }
        // llama_batch_add increments batch.n_tokens internally.

        if (this->batch.n_tokens > 0) {
            if (llama_decode(ctx, this->batch) != 0) {
                UE_LOG(LogTemp, Error, TEXT("Llama thread %p: llama_decode failed in ProcessTokenVectorChunked."), this);
                eos_reached = true; 
                current_ai_state = LlamaState::IDLE;
                return; 
            }
            // Advance global KV position by the number of tokens successfully decoded in this batch
            this->current_sequence_pos += this->batch.n_tokens; 
            chunk_offset += this->batch.n_tokens; // Advance chunk_offset correctly
        } else {
            break; // No tokens added to batch, something is wrong
        }
    }
    // After all chunks are processed, current_eval_pos should reflect the new end of evaluated tokens
    // within the tokens_to_process vector.
    this->current_eval_pos = tokens_to_process.size(); 
    UE_LOG(LogTemp, Log, TEXT("ProcessTokenVectorChunked: Finished. New current_eval_pos: %d, New current_sequence_pos: %d"), 
        this->current_eval_pos, this->current_sequence_pos);
}

// Helper for KV Cache invalidation and token history truncation
void Llama::InvalidateKVCacheAndTokensFrom(int32_t position_to_keep_kv_until) {
    if (position_to_keep_kv_until < current_sequence_pos) {
        UE_LOG(LogTemp, Log, TEXT("KV Cache: Invalidating from pos %d up to %d."), position_to_keep_kv_until, current_sequence_pos);
        llama_kv_self_seq_rm(ctx, 0, position_to_keep_kv_until, current_sequence_pos);
        current_sequence_pos = position_to_keep_kv_until;
        current_eval_pos = std::min(current_eval_pos, current_sequence_pos); // Ensure eval_pos doesn't exceed new sequence_pos
        
        if (current_conversation_tokens.size() > static_cast<size_t>(position_to_keep_kv_until)) {
            current_conversation_tokens.resize(position_to_keep_kv_until);
        }
        // Potentially update last_n_tokens_for_penalty here too if it's longer than new current_sequence_pos
    }
}

void Llama::threadRun() {
    UE_LOG(LogTemp, Log, TEXT("Llama thread %p: Starting."), this);
    bool initbatch = true;

    while (running) {
        while (qMainToThread.processQ()) {}
        if (!ctx || !model || !running) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            continue;
        }
    	if (initbatch) {
    		batch = llama_batch_init((int32_t)llama_n_batch(ctx), 0, 1); // Use context's n_batch
    		initbatch = false;
		}

        // --- 0. Initial Static Context + Status Processing (Once) ---
        if (!initial_context_processed) {
            current_ai_state = LlamaState::PROCESSING_INPUT;
            // current_conversation_tokens has static prefix from unsafeActivate
            N_STATIC_END_POS = current_conversation_tokens.size();
            current_eval_pos = 0; // Process from start
            current_sequence_pos = 0; // KV cache starts empty

            ProcessTokenVectorChunked(current_conversation_tokens, 0, false); // Decode static, no logits needed at end of this
            N_CONVO_END_POS_BEFORE_STATUS = current_sequence_pos; // After static, before first status

            std::vector<llama_token> initial_status_tokens = GetCurrentSubmarineStatusTokens();
            current_conversation_tokens.insert(current_conversation_tokens.end(), initial_status_tokens.begin(), initial_status_tokens.end());
            ProcessTokenVectorChunked(current_conversation_tokens, N_CONVO_END_POS_BEFORE_STATUS, true); // Decode status, logits for last token

            initial_context_processed = true;
            current_ai_state = LlamaState::IDLE; // Or GENERATING_RESPONSE if prompt ends with assistant turn
            UE_LOG(LogTemp, Log, TEXT("Initial context processed. current_sequence_pos: %d"), current_sequence_pos);
            continue;
        }

        // --- Flags for this iteration ---
        bool new_captain_or_system_input_received = false;
        std::vector<llama_token> new_external_tokens;

        { // Check for new input from main thread
            std::lock_guard<std::mutex> lock(input_mutex);
            if (new_input_flag) {
                std::string text_chunk;
                text_chunk.swap(new_input_buffer);
                new_input_flag = false;
                new_captain_or_system_input_received = true;
                eos_reached = false; // New input means AI needs to respond
                is_generating_response_internally = false; // Stop any ongoing generation
                current_ai_response_buffer.clear();

                new_external_tokens = my_llama_tokenize(this->model, text_chunk, false, false);
                UE_LOG(LogTemp, Log, TEXT("New external input: %d tokens."), new_external_tokens.size());
            }
        }

        // --- Context Management & Decoding ---
        if (new_captain_or_system_input_received || (current_ai_state == LlamaState::IDLE && pending_status_update_flag)) {
            current_ai_state = LlamaState::PROCESSING_INPUT;
            UE_LOG(LogTemp, Log, TEXT("State: PROCESSING_INPUT (due to new input or status update)"));

            pending_status_update_flag = false;

            // 1. Roll back KV cache to before the *previous* status block
            InvalidateKVCacheAndTokensFrom(N_CONVO_END_POS_BEFORE_STATUS);

            // 2. Add new external input (if any) to conversation history
            if (new_captain_or_system_input_received) {
                current_conversation_tokens.insert(current_conversation_tokens.end(), new_external_tokens.begin(), new_external_tokens.end());
                AddToConversationHistoryDeque("user", new_external_tokens); // Add to structured history
            }

            // 3. Prune conversation history if needed (and KV cache accordingly)
            if (ConversationHistoryNeedsPruning()) {
                PruneConversationHistoryAndUpdateContext(); // This helper will update N_STATIC_END_POS, current_conversation_tokens, and KV cache
            }
            
            // 4. Mark end of current conversation before adding new status
            N_CONVO_END_POS_BEFORE_STATUS = current_conversation_tokens.size();

            // 5. Append fresh submarine status
            std::vector<llama_token> status_tokens = GetCurrentSubmarineStatusTokens();
            current_conversation_tokens.insert(current_conversation_tokens.end(), status_tokens.begin(), status_tokens.end());

            // 6. Decode the new parts (pruned convo tail + new input + new status)
            ProcessTokenVectorChunked(current_conversation_tokens, current_eval_pos, true); // Logits for last token (of status)

            if (!eos_reached) { // If no error during decode
                current_ai_state = LlamaState::GENERATING_RESPONSE;
                is_generating_response_internally = true;
                current_ai_response_buffer.clear();
                kv_pos_before_current_ai_response = current_sequence_pos; // Mark KV state before AI response
                UE_LOG(LogTemp, Log, TEXT("Input processed. State: GENERATING_RESPONSE. current_sequence_pos: %d"), current_sequence_pos);
            } else {
                current_ai_state = LlamaState::IDLE;
            }
        }


        // --- AI Token Generation ---
        if (current_ai_state == LlamaState::GENERATING_RESPONSE && is_generating_response_internally && !eos_reached.load()) {
            if (current_eval_pos != current_sequence_pos || current_eval_pos != current_conversation_tokens.size()) {
                UE_LOG(LogTemp, Error, TEXT("Mismatch before sampling! eval_pos: %d, seq_pos: %d, convo_size: %d"), 
                    current_eval_pos, current_sequence_pos, current_conversation_tokens.size());
                // This indicates a logic error, try to recover by reprocessing
                current_ai_state = LlamaState::PROCESSING_INPUT;
                InvalidateKVCacheAndTokensFrom(N_CONVO_END_POS_BEFORE_STATUS); // Force full re-eval of convo + status
                continue;
            }

            float* logits = llama_get_logits_ith(ctx, batch.n_tokens - 1); // From last token of status block OR last AI token
            int n_vocab = llama_vocab_n_tokens(llama_model_get_vocab(model));

            std::vector<llama_token_data> candidates_data_vec(n_vocab);
            for (int token_id = 0; token_id < n_vocab; ++token_id) {
                candidates_data_vec[token_id] = llama_token_data{ (llama_token)token_id, logits[token_id], 0.0f };
            }
            llama_token_data_array candidates_p = { candidates_data_vec.data(), candidates_data_vec.size(), false };

            llama_token new_id = llama_sampler_sample(sampler_chain_instance, ctx, batch.n_tokens - 1); // Or your chosen sample func

//const char* piece_str_debug = llama_vocab_get_text(llama_model_get_vocab(model), new_id);
//UE_LOG(LogTemp, Warning, TEXT("Generated Token ID: %d, Piece: '%s'"), new_id, UTF8_TO_TCHAR(piece_str_debug ? piece_str_debug : "NULL"));

            llama_sampler_accept(sampler_chain_instance, new_id);

            if (new_id == llama_vocab_eos(llama_model_get_vocab(model)) || CheckStopSequences(new_id)) {
                UE_LOG(LogTemp, Log, TEXT("AI Turn Ended (EOS/Stop). State: IDLE."), this);
                eos_reached = true;
                is_generating_response_internally = false;
                // AddToConversationHistoryDeque("assistant", current_ai_response_buffer); // Add completed response
                current_ai_response_buffer.clear();
                current_ai_state = LlamaState::IDLE; // Ready for new input or status update
                // The next time input comes or status updates, KV will be rolled back to N_CONVO_END_POS_BEFORE_STATUS,
                // the AI's full response will be added to current_conversation_tokens, then new status.
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
					{
						size_t start_pos = 0;
						while((start_pos = piece_std_str.find("\xC4\x8A", start_pos)) != std::string::npos) {
							piece_std_str.replace(start_pos, 2, "\n");
							start_pos++;
						}
					}

                    FString token_fstring = UTF8_TO_TCHAR(piece_std_str.c_str());
                    qThreadToMain.enqueue([token_fstring, this]() mutable {
                        if (tokenCb) tokenCb(std::move(token_fstring));
                    });
                }

                current_conversation_tokens.push_back(new_id); // Buffer this token

                // Decode just this single new AI token to update KV cache for next generation step
                common_batch_clear(batch);
                common_batch_add(this->batch, new_id, current_sequence_pos, {0}, true); // Logits for this new token

                if (llama_decode(ctx, batch) != 0) { /* error, set eos_reached, set IDLE */ }
                else {
                    current_sequence_pos++; // KV cache advanced by one token
                    current_eval_pos++;     // This AI token is now "evaluated"
                    // current_conversation_tokens is NOT updated here token-by-token during generation.
                    // It's updated with the full AI response once generation is complete.
                }
            }
        } else if (current_ai_state == LlamaState::IDLE) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        } else { // Should be PROCESSING_INPUT but nothing to process, or some other state
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    } // end while(running)

//	llama_batch_free(batch);
    unsafeDeactivate();
    UE_LOG(LogTemp, Log, TEXT("Llama thread %p: Stopped."), this);
}

// In Internal::Llama.cpp

// Function to assemble the current context string (you'll call this from RequestFullContextDump)
// This function will be VERY SIMILAR to the logic you use to prepare the prompt string
// right before tokenization in your threadRun or a helper it calls.
std::string Llama::AssembleFullContextForDump() {
    std::string full_context_str;

    // --- This logic MUST MIRROR how you build the prompt for llama_decode ---

    // 1. System Prompt (from params_editor or a member variable)
    //    Assuming you have access to the initial system prompt text.
    //    Let's say it's stored in a member: std::string system_prompt_text;
    //    (This would be set in unsafeActivate from params_editor.prompt)
//    full_context_str += "SYSTEM PROMPT PLACEHOLDER\n"; // Make sure it has newlines if needed by the template
//    full_context_str += "\n"; // Separator

    // 2. Static Grid Info, Operating Procedures, Mission Objectives, Retrieved Summaries
    //    If these are separate strings or loaded data, append them here.
    //    For example:
    //    full_context_str += GetFormattedStaticGridInfo(); // Your helper function
    //    full_context_str += "\n";
    //    full_context_str += GetFormattedOperatingProcedures();
    //    full_context_str += "\n";
    //    full_context_str += GetFormattedMissionObjectives();
    //    full_context_str += "\n";
    //    for (const auto& summary : retrieved_mission_summaries_for_context) {
    //        full_context_str += "Previous Mission Summary:\n" + summary + "\n";
    //    }

    // 3. Current Conversation History (from your conversation_history_turns deque)
    //    This needs to be formatted according to the chat template (e.g., Qwen's <|im_start|>role\ncontent<|im_end|>)
    //    This is the most complex part to replicate accurately outside the main prompt formatting logic.
    //    Ideally, you'd have a shared helper function that both threadRun and this dump function use.
    UE_LOG(LogTemp, Log, TEXT("AssembleFullContextForDump: Formatting conversation history (%d turns)."), conversation_history_turns.size());
    for (const auto& turn : conversation_history_turns) { // Assuming turn is std::pair<std::string_role, std::vector<llama_token>>
                                                          // or similar structure
        // std::string role_str = turn.first;
        // std::string content_str;
        // for (llama_token token_id : turn.second) {
        //     const char* piece = llama_token_get_text(this->model, token_id);
        //     if (piece) content_str += piece;
        // }
        // full_context_str += "<|im_start|>" + role_str + "\n" + content_str + "<|im_end|>\n";
        // THIS IS A SIMPLIFICATION. You need to replicate the Qwen template logic exactly.
        // Including handling of tool_calls and tool_responses within the history.
    }
    // For now, let's just dump the raw current_conversation_tokens (flat vector)
    // as a proxy, though it won't have the role markers unless you add them.
    // This part needs the most care to match your actual prompt formatter.
//    full_context_str += "\n--- Current Flat Token History (for decode) ---\n";
    for (llama_token token_id : current_conversation_tokens) { // This is the vector fed to decode
         const char* piece = llama_vocab_get_text(llama_model_get_vocab(model), token_id);
         if (piece) {
             // Handle special characters like Ġ and Ċ for readability if desired
             std::string p_str = piece;
             if (p_str.rfind("\xC4\xA0", 0) == 0) { p_str.replace(0, 2, " ");}
             if (p_str.rfind("\xC4\x8A", 0) == 0) { p_str.replace(0, 2, "\n");}
             full_context_str += p_str;
         }
    }
//    full_context_str += "\n--- End Current Flat Token History ---\n";


    // 4. Current Submarine Status Block (The one that would be appended last)
    //    You might not have this readily available as a separate string if it's always
    //    tokenized and appended directly to current_conversation_tokens.
    //    If you want to show what *would be* the next status block:
    //    std::vector<llama_token> next_status_tokens = GetCurrentSubmarineStatusTokens();
    //    std::string next_status_str = "\n[Current Submarine Status (if generated now)]:\n";
    //    for (llama_token token_id : next_status_tokens) {
    //        const char* piece = llama_token_get_text(this->model, token_id);
    //        if (piece) next_status_str += piece;
    //    }
    //    full_context_str += next_status_str;
    //    full_context_str += "\n";

    // 5. List of Available Tools/Commands/Queries (as defined in your system prompt's <tools> section)
    //    You can re-append the <tools>...</tools> block here for clarity.
    //    full_context_str += "\n<tools>\n ... your tool definitions ... \n</tools>\n";

    // 6. The final prompt for the AI to start generating
    //    (e.g., "<|im_start|>assistant\n" for Qwen)
    //    full_context_str += GetAIAssistantTurnPrefix(); // Your helper

    return full_context_str;
}

void Llama::RequestFullContextDump() {
    // This function will be called from the main thread via the qMainToThread queue.
    // The actual assembly and callback should happen on the llama thread to ensure
    // thread safety with context variables, or variables should be copied carefully.
    // For simplicity, let's assume it's safe to call AssembleFullContextForDump here
    // if this function itself is enqueued and executed by the llama thread.

    qMainToThread.enqueue([this]() { // Ensure this lambda runs on the llama thread
        std::string context_dump_std_str = AssembleFullContextForDump();
        FString context_dump_fstr = UTF8_TO_TCHAR(context_dump_std_str.c_str());
        std::string fstr = std::string(TCHAR_TO_UTF8(*context_dump_fstr));

		{
			size_t start_pos = 0;
			while((start_pos = fstr.find("\xC4\xA0", start_pos)) != std::string::npos) {
				fstr.replace(start_pos, 2, " ");
				start_pos++;
			}
		}
		{
			size_t start_pos = 0;
			while((start_pos = fstr.find("\xC4\x8A", start_pos)) != std::string::npos) {
				fstr.replace(start_pos, 2, "\n");
				start_pos++;
			}
		}
		context_dump_fstr = FString(fstr.c_str());

        // Send it back to the main thread for display
        qThreadToMain.enqueue([this, context_dump_fstr]() mutable {
            if (fullContextDumpCb) {
                fullContextDumpCb(std::move(context_dump_fstr));
            }
        });
    });
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
    int n_prompt = 32768 - 1024;
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

    // stop sequences

	StopSeqHelper(TEXT("<|im_end|>"));
	StopSeqHelper(TEXT("<|endoftext|>"));
	StopSeqHelper(TEXT("~~~END_AIXO_TURN~~~"));
	StopSeqHelper(TEXT("Captain: "));

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
//    llama_batch_free(batch);
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
  //
    llama->tokenCb = [this](FString NewToken) { OnNewTokenGenerated.Broadcast(std::move(NewToken)); };
    // Set the new callback for context dumps
    llama->fullContextDumpCb = [this](FString ContextDump) {
        // This lambda will be called from the llama thread via qThreadToMain.
        // We need to marshal the call to OnFullContextDumpReady to the game thread.
        AsyncTask(ENamedThreads::GameThread, [this, ContextDump]() {
            HandleFullContextDump(ContextDump);
        });
    };
}

void ULlamaComponent::TriggerFullContextDump()
{
    if (llama) {
        llama->RequestFullContextDump();
    }
}

void ULlamaComponent::HandleFullContextDump(FString ContextDump)
{
    // This is now running on the Game Thread
    OnFullContextDumpReady.Broadcast(ContextDump);
    UE_LOG(LogTemp, Log, TEXT("Full Context Dump Requested and Ready (length: %d)"), ContextDump.Len());
    // You could also directly print it to a UMG text box here if you have a reference.
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
