#pragma once

#include "ICommandHandler.h"
#include "PWRJ_MultiSelectJunction.h"
#include "SubmarineState.h"
//#include <string>
//#include <vector>
//#include <algorithm>

// Enumeration for Airlock States
enum class EAirlockState {
    TS_FacingIn,
    TS_FacingOut,
    TS_FacingThrough,
    ClosingInnerDoor,
    GasExchangePressurize,
    Equip,
    Flooding,
    EqualizingToOutside,
    EqualizingToInside,
    OpeningOuterHatch,
    ClosingOuterHatch,
    Draining,
    DeEquip,
    GasExchangeDepressurize,
    OpeningInnerDoor
};

// Airlock System Class
class SS_Airlock : public PWRJ_MultiSelectJunction {
public:
    SS_Airlock(const FString& Name, ASubmarineState* InSubState, float X, float Y, float InW=150, float InH=24)
        : PWRJ_MultiSelectJunction(Name, X, Y, InW, InH),
          SubState(InSubState),
          CurrentState(EAirlockState::TS_FacingIn),
          StateTimer(0.0f) {
			DefaultPowerUsage = 1.5f; // Base power usage when active
			DefaultNoiseLevel = 0.1f; // Base noise level when active          
		  }
	virtual FString GetTypeString() const override { return TEXT("SS_Airlock"); }

    virtual ECommandResult HandleCommand(const FString& Aspect, const FString& Command, const FString& Value) override {
        if (Aspect == "CYCLE" && Command == "START") {
            if (Value == "EXIT")       { InitiateCycle(EAirlockState::ClosingInnerDoor, 1.0f); return ECommandResult::Handled; }
            else if (Value == "ENTER") { InitiateCycle(EAirlockState::ClosingOuterHatch, 1.0f); return ECommandResult::Handled; }
            else if (Value == "QUICKREADY") { InitiateCycle(EAirlockState::Flooding, 1.0f); return ECommandResult::Handled; }
            else if (Value == "QUICKRESET") { InitiateCycle(EAirlockState::Draining, 1.0f); return ECommandResult::Handled; }
            else if (Value == "OPEN") { InitiateCycle(EAirlockState::OpeningOuterHatch, 1.0f); return ECommandResult::Handled; }
            else if (Value == "CLOSE") { InitiateCycle(EAirlockState::ClosingOuterHatch, 1.0f); return ECommandResult::Handled; }
        }
        return PWRJ_MultiSelectJunction::HandleCommand(Aspect, Command, Value);
    }

    virtual bool CanHandleCommand(const FString& Aspect, const FString& Command, const FString& Value) const override {
        if (Aspect == "CYCLE" && Command == "START") {
            return Value == "EXIT" || Value == "ENTER" || Value == "QUICKREADY" || Value == "QUICKRESET" || Value == "OPEN" || Value == "CLOSE";
        }
        return PWRJ_MultiSelectJunction::CanHandleCommand(Aspect, Command, Value);
    }

    virtual FString QueryState(const FString& Aspect) const override {
        if (Aspect == "CYCLE") return FString(StateToString(CurrentState).c_str());
        if (Aspect == "OCCUPANCY") return SubState->Occupied ? TEXT("true") : TEXT("false");
        if (Aspect == "WATERLEVEL") return FString::SanitizeFloat(SubState->WaterLevel);
        if (Aspect == "WATERPRESSURE") return FString::SanitizeFloat(SubState->Pressure);
        if (Aspect == "HATCHOPEN") return FString::SanitizeFloat(SubState->OuterHatchOpen);
        if (Aspect == "DOOROPEN") return FString::SanitizeFloat(SubState->InnerDoorOpen);
        if (Aspect == "POWERLEVEL") return FString::SanitizeFloat(GetCurrentPowerUsage());
        return PWRJ_MultiSelectJunction::QueryState(Aspect);
    }

    virtual TArray<FString> QueryEntireState() const override
    {
        // Get state from the base class (PWRJ_MultiSelectJunction, which includes POWER SET)
        TArray<FString> Out = PWRJ_MultiSelectJunction::QueryEntireState();

        // Airlock state itself is transient and controlled by CYCLE START, 
        // not directly restorable via a simple SET command based on current HandleCommand.
        // Therefore, we don't add a specific line for CurrentState here.
        // Other queryable aspects (OCCUPANCY, WATERLEVEL, etc.) are sensors/derived 
        // state, not directly set command parameters.

        return Out;
    }

    virtual TArray<FString> GetAvailableCommands() const override {
        TArray<FString> Out = PWRJ_MultiSelectJunction::GetAvailableCommands();
        Out.Add("CYCLE START EXIT");
        Out.Add("CYCLE START ENTER");
        Out.Add("CYCLE START QUICKREADY");
        Out.Add("CYCLE START QUICKRESET");
        Out.Add("CYCLE START OPEN");
        Out.Add("CYCLE START CLOSE");
        return Out;
    }

    /** Returns a list of aspects that can be queried. */
    virtual TArray<FString> GetAvailableQueries() const override
    {
        TArray<FString> Queries = PWRJ_MultiSelectJunction::GetAvailableQueries(); // Inherit base queries
        Queries.Append({ 
            "CYCLE", 
            "OCCUPANCY", 
            "WATERLEVEL", 
            "WATERPRESSURE", 
            "HATCHOPEN", 
            "DOOROPEN", 
            "POWERLEVEL"
        });
        return Queries;
    }

    /** Override power usage based on current state. */
    virtual float GetCurrentPowerUsage() const override
    {
        if (IsShutdown()) return 0.0f; // Check base shutdown status
        switch (CurrentState)
        {
            case EAirlockState::OpeningOuterHatch:
            case EAirlockState::ClosingOuterHatch:
            case EAirlockState::OpeningInnerDoor:
            case EAirlockState::ClosingInnerDoor:
            case EAirlockState::EqualizingToOutside:
            case EAirlockState::EqualizingToInside:
            case EAirlockState::Flooding: // Added based on likely power draw
            case EAirlockState::Draining: // Added based on likely power draw
            case EAirlockState::GasExchangePressurize: // Added based on likely power draw
            case EAirlockState::GasExchangeDepressurize: // Added based on likely power draw
                return DefaultPowerUsage; // Use the default passed in constructor
            default:
                return 0.1f;
        }
    }

    /** Override noise level based on current state (assuming noise follows power). */
    virtual float GetCurrentNoiseLevel() const override
    {
        // Simple: noise if power is being used
        return GetCurrentPowerUsage() > 0.0f ? DefaultNoiseLevel : 0.0f;
    }

    void Tick(float DeltaTime) {
        StateTimer -= DeltaTime;

        switch (CurrentState) {
            case EAirlockState::OpeningOuterHatch:
                SubState->OuterHatchOpen = std::min(SubState->OuterHatchOpen + DeltaTime, 1.0f);
                break;
            case EAirlockState::ClosingOuterHatch:
                SubState->OuterHatchOpen = std::max(SubState->OuterHatchOpen - DeltaTime, 0.0f);
                break;
            case EAirlockState::OpeningInnerDoor:
                SubState->InnerDoorOpen = std::min(SubState->InnerDoorOpen + DeltaTime, 1.0f);
                break;
            case EAirlockState::ClosingInnerDoor:
                SubState->InnerDoorOpen = std::max(SubState->InnerDoorOpen - DeltaTime, 0.0f);
                break;
            case EAirlockState::Flooding:
                SubState->WaterLevel = std::min(SubState->WaterLevel + DeltaTime, 1.0f);
                break;
            case EAirlockState::Draining:
                SubState->WaterLevel = std::max(SubState->WaterLevel - DeltaTime, 0.0f);
                break;
            case EAirlockState::EqualizingToOutside:
                SubState->Pressure = std::min(SubState->Pressure + DeltaTime, 1.0f);
                break;
            case EAirlockState::EqualizingToInside:
                SubState->Pressure = std::max(SubState->Pressure - DeltaTime, 0.0f);
                break;
            case EAirlockState::GasExchangePressurize:
                SubState->Pressure = std::min(SubState->Pressure + DeltaTime, 1.0f);
                break;
            case EAirlockState::GasExchangeDepressurize:
                SubState->Pressure = std::max(SubState->Pressure - DeltaTime, 0.0f);
                break;
            default:
                break;
        }

        if (StateTimer <= 0.0f) {
            AdvanceState();
        }
    }

	virtual void RenderUnderlay(RenderingContext& Context) override
	{
        FVector2D Position;
        Position.X = X + W/2;
        Position.Y = Y + H/2;
		FVector2D Position2 = Position;
		FVector2D Position3 = Position;
		Position2.X = X+279+20;        	Position2.Y -= 12;
		Position3.X = X+279+20;        	Position3.Y += 12;
		Context.DrawTriangle(Position, Position2, Position3, FLinearColor(0.85f, 0.85f, 1.0f));
	}

private:
    ASubmarineState* SubState;
    EAirlockState CurrentState;
    float StateTimer;

    void InitiateCycle(EAirlockState InitialState, float Duration) {
        CurrentState = InitialState;
        StateTimer = Duration;
        AdvanceState();
    }

    void AdvanceState() {
        switch (CurrentState) {
            case EAirlockState::ClosingInnerDoor:
                CurrentState = EAirlockState::GasExchangePressurize;
                StateTimer = 2.0f;
                break;
            case EAirlockState::GasExchangePressurize:
                CurrentState = EAirlockState::Equip;
                StateTimer = 1.5f;
                break;
            case EAirlockState::Equip:
                CurrentState = EAirlockState::Flooding;
                StateTimer = 2.5f;
                break;
            case EAirlockState::Flooding:
                CurrentState = EAirlockState::EqualizingToOutside;
                StateTimer = 1.0f;
                break;
            case EAirlockState::EqualizingToOutside:
                CurrentState = EAirlockState::OpeningOuterHatch;
                StateTimer = 1.0f;
                break;
            case EAirlockState::OpeningOuterHatch:
                CurrentState = EAirlockState::TS_FacingOut;
                StateTimer = 0.0f;
                break;
            case EAirlockState::ClosingOuterHatch:
                CurrentState = EAirlockState::Draining;
                StateTimer = 2.0f;
                break;
            case EAirlockState::Draining:
                CurrentState = EAirlockState::DeEquip;
                StateTimer = 1.5f;
                break;
            case EAirlockState::DeEquip:
                CurrentState = EAirlockState::GasExchangeDepressurize;
                StateTimer = 1.0f;
                break;
            case EAirlockState::GasExchangeDepressurize:
                CurrentState = EAirlockState::EqualizingToInside;
                StateTimer = 1.0f;
                break;
            case EAirlockState::EqualizingToInside:
                CurrentState = EAirlockState::OpeningInnerDoor;
                StateTimer = 1.0f;
                break;
            case EAirlockState::OpeningInnerDoor:
                CurrentState = EAirlockState::TS_FacingIn;
                StateTimer = 0.0f;
                break;
            default:
                break;
        }
    }

    std::string StateToString(EAirlockState State) const {
        switch (State) {
            case EAirlockState::TS_FacingIn: return "TS_FacingIn";
            case EAirlockState::TS_FacingOut: return "TS_FacingOut";
            case EAirlockState::TS_FacingThrough: return "TS_FacingThrough";
            default: return "TRANSITIONING";
        }
    }
};
