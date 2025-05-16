#pragma once

#include "CoreMinimal.h"
#include "ICommandHandler.h"

// =====================================
// ICH_Actuator - angle-based actuator
// =====================================
class ICH_Actuator : public ICommandHandler
{
protected:
    float CurrentAngle = 0.f;
    float TargetAngle = 0.f;
    float MovementRate = 0.5f; // deg/sec
    bool bIsFouled = false;
    bool bActive = true;
    ICH_PowerJunction* Owner = nullptr;

public:
    ICH_Actuator(const FString& Name, ICH_PowerJunction* InOwner) { SystemName = Name; Owner = InOwner; }

    bool IsActive() const { return bActive; }
    bool IsMoving() const { return !FMath::IsNearlyEqual(CurrentAngle, TargetAngle, 0.1f); }
    float GetCurrentAngle() const { return CurrentAngle; }
    float GetTargetAngle() const { return TargetAngle; }
    void SetTargetAngle(float Angle) { TargetAngle = Angle; }

    virtual void Tick(float DeltaTime) override
    {
		if (Owner && !Owner->HasPower()) return;
        if (!bActive || bIsFouled) return;
        float Delta = FMath::Clamp(TargetAngle - CurrentAngle, -MovementRate * DeltaTime, MovementRate * DeltaTime);
        CurrentAngle += Delta;
    }
    
    virtual void UpdateChange() {
    	PostHandleCommand();
    	if (Owner) Owner->PostHandleCommand();
    }

    virtual ECommandResult HandleCommand(const FString& Aspect, const FString& Command, const FString& Value) override
    {
//UE_LOG(LogTemp, Warning, TEXT("ICH_Actuator::HandleCommand %s %s %s"), *Aspect, *Command, *Value);
        if (Aspect == "ANGLE")
        {
// TODO: checking for power while reading the JSON is causing some commands to fail
//        	if (Owner && !Owner->HasPower()) return ECommandResult::NotHandled;
            if (Command == "SET") {
            	TargetAngle = FCString::Atof(*Value);
            	UpdateChange();
            	return ECommandResult::Handled;
			}
        }
        else if (Aspect == "ACTUATOR")
        {
// TODO: checking for power while reading the JSON is causing some commands to fail
//        	if (Owner && !Owner->HasPower()) return ECommandResult::NotHandled;
            if (Command == "DEACTIVATE") { bActive = false; UpdateChange(); return ECommandResult::Handled; }
            if (Command == "ACTIVATE") { bActive = true; UpdateChange(); return ECommandResult::Handled; }
        }
        return ECommandResult::NotHandled;
    }

    virtual FString QueryState(const FString& Aspect) const override
    {
//if (Aspect == "ANGLE") UE_LOG(LogTemp, Warning, TEXT("ICH_Actuator::QueryState %s returns %s"), *Aspect, *FString::Printf(TEXT("%.2f"), CurrentAngle));
//if (Aspect == "TARGET") UE_LOG(LogTemp, Warning, TEXT("ICH_Actuator::QueryState %s returns %s"), *Aspect, *FString::Printf(TEXT("%.2f"), TargetAngle));
        if (Aspect == "ANGLE") return FString::Printf(TEXT("%.2f"), TargetAngle);
        if (Aspect == "ACTUAL") return FString::Printf(TEXT("%.2f"), CurrentAngle);
        if (Aspect == "ACTUATOR") return bActive ? "ACTIVE" : "INACTIVE";
        return "";
    }

    virtual TArray<FString> QueryEntireState() const override
    {
        return {
            FString::Printf(TEXT("ANGLE SET %.2f"), TargetAngle),
            FString::Printf(TEXT("ACTUATOR %s"), bActive ? TEXT("ACTIVATE") : TEXT("DEACTIVATE"))
        };
    }

    virtual TArray<FString> GetAvailableCommands() const override
    {
        return { "ANGLE SET <float>", "ACTUATOR ACTIVATE/DEACTIVATE" };
    }

    /** Returns a list of aspects that can be queried. */
    virtual TArray<FString> GetAvailableQueries() const override
    {
        return { "ANGLE", "ACTUAL", "ACTUATOR" };
    }

    virtual bool CanHandleCommand(const FString& Aspect, const FString& Command, const FString& Value) const override
    {
        if (Aspect == "ANGLE" && Command == "SET") return true;
        if (Aspect == "ACTUATOR" && (Command == "ACTIVATE" || Command == "DEACTIVATE")) return true;
        return false;
    }
};


