// LlamaComponent.cpp
#include "LlamaComponent.h"
#include "Misc/FileHelper.h"      // **** FOR FILE OPERATIONS ****
#include "Misc/Paths.h"           // **** FOR PROJECT PATHS ****

////////////////////////////////////////////////////////////////////////////////////////////////

// --- ULlamaComponent Method Implementations ---
ULlamaComponent::ULlamaComponent(const FObjectInitializer& ObjectInitializer)
    : Super(ObjectInitializer)
{
    PrimaryComponentTick.bCanEverTick = true;
    PrimaryComponentTick.bStartWithTickEnabled = true;
    LlamaImpl = new LLInternal();

    // Setup delegates to marshal callbacks from LLInternal to ULlamaComponent's Blueprint delegates
    LlamaImpl->tokenCb = [this](FString NewToken) {
        { // Ensure BP delegate is called on game thread
            OnNewTokenGenerated.Broadcast(NewToken);
        }
    };
    LlamaImpl->fullContextDumpCb = [this](FString ContextDump) {
        {
            OnFullContextDumpReady.Broadcast(ContextDump);
        }
    };
    LlamaImpl->errorCb = [this](FString ErrorMessage) {
        {
            OnLlamaErrorOccurred.Broadcast(ErrorMessage);
        }
    };
    LlamaImpl->progressCb = [this](float Progress) {
		{
            OnLlamaLoadingProgressDelegate.Broadcast(Progress);
        }
    };
	LlamaImpl->toolCallCb = [this](FString ToolCallJsonRaw) {
		{
			OnToolCallDetected.Broadcast(ToolCallJsonRaw);
		}
	};
    //
    LlamaImpl->readyCb = [this](FString ReadyMessage) {
        {
            bIsLlamaCoreReady = true;
            //
            OnLlamaReady.Broadcast(ReadyMessage);
        }
    };
    LlamaImpl->contextChangedCb = [this](const FContextVisPayload& contextBlocks) {
        {
        	FContextVisPayload FinalPayload = (FContextVisPayload&)contextBlocks;
			FinalPayload.bIsStaticWorldInfoUpToDate = contextBlocks.KvCacheDecodedTokenCount >= contextBlocks.Blocks[((int)EContextVisBlockType::StaticWorldInfo)+1].NormalizedStartInTokens;	// if the cursor is at or past the end of the block
			FinalPayload.bIsLowFrequencyStateUpToDate = contextBlocks.KvCacheDecodedTokenCount >= contextBlocks.Blocks[((int)EContextVisBlockType::LowFrequencyState)+1].NormalizedStartInTokens;	// if the cursor is at or past the end of the block
			FinalPayload.bIsLlamaCoreActuallyReady = bIsLlamaCoreReady;
			FinalPayload.bIsLlamaCurrentlyIdle = bIsLlamaCoreReady && !bIsLlamaGenerating;
			//
			// also see VisualTestHarnessActor::AugmentContextVisPayload, called from blueprint before contextBlocks gets to LLGaugeWidget
            OnLlamaContextChangedDelegate.Broadcast(FinalPayload);
        }
    };
    LlamaImpl->setIsGeneratingCb = [this](bool isGen) {
        {
        	bIsLlamaGenerating = isGen;
        }
    };
}

ULlamaComponent::~ULlamaComponent()
{
    delete LlamaImpl;
}

void ULlamaComponent::BeginPlay()
{
    Super::BeginPlay();
}

void ULlamaComponent::ActivateLlamaComponent(FString SystemsContextBlock, FString LowFreqContextBlock)		// called by VisualTestHarnessActor::BeginPlay because it is first
{
	UE_LOG(LogTemp, Log, TEXT("ULlamaComponent::ActivateLlamaComponent."));

    if (LlamaImpl && !PathToModel.IsEmpty() && !SystemPromptFileName.IsEmpty()) {
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

        LlamaImpl->InitializeLlama(ModelPathCopy, SystemPromptCopy, SystemsContextBlock, LowFreqContextBlock /*, other params */);
    } else {
        UE_LOG(LogTemp, Error, TEXT("ULlamaComponent: PathToModel or SystemPromptFileName is empty. Llama not initialized."));
    }
}

void ULlamaComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    if (LlamaImpl) {
        LlamaImpl->SignalStopRunning(); // Signal thread to stop
        // The LlamaImpl destructor will join the thread.
        // If you need to explicitly queue a shutdown command:
        // LLInternal->qMainToLlama.enqueue([this]() {
        //     LlamaImpl->ShutdownLlama_LlamaThread();
        // });
    }
    Super::EndPlay(EndPlayReason);
}

void ULlamaComponent::UpdateContextBlock(ELlamaContextBlockType BlockType, const FString& NewTextContent)
{
    if (LlamaImpl) {
        FString TextCopy = NewTextContent; // Ensure lifetime for lambda
		LlamaImpl->UpdateContextBlock(BlockType, TextCopy);
    }
}

void ULlamaComponent::ProcessInput(const FString& InputText, const FString& HighFrequencyContextText, const FString& InputTypeHint)
{
    if (!bIsLlamaCoreReady) { // Check the flag
        UE_LOG(LogTemp, Warning, TEXT("ULlamaComponent: ProcessInput called but Llama core not ready. Replying with 'Not Ready'."));
        // Send "AIXO Not Ready" directly to your dialogue output system (e.g., TTS or UMG)
        // This does NOT go through the Llama thread.
        if (OnNewTokenGenerated.IsBound()) { // Or a dedicated "system message" delegate
            OnNewTokenGenerated.Broadcast(TEXT("AIXO is currently initializing systems.\nPlease try again shortly.\n\n"));
//            OnNewTokenGenerated.Broadcast(TEXT("~~~END_AIXO_TURN~~~")); // Signal end of this system message
        }
        return;
    }

    if (LlamaImpl) {
        FString InputCopy = InputText;
        FString HFSCopy = HighFrequencyContextText;
        FString HintCopy = InputTypeHint;
        LlamaImpl->ProcessInputAndGenerate(InputCopy, HFSCopy, HintCopy);
    }
}

void ULlamaComponent::TriggerFullContextDump()
{
    if (LlamaImpl) {
        LlamaImpl->RequestFullContextDump();
    }
}

void ULlamaComponent::TickComponent(float DeltaTime,
                               enum ELevelTick TickType,
                               FActorComponentTickFunction* ThisTickFunction) {
//    UE_LOG(LogTemp, Verbose, TEXT("ULlamaComponent::TickComponent CALLED")); // Check if this appears
    Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	if (LlamaImpl) {
		// UE_LOG(LogTemp, Verbose, TEXT("ULlamaComponent::TickComponent - LlamaImpl is VALID. Processing qLlamaToMain."));
		LlamaImpl->ProcessLlamaToMainQueue();
	} else {
		UE_LOG(LogTemp, Error, TEXT("ULlamaComponent::TickComponent - LlamaImpl IS NULL!"));
	}

    if (bIsLlamaCoreReady) { // Or some other condition to start broadcasting perf
//        FPerformanceUIPayload PerfPayload;
//        PerfPayload.LlamaMetrics = LastLlamaMetrics; // Use the latest received
//
//        PerfPayload.GameMetrics.FrameTimeMs = GetWorld()->GetDeltaSeconds() * 1000.0f;
//        // Getting accurate GameThread, RenderThread, GpuTime can be complex and involve stats system
//        // For a start, FrameTimeMs is good.
//        // For more detailed stats, look into FEnginePerformanceTimers or the "Stat Unit" data.
//        // Example for GPU time (can be latent):
//        PerfPayload.GameMetrics.GpuTimeMs = FPlatformTime::ToMilliseconds(RHIGetGPUFrameCycles(0)); // Might need specific RHI
//
//        if (OnPerformanceDataUpdatedDelegate.IsBound()) {
//            OnPerformanceDataUpdatedDelegate.Broadcast(PerfPayload);
//        }


// TODO: update the PerfPayload with per-tick measurements
/*
        FPerformanceUIPayload PerfPayload;
        PerfPayload.LlamaMetrics = LastLlamaMetrics; // LastLlamaMetrics is updated by ForwardLlamaMetricsToGameThread

        PerfPayload.GameMetrics.FrameTimeMs = GetWorld()->GetDeltaSeconds() * 1000.0f;
        // For GPU time, ensure RHIGetGPUFrameCycles is giving non-zero results if used.
        PerfPayload.GameMetrics.GpuTimeMs = FPlatformTime::ToMilliseconds(RHIGetGPUFrameCycles()); // Pass GFrameNumber or 0

        // **** LOG PerfPayload BEFORE BROADCAST ****
        UE_LOG(LogTemp, Warning, TEXT("ULlamaComponent::Tick - Broadcasting Perf - Frame:%.2f, PromptEval:%.2f, LLMTokensGen: %d"),
            PerfPayload.GameMetrics.FrameTimeMs,
            PerfPayload.LlamaMetrics.PromptEvalTimeMs,
            PerfPayload.LlamaMetrics.TokensGenerated);
*/
    }

}

