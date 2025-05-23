#pragma once

#include "ICommandHandler.h"
#include "ICH_OpenClose.h"
#include "VE_ToggleButton.h"

class MSJ_Powered_OpenClose : public PWRJ_MultiSelectJunction
{
protected:
    ASubmarineState* SubState;

    ICH_OpenClose *OpenClosePart;
    ICH_PowerJunction* Owner = nullptr;

public:
    MSJ_Powered_OpenClose(const FString& Name, ICH_PowerJunction* InOwner, ASubmarineState* InSubState, float InX, float InY, float InW=150, float InH=24)
        : PWRJ_MultiSelectJunction(Name, InX, InY, InW, InH), // Pass power/noise to PWR base
          SubState(InSubState),
          OpenClosePart(new ICH_OpenClose(Name + "_OpenClose", InOwner)) // Initialize composed OpenClosePart
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
        // Try OpenClose part first
        ECommandResult Result = OpenClosePart->HandleCommand(Aspect, Command, Value);
        if (Result == ECommandResult::Handled) UpdateChange();
        if (Result != ECommandResult::NotHandled) 
        {
            return Result;
        }
        // If not handled, try PWR base part
        Result = PWRJ_MultiSelectJunction::HandleCommand(Aspect, Command, Value);
        if (Result == ECommandResult::Handled) UpdateChange();
        return Result;
    }

    virtual bool CanHandleCommand(const FString& Aspect, const FString& Command, const FString& Value) const override
    {
        return OpenClosePart->CanHandleCommand(Aspect, Command, Value) ||
               PWRJ_MultiSelectJunction::CanHandleCommand(Aspect, Command, Value);
    }

    virtual FString QueryState(const FString& Aspect) const override
    {
        // Try OpenClose part first
        FString Result = OpenClosePart->QueryState(Aspect);
        if (!Result.IsEmpty()) 
        {
            return Result;
        }
        // If not handled, try PWR base part
        return PWRJ_MultiSelectJunction::QueryState(Aspect);
    }

    virtual TArray<FString> QueryEntireState() const override
    {
        // Get state from OpenClose part (Category 5)
        TArray<FString> Out = OpenClosePart->QueryEntireState(); // Gets ON SET ...
        // Append state from PWR base part (Category 5)
        Out.Append(PWRJ_MultiSelectJunction::QueryEntireState());
        
        Out.Sort();
        return Out;
    }

    virtual TArray<FString> GetAvailableCommands() const override
    {
        // Aggregate commands from parts
        TArray<FString> Out = OpenClosePart->GetAvailableCommands();
        Out.Append(PWRJ_MultiSelectJunction::GetAvailableCommands());
        Out.Sort();
        return Out;
    }

    virtual TArray<FString> GetAvailableQueries() const override
    {
        TArray<FString> Queries = OpenClosePart->GetAvailableQueries();
        Queries.Append(PWRJ_MultiSelectJunction::GetAvailableQueries());
        Queries.Sort();
        return Queries;
    }

    // Override GetCurrentPowerUsage/GetCurrentNoiseLevel based on OpenClosePart state
    virtual float GetCurrentPowerUsage() const override 
    {
        // Report generation level as usage for simplicity (or adjust based on power model needs)
        if (IsShutdown()) return 0.0f;
        return (!OpenClosePart->IsMoving()) ? 0.1f : DefaultPowerUsage; 
    }

    virtual float GetCurrentNoiseLevel() const override 
    {
        return (IsShutdown() || !OpenClosePart->IsMoving()) ? 0.0f : DefaultNoiseLevel;
    }

    virtual void Tick(float InDeltaTime) override
    {
		if (Owner && !Owner->HasPower()) return;
    	OpenClosePart->Tick(InDeltaTime);
    }

	virtual void InitializeVisualElements() override
	{
		ICH_PowerJunction::InitializeVisualElements(); // Call base class if it does anything

		// Toggle Button Implementation
		FBox2D ToggleBounds(FVector2D(W-40, 2), FVector2D(W-2, 14)); 
		VisualElements.Add(new VE_ToggleButton(this, 
												 ToggleBounds,
												 TEXT("OPEN"),      // Query Aspect
												 TEXT("OPEN SET true"),    // Command On
												 TEXT("OPEN SET false"),   // Command Off
												 TEXT("OPEN"),			// expected value for "ON"
												 TEXT("CLOSED"),			// expected value for "OFF"
												 TEXT("OPEN"),          // Text On
												 TEXT("CLOSE")          // Text Off
												 ));
	}
};

