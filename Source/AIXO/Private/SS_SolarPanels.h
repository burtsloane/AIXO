#pragma once

#include "ICommandHandler.h"
#include "ICH_ExtendRetractOnOff.h"
#include "MSJ_Powered_ExtendRetractOnOff.h"
#include "SubmarineState.h"

class SS_SolarPanels : public MSJ_Powered_ExtendRetractOnOff {
protected:
    ASubmarineState* SubState;

public:
    SS_SolarPanels(const FString& Name, ASubmarineState* InSubState, float InX, float InY, float InW=150, float InH=24)
        : MSJ_Powered_ExtendRetractOnOff(Name, this, InSubState, InX, InY, InW, InH),
          SubState(InSubState)
    {
        bIsPowerSource = true;
		DefaultPowerUsage = 5.0f; // Base power usage when active
		DefaultNoiseLevel = 0.02f; // Base noise level when active          
    }
	virtual FString GetTypeString() const override { return TEXT("SS_SolarPanels"); }

/*
    virtual ECommandResult HandleCommand(const FString& Aspect, const FString& Command, const FString& Value) override	// listener tap
    {
		ECommandResult r = MSJ_Powered_ExtendRetractOnOff::HandleCommand(Aspect, Command, Value);
UE_LOG(LogTemp, Warning, TEXT("SS_SolarPanels::HandleCommand: %s %s %s returns %d"), *Aspect, *Command, *Value, (int)r);
		return r;
    }

    virtual TArray<FString> QueryEntireState() const override	// listener tap
    {
        TArray<FString> Out = MSJ_Powered_ExtendRetractOnOff::QueryEntireState();
UE_LOG(LogTemp, Warning, TEXT("SS_SolarPanels::QueryEntireState: returns:"));
for (int i=0; i<Out.Num(); i++) UE_LOG(LogTemp, Warning, TEXT("                                : %s"), *Out[i]);
        return Out;
    }
*/

    // Power usage is complex: it *generates* power but might *use* power for mechanics.
    // ICH_PowerJunction::GetCurrentPowerUsage normally reports *consumption*. 
    // We'll override it to report mechanical usage, generation handled elsewhere?
    virtual float GetCurrentPowerUsage() const override 
    {
        if (IsShutdown()) return 0.0f;
        // Report power used by the deployment mechanism
        return ExtendRetractOnOffPart->IsMoving() ? DefaultPowerUsage : 0.1f;
    }

    // Noise depends on mechanism movement and potentially deployed state.
    virtual float GetCurrentNoiseLevel() const override 
    {
        if (IsShutdown()) return 0.0f;
        // Noise from the mechanism during movement/operation
        float MovementNoise = ExtendRetractOnOffPart->IsMoving() ? DefaultNoiseLevel : 0.0f;
        return MovementNoise; 
    }

    float GetExtensionLevel() const 
    {
        return ExtendRetractOnOffPart->GetExtensionLevel();
    }

    virtual bool IsOn() const override { return ExtendRetractOnOffPart->IsOn(); }

    virtual float GetPowerAvailable() const override {
        if (IsShutdown()) return 0.0f;
        //if (!SubState->SolarSurfaced()) return 0.0f;
    	if (!ExtendRetractOnOffPart->IsExtended()) return 0.1f;
    	return 20.0f;
	}

    virtual void Tick(float InDeltaTime) override
    {
//		if (Owner && !Owner->HasPower()) return;		// allow extension
    	ExtendRetractOnOffPart->Tick(InDeltaTime);
    }

    virtual bool HasPower() const override
    {
        return true;					// TODO: only has power when surfaced
    }

	virtual void RenderUnderlay(RenderingContext& Context) override
	{
		float ext = ExtendRetractOnOffPart->GetExtensionLevel();
		FVector2D a, b, c, d, c1, d1, e, f, e1, f1, g, h;
		a.X = X + W - 4;				a.Y = Y + H/2;
		b.X = X + W - 4 + 20*ext;		b.Y = Y + H/2;
		c.X = X + W - 4 + 20*ext;		c.Y = Y + 0;
		d.X = c.X;						d.Y = Y + H;
		c1.X = X + W - 4 + 30*ext;		c1.Y = Y + 0;
		d1.X = c1.X;					d1.Y = Y + H;
		e.X = X + W - 4 + 40*ext;		e.Y = Y + 0;
		f.X = e.X;						f.Y = Y + H;
		e1.X = X + W - 4 + 50*ext;		e1.Y = Y + 0;
		f1.X = e1.X;					f1.Y = Y + H;
		g.X = X + W - 4 + 60*ext;		g.Y = Y + 0;
		h.X = g.X;						h.Y = Y + H;
		Context.DrawLine(a, b, FLinearColor::Black, 4.0f);
		Context.DrawLine(c, g, FLinearColor::Black, 2.0f);
		Context.DrawLine(d, h, FLinearColor::Black, 2.0f);
		Context.DrawLine(c, d, FLinearColor::Black, 2.0f);
		Context.DrawLine(e, f, FLinearColor::Black, 2.0f);
		Context.DrawLine(c1, d1, FLinearColor::Black, 2.0f);
		Context.DrawLine(e1, f1, FLinearColor::Black, 2.0f);
		Context.DrawLine(g, h, FLinearColor::Black, 2.0f);
	}

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
												 TEXT("OUT"),          // Text On
												 TEXT("IN")          // Text Off
												 ));
	}
};
