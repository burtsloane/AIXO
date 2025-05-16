#pragma once

#include "ICommandHandler.h"
#include "PWRJ_MultiSelectJunction.h"

// =============== ICH_Pump (new middle class) ===============
class ICH_Pump : public ICommandHandler
{
protected:
    float PumpRate = 0.0f; // -1.0 to 1.0 normalized
    ICH_PowerJunction* Owner = nullptr;

public:
    ICH_Pump(const FString& Name, ICH_PowerJunction* InOwner) { SystemName = Name; Owner = InOwner; }

    virtual void UpdateChange() {
    	PostHandleCommand();
    	if (Owner) Owner->PostHandleCommand();
    }

    virtual ECommandResult HandleCommand(const FString& Aspect, const FString& Command, const FString& Value) override
    {
        if (Aspect == "PUMPRATE") {
// TODO: checking for power while reading the JSON is causing some commands to fail
//        	if (Owner && !Owner->HasPower()) return ECommandResult::NotHandled;
            if (Command == "SET") { PumpRate = FCString::Atof(*Value); UpdateChange(); return ECommandResult::Handled; }
        }
        return ECommandResult::NotHandled;
    }
    
    virtual void SetPumpRate(float InPumpRate) { PumpRate = InPumpRate; }

    virtual FString QueryState(const FString& Aspect) const override
    {
        if (Aspect == "PUMPRATE") return FString::Printf(TEXT("%.2f"), PumpRate);
        return "";
    }

    virtual TArray<FString> QueryEntireState() const override
    {
        return { FString::Printf(TEXT("PUMPRATE SET %.2f"), PumpRate) };
    }

    virtual TArray<FString> GetAvailableCommands() const override
    {
        return { "PUMPRATE SET <float>" };
    }

    /** Returns a list of aspects that can be queried. */
    virtual TArray<FString> GetAvailableQueries() const override
    {
        return { "PUMPRATE" };
    }

    /** Performs per-frame updates (currently none needed for base pump). */
    virtual void Tick(float DeltaTime) override
    {
        // Base pump has no tick logic by itself
    }
    
    virtual float GetPumpRate() { return PumpRate; }

    virtual bool CanHandleCommand(const FString& Aspect, const FString& Command, const FString& Value) const override
    {
        return Aspect == "PUMPRATE" && Command == "SET";
    }
    
    
};

