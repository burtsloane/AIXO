#pragma once

#include "ICommandHandler.h"
#include "PWRJ_MultiSelectJunction.h"
#include "ICH_Pump.h"
#include "SubmarineState.h"

class SS_CrossTBTPump : public PWRJ_MultiSelectJunction
{
protected:
    ICH_Pump *PumpPart;
    ASubmarineState* SubState;

public:
    SS_CrossTBTPump(const FString& Name, ASubmarineState* InSubState, float InX, float InY, float InW=150, float InH=24)
        : PWRJ_MultiSelectJunction(Name, InX, InY, InW, InH),
          SubState(InSubState)
    {
    	PumpPart = new ICH_Pump(Name + "_PumpPart", this);
		DefaultPowerUsage = 1.0f; // Base power usage when active
		DefaultNoiseLevel = 0.2f; // Base noise level when active          
    }
	virtual FString GetTypeString() const override { return TEXT("SS_CrossTBTPump"); }

    virtual ECommandResult HandleCommand(const FString& Aspect, const FString& Command, const FString& Value) override
    {
        auto Result = PumpPart->HandleCommand(Aspect, Command, Value);
        if (Result != ECommandResult::NotHandled)
            return Result;
        return PWRJ_MultiSelectJunction::HandleCommand(Aspect, Command, Value);
    }

    virtual void SetPumpRate(float InPumpRate) { PumpPart->SetPumpRate(InPumpRate); }

    virtual bool CanHandleCommand(const FString& Aspect, const FString& Command, const FString& Value) const override
    {
        return PumpPart->CanHandleCommand(Aspect, Command, Value) || PWRJ_MultiSelectJunction::CanHandleCommand(Aspect, Command, Value);
    }

    virtual FString QueryState(const FString& Aspect) const override
    {
        FString Result = PumpPart->QueryState(Aspect);
        return !Result.IsEmpty() ? Result : PWRJ_MultiSelectJunction::QueryState(Aspect);
    }

    virtual TArray<FString> QueryEntireState() const override
    {
        TArray<FString> Out = PumpPart->QueryEntireState();
        Out.Append(PWRJ_MultiSelectJunction::QueryEntireState());
        Out.Sort();
        return Out;
    }

    virtual TArray<FString> GetAvailableCommands() const override
    {
        TArray<FString> Out = PumpPart->GetAvailableCommands();
        Out.Append(PWRJ_MultiSelectJunction::GetAvailableCommands());
        Out.Sort();
        return Out;
    }

    virtual TArray<FString> GetAvailableQueries() const override
    {
        TArray<FString> Queries = PumpPart->GetAvailableQueries();
        Queries.Append(PWRJ_MultiSelectJunction::GetAvailableQueries());
        Queries.Sort();
        return Queries;
    }

    virtual float GetCurrentPowerUsage() const override
    {
		if (IsShutdown()) return 0.0f;
        return (FMath::IsNearlyZero(PumpPart->GetPumpRate())) ? 0.1f : DefaultPowerUsage;
    }

    virtual float GetCurrentNoiseLevel() const override
    {
        return (IsShutdown() || FMath::IsNearlyZero(PumpPart->GetPumpRate())) ? 0.0f : DefaultNoiseLevel;
    }

	void RenderLabels(RenderingContext& Context)
	{
		return;
	}

    virtual void Tick(float DeltaTime) override
    {
        if (!HasPower()) {
        	if (PumpPart->GetPumpRate() != 0) {
        		PumpPart->SetPumpRate(0.0f);
        		PostHandleCommand();
        	}
        	return;
		}
        
        float CurrentPumpRate = PumpPart->GetPumpRate() * 0.1f;
        
        if (FMath::Abs(CurrentPumpRate) > 0.01f && SubState)
        {
        	float delta = CurrentPumpRate * DeltaTime;
            float NewFTBTLevel = SubState->ForwardTBTLevel + delta;
            float NewRTBTLevel = SubState->RearTBTLevel - delta;
//UE_LOG(LogTemp, Warning, TEXT("SS_CrossTBTPump::Tick: %d %d delta=%d"), (int)(10000*SubState->RearTBTLevel), (int)(10000*SubState->ForwardTBTLevel), (int)(10000*delta));

            if (NewFTBTLevel <= 0.0f) 
            {
                PumpPart->SetPumpRate(0.0f);
				if (SubState->ForwardTBTLevel > 0.0f)
					AddToNotificationQueue(FString::Printf(TEXT("%s forward tank %s"), *GetSystemName(), TEXT("empty")));
                NewRTBTLevel += NewFTBTLevel;
//UE_LOG(LogTemp, Warning, TEXT("                 upd: %d %d"), (int)(10000*NewRTBTLevel), (int)(10000*NewFTBTLevel));
                NewFTBTLevel = 0.0f;
				PostHandleCommand();
            }
            if (NewFTBTLevel >= 1.0f) 
            {
                PumpPart->SetPumpRate(0.0f);
				if (SubState->ForwardTBTLevel < 1.0f)
					AddToNotificationQueue(FString::Printf(TEXT("%s forward tank %s"), *GetSystemName(), TEXT("full")));
                NewRTBTLevel += NewFTBTLevel - 1.0f;
//UE_LOG(LogTemp, Warning, TEXT("                 upd: %d %d"), (int)(10000*NewRTBTLevel), (int)(10000*NewFTBTLevel));
                NewFTBTLevel = 1.0f;
				PostHandleCommand();
            }
            if (NewRTBTLevel <= 0.0f) 
            {
                PumpPart->SetPumpRate(0.0f);
				if (SubState->RearTBTLevel > 0.0f)
					AddToNotificationQueue(FString::Printf(TEXT("%s rear tank %s"), *GetSystemName(), TEXT("empty")));
                NewFTBTLevel += NewRTBTLevel;
//UE_LOG(LogTemp, Warning, TEXT("                 upd: %d %d"), (int)(10000*NewRTBTLevel), (int)(10000*NewFTBTLevel));
                NewRTBTLevel = 0.0f;
				PostHandleCommand();
            }
            if (NewRTBTLevel >= 1.0f) 
            {
                PumpPart->SetPumpRate(0.0f);
				if (SubState->RearTBTLevel < 1.0f)
					AddToNotificationQueue(FString::Printf(TEXT("%s rear tank %s"), *GetSystemName(), TEXT("full")));
                NewFTBTLevel += NewRTBTLevel - 1.0f;
//UE_LOG(LogTemp, Warning, TEXT("                 upd: %d %d"), (int)(10000*NewRTBTLevel), (int)(10000*NewFTBTLevel));
                NewRTBTLevel = 1.0f;
				PostHandleCommand();
            }

			SubState->ForwardTBTLevel = NewFTBTLevel;
			SubState->RearTBTLevel = NewRTBTLevel;
//UE_LOG(LogTemp, Warning, TEXT("                 done: %d %d"), (int)(10000*SubState->RearTBTLevel), (int)(10000*SubState->ForwardTBTLevel));
        }
    }

	virtual void InitializeVisualElements() override
	{
		ICH_PowerJunction::InitializeVisualElements(); // Call base class if it does anything

		FBox2D ToggleBounds(FVector2D(W-16, 2), FVector2D(W-2, 14)); 
		VisualElements.Add(new VE_ToggleButton(this, 
												 ToggleBounds,
												 TEXT("PUMPRATE"),      // Query Aspect
												 TEXT("PUMPRATE SET 1"),    // Command On
												 TEXT("PUMPRATE SET 0"),   // Command Off
												 TEXT("1.00"),			// expected value for "ON"
												 TEXT("0.00"),			// expected value for "OFF"
												 TEXT("^"),          // Text On
												 TEXT("^")          // Text Off
												 ));
		FBox2D ToggleBounds2(FVector2D(W-16-18, 2), FVector2D(W-2-18, 14)); 
		VisualElements.Add(new VE_ToggleButton(this, 
												 ToggleBounds2,
												 TEXT("PUMPRATE"),      // Query Aspect
												 TEXT("PUMPRATE SET -1"),    // Command On
												 TEXT("PUMPRATE SET 0"),   // Command Off
												 TEXT("-1.00"),			// expected value for "ON"
												 TEXT("0.00"),			// expected value for "OFF"
												 TEXT("v"),          // Text On
												 TEXT("v")          // Text Off
												 ));
	}

	friend class PID_TrimPitch;
};
