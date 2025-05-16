#pragma once

#include "ICommandHandler.h"
#include "ICH_ExtendRetractOnOff.h"

class MSJ_Powered_ExtendRetractOnOff : public PWRJ_MultiSelectJunction
{
protected:
    ASubmarineState* SubState;

    ICH_ExtendRetractOnOff *ExtendRetractOnOffPart;
    ICH_PowerJunction* Owner = nullptr;

public:
    MSJ_Powered_ExtendRetractOnOff(const FString& Name, ICH_PowerJunction* InOwner, ASubmarineState* InSubState, float InX, float InY, float InW=150, float InH=24)
        : PWRJ_MultiSelectJunction(Name, InX, InY, InW, InH), // Pass power/noise to PWR base
          SubState(InSubState),
          ExtendRetractOnOffPart(new ICH_ExtendRetractOnOff(Name + "_ExtendRetractOnOff", InOwner)) // Initialize composed ExtendRetractOnOffPart
    {
        Owner = InOwner;
        ExtendRetractOnOffPart->HandleCommand("ON", "SET", "true");
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
        ECommandResult Result = ExtendRetractOnOffPart->HandleCommand(Aspect, Command, Value);
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
        return ExtendRetractOnOffPart->CanHandleCommand(Aspect, Command, Value) ||
               PWRJ_MultiSelectJunction::CanHandleCommand(Aspect, Command, Value);
    }

    virtual FString QueryState(const FString& Aspect) const override
    {
        // Try OpenClose part first
        FString Result = ExtendRetractOnOffPart->QueryState(Aspect);
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
        TArray<FString> Out = ExtendRetractOnOffPart->QueryEntireState(); // Gets ON SET ...
        // Append state from PWR base part (Category 5)
        Out.Append(PWRJ_MultiSelectJunction::QueryEntireState());
        
        Out.Sort();
        return Out;
    }

    virtual TArray<FString> GetAvailableCommands() const override
    {
        // Aggregate commands from parts
        TArray<FString> Out = ExtendRetractOnOffPart->GetAvailableCommands();
        Out.Append(PWRJ_MultiSelectJunction::GetAvailableCommands());
        Out.Sort();
        return Out;
    }

    virtual TArray<FString> GetAvailableQueries() const override
    {
        TArray<FString> Queries = ExtendRetractOnOffPart->GetAvailableQueries();
        Queries.Append(PWRJ_MultiSelectJunction::GetAvailableQueries());
        Queries.Sort();
        return Queries;
    }

    // Override GetCurrentPowerUsage/GetCurrentNoiseLevel based on ExtendRetractOnOffPart state
    virtual float GetCurrentPowerUsage() const override 
    {
        // Report generation level as usage for simplicity (or adjust based on power model needs)
        if (IsShutdown()) return 0.0f;
        if (ExtendRetractOnOffPart->IsMoving()) return 1.0f;
        if (!ExtendRetractOnOffPart->IsExtended()) return 0.1f;
        return DefaultPowerUsage; 
    }

    virtual float GetCurrentNoiseLevel() const override 
    {
        if (IsShutdown()) return 0.0f;
        if (!ExtendRetractOnOffPart->IsExtended()) return 0.0f;
        if (!ExtendRetractOnOffPart->IsMoving()) return 0.1f;
        return DefaultNoiseLevel;
    }

    virtual void Tick(float InDeltaTime) override
    {
		if (Owner && !Owner->HasPower()) return;
    	ExtendRetractOnOffPart->Tick(InDeltaTime);
    }

    virtual bool IsOn() const override { return ExtendRetractOnOffPart->IsOn(); }
    bool IsExtended() const { return ExtendRetractOnOffPart->IsExtended(); }
    bool IsMoving() const { return ExtendRetractOnOffPart->IsMoving(); }

	virtual void InitializeVisualElements() override
	{
		ICH_PowerJunction::InitializeVisualElements(); // Call base class if it does anything

		// Toggle Button Implementation
		FBox2D ToggleBounds(FVector2D(W-29, 2), FVector2D(W-2, 14)); 
		VisualElements.Add(new VE_ToggleButton(this, 
												 ToggleBounds,
												 TEXT("EXTEND"),      // Query Aspect
												 TEXT("EXTEND SET true"),    // Command On
												 TEXT("EXTEND SET false"),   // Command Off
												 TEXT("OUT"),			// expected value for "ON"
												 TEXT("IN"),			// expected value for "OFF"
												 TEXT("IN"),          // Text On
												 TEXT("OUT")          // Text Off
												 ));
    }

	void RenderLabels(RenderingContext& Context) { return; }

//	virtual void RenderUnderlay(RenderingContext& Context) override
//	{
//		FVector2D a, b;
//		a.X = X + W - 4;
//		a.Y = Y + H/2;
//		b.X = X + W - 4 + 60*ExtendRetractOnOffPart->GetExtensionLevel();
//		b.Y = Y + H/2;
//		Context.DrawLine(a, b, FLinearColor::Black, 4.0f);
//	}
};
