#pragma once

#include "MSJ_Powered_ExtendRetractOnOff.h"
#include "SubmarineState.h" // Included as constructor uses it

class SS_Antenna : public MSJ_Powered_ExtendRetractOnOff {
public: 
    SS_Antenna(const FString& Name, ASubmarineState* InSubState, float X, float Y, float InW=150, float InH=24)
	    : MSJ_Powered_ExtendRetractOnOff(Name, this, InSubState, X, Y, InW, InH) 
    {
		bIsPowerSource = false;
		DefaultPowerUsage = 1.0f; // Base power usage when active
		DefaultNoiseLevel = 0.1f; // Base noise level when active          
	}
	virtual FString GetTypeString() const override { return TEXT("SS_Antenna"); }

	virtual void RenderUnderlay(RenderingContext& Context) override
	{
		float ext = ExtendRetractOnOffPart->GetExtensionLevel();
		float nearX = X + W - 4;
		float farX = X + W - 4 + 54*ext;
		FVector2D a, b, e1, f1, g, h;
		a.X = X + W - 4;				a.Y = Y + H/2;
		b.X = farX - (16);				b.Y = Y + H/2;
		Context.DrawLine(a, b, FLinearColor::Black, 4.0f);
		e1.X = farX - (16);				e1.Y = Y + H/2 - 0;
		f1.X = e1.X;					f1.Y = Y + H/2 + 0;
		g.X = farX;						g.Y = Y + 0;
		h.X = g.X;						h.Y = Y + H;
		Context.DrawLine(e1, g, FLinearColor::Black, 1.0f);
		Context.DrawLine(g, h, FLinearColor::Black, 1.0f);
		Context.DrawLine(f1, h, FLinearColor::Black, 1.0f);
	}
};
