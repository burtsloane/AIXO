// LlamaComponent.cpp
#include "LlamaComponent.h"
#include "Misc/FileHelper.h"      // **** ADD THIS FOR FILE OPERATIONS ****
#include "Misc/Paths.h"           // **** ADD THIS FOR PROJECT PATHS ****

////////////////////////////////////////////////////////////////////////////////////////////////

// --- ULlamaComponent Method Implementations ---
ULlamaComponent::ULlamaComponent(const FObjectInitializer& ObjectInitializer)
    : Super(ObjectInitializer)
{
    PrimaryComponentTick.bCanEverTick = true;
    PrimaryComponentTick.bStartWithTickEnabled = true;
    HarnessActor = nullptr;
    LlamaImpl = new LlamaInternal();

    // Setup delegates to marshal callbacks from LlamaInternal to ULlamaComponent's Blueprint delegates
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
			FinalPayload.bIsStaticWorldInfoUpToDate = !bPendingStaticWorldInfoUpdate; // If pending, it's not up-to-date on Llama thread yet
			FinalPayload.bIsLowFrequencyStateUpToDate = !bPendingLowFrequencyStateUpdate;
			FinalPayload.bIsLlamaCoreActuallyReady = this->bIsLlamaCoreReady;
			FinalPayload.bIsLlamaCurrentlyIdle = this->bIsLlamaCoreReady && !this->bIsLlamaGenerating.load(std::memory_order_acquire);
			//
            OnLlamaContextChangedDelegate.Broadcast(FinalPayload);
        }
    };
	LlamaImpl->toolCallCb = [this](FString ToolCallJsonRaw) {
		{
			ProcessToolCall(ToolCallJsonRaw);
			OnToolCallDetected.Broadcast(ToolCallJsonRaw);
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

void ULlamaComponent::ActivateLlamaComponent(AVisualTestHarnessActor* InHarnessActor)		// called by VisualTestHarnessActor::BeginPlay because it is first
{
	HarnessActor = InHarnessActor;

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

        LlamaImpl->qMainToLlama.enqueue([this, ModelPathCopy, SystemPromptCopy, SystemsContextBlock, LowFreqContextBlock]() {
	UE_LOG(LogTemp, Log, TEXT("ULlamaComponent::ActivateLlamaComponent calling InitializeLlama_LlamaThread."));
            LlamaImpl->InitializeLlama_LlamaThread(ModelPathCopy, SystemPromptCopy, SystemsContextBlock, LowFreqContextBlock /*, other params */);
        });
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
        // LlamaInternal->qMainToLlama.enqueue([this]() {
        //     LlamaImpl->ShutdownLlama_LlamaThread();
        // });
    }
    Super::EndPlay(EndPlayReason);
}

void ULlamaComponent::UpdateContextBlock(ELlamaContextBlockType BlockType, const FString& NewTextContent)
{
    if (LlamaImpl) {
        FString TextCopy = NewTextContent; // Ensure lifetime for lambda
        LlamaImpl->qMainToLlama.enqueue([this, BlockType, TextCopy]() {
            LlamaImpl->UpdateContextBlock_LlamaThread(BlockType, TextCopy);
        });
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
        HFSCopy += MakeHFSString();
        FString HintCopy = InputTypeHint;
        LlamaImpl->qMainToLlama.enqueue([this, InputCopy, HFSCopy, HintCopy]() {
            LlamaImpl->ProcessInputAndGenerate_LlamaThread(InputCopy, HFSCopy, HintCopy);
        });
    }
}

void ULlamaComponent::TriggerFullContextDump()
{
    if (LlamaImpl) {
        LlamaImpl->qMainToLlama.enqueue([this]() {
            LlamaImpl->RequestFullContextDump_LlamaThread();
        });
    }
}

void ULlamaComponent::TickComponent(float DeltaTime,
                               enum ELevelTick TickType,
                               FActorComponentTickFunction* ThisTickFunction) {
//    UE_LOG(LogTemp, Verbose, TEXT("ULlamaComponent::TickComponent CALLED")); // Check if this appears
    Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	if (LlamaImpl) {
		// UE_LOG(LogTemp, Verbose, TEXT("ULlamaComponent::TickComponent - LlamaImpl is VALID. Processing qLlamaToMain."));
		while(LlamaImpl->qLlamaToMain.processQ());
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

    if (bIsLlamaCoreReady && HarnessActor) {
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

#pragma mark Context Blocks
FString ULlamaComponent::MakeCommandHandlerString(ICommandHandler *ich) {
	FString str;
#ifdef why //why
	ICommandHandler *FoundHandler = ich;
	if (FoundHandler)
	{
		const ICH_PowerJunction *pj = FoundHandler->GetAsPowerJunction();
		str += "**SYSTEM:" + FoundHandler->GetSystemName() + "\n";
		if (pj && pj->IsPowerSource()) str += "POWER_SOURCE:true\n";
		else str += "POWER_SOURCE:false\n";
		if (pj) {
			switch (pj->GetStatus()) {
				case EPowerJunctionStatus::NORMAL:
					str += "STATUS:NORMAL\n";
					break;
				case EPowerJunctionStatus::DAMAGED50:
					str += "STATUS:50%% DAMAGED\n";
					break;
				case EPowerJunctionStatus::DAMAGED100:
					str += "STATUS:DAMAGED\n";
					break;
				case EPowerJunctionStatus::DESTROYED:
					str += "STATUS:DESTROYED\n";
					break;
			}
			if (pj->IsPowerSource()) {
				str += FString::Printf(TEXT("POWER_AVAILABLE:%g\n"), pj->GetPowerAvailable());
			} else {
				str += FString::Printf(TEXT("POWER_USAGE:%g\n"), pj->GetCurrentPowerUsage());
			}
			str += FString::Printf(TEXT("NOISE:%g\n"), pj->GetCurrentNoiseLevel());
		}
		FString gd = FoundHandler->GetSystemGuidance();
		if (gd.Len() > 0) str += "GUIDANCE: " + gd + "\n";
		FString st = FoundHandler->GetSystemStatus();
		if (st.Len() > 0) str += "STATUS: " + st + "\n";
		TArray<FString> cm = FoundHandler->GetAvailableCommands();
		if (cm.Num() > 0) {
			str += "*COMMANDS:\n";
			for (FString& s : cm) str += s + "\n";
		}
		TArray<FString> qr = FoundHandler->GetAvailableQueries();
		if (qr.Num() > 0) {
			str += "*QUERIES:";
			for (FString& s : qr) str += " " + s;
			str += "\n";
		}
		//
		if (pj) {
			str += "*PORTS:\n";
			for (int i=0; i<pj->Ports.Num(); i++) {
				PWR_PowerSegment *seg = pj->Ports[i];
				str += seg->GetName();
				str += ":";
				ICH_PowerJunction *other_junction = seg->GetJunctionA();
				int32 other_pin = seg->GetPortA();
				if (pj == other_junction) {
					other_junction = seg->GetJunctionB();
					other_pin = seg->GetPortB();
				}
				str += other_junction->GetSystemName();
				str += ".";
				str += FString::Printf(TEXT("%d"), other_pin);
				str += "\n";
			}
		}
		//
		str += "*STATE:\n";
		str += FString::Join(FoundHandler->QueryEntireState(), TEXT("\n"));
		str += "\n";
		//
		if (pj) {
			str += "*POWER_PATH:";
			const TArray<PWR_PowerSegment*> PPath = pj->GetPathToSourceSegments();
			const TArray<ICH_PowerJunction*> JPath = pj->GetPathToSourceJunction();
			for (int i=0; i < JPath.Num(); i++) {
				if (i > 0) str += ":";
				str += JPath[i]->GetSystemName();
				if (PPath.Num() > i) str += ":" + PPath[i]->GetName();
				else if (PPath.Num() < i) str += "<PATH MISSING>";
			}
			str += "\n";
		}
		//
		if (pj) {
			str += "*PORT_STATE:\n";
			for (int i=0; i<pj->Ports.Num(); i++) {
				const PWR_PowerSegment *seg = pj->Ports[i];
				if (!pj->IsPortEnabled(i)) str += "#";
				switch (seg->GetStatus()) {
					case EPowerSegmentStatus::NORMAL:
						str += "STATUS:NORMAL";
						break;
					case EPowerSegmentStatus::SHORTED:
						str += "STATUS:SHORTED";
						break;
					case EPowerSegmentStatus::OPENED:
						str += "STATUS:OPENED";
						break;
				}
				str += FString::Printf(TEXT(" POWER:%g"), seg->GetPowerLevel());
//				str += FString::Printf(TEXT(" DIRECTION:%g"), seg->GetPowerFlowDirection());
				str += "\n";
			}
		}
	}
#endif //why
	return str;
}

FString ULlamaComponent::MakeHFSString()
{
//why	ASubmarineState *ss = HarnessActor->SubmarineState;
	FString str;// = "\nSUBMARINE STATE:";
#ifdef why //why
	str += "\nLOCATION: " + std::to_string(ss->SubmarineLocation.X) + "," +
	std::to_string(ss->SubmarineLocation.Y) + "," +
	std::to_string(ss->SubmarineLocation.Z);
	str += "\nROTATION: " + std::to_string(ss->SubmarineRotation.Pitch) + "," +
	std::to_string(ss->SubmarineRotation.Yaw) + "," +
	std::to_string(ss->SubmarineRotation.Roll);
	str += "\nVELOCITY: " + std::to_string(ss->Velocity.X) + "," +
	std::to_string(ss->Velocity.Y) + "," +
	std::to_string(ss->Velocity.Z);
	str += "\nLOXLEVEL: " + std::to_string(ss->LOXLevel);
	str += "\nFLASK1LEVEL: " + std::to_string(ss->Flask1Level);
	str += "\nFLASK2LEVEL: " + std::to_string(ss->Flask1Level);
	str += "\nBATTERY1LEVEL: " + std::to_string(ss->Battery1Level);
	str += "\nBATTERY2LEVEL: " + std::to_string(ss->Battery2Level);
	str += "\nALERTLEVEL: " + std::string(TCHAR_TO_UTF8(*ss->AlertLevel));
	str += "\nRUDDERANGLE: " + std::to_string(ss->RudderAngle);
	str += "\nELEVATORANGLE: " + std::to_string(ss->ElevatorAngle);
	str += "\nRIGHTBOWPLANEANGLE: " + std::to_string(ss->RightBowPlanesAngle);
	str += "\nLEFTBOWPLANEANGLE: " + std::to_string(ss->LeftBowPlanesAngle);
	str += "\nFORWARDMBTLEVEL: " + std::to_string(ss->ForwardMBTLevel);
	str += "\nREARMBTLEVEL: " + std::to_string(ss->RearMBTLevel);
	str += "\nFORWARDTBTLEVEL: " + std::to_string(ss->ForwardTBTLevel);
	str += "\nREARTBTLEVEL: " + std::to_string(ss->RearTBTLevel);
#endif //why
	str += "\n\n";
	return str;
}

//#define FULL_SYSTEMS_DESC_IN_CONTEXT
FString ULlamaComponent::MakeSystemsBlock()
{
#ifdef FULL_SYSTEMS_DESC_IN_CONTEXT
	FString str = "\nSUBMARINE SYSTEMS:\n";		// StaticWorldInfo
	for (const ICommandHandler* Handler : HarnessActor->CmdDistributor.CommandHandlers)
	{
		if (Handler)
		{
			const ICH_PowerJunction *pj = Handler->GetAsPowerJunction();
			if (!pj) continue;
			if (pj && pj->IsPowerSource()) str += "**" + Handler->GetSystemName() + " IS A POWER SOURCE\n";
			else str += "**" + Handler->GetSystemName() + "\n";
			FString gd = Handler->GetSystemGuidance();
			if (gd.Len() > 0) str += "*GUIDANCE: " + gd + "\n";
			FString st = Handler->GetSystemStatus();
			if (st.Len() > 0) str += "*STATUS: " + st + "\n";
			TArray<FString> cm = Handler->GetAvailableCommands();
			if (cm.Num() > 0) {
				str += "*AVAILABLE COMMANDS:\n";
				for (FString& s : cm) str += s + "\n";
			}
			TArray<FString> qr = Handler->GetAvailableQueries();
			if (qr.Num() > 0) {
				str += "*AVAILABLE QUERIES:";
				for (FString& s : qr) str += " " + s;
				str += "\n";
			}
			//
			if (pj) {
				str += "*CONNECTS TO:";
				for (int i=0; i<pj->Ports.Num(); i++) {
					str += " " + pj->Ports[i]->GetName();
				}
				str += "\n";
			}
		}
//break;		// TESTING because this is too long
	}
	str += "\nPOWER GRID SEGMENTS:\n";
	for (const PWR_PowerSegment* seg : HarnessActor->CmdDistributor.GetSegments())
	{
		if (seg->GetJunctionA()) str += FString::Printf(TEXT("%s.A->%s.%d\n"), *seg->GetName(), *seg->GetJunctionA()->GetSystemName(), seg->GetPortA());
		else str += FString::Printf(TEXT("%s.A: NULL.%d\n"), *seg->GetName(), seg->GetPortA());
		if (seg->GetJunctionB()) str += FString::Printf(TEXT("%s.B->%s.%d\n"), *seg->GetName(), *seg->GetJunctionB()->GetSystemName(), seg->GetPortB());
		else str += FString::Printf(TEXT("%s.B: NULL.%d\n"), *seg->GetName(), seg->GetPortB());
	
	}
#else // !FULL_SYSTEMS_DESC_IN_CONTEXT
	FString str = "\nSUBMARINE SYSTEMS:";
//why
//	for (const ICommandHandler* Handler : HarnessActor->CmdDistributor.CommandHandlers)
//	{
//		str += " " + Handler->GetSystemName();
//	}
#endif // !FULL_SYSTEMS_DESC_IN_CONTEXT
	return str;
}

FString ULlamaComponent::MakeStatusBlock()
{
#ifdef FULL_SYSTEMS_DESC_IN_CONTEXT
	FString str = "\nSUBMARINE SYSTEMS STATUS:\n";
// & queryEntireState - changes by command
// & per-junction status, power usage and noise - changes by command or damage or auto
// & & GetSystemStatus(): projected battery/LOX depletion times - GetSystemStatus()
// & & the entire path to the power source, to avoid hallucinations
	for (const ICommandHandler* Handler : HarnessActor->CmdDistributor.CommandHandlers)
	{
		if (Handler)
		{
			const ICH_PowerJunction *pj = Handler->GetAsPowerJunction();
			if (!pj) continue;
			str += "**" + Handler->GetSystemName();
			switch (pj->GetStatus()) {
				case EPowerJunctionStatus::NORMAL:
					str += " NORMAL";
					break;
				case EPowerJunctionStatus::DAMAGED50:
					str += " 50%% DAMAGED";
					break;
				case EPowerJunctionStatus::DAMAGED100:
					str += " DAMAGED";
					break;
				case EPowerJunctionStatus::DESTROYED:
					str += " DESTROYED";
					break;
			}
			if (pj->IsPowerSource()) {
				str += FString::Printf(TEXT(" POWER AVAILABLE=%g"), pj->GetPowerAvailable());
			} else {
				str += FString::Printf(TEXT(" POWER USAGE=%g"), pj->GetCurrentPowerUsage());
			}
			str += FString::Printf(TEXT(" NOISE=%g"), pj->GetCurrentNoiseLevel());
			str += "\n";
			// queried state
			TArray<FString> qs = Handler->QueryEntireState();
			if (qs.Num() > 0) {
//						str += Handler->GetSystemName() + " ENTIRE STATE:\n";
				for (FString& s : qs) str += "    " + s + "\n";
			}
			// TODO: status: projected battery/LOX depletion times
			FString sts = Handler->GetSystemStatus();
			if (sts.Len() > 0) {
				str += Handler->GetSystemName() + " STATUS:\n" + sts + "\n";
			}
			// entire path to the power source
			str += Handler->GetSystemName() + " POWER PATH: ";
			const TArray<PWR_PowerSegment*> PPath = pj->GetPathToSourceSegments();
			const TArray<ICH_PowerJunction*> JPath = pj->GetPathToSourceJunction();
			for (int i=0; i < JPath.Num(); i++) {
				if (i > 0) str += ":";
				str += JPath[i]->GetSystemName();
				if (PPath.Num() > i) str += ":" + PPath[i]->GetName();
				else if (PPath.Num() < i) str += "<PATH MISSING>";
			}
			str += "\n";
		}
//break;		// TESTING because this is too long
	}
// & per-segment status and power usage/direction - changes by command or damage or auto
	str += "\nPOWER GRID SEGMENTS STATUS:\n";
	for (const PWR_PowerSegment* seg : HarnessActor->CmdDistributor.GetSegments())
	{
		str += "**" + seg->GetName();
		switch (seg->GetStatus()) {
			case EPowerSegmentStatus::NORMAL:
				str += " NORMAL";
				break;
			case EPowerSegmentStatus::SHORTED:
				str += " SHORTED";
				break;
			case EPowerSegmentStatus::OPENED:
				str += " OPENED";
				break;
		}
		str += FString::Printf(TEXT(" POWER=%g"), seg->GetPowerLevel());
//		str += FString::Printf(TEXT(" DIRECTION=%g"), seg->GetPowerFlowDirection());
		str += "\n";
	}
	return str;
#else // !FULL_SYSTEMS_DESC_IN_CONTEXT
	return "SUBMARINE SYSTEMS STATUS CONTAINED IN SYSTEMS INFO\n";
#endif // !FULL_SYSTEMS_DESC_IN_CONTEXT
}

#pragma mark Tools
// This function is called by the lambda queued from LlamaImpl::toolCallCb
void ULlamaComponent::HandleToolCall_GetSystemInfo(const FString& SystemName)
{
#ifdef why //why
    if (!HarnessActor) {
        SendToolResponseToLlama(TEXT("get_system_info"), TEXT("{\"error\": \"Submarine systems unavailable (HarnessActor null)\"}"));
        return;
    }

    UE_LOG(LogTemp, Log, TEXT("ToolCall: GetSystemInfo - System: '%s'"), *SystemName);

    ICommandHandler* FoundHandler = nullptr;
    for (ICommandHandler* Handler : HarnessActor->CmdDistributor.CommandHandlers) {
        if (Handler && Handler->GetSystemName().Equals(SystemName, ESearchCase::IgnoreCase)) {
            FoundHandler = Handler;
            break;
        }
    }

    if (!FoundHandler) {
        SendToolResponseToLlama(TEXT("get_system_info"), 
            FString::Printf(TEXT("{\"error\": \"System '%s' not found.\"}"), *SystemName));
        return;
    }

	FString str = MakeCommandHandlerString(FoundHandler);

    SendToolResponseToLlama(TEXT("get_system_info"), str);
#endif //why
}

void ULlamaComponent::HandleToolCall_QuerySubmarineSystem(const FString& QueryString)
{
#ifdef why //why
    if (!HarnessActor) {
        SendToolResponseToLlama(TEXT("query_submarine_system"), TEXT("{\"error\": \"Submarine systems unavailable (HarnessActor null)\"}"));
        return;
    }

    FString SystemName;
    FString Aspect;
    FString TempQueryString = QueryString.TrimStartAndEnd();

    if (!TempQueryString.Split(TEXT("."), &SystemName, &Aspect, ESearchCase::IgnoreCase, ESearchDir::FromStart)) {
		SendToolResponseToLlama(TEXT("query_submarine_system"), TEXT("{\"error\": \"Invalid format. Expected 'SYSTEM_NAME.ASPECT'.\"}"));
		return;
    }
    Aspect = Aspect.TrimStartAndEnd().ToUpper(); // Normalize query type

    UE_LOG(LogTemp, Log, TEXT("ToolCall: QuerySubmarineSystem - System: '%s', Aspect: '%s'"), *SystemName, *Aspect);

    ICommandHandler* FoundHandler = nullptr;
    for (ICommandHandler* Handler : HarnessActor->CmdDistributor.CommandHandlers) {
        if (Handler && Handler->GetSystemName().Equals(SystemName, ESearchCase::IgnoreCase)) {
            FoundHandler = Handler;
            break;
        }
    }

    if (!FoundHandler) {
        SendToolResponseToLlama(TEXT("query_submarine_system"), 
            FString::Printf(TEXT("{\"error\": \"System '%s' not found.\"}"), *SystemName));
        return;
    }

	FString str = FoundHandler->QueryState(Aspect);

    SendToolResponseToLlama(TEXT("query_submarine_system"), str);
#endif //why
}

void ULlamaComponent::HandleToolCall_CommandSubmarineSystem(const FString& QueryString)
{
#ifdef why //why
    if (!HarnessActor) {
        SendToolResponseToLlama(TEXT("execute_submarine_command"), TEXT("{\"error\": \"Submarine systems unavailable (HarnessActor null)\"}"));
        return;
    }

	TArray<FString> Cmds;
	QueryString.ParseIntoArray(Cmds, TEXT("\n"), true);

	for (FString TempQueryString: Cmds) {
		FString SystemName;
		FString Aspect;
		FString Verb;
		FString Value;

		if (!TempQueryString.Split(TEXT("."), &SystemName, &Aspect, ESearchCase::IgnoreCase, ESearchDir::FromStart)) {
			SendToolResponseToLlama(TEXT("execute_submarine_command"), TEXT("{\"error\": \"Invalid format. Expected 'SYSTEM_NAME.ASPECT'.\"}"));
			return;
		}
		Aspect = Aspect.TrimStartAndEnd().ToUpper(); // Normalize query type
		// split Aspect into Aspect Verb[ Value]
		if (!TempQueryString.Split(TEXT(" "), &Aspect, &Verb, ESearchCase::IgnoreCase, ESearchDir::FromStart)) {
			SendToolResponseToLlama(TEXT("execute_submarine_command"), TEXT("{\"error\": \"Invalid format. Expected 'SYSTEM_NAME.ASPECT VERB'.\"}"));
			return;
		}
		TempQueryString.Split(TEXT(" "), &Verb, &Value, ESearchCase::IgnoreCase, ESearchDir::FromStart);

		ICommandHandler* FoundHandler = nullptr;
		for (ICommandHandler* Handler : HarnessActor->CmdDistributor.CommandHandlers) {
			if (Handler && Handler->GetSystemName().Equals(SystemName, ESearchCase::IgnoreCase)) {
				FoundHandler = Handler;
				break;
			}
		}

		if (!FoundHandler) {
			SendToolResponseToLlama(TEXT("execute_submarine_command"), 
				FString::Printf(TEXT("{\"error\": \"System '%s' not found.\"}"), *SystemName));
			return;
		}

		ECommandResult r = FoundHandler->HandleCommand(Aspect, Verb, Value);
		if (r == ECommandResult::Blocked) {
			SendToolResponseToLlama(TEXT("execute_submarine_command"), 
				FString::Printf(TEXT("{\"error\": \"Command blocked.\"}")));
			return;
		}
		if (r == ECommandResult::NotHandled) {
			SendToolResponseToLlama(TEXT("execute_submarine_command"), 
				FString::Printf(TEXT("{\"error\": \"Command not handled.\"}")));
			return;
		}
		if (r == ECommandResult::HandledWithError) {
			SendToolResponseToLlama(TEXT("execute_submarine_command"), 
				FString::Printf(TEXT("{\"error\": \"Command handled with error.\"}")));
			return;
		}
		SendToolResponseToLlama(TEXT("execute_submarine_command"), 
			FString::Printf(TEXT("{\"accepted\": \"Command processed.\"}")));
	}
	SendToolResponseToLlama(TEXT("execute_submarine_command"), 
		FString::Printf(TEXT("{\"error\": \"Command completed.\"}")));
#endif //why
}

void ULlamaComponent::ProcessToolCall(FString ToolCallJsonRaw) {
	UE_LOG(LogTemp, Log, TEXT("MainThread: Received ToolCall: %s"), *ToolCallJsonRaw);
	// Parse ToolCallJsonRaw to get tool name and arguments
	TSharedPtr<FJsonObject> JsonObject;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(ToolCallJsonRaw);
	if (FJsonSerializer::Deserialize(Reader, JsonObject) && JsonObject.IsValid()) {
		FString ToolName;
		if (JsonObject->TryGetStringField(TEXT("name"), ToolName)) {
			if (ToolName.Equals(TEXT("get_system_info"))) {
				FString QueryString;
				const TSharedPtr<FJsonObject>* ArgsObject;
				if (JsonObject->TryGetObjectField(TEXT("arguments"), ArgsObject) && 
					(*ArgsObject)->TryGetStringField(TEXT("system_name"), QueryString)) {
					HandleToolCall_GetSystemInfo(QueryString);
				} else {
					SendToolResponseToLlama(ToolName, TEXT("{\"error\": \"Missing or invalid 'system_name' in arguments.\"}"));
				}
			}
			else if (ToolName.Equals(TEXT("execute_submarine_command"))) {
				FString QueryString;
				const TSharedPtr<FJsonObject>* ArgsObject;
				if (JsonObject->TryGetObjectField(TEXT("arguments"), ArgsObject) && 
					(*ArgsObject)->TryGetStringField(TEXT("command_string"), QueryString)) {
					HandleToolCall_CommandSubmarineSystem(QueryString);
				} else {
					SendToolResponseToLlama(ToolName, TEXT("{\"error\": \"Missing or invalid 'command_string' in arguments.\"}"));
				}
			}
			else if (ToolName.Equals(TEXT("query_submarine_system_aspect"))) {
				FString QueryString;
				const TSharedPtr<FJsonObject>* ArgsObject;
				if (JsonObject->TryGetObjectField(TEXT("arguments"), ArgsObject) && 
					(*ArgsObject)->TryGetStringField(TEXT("query_string"), QueryString)) {
					HandleToolCall_QuerySubmarineSystem(QueryString);
				} else {
					SendToolResponseToLlama(ToolName, TEXT("{\"error\": \"Missing or invalid 'query_string' in arguments.\"}"));
				}
			}
			else {
				SendToolResponseToLlama(ToolName, FString::Printf(TEXT("{\"error\": \"Unknown tool name: %s\"}"), *ToolName));
			}
		} else {
			 SendToolResponseToLlama(TEXT("unknown_tool"), TEXT("{\"error\": \"Tool call JSON missing 'name' field.\"}"));
		}
	} else {
		 SendToolResponseToLlama(TEXT("unknown_tool"), TEXT("{\"error\": \"Invalid tool call JSON format.\"}"));
	}
}

void ULlamaComponent::SendToolResponseToLlama(const FString& ToolName, const FString& JsonResponseContent)
{
    // Format according to how your LLM expects tool responses (e.g., Qwen's <|im_start|>tool...)
    // This is the CONTENT that goes inside the tool role tags.
    // The ProcessInputAndGenerate_LlamaThread will wrap it with <|im_start|>tool ... <|im_end|>
    UE_LOG(LogTemp, Log, TEXT("Sending Tool Response for '%s': %s"), *ToolName, *JsonResponseContent);
    ProcessInput(JsonResponseContent, TEXT(""), TEXT("tool"));
}
