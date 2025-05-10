#pragma once

#include "MSJ_Powered_ExtendRetractOnOff.h"
#include "SubmarineState.h" // Included as constructor uses it

class SS_Camera : public MSJ_Powered_ExtendRetractOnOff {
public: 
    SS_Camera(const FString& Name, ASubmarineState* InSubState, float X, float Y, float InW=150, float InH=24)
	    : MSJ_Powered_ExtendRetractOnOff(Name, this, InSubState, X, Y, InW, InH) 
    {
		bIsPowerSource = false;
		DefaultPowerUsage = 1.0f; // Base power usage when active
		DefaultNoiseLevel = 0.1f; // Base noise level when active          
	}
	virtual FString GetTypeString() const override { return TEXT("SS_Camera"); }

	virtual void RenderUnderlay(RenderingContext& Context) override
	{
		float ext = ExtendRetractOnOffPart->GetExtensionLevel();
		float nearX = X + W - 4;
		float farX = X + W - 4 + 60*ext;
		FVector2D a, b, c, d, e, f, e1, f1, g, h;
		a.X = X + W - 4;				a.Y = Y + H/2;
		b.X = farX - (8+16+1);			b.Y = Y + H/2;
		Context.DrawLine(a, b, FLinearColor::Black, 4.0f);
		c.X = farX - (8+16);			c.Y = Y + H/2 - 7;
		d.X = c.X;						d.Y = Y + H/2 + 7;
		e.X = farX - (8);				e.Y = Y + H/2 - 7;
		f.X = e.X;						f.Y = Y + H/2 + 7;
		Context.DrawLine(c, d, FLinearColor::Black, 1.0f);
		Context.DrawLine(c, e, FLinearColor::Black, 1.0f);
		Context.DrawLine(d, f, FLinearColor::Black, 1.0f);
		Context.DrawLine(e, f, FLinearColor::Black, 1.0f);
		e1.X = farX - (8);				e1.Y = Y + H/2 - 4;
		f1.X = e.X;						f1.Y = Y + H/2 + 4;
		g.X = farX;						g.Y = Y + H/2 - 7;
		h.X = g.X;						h.Y = Y + H/2 + 7;
		Context.DrawLine(e1, g, FLinearColor::Black, 1.0f);
		Context.DrawLine(g, h, FLinearColor::Black, 1.0f);
		Context.DrawLine(f1, h, FLinearColor::Black, 1.0f);
	}
}; 
