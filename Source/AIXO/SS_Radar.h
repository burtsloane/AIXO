#pragma once

#include "MSJ_Powered_ExtendRetractOnOff.h"
#include "SubmarineState.h" // Included as constructor uses it

class SS_Radar : public MSJ_Powered_ExtendRetractOnOff {
public: 
    SS_Radar(const FString& Name, ASubmarineState* InSubState, float X, float Y, float InW=150, float InH=24)
	    : MSJ_Powered_ExtendRetractOnOff(Name, this, InSubState, X, Y, InW, InH) 
    {
		bIsPowerSource = false;
		DefaultPowerUsage = 1.0f; // Base power usage when active
		DefaultNoiseLevel = 0.1f; // Base noise level when active          
	}
	virtual FString GetTypeString() const override { return TEXT("SS_Radar"); }

	virtual void RenderUnderlay(RenderingContext& Context) override
	{
		float ext = ExtendRetractOnOffPart->GetExtensionLevel();
		float nearX = X + W - 4;
		float farX = X + W - 4 + 60*ext;
		float R = H/2 - 2;
		FVector2D a, b;
		a.X = X + W - 4;				a.Y = Y + H/2;
		b.X = farX - (2*R);				b.Y = Y + H/2;
		Context.DrawLine(a, b, FLinearColor::Black, 4.0f);
		
		a.X = farX - (R);				a.Y = Y + H/2;
        Context.DrawCircle(a, R, FLinearColor::White, true);
        Context.DrawCircle(a, R, FLinearColor::Black, false);
        b = a;
		b.X -= R*FMath::Sin(FPlatformTime::Seconds() * 1.0f);
		b.Y += R*FMath::Cos(FPlatformTime::Seconds() * 1.0f);
		Context.DrawLine(a, b, FLinearColor::Black, 1.0f);
	}
}; 
