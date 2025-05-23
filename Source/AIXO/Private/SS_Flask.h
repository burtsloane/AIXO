#pragma once

#include "ICommandHandler.h"
#include "MSJ_Powered_OnOff.h"

class SS_Flask : public MSJ_Powered_OnOff {
public:
	SS_Flask(const FString& Name, ASubmarineState* InSubState, float X, float Y, float InW=120, float InH=32)
	: MSJ_Powered_OnOff(Name, this, InSubState, X, Y, InW, InH) {
		bIsPowerSource = false;
		DefaultPowerUsage = 1.0f; // Base power usage when active
		DefaultNoiseLevel = 5.0f; // Base noise level when active          
	}
	virtual FString GetTypeString() const override { return TEXT("SS_Flask"); }

public:
    virtual float GetLevel()
    {
    	if (SystemName == "RFLASK") return SubState->Flask2Level;
    	return SubState->Flask1Level;
    }

    virtual void SetLevel(float val)
    {
		if (SystemName == "RFLASK") SubState->Flask2Level = val;
		else SubState->Flask1Level = val;
    }

public:
    virtual void Tick(float DeltaTime) override
    {
    	if (!SubState) return;
        if (!OnOffPart->IsOn()) {
        	if (GetLevel() < 0.25f) {
                OnOffPart->HandleCommand("ON", "SET", "true");		// TODO: this is very bad for silence
	            AddToNotificationQueue(SystemName + " ONLINE: Flask below 25%");
                UpdateChange();
        	}
        	return;
        }

        float oldval1 = GetLevel();
        SetLevel(FMath::Clamp(GetLevel() + 0.05f * DeltaTime, 0.0f, 1.0f));

		if (oldval1 == GetLevel()) {
            // Need to turn off via the composed part
            OnOffPart->HandleCommand("ON", "SET", "false"); 
            AddToNotificationQueue(SystemName + " OFFLINE: Flask full");
			UpdateChange();
        }
    }

	virtual void Render(RenderingContext& Context) override
	{
		MSJ_Powered_OnOff::Render(Context);
		//
        FBox2D bar;
        bar.Min.X = X+8;
        bar.Min.Y = Y+17;
        bar.Max.X = bar.Min.X + (W-2*8)*GetLevel();
        bar.Max.Y = bar.Min.Y+12;
        Context.DrawRectangle(bar, FLinearColor::Green, true);
        bar.Min.X = X+8;
        bar.Min.Y = Y+17;
        bar.Max.X = bar.Min.X-2*8 + W;
        bar.Max.Y = bar.Min.Y+12;
        Context.DrawRectangle(bar, FLinearColor::Black, false);
    }

	virtual void RenderUnderlay(RenderingContext& Context) override
	{
return;		// old
		int32 erdy = 80;
		int32 fH = 32;
		int32 fW = W;
		FVector2D f, f2, t;
		f.X = X + 12;
		f.Y = Y - (281 + 30) + erdy;
		t.X = f.X;
		t.Y = Y + fH/2;
		f2 = f;
		f2.Y = Y + (297 + 50) + erdy;
		FVector2D f1 = f;
		f1.Y += fH/2;
		if (OnOffPart->IsOn())
		{
			float d = 0.0f;
			d += 0.25f*FMath::Sin(FPlatformTime::Seconds() * 8.0f);
			if (d < 0) d = 0;
			Context.DrawLine(f1, t, FLinearColor(d, 1.0f, d), 12.0f);
			Context.DrawLine(f2, t, FLinearColor(d, 1.0f, d), 12.0f);
		}
		else
		{
			Context.DrawLine(f1, t, FLinearColor(0.6f, 1.0f, 0.6f), 6.0f);
			Context.DrawLine(f2, t, FLinearColor(0.6f, 1.0f, 0.6f), 6.0f);
		}
		//
		FVector2D a, b;
		a.X = X + 75;
		a.Y = f.Y + fH/2;
		b.X = X + 75;
		b.Y = f.Y + fH + 20;
		Context.DrawLine(a, b, FLinearColor(0.5f, 1.0f, 0.5f), 6.0f);
		//
		a.X = X + 75;
		a.Y = f2.Y - 20;
		b.X = X + 75;
		b.Y = f2.Y + fH/2;
		Context.DrawLine(a, b, FLinearColor(0.5f, 1.0f, 0.5f), 6.0f);
	}

	void RenderLabels(RenderingContext& Context)
	{
		return;
	}
};
