#pragma once

#include "CoreMinimal.h"
#include "ICommandHandler.h"
#include "MSJ_Powered_OnOff.h"
#include "VE_ToggleButton.h"

// =============== Subclasses of ICH_OnOff ===============
class SS_Sonar : public MSJ_Powered_OnOff {
public:
	SS_Sonar(const FString& Name, ASubmarineState* InSubState, float X, float Y, float InW=150, float InH=24)
		: MSJ_Powered_OnOff(Name, this, InSubState, X, Y, InW, InH)
	{
		bIsPowerSource = false; 
		DefaultPowerUsage = 1.0f; // Base power usage when active
		DefaultNoiseLevel = 0.1f; // Base noise level when active          
	}
	virtual FString GetTypeString() const override { return TEXT("SS_Sonar"); }
	void RenderLabels(RenderingContext& Context) { return; }

	virtual void RenderBG(RenderingContext& Context) override
	{
        FVector2D MyPosition = GetPosition();
        FLinearColor tc = FLinearColor::Black;
        FLinearColor c = RenderBGGetColor();
		if (bIsSelected) {
			if (((int32)(FPlatformTime::Seconds() / 0.5f)) & 1) {
				c = FLinearColor(0.0f, 0.5f, 0.0f);		// selected color
				tc = FLinearColor::White;
			}
		}
        
        FVector2D Position;
        Position.X = X + W/2;
        Position.Y = Y + H/2;
        Context.DrawCircle(Position, W/2, c, true);
        Context.DrawCircle(Position, W/2, FLinearColor::Black, false); // outline

		if (OnOffPart->IsOn() && HasPower()) {
			double CurrentTime = FPlatformTime::Seconds();
			int32 d = (int32)(CurrentTime * W);
			d = d % (int32)(W*1.5f);
			if (d < W/2) {
				Context.DrawCircle(Position, d, FLinearColor::Black, false); // sweep
			}
		}
	}

    virtual void RenderName(RenderingContext& Context)
    {
        FVector2D Position;
        Position.X = X + W/2 - 22;
        Position.Y = Y + H/2 - 5;
        Context.DrawText(Position, SystemName, FLinearColor::Black);
    }

    virtual bool IsOn() const override { return OnOffPart->IsOn(); }

	virtual void InitializeVisualElements() override
	{
		ICH_PowerJunction::InitializeVisualElements(); // Call base class if it does anything

		// Toggle Button Implementation
		FBox2D ToggleBounds(FVector2D(W/2-12, H/2+10), FVector2D(W/2+12, H/2+22)); 
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
};
