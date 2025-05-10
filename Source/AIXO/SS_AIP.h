#pragma once

#include "ICommandHandler.h"
#include "PWRJ_MultiFeederJunction.h"
#include "ICH_OnOff.h"
class SubmarineState; // Forward declaration

class SS_AIP : public PWRJ_MultiFeederJunction
{
protected:
    ASubmarineState* SubState;
    float PowerOutput = 5.0f; // Constant power output when ON
    float BaseNoiseLevel = 0.1f; // Constant noise level when ON

    ICH_OnOff *OnOffPart;

public:
    SS_AIP(const FString& Name, ASubmarineState* InSubState, float InX, float InY, float InW=150, float InH=24)
        : PWRJ_MultiFeederJunction(Name, InX, InY, InW, InH), // Pass power/noise to PWR base
          SubState(InSubState),
          OnOffPart(new ICH_OnOff(Name + "_OnOff", this)) // Initialize composed OnOffPart
    {
        bIsPowerSource = true;
        PowerOutput = 5.0f;
        BaseNoiseLevel = 0.1f;
        // SystemName, X, Y are set by PWRJ_MultiFeederJunction base
    }
	virtual FString GetTypeString() const override { return TEXT("SS_AIP"); }

    // --- ICommandHandler Overrides: Delegate to OnOffPart or PWR base ---

    virtual ECommandResult HandleCommand(const FString& Aspect, const FString& Command, const FString& Value) override
    {
        // Try OnOff part first
        ECommandResult Result = OnOffPart->HandleCommand(Aspect, Command, Value);
        if (Result != ECommandResult::NotHandled) 
        {
            return Result;
        }
        // If not handled, try PWR base part
        return PWRJ_MultiFeederJunction::HandleCommand(Aspect, Command, Value);
    }

    virtual bool CanHandleCommand(const FString& Aspect, const FString& Command, const FString& Value) const override
    {
        return OnOffPart->CanHandleCommand(Aspect, Command, Value) ||
               PWRJ_MultiFeederJunction::CanHandleCommand(Aspect, Command, Value);
    }

    virtual FString QueryState(const FString& Aspect) const override
    {
        if (!SubState && (Aspect == "H2" || Aspect == "LOX")) return TEXT("0.0"); // Handle null SubState early
        if (Aspect == "H2") return FString::SanitizeFloat(SubState->H2Level);
        if (Aspect == "LOX") return FString::SanitizeFloat(SubState->LOXLevel);
        
        // Try OnOff part first
        FString Result = OnOffPart->QueryState(Aspect);
        if (!Result.IsEmpty()) 
        {
            return Result;
        }
        // If not handled, try PWR base part
        return PWRJ_MultiFeederJunction::QueryState(Aspect);
    }

    virtual TArray<FString> QueryEntireState() const override
    {
        // Get state from OnOff part (Category 5)
        TArray<FString> Out = OnOffPart->QueryEntireState(); // Gets ON SET ...
        // Append state from PWR base part (Category 5)
        Out.Append(PWRJ_MultiFeederJunction::QueryEntireState());
        
        // H2 and LOX are Category 1 (Sensor/State), not restorable Category 5.
        
        Out.Sort();
        return Out;
    }

    virtual TArray<FString> GetAvailableCommands() const override
    {
        // Aggregate commands from parts
        TArray<FString> Out = OnOffPart->GetAvailableCommands();
        Out.Append(PWRJ_MultiFeederJunction::GetAvailableCommands());
        Out.Sort();
        return Out;
    }

    virtual TArray<FString> GetAvailableQueries() const override
    {
        TArray<FString> Queries = OnOffPart->GetAvailableQueries();
        Queries.Append(PWRJ_MultiFeederJunction::GetAvailableQueries());
        Queries.Append({ "H2", "LOX" }); // Add specific queries
        Queries.Sort();
        return Queries;
    }

    // Override GetCurrentPowerUsage/GetCurrentNoiseLevel based on OnOffPart state
    virtual float GetCurrentPowerUsage() const override 
    {
        // Report generation level as usage for simplicity (or adjust based on power model needs)
        if (IsShutdown()) return 0.0f;
        return (!OnOffPart->IsOn()) ? 0.1f : PowerOutput; 
    }

    virtual float GetCurrentNoiseLevel() const override 
    {
        return (IsShutdown() || !OnOffPart->IsOn()) ? 0.0f : BaseNoiseLevel;
    }

    virtual void Tick(float DeltaTime) override
    {
        if (!SubState || !OnOffPart->IsOn()) return; // Check composed part state

        if (SubState->H2Level > 0.0f && SubState->LOXLevel > 0.0f)
        {
            SubState->H2Level = FMath::Clamp(SubState->H2Level - 0.005f * DeltaTime, 0.0f, 1.0f);
            SubState->LOXLevel = FMath::Clamp(SubState->LOXLevel - 0.005f * DeltaTime, 0.0f, 1.0f);
        }
        else
        {
        	SubState->H2Level = 0.0f;
        	SubState->LOXLevel = 0.0f;
            // Need to turn off via the composed part
            OnOffPart->HandleCommand("ON", "SET", "false"); 
            AddToNotificationQueue("AIP SYSTEM OFFLINE: Fuel depleted");
			PostHandleCommand();
        }
    }

    virtual float GetPowerAvailable() const override { return OnOffPart->IsOn()?200.0f:0.0f; }
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
												 TEXT("OFF"),          // Text On
												 TEXT("ON")          // Text Off
												 ));
	}

	const int32 DX = 150 + 16;
	const int32 DY = 40;
	virtual void RenderUnderlay(RenderingContext& Context) override
	{
        FVector2D Position;
        Position.X = X + H/2;
        Position.Y = Y + H/2;
		FVector2D Position2 = Position;
		FVector2D Position3 = Position;
		Position2.X = X-DX;        	Position2.Y -= 30 - DY;
		Position3.X = X-DX;        	Position3.Y += 30 + DY;
		Context.DrawTriangle(Position, Position2, Position3, FLinearColor(0.6f, 1.0f, 0.6f));
    }

	virtual void Render(RenderingContext& Context) override
	{
		PWRJ_MultiFeederJunction::Render(Context);
		//
        FVector2D Position;
		FBox2D r;
		r.Min.X = X-DX - 120;
		r.Min.Y = Y + H/2 - 30 + DY;
		r.Max.X = X-DX;
		r.Max.Y = Y + H/2 + 30 + DY;
		Context.DrawRectangle(r, FLinearColor::White, true);
		FBox2D r2 = r;
		r2.Min.Y += 60-60*SubState->LOXLevel;
		FBox2D r3 = r2;
		r2.Max.X -= 40;
		r3.Min.X += 120 - 40;
		Context.DrawRectangle(r2, FLinearColor(0.6f, 1.0f, 0.6f), true);
		Context.DrawRectangle(r3, FLinearColor(1.0f, 0.6f, 0.6f), true);
		Context.DrawRectangle(r, FLinearColor::Black, false);
		FBox2D r4 = r;
		r4.Min.X += 120 - 40;
		Context.DrawRectangle(r4, FLinearColor::Black, false);
		//
		Position = r.Min;
		Position.Y += 1;
		Position.X += 1;
		Context.DrawText(Position, "LOX", FLinearColor::Black);
		Position.X += 50;
		if (SubState->LOXLevel == 1.0f) Position.X -= 7;
		Context.DrawText(Position, FString::Printf(TEXT("%d%%"), (int)(100*SubState->LOXLevel)), FLinearColor::Black);
		Position = r.Min;
		Position.Y += 1;
		Position.X += 81;
		Context.DrawText(Position, "H2", FLinearColor::Black);
	}
};
