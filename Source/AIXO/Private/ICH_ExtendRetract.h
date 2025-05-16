#pragma once

#include "CoreMinimal.h"
#include "ICommandHandler.h"

// ========================================================
// ICH_ExtendRetract - in/moving/out logic with a timer
// ========================================================
enum EExtendState { IN_POS, MOVING, OUT_POS };

class ICH_ExtendRetract : public ICommandHandler
{
protected:
    EExtendState State = EExtendState::IN_POS;
    float Extension = 0.f;
    float MoveTimer = 1.5f;
    float MoveDuration = 1.5f;
    bool bTargetOut = false;
    ICH_PowerJunction *Owner = nullptr;

public:
    ICH_ExtendRetract(const FString& Name, ICH_PowerJunction* InOwner) { SystemName = Name; Owner = InOwner; }

    virtual void UpdateChange() {
    	PostHandleCommand();
    	if (Owner) Owner->PostHandleCommand();
    }

    virtual void Tick(float DeltaTime) override
    {
		if (Owner && !Owner->HasPower()) return;
        if (State == EExtendState::MOVING)
        {
            MoveTimer += DeltaTime;
            if (MoveTimer >= MoveDuration)
            {
                State = bTargetOut ? EExtendState::OUT_POS : EExtendState::IN_POS;
                Extension = bTargetOut ? 1.0f : 0.0f;
                MoveTimer = 0.f;
				UpdateChange();
            } else {
            	if (bTargetOut) {
					Extension = MoveTimer / MoveDuration;
            	} else { 
					Extension = 1 - (MoveTimer / MoveDuration);
            	}
            }
//UE_LOG(LogTemp, Warning, TEXT("ICH_ExtendRetract::Tick %s moved to %.2f"), *GetSystemName(), Extension);
		}
    }

    virtual ECommandResult HandleCommand(const FString& Aspect, const FString& Command, const FString& Value) override
    {
        if (Aspect == "EXTEND")
        {
            if (Command == "SET")
            {
// TODO: checking for power while reading the JSON is causing some commands to fail
//	        	if (Owner && !Owner->HasPower()) return ECommandResult::NotHandled;
                bool bExtend = Value.ToBool();
                if (bExtend && State == EExtendState::OUT_POS) return ECommandResult::Handled;
                if (!bExtend && State == EExtendState::IN_POS) return ECommandResult::Handled;
                {
                    State = EExtendState::MOVING;
                    MoveTimer = 0.f;
                    bTargetOut = bExtend;
					UpdateChange();
                    return ECommandResult::Handled;
                }
            }
        }
        return ECommandResult::NotHandled;
    }

    virtual bool CanHandleCommand(const FString& Aspect, const FString& Command, const FString& Value) const override
    {
        return Aspect == "EXTEND" && Command == "SET";
    }

    virtual FString QueryState(const FString& Aspect) const override
    {
        if (Aspect == "EXTEND")
        {
            switch (State)
            {
                case EExtendState::IN_POS: return "IN";
                case EExtendState::OUT_POS: return "OUT";
                case EExtendState::MOVING: return "MOVING";
            }
        }
        return "";
    }

    virtual TArray<FString> QueryEntireState() const override
    {
        // Represents the target state (OUT=true, IN=false)
        return { FString::Printf(TEXT("EXTEND SET %s"), bTargetOut ? TEXT("true") : TEXT("false")) };
    }

    virtual TArray<FString> GetAvailableCommands() const override
    {
        return { "EXTEND SET <bool>" };
    }

    /** Returns a list of aspects that can be queried. */
    virtual TArray<FString> GetAvailableQueries() const override
    {
        return { "EXTEND" };
    }

    bool IsExtended() const { return State == EExtendState::OUT_POS; }
    bool IsMoving() const { return State == EExtendState::MOVING; }
    float GetExtensionLevel() const {
    	return Extension;
    }
};
