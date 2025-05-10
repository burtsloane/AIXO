#pragma once

#include "MSJ_Powered_ExtendRetractOnOff.h"
#include "SubmarineState.h" // Included as constructor uses it

class SS_TowedSonarArray : public MSJ_Powered_ExtendRetractOnOff {
public: 
    SS_TowedSonarArray(const FString& Name, ASubmarineState* InSubState, float X, float Y, float InW=150, float InH=24)
	    : MSJ_Powered_ExtendRetractOnOff(Name, this, InSubState, X, Y, InW, InH) 
    {
		bIsPowerSource = false;
		DefaultPowerUsage = 1.0f; // Base power usage when active
		DefaultNoiseLevel = 0.1f; // Base noise level when active          
	}
	virtual FString GetTypeString() const override { return TEXT("SS_TowedSonarArray"); }

	virtual void RenderUnderlay(RenderingContext& Context) override
	{
		float ext = ExtendRetractOnOffPart->GetExtensionLevel();
		float nearY = Y + H - 4;
		float farY = Y + H - 4 + 100*ext;
		float maxY = Y + H - 4 + 100;
		float R = H/2 - 2;
		FVector2D a, b;
		a.X = X + W*0.8f;				a.Y = Y + H/2;
		b.X = X + W*0.8f;				b.Y = farY - R;
		Context.DrawLine(a, b, FLinearColor::Black, 4.0f);
		
		a.X = X + W*0.8f;				a.Y = farY - R;
        if (a.Y > (maxY - R*5.0f)) a.Y = maxY - R*5.0f;
        Context.DrawCircle(a, R, FLinearColor::White, true);
        Context.DrawCircle(a, R, FLinearColor::Black, false);
        a.Y = farY - R;
        if (a.Y > (maxY - R*3.0f)) a.Y = maxY - R*3.0f;
        Context.DrawCircle(a, R, FLinearColor::White, true);
        Context.DrawCircle(a, R, FLinearColor::Black, false);
        a.Y = farY - R;
        Context.DrawCircle(a, R, FLinearColor::White, true);
        Context.DrawCircle(a, R, FLinearColor::Black, false);
	}
}; 
