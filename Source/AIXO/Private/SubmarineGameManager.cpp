#include "SubmarineGameManager.h"
#include "CommandDistributor.h"
#include "ICH_PowerJunction.h"
#include "VisualTestHarnessActor.h"
#include "CommandHandlerStringUtils.h"

//#define FULL_SYSTEMS_DESC_IN_CONTEXT

ASubmarineGameManager::ASubmarineGameManager()
{
    PrimaryActorTick.bCanEverTick = false;
}

void ASubmarineGameManager::BeginPlay()
{
    Super::BeginPlay();
}

FString ASubmarineGameManager::GetSystemsContextBlock() const
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
	for (const ICommandHandler* Handler : HarnessActor->CmdDistributor.CommandHandlers)
	{
		str += " " + Handler->GetSystemName();
	}
#endif // !FULL_SYSTEMS_DESC_IN_CONTEXT
	return str;
//    if (!CmdDistributor)
//    {
//        return TEXT("// STUBBED OUT: Command distributor not initialized");
//    }
//
//    FString str = TEXT("\nSUBMARINE SYSTEMS:\n");
//    
//    // List all systems and their basic info
//    for (const ICommandHandler* Handler : CmdDistributor->CommandHandlers)
//    {
//        if (!Handler) continue;
//
//        const ICH_PowerJunction* pj = Handler->GetAsPowerJunction();
//        if (!pj) continue;
//
//        // System name and power source status
//        if (pj->IsPowerSource()) 
//            str += TEXT("**") + Handler->GetSystemName() + TEXT(" IS A POWER SOURCE\n");
//        else 
//            str += TEXT("**") + Handler->GetSystemName() + TEXT("\n");
//
//        // System guidance and status
//        FString gd = Handler->GetSystemGuidance();
//        if (!gd.IsEmpty()) 
//            str += TEXT("*GUIDANCE: ") + gd + TEXT("\n");
//
//        FString st = Handler->GetSystemStatus();
//        if (!st.IsEmpty()) 
//            str += TEXT("*STATUS: ") + st + TEXT("\n");
//
//        // Available commands
//        TArray<FString> cm = Handler->GetAvailableCommands();
//        if (cm.Num() > 0)
//        {
//            str += TEXT("*AVAILABLE COMMANDS:\n");
//            for (const FString& cmd : cm) 
//                str += cmd + TEXT("\n");
//        }
//
//        // Available queries
//        TArray<FString> qr = Handler->GetAvailableQueries();
//        if (qr.Num() > 0)
//        {
//            str += TEXT("*AVAILABLE QUERIES:");
//            for (const FString& q : qr) 
//                str += TEXT(" ") + q;
//            str += TEXT("\n");
//        }
//
//        // Power connections
//        if (pj)
//        {
//            str += TEXT("*CONNECTS TO:");
//            for (int i = 0; i < pj->Ports.Num(); i++)
//            {
//                str += TEXT(" ") + pj->Ports[i]->GetName();
//            }
//            str += TEXT("\n");
//        }
//    }
//
//    // Add power grid segments
//    str += TEXT("\nPOWER GRID SEGMENTS:\n");
//    for (const PWR_PowerSegment* seg : CmdDistributor->GetSegments())
//    {
//        if (seg->GetJunctionA()) 
//            str += FString::Printf(TEXT("%s.A->%s.%d\n"), *seg->GetName(), *seg->GetJunctionA()->GetSystemName(), seg->GetPortA());
//        else 
//            str += FString::Printf(TEXT("%s.A: NULL.%d\n"), *seg->GetName(), seg->GetPortA());
//
//        if (seg->GetJunctionB()) 
//            str += FString::Printf(TEXT("%s.B->%s.%d\n"), *seg->GetName(), *seg->GetJunctionB()->GetSystemName(), seg->GetPortB());
//        else 
//            str += FString::Printf(TEXT("%s.B: NULL.%d\n"), *seg->GetName(), seg->GetPortB());
//    }
//
//    return str;
}

FString ASubmarineGameManager::GetStaticWorldInfoBlock() const
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
//    if (!CmdDistributor || !MissionState)
//    {
//        return TEXT("// STUBBED OUT: Required systems not initialized");
//    }
//
//    FString str = TEXT("\nSTATIC WORLD INFORMATION:\n");
//    
//    // Add mission SOPs
//    str += TEXT("*MISSION STANDING OPERATING PROCEDURES:\n");
//    str += MissionState->GetMissionSOPs() + TEXT("\n");
//
//    // Add grid topology
//    str += TEXT("*GRID TOPOLOGY:\n");
//    str += PowerGrid->GetGridTopology() + TEXT("\n");
//
//    // Add system capabilities
//    str += TEXT("*SYSTEM CAPABILITIES:\n");
//    for (const ICommandHandler* Handler : CmdDistributor->CommandHandlers)
//    {
//        if (!Handler) continue;
//        str += Handler->GetSystemCapabilities() + TEXT("\n");
//    }
//
//    return str;
}

FString ASubmarineGameManager::GetLowFrequencyStateBlock() const
{
	ASubmarineState *ss = HarnessActor->SubmarineState;
	std::string str;// = "\nSUBMARINE STATE:";
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
	str += "\n\n";
	return UTF8_TO_TCHAR(str.c_str());
//    if (!MissionState)
//    {
//        return TEXT("// STUBBED OUT: Mission state not initialized");
//    }
//
//    FString str = TEXT("\nMISSION STATE:\n");
//    
//    // Add mission phase
//    str += TEXT("*MISSION PHASE: ") + MissionState->GetCurrentPhase() + TEXT("\n");
//    
//    // Add geopolitical updates
//    str += TEXT("*GEOPOLITICAL UPDATES:\n");
//    str += MissionState->GetGeopoliticalUpdates() + TEXT("\n");
//    
//    // Add mission objectives
//    str += TEXT("*CURRENT OBJECTIVES:\n");
//    str += MissionState->GetCurrentObjectives() + TEXT("\n");
//
//    return str;
}

FString ASubmarineGameManager::HandleGetSystemInfo(const FString& QueryString)
{
    ICommandHandler* Handler = FindHandlerByName(QueryString);
    if (!Handler)
    {
        return FormatJsonResponse(FString::Printf(TEXT("System '%s' not found"), *QueryString));
    }

    return AIXO::CommandHandlerStringUtils::MakeCommandHandlerString(Handler);
}

FString ASubmarineGameManager::HandleCommandSubmarineSystem(const FString& CommandString)
{
    TArray<FString> Commands;
    CommandString.ParseIntoArray(Commands, TEXT("\n"), true);

    FString Response;
    for (const FString& Cmd : Commands)
    {
        FString SystemName, Aspect, Verb, Value;
        
        // Parse command format: SYSTEM_NAME.ASPECT VERB [VALUE]
        if (!Cmd.Split(TEXT("."), &SystemName, &Aspect, ESearchCase::IgnoreCase, ESearchDir::FromStart))
        {
            return FormatJsonResponse(TEXT("Invalid format. Expected 'SYSTEM_NAME.ASPECT VERB [VALUE]'"));
        }

        Aspect = Aspect.TrimStartAndEnd().ToUpper();
        if (!Aspect.Split(TEXT(" "), &Aspect, &Verb, ESearchCase::IgnoreCase, ESearchDir::FromStart))
        {
            return FormatJsonResponse(TEXT("Invalid format. Expected 'SYSTEM_NAME.ASPECT VERB [VALUE]'"));
        }

        Verb.Split(TEXT(" "), &Verb, &Value, ESearchCase::IgnoreCase, ESearchDir::FromStart);

        ICommandHandler* Handler = FindHandlerByName(SystemName);
        if (!Handler)
        {
            return FormatJsonResponse(FString::Printf(TEXT("System '%s' not found"), *SystemName));
        }

        ECommandResult Result = Handler->HandleCommand(Aspect, Verb, Value);
        switch (Result)
        {
            case ECommandResult::Blocked:
                return FormatJsonResponse(TEXT("Command blocked"));
            case ECommandResult::NotHandled:
                return FormatJsonResponse(TEXT("Command not handled"));
            case ECommandResult::HandledWithError:
                return FormatJsonResponse(TEXT("Command handled with error"));
            case ECommandResult::Handled:
                Response = FormatJsonResponse(TEXT("accepted"), TEXT("Command processed"));
                break;
        }
    }

    return Response.IsEmpty() ? FormatJsonResponse(TEXT("accepted"), TEXT("All commands completed")) : Response;
}

FString ASubmarineGameManager::HandleQuerySubmarineSystem(const FString& QueryString)
{
    FString SystemName, Aspect;
    if (!QueryString.Split(TEXT("."), &SystemName, &Aspect, ESearchCase::IgnoreCase, ESearchDir::FromStart))
    {
        return FormatJsonResponse(TEXT("Invalid format. Expected 'SYSTEM_NAME.ASPECT'"));
    }

    Aspect = Aspect.TrimStartAndEnd().ToUpper();

    ICommandHandler* Handler = FindHandlerByName(SystemName);
    if (!Handler)
    {
        return FormatJsonResponse(FString::Printf(TEXT("System '%s' not found"), *SystemName));
    }

    FString Response = Handler->QueryState(Aspect);
    return Response.IsEmpty() ? FormatJsonResponse(TEXT("No response from system")) : Response;
}

ICommandHandler* ASubmarineGameManager::FindHandlerByName(const FString& SystemName) const
{
    for (ICommandHandler* Handler : HarnessActor->CmdDistributor.CommandHandlers)
    {
        if (Handler && Handler->GetSystemName().Equals(SystemName, ESearchCase::IgnoreCase))
        {
            return Handler;
        }
    }
    return nullptr;
}

FString ASubmarineGameManager::FormatJsonResponse(const FString& ErrorMessage) const
{
    return FString::Printf(TEXT("{\"error\": \"%s\"}"), *ErrorMessage);
}

FString ASubmarineGameManager::FormatJsonResponse(const FString& Key, const FString& Value) const
{
    return FString::Printf(TEXT("{\"%s\": \"%s\"}"), *Key, *Value);
} 
