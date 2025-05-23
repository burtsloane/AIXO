#pragma once

#include "ICommandHandler.h"
#include "PWRJ_MultiSelectJunction.h"
#include "ICH_OnOff.h"
#include "VE_ToggleButton.h"

class MSJ_Powered_OnOff : public PWRJ_MultiSelectJunction
{
protected:
    ASubmarineState* SubState;

    ICH_OnOff *OnOffPart;
    ICH_PowerJunction* Owner = nullptr;

public:
    MSJ_Powered_OnOff(const FString& Name, ICH_PowerJunction* InOwner, ASubmarineState* InSubState, float InX, float InY, float InW=150, float InH=24)
        : PWRJ_MultiSelectJunction(Name, InX, InY, InW, InH), // Pass power/noise to PWR base
          SubState(InSubState),
          OnOffPart(new ICH_OnOff(Name + "_OnOff", InOwner)) // Initialize composed OnOffPart
    {
    	Owner = InOwner;
		DefaultPowerUsage = 5.0f; // Base power usage when active
		DefaultNoiseLevel = 0.1f; // Base noise level when active          
    }

    virtual void UpdateChange() {
    	PostHandleCommand();
    	if (Owner) Owner->PostHandleCommand();
    }

    virtual ECommandResult HandleCommand(const FString& Aspect, const FString& Command, const FString& Value) override
    {
        // Try OnOff part first
        ECommandResult Result = OnOffPart->HandleCommand(Aspect, Command, Value);
        if (Result == ECommandResult::Handled) UpdateChange();
        if (Result != ECommandResult::NotHandled) 
        {
//UE_LOG(LogTemp, Warning, TEXT("MSJ_Powered_OnOff::HandleCommand: %s %s %s returns %d"), *Aspect, *Command, *Value, (int)Result);
            return Result;
        }
        // If not handled, try PWR base part
        Result = PWRJ_MultiSelectJunction::HandleCommand(Aspect, Command, Value);
        if (Result == ECommandResult::Handled) UpdateChange();
//UE_LOG(LogTemp, Warning, TEXT("MSJ_Powered_OnOff::HandleCommand:: %s %s %s returns %d"), *Aspect, *Command, *Value, (int)Result);
        return Result;
    }

    virtual bool CanHandleCommand(const FString& Aspect, const FString& Command, const FString& Value) const override
    {
        return OnOffPart->CanHandleCommand(Aspect, Command, Value) ||
               PWRJ_MultiSelectJunction::CanHandleCommand(Aspect, Command, Value);
    }

    virtual FString QueryState(const FString& Aspect) const override
    {
        // Try OnOff part first
        FString Result = OnOffPart->QueryState(Aspect);
        if (!Result.IsEmpty()) 
        {
            return Result;
        }
        // If not handled, try PWR base part
        return PWRJ_MultiSelectJunction::QueryState(Aspect);
    }

    virtual TArray<FString> QueryEntireState() const override
    {
        // Get state from OnOff part (Category 5)
        TArray<FString> Out = OnOffPart->QueryEntireState(); // Gets ON SET ...
        // Append state from PWR base part (Category 5)
        Out.Append(PWRJ_MultiSelectJunction::QueryEntireState());
        
        Out.Sort();
        return Out;
    }

    virtual TArray<FString> GetAvailableCommands() const override
    {
        // Aggregate commands from parts
        TArray<FString> Out = OnOffPart->GetAvailableCommands();
        Out.Append(PWRJ_MultiSelectJunction::GetAvailableCommands());
        Out.Sort();
        return Out;
    }

    virtual TArray<FString> GetAvailableQueries() const override
    {
        TArray<FString> Queries = OnOffPart->GetAvailableQueries();
        Queries.Append(PWRJ_MultiSelectJunction::GetAvailableQueries());
        Queries.Sort();
        return Queries;
    }

    // Override GetCurrentPowerUsage/GetCurrentNoiseLevel based on OnOffPart state
    virtual float GetCurrentPowerUsage() const override 
    {
        if (IsShutdown()) return 0.0f;
        if (!OnOffPart->IsOn()) return 0.1f;
        return DefaultPowerUsage; 
    }

    virtual float GetCurrentNoiseLevel() const override 
    {
        return (IsShutdown() || !OnOffPart->IsOn()) ? 0.0f : DefaultNoiseLevel;
    }

    virtual void Tick(float InDeltaTime) override
    {
		if (Owner && !Owner->HasPower()) return;
    	OnOffPart->Tick(InDeltaTime);
    }

    virtual bool IsOn() const override { return OnOffPart->IsOn(); }

	virtual void InitializeVisualElements() override
	{
		ICH_PowerJunction::InitializeVisualElements(); // Call base class if it does anything

		// Toggle Button Implementation
		FBox2D ToggleBounds(FVector2D(W-26, 2), FVector2D(W-2, 14)); 
		VisualElements.Add(new VE_ToggleButton(this, 
												 ToggleBounds,
												 TEXT("ON"),      // Query Aspect
												 TEXT("ON SET true"),    // Command On
												 TEXT("ON SET false"),   // Command Off
												 TEXT("true"),			// expected value for "ON"
												 TEXT("false"),			// expected value for "OFF"
												 TEXT("ON"),          // Text On
												 TEXT("OFF")          // Text Off
												 ));
	}
};


