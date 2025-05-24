#pragma once

// Engine includes first
#include "CoreMinimal.h"

// Third-party includes
#include "llama-cpp.h"  // This includes the full llama.h with all type definitions
#include "ConcurrentQueue.h"

// Plugin includes
#include "LlamaTypes.h"  // For ELlamaContextBlockType
#include "ContextVisualizationData.h"  // For FContextVisPayload

// STL includes last
#include <thread>
#include <memory>
#include <vector>
#include <string>
#include <functional>
#include <atomic>

namespace Internal
{
    class LlamaInternal
    {
    public:
        LlamaInternal() noexcept;
        ~LlamaInternal();

        // Thread management
        void ThreadRun_LlamaThread();
        void InitializeLlama_LlamaThread(const FString& ModelPathFStr, const FString& InitialSystemPromptFStr, const FString& Systems, const FString& LowFreq);
        void ShutdownLlama_LlamaThread();
        void UpdateContextBlock_LlamaThread(ELlamaContextBlockType BlockType, const FString& NewTextContent);
        void ProcessInputAndGenerate_LlamaThread(const FString& InputText, const FString& HighFrequencyContextText, const FString& InputTypeHint);
        void RequestFullContextDump_LlamaThread();
        void DecodeTokensAndSample(std::vector<llama_token>& TokensToDecode, bool bIsFinalPromptTokenLogits);
        void SignalStopRunning() { bIsRunning = false; }

        // Queue members
        ConcurrentQueue<std::function<void()>> qMainToLlama;
        ConcurrentQueue<std::function<void()>> qLlamaToMain;

        // Callbacks
        std::function<void(FString)> tokenCb;
        std::function<void(FString)> fullContextDumpCb;
        std::function<void(FString)> errorCb;
        std::function<void(float)> progressCb;
        std::function<void(const FContextVisPayload&)> contextChangedCb;
        std::function<void(FString)> readyCb;
        std::function<void(FString)> toolCallCb;
        std::function<void(bool)> setIsGeneratingCb;

        // State
        std::atomic<bool> bIsGenerating = false;
        std::atomic<bool> bIsRunning = true;

    private:
        // Thread handle
        std::thread ThreadHandle;

        // Core Llama state
        llama_model* model = nullptr;
        llama_context* ctx = nullptr;
        llama_batch batch;
        int32_t batch_capacity;
        llama_sampler* sampler_chain_instance = nullptr;
        const llama_vocab* vocab = nullptr;
        int32_t n_ctx_from_model = 0;
        int32_t current_kv_pos_for_predecode = 0;
        int32_t kv_cache_token_cursor = 0;

        // Context management
        struct FTokenizedContextBlock {
            std::vector<llama_token> Tokens;
        };
        TMap<ELlamaContextBlockType, FTokenizedContextBlock> FixedContextBlocks;
        std::vector<llama_token> ConversationHistoryTokens;
        std::vector<llama_token> CurrentHighFrequencyStateTokens;
        std::vector<llama_token> MirroredKvCacheTokens;
        std::vector<llama_token> CurrentTurnAIReplyTokens;
        std::atomic<bool> eos_reached = false;

        // Helper methods
        void _TokenizeAndStoreFixedBlockInternal(ELlamaContextBlockType BlockType, const FString& Text, bool bAddBosForThisBlock);
        void BroadcastContextVisualUpdate_LlamaThread(int32 nTokens = 0, float msDecode = 0.0f, float msGenerate = 0.0f);
    };
} // namespace Internal
// ... existing code ... 