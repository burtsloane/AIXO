#pragma once

#include "ICommandHandler.h"
#include "PWRJ_MultiSelectJunction.h"
#include "SubmarineState.h"

class SS_TorpedoTube : public PWRJ_MultiSelectJunction
{
public:
	enum class ETorpedoTubeState {
		Empty, Loading,
		Loaded, ClosingInner, Flooding, EqualizingSea,
		Ready, OpeningOuter, Firing,
		Done, ClosingOuter, Draining, EqualizingCabin, OpeningInner
	};

	enum class ERAMValvePosition {
		Off, Vent, RAM
	};

protected:
	ASubmarineState* SubState;

	// Internal states
	bool bTorpedoLoaded = false;
	float InnerDoorClosedAmount = 1.0f; // 0.0 = open, 1.0 = closed
	float OuterDoorClosedAmount = 1.0f;
	float WaterLevel = 0.0f;
	float TubePressure = 1.0f;
	bool bRAMCharged = false;
	ETorpedoTubeState TubeState = ETorpedoTubeState::Empty;
	ERAMValvePosition RAMValve = ERAMValvePosition::Off;
	float LoadingProgress = 0.0f;
	float TimeLeftToWait = 0.0f;

	FString StateToString(ETorpedoTubeState State) const
	{
		switch (State)
		{
		case ETorpedoTubeState::Empty: return "EMPTY";
		case ETorpedoTubeState::Loading: return "LOADING";
		case ETorpedoTubeState::Loaded: return "LOADED";
		case ETorpedoTubeState::ClosingInner: return "CLOSING_INNER";
		case ETorpedoTubeState::Flooding: return "FLOODING";
		case ETorpedoTubeState::EqualizingSea: return "EQUALIZING_SEA";
		case ETorpedoTubeState::Ready: return "READY";
		case ETorpedoTubeState::OpeningOuter: return "OPENING_OUTER";
		case ETorpedoTubeState::Firing: return "FIRING";
		case ETorpedoTubeState::Done: return "DONE";
		case ETorpedoTubeState::ClosingOuter: return "CLOSING_OUTER";
		case ETorpedoTubeState::Draining: return "DRAINING";
		case ETorpedoTubeState::EqualizingCabin: return "EQUALIZING_CABIN";
		case ETorpedoTubeState::OpeningInner: return "OPENING_INNER";
		default: return "UNKNOWN";
		}
	}

	FString RAMValveToString(ERAMValvePosition Pos) const
	{
		switch (Pos)
		{
		case ERAMValvePosition::Off: return "OFF";
		case ERAMValvePosition::Vent: return "VENT";
		case ERAMValvePosition::RAM: return "RAM";
		default: return "UNKNOWN";
		}
	}

public:
	SS_TorpedoTube(const FString& Name, ASubmarineState* InSub, float InX, float InY, float InW=150, float InH=24)
		: PWRJ_MultiSelectJunction(Name, InX, InY, InW, InH), SubState(InSub)
	{
    	DefaultPowerUsage = 0.1f;
	}
	virtual FString GetTypeString() const override { return TEXT("SS_TorpedoTube"); }

	virtual void Tick(float DeltaTime) override
	{
		if (!HasPower()) return;

		if (TimeLeftToWait > 0.0f)
		{
			TimeLeftToWait -= DeltaTime;
			return;
		}

		switch (TubeState)
		{
		case ETorpedoTubeState::Loading:
			LoadingProgress += DeltaTime;
			if (LoadingProgress >= 1.0f)
			{
				LoadingProgress = 1.0f;
				bTorpedoLoaded = true;
				TubeState = ETorpedoTubeState::Loaded;
				TimeLeftToWait = 0.5f;
			}
			break;

		case ETorpedoTubeState::ClosingInner:
			InnerDoorClosedAmount += DeltaTime;
			if (InnerDoorClosedAmount >= 1.0f)
			{
				InnerDoorClosedAmount = 1.0f;
				TubeState = ETorpedoTubeState::Flooding;
				TimeLeftToWait = 0.5f;
			}
			break;

		case ETorpedoTubeState::Flooding:
			if (RAMValve == ERAMValvePosition::Vent)
				bRAMCharged = true;
			WaterLevel += DeltaTime * 0.5f;
			if (WaterLevel >= 1.0f)
			{
				WaterLevel = 1.0f;
				TubeState = ETorpedoTubeState::EqualizingSea;
				TimeLeftToWait = 0.5f;
			}
			break;

		case ETorpedoTubeState::EqualizingSea:
			TubePressure = 1.5f;
			TubeState = ETorpedoTubeState::Ready;
			TimeLeftToWait = 0.5f;
			break;

		case ETorpedoTubeState::OpeningOuter:
			OuterDoorClosedAmount -= DeltaTime;
			if (OuterDoorClosedAmount <= 0.0f)
			{
				OuterDoorClosedAmount = 0.0f;
				TubeState = ETorpedoTubeState::Firing;
				TimeLeftToWait = 0.5f;
			}
			break;

		case ETorpedoTubeState::Firing:
			if (RAMValve == ERAMValvePosition::RAM && bRAMCharged)
			{
				bTorpedoLoaded = false;
				bRAMCharged = false;
			}
			TubeState = ETorpedoTubeState::Done;
			TimeLeftToWait = 0.5f;
			break;

		case ETorpedoTubeState::ClosingOuter:
			OuterDoorClosedAmount += DeltaTime;
			if (OuterDoorClosedAmount >= 1.0f)
			{
				OuterDoorClosedAmount = 1.0f;
				TubeState = ETorpedoTubeState::Draining;
				TimeLeftToWait = 0.5f;
			}
			break;

		case ETorpedoTubeState::Draining:
			WaterLevel -= DeltaTime * 0.5f;
			if (WaterLevel <= 0.0f)
			{
				WaterLevel = 0.0f;
				TubeState = ETorpedoTubeState::EqualizingCabin;
				TimeLeftToWait = 0.5f;
			}
			break;

		case ETorpedoTubeState::EqualizingCabin:
			TubePressure = 1.0f;
			TubeState = ETorpedoTubeState::OpeningInner;
			TimeLeftToWait = 0.5f;
			break;

		case ETorpedoTubeState::OpeningInner:
			InnerDoorClosedAmount -= DeltaTime;
			if (InnerDoorClosedAmount <= 0.0f)
			{
				InnerDoorClosedAmount = 0.0f;
				TubeState = ETorpedoTubeState::Empty;
				TimeLeftToWait = 0.5f;
			}
			break;

		default:
			break;
		}
	}

	virtual ECommandResult HandleCommand(const FString& Aspect, const FString& Command, const FString& Value) override
	{
		FString UAspect = Aspect.ToUpper();
		FString UCommand = Command.ToUpper();
		FString UValue = Value.ToUpper();

		if (UAspect == "RAM_VALVE" && UCommand == "SET") {
			if (UValue == "OFF") { RAMValve = ERAMValvePosition::Off; return ECommandResult::Handled; }
			if (UValue == "VENT") { RAMValve = ERAMValvePosition::Vent; return ECommandResult::Handled; }
			if (UValue == "RAM") { RAMValve = ERAMValvePosition::RAM; return ECommandResult::Handled; }
			return ECommandResult::HandledWithError; // Invalid value
		}

		// --- Action Triggers (Guesses - Logic would go in Tick/Helper methods) ---
		if (UAspect == "TUBE" && UCommand == "LOAD") {
			// Initiate loading sequence if TubeState == Empty
			if (TubeState == ETorpedoTubeState::Empty) {
				// InitiateCycle(ETorpedoTubeState::Loading, ...); // Assuming helper
				TubeState = ETorpedoTubeState::Loading; // Simplified guess
				TimeLeftToWait = 1.0f; // Example duration
				return ECommandResult::Handled;
			}
			return ECommandResult::Blocked; // Already loading or not empty
		}
		if (UAspect == "TUBE" && UCommand == "PREPARE_FIRE") {
			// Initiate closing/flooding if TubeState == Loaded
			if (TubeState == ETorpedoTubeState::Loaded) {
				// InitiateCycle(ETorpedoTubeState::ClosingInner, ...); // Assuming helper
				TubeState = ETorpedoTubeState::ClosingInner; // Simplified guess
				TimeLeftToWait = 1.0f; // Example duration
				return ECommandResult::Handled;
			}
			return ECommandResult::Blocked; // Not in Loaded state
		}
		if (UAspect == "TUBE" && UCommand == "FIRE") {
			// Initiate firing if TubeState == Ready (or OpeningOuter?)
			if (TubeState == ETorpedoTubeState::Ready) {
				// InitiateCycle(ETorpedoTubeState::OpeningOuter, ...); // Assuming helper
				TubeState = ETorpedoTubeState::OpeningOuter; // Simplified guess
				TimeLeftToWait = 1.0f; // Example duration
				return ECommandResult::Handled;
			}
			return ECommandResult::Blocked; // Not Ready
		}
		if (UAspect == "TUBE" && UCommand == "RESET") {
			// Initiate closing/draining if TubeState implies fired or ready
			if (TubeState == ETorpedoTubeState::Done || TubeState == ETorpedoTubeState::Firing || TubeState == ETorpedoTubeState::Ready || TubeState == ETorpedoTubeState::OpeningOuter) {
				// InitiateCycle(ETorpedoTubeState::ClosingOuter, ...); // Assuming helper
				TubeState = ETorpedoTubeState::ClosingOuter; // Simplified guess
				TimeLeftToWait = 1.0f; // Example duration
				return ECommandResult::Handled;
			}
			return ECommandResult::Blocked; // Cannot reset from current state
		}
		// --- End Action Triggers ---

		// Delegate to base class if not handled
		return PWRJ_MultiSelectJunction::HandleCommand(Aspect, Command, Value);
	}

	virtual bool CanHandleCommand(const FString& Aspect, const FString& Command, const FString& Value) const override
	{
		FString UAspect = Aspect.ToUpper();
		FString UCommand = Command.ToUpper();

		if (UAspect == "RAM_VALVE" && UCommand == "SET") return true;
		if (UAspect == "TUBE" && (UCommand == "LOAD" || UCommand == "PREPARE_FIRE" || UCommand == "FIRE" || UCommand == "RESET")) return true;

		// Delegate check to base class
		return PWRJ_MultiSelectJunction::CanHandleCommand(Aspect, Command, Value);
	}

	virtual FString QueryState(const FString& Aspect) const override
	{
		FString UAspect = Aspect.ToUpper();
		if (UAspect == "STATE") return StateToString(TubeState);
		if (UAspect == "RAM_VALVE") return RAMValveToString(RAMValve);
		if (UAspect == "LOADED") return bTorpedoLoaded ? TEXT("true") : TEXT("false");
		if (UAspect == "INNER_DOOR") return FString::SanitizeFloat(1.0f - InnerDoorClosedAmount); // % Open
		if (UAspect == "OUTER_DOOR") return FString::SanitizeFloat(1.0f - OuterDoorClosedAmount); // % Open
		if (UAspect == "WATER_LEVEL") return FString::SanitizeFloat(WaterLevel);
		if (UAspect == "PRESSURE") return FString::SanitizeFloat(TubePressure);
		if (UAspect == "LOADING_PROGRESS") return FString::SanitizeFloat(LoadingProgress); // May not be useful

		// Delegate to base class if not handled
		return PWRJ_MultiSelectJunction::QueryState(Aspect);
	}

	virtual TArray<FString> QueryEntireState() const override
	{
		// Get restorable state (Category 5) from base class
		TArray<FString> Out = PWRJ_MultiSelectJunction::QueryEntireState();

		// Add local restorable state (Category 5)
		Out.Add(FString::Printf(TEXT("RAM_VALVE SET %s"), *RAMValveToString(RAMValve)));

		// TubeState, Loaded status, Doors, etc. are Category 3/4 and not directly restorable via SET.
		return Out;
	}

	virtual TArray<FString> GetAvailableCommands() const override
	{
		TArray<FString> Out = PWRJ_MultiSelectJunction::GetAvailableCommands();
		Out.Append({
			TEXT("RAM_VALVE SET <OFF|VENT|RAM>"),
			TEXT("TUBE LOAD"),          // Action
			TEXT("TUBE PREPARE_FIRE"), // Action
			TEXT("TUBE FIRE"),          // Action
			TEXT("TUBE RESET")          // Action
		});
		return Out;
	}

	virtual TArray<FString> GetAvailableQueries() const override
	{
		TArray<FString> Queries = PWRJ_MultiSelectJunction::GetAvailableQueries();
		Queries.Append({
			TEXT("STATE"),
			TEXT("RAM_VALVE"),
			TEXT("LOADED"),
			TEXT("INNER_DOOR"),
			TEXT("OUTER_DOOR"),
			TEXT("WATER_LEVEL"),
			TEXT("PRESSURE"),
			TEXT("LOADING_PROGRESS") // Maybe remove if too internal
		});
		return Queries;
	}
};



