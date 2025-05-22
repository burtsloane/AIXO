#pragma once

#include "CoreMinimal.h"
#include "ICommandHandler.h"
#include "ICH_PowerJunction.h"

// ==========================================
// ICH_OpenClose - variant for vents/hatches
// ==========================================
enum class EOpenState { CLOSED, MOVING, OPEN };

class ICH_OpenClose : public ICommandHandler
{
protected:
    EOpenState State = EOpenState::CLOSED;
    float MoveTimer = 0.f;
    bool bTargetOpen = false;
    ICH_PowerJunction *Owner = nullptr;

public:
    float MoveDuration = 1.5f;

public:
    ICH_OpenClose(const FString& Name, ICH_PowerJunction* InOwner) { SystemName = Name; Owner = InOwner; }

    virtual void UpdateChange() {
    	PostHandleCommand();
    	if (Owner) Owner->PostHandleCommand();
    }

    virtual void Tick(float DeltaTime) override
    {
    	if (Owner && !Owner->HasPower()) {
	        if (State == EOpenState::MOVING) {
	        	if (MoveTimer == 0) State = EOpenState::CLOSED;
	        	if (MoveTimer == MoveDuration) State = EOpenState::OPEN;
	        }
    		return;
		}
        if (State == EOpenState::MOVING)
        {
        	if (bTargetOpen) {
				MoveTimer += DeltaTime;
				if (MoveTimer >= MoveDuration)
				{
					MoveTimer = MoveDuration;
					State = EOpenState::OPEN;
					UpdateChange();
				}
        	} else {
				MoveTimer -= DeltaTime;
				if (MoveTimer <= 0)
				{
					MoveTimer = 0;
					State = EOpenState::CLOSED;
					UpdateChange();
				}
        	}
        }
    }

    virtual ECommandResult HandleCommand(const FString& Aspect, const FString& Command, const FString& Value) override
    {
        if (Aspect == "OPEN")
        {
            if (Command == "SET") {
// TODO: checking for power while reading the JSON is causing some commands to fail
//	        	if (Owner && !Owner->HasPower()) return ECommandResult::NotHandled;
                bool bOpen = Value.ToBool();
                if (bOpen && State == EOpenState::OPEN) return ECommandResult::Handled;
                if (!bOpen && State == EOpenState::CLOSED) return ECommandResult::Handled;
                {
		        	if (bOpen) {
						bTargetOpen = true;
						State = EOpenState::MOVING;
		        	} else {
						bTargetOpen = false;
						State = EOpenState::MOVING;
		        	}
                    UpdateChange();
                    return ECommandResult::Handled;
                }
            }
        }
        return ECommandResult::NotHandled;
    }

    virtual bool CanHandleCommand(const FString& Aspect, const FString& Command, const FString& Value) const override
    {
        return Aspect == "OPEN";
    }

    virtual FString QueryState(const FString& Aspect) const override
    {
        if (Aspect == "OPEN") {
            switch (State)
            {
                case EOpenState::CLOSED: return "CLOSED";
                case EOpenState::OPEN: return "OPEN";
                case EOpenState::MOVING: return "MOVING";
            }
        }
        return "";
    }

    virtual TArray<FString> QueryEntireState() const override
    {
        // Returns the target state (OPEN=true, CLOSED=false)
        return {
            FString::Printf(TEXT("OPEN SET %s"), bTargetOpen ? TEXT("true") : TEXT("false"))
        };
    }

    virtual TArray<FString> GetAvailableCommands() const override
    {
        return { "OPEN SET <bool>" };
    }

    /** Returns a list of aspects that can be queried. */
    virtual TArray<FString> GetAvailableQueries() const override
    {
        return { "OPEN" };
    }

    bool IsOpen() const { return State == EOpenState::OPEN; }
    bool IsMoving() const { return State == EOpenState::MOVING; }
    float GetMovementAmount() {
    	return MoveTimer / MoveDuration;
    }
};
