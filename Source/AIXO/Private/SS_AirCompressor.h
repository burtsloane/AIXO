#pragma once

#include "ICommandHandler.h"
#include "MSJ_Powered_OnOff.h"

// =============== Subclasses of ICH_OnOff ===============
class SS_AirCompressor : public MSJ_Powered_OnOff {
public:
	SS_AirCompressor(const FString& Name, ASubmarineState* InSubState, float X, float Y, float InW=150, float InH=24)
	: MSJ_Powered_OnOff(Name, this, InSubState, X, Y, InW, InH) {
		bIsPowerSource = false;
		DefaultPowerUsage = 1.0f; // Base power usage when active
		DefaultNoiseLevel = 0.1f; // Base noise level when active          
	}
	virtual FString GetTypeString() const override { return TEXT("SS_AirCompressor"); }
public:
    virtual void Tick(float DeltaTime) override
    {
    	if (!SubState) return;
        if (!OnOffPart->IsOn()) {
        	if ((SubState->Flask1Level < 0.25f) || (SubState->Flask2Level < 0.25f)) {
                OnOffPart->HandleCommand("ON", "SET", "true");		// TODO: this is very bad for silence
	            AddToNotificationQueue("AIR COMPRESSOR ONLINE: Flasks below 25%");
                UpdateChange();
        	}
        	return;
        }

        float oldval1 = SubState->Flask1Level;
        SubState->Flask1Level = FMath::Clamp(SubState->Flask1Level + 0.05f * DeltaTime, 0.0f, 1.0f);
        float oldval2 = SubState->Flask2Level;
        SubState->Flask2Level = FMath::Clamp(SubState->Flask2Level + 0.05f * DeltaTime, 0.0f, 1.0f);

		if ((oldval1 == SubState->Flask1Level) && (oldval2 == SubState->Flask2Level)) {
            // Need to turn off via the composed part
            OnOffPart->HandleCommand("ON", "SET", "false"); 
            AddToNotificationQueue("AIR COMPRESSOR OFFLINE: Flasks full");
			UpdateChange();
        }
    }

	virtual void Render(RenderingContext& Context) override
	{
		MSJ_Powered_OnOff::Render(Context);
return;		// old
		//
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
		//
        FBox2D border;
        FBox2D inner;
        FBox2D bar;
        FVector2D Position;
        inner.Min.X = X;
        inner.Min.Y = f.Y;
        inner.Max.X = X+fW-1;
        inner.Max.Y = f.Y+fH-1;
        Context.DrawRectangle(inner, FLinearColor::White, true);
        border.Min.X = X;
        border.Min.Y = f.Y;
        border.Max.X = X+fW;
        border.Max.Y = f.Y+fH;
        Context.DrawRectangle(border, FLinearColor::Black, false);
        Position.X = X+1;
        Position.Y = f.Y+1;
        Context.DrawText(Position, TEXT("FLASK1"), FLinearColor::Black);
        //
        bar.Min.X = X+8;
        bar.Min.Y = f.Y+15;
        bar.Max.X = bar.Min.X + (fW-2*8)*SubState->Flask1Level;
        bar.Max.Y = bar.Min.Y+12;
        Context.DrawRectangle(bar, FLinearColor::Green, true);
        bar.Min.X = X+8;
        bar.Min.Y = f.Y+15;
        bar.Max.X = bar.Min.X-2*8 + fW;
        bar.Max.Y = bar.Min.Y+12;
        Context.DrawRectangle(bar, FLinearColor::Black, false);
        //
        inner.Min.X = X;
        inner.Min.Y = f2.Y;
        inner.Max.X = X+fW-1;
        inner.Max.Y = f2.Y+fH-1;
        Context.DrawRectangle(inner, FLinearColor::White, true);
        border.Min.X = X;
        border.Min.Y = f2.Y;
        border.Max.X = X+fW;
        border.Max.Y = f2.Y+fH;
        Context.DrawRectangle(border, FLinearColor::Black, false);
        Position.X = X+1;
        Position.Y = f2.Y+1;
        Context.DrawText(Position, TEXT("FLASK2"), FLinearColor::Black);
        //
        bar.Min.X = X+8;
        bar.Min.Y = f2.Y+15;
        bar.Max.X = bar.Min.X + (fW-2*8)*SubState->Flask2Level;
        bar.Max.Y = bar.Min.Y+12;
        Context.DrawRectangle(bar, FLinearColor::Green, true);
        bar.Min.X = X+8;
        bar.Min.Y = f2.Y+15;
        bar.Max.X = bar.Min.X-2*8 + fW;
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
