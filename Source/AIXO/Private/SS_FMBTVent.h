#pragma once

#include "ICommandHandler.h"
#include "MSJ_Powered_OpenClose.h"
#include "SS_RMBTVent.h"

class SS_FMBTVent : public SS_RMBTVent {
public:
	SS_FMBTVent(const FString& Name, ASubmarineState* InSubState, float X, float Y, float InW=150, float InH=24)
	: SS_RMBTVent(Name, InSubState, X, Y, InW, InH) {
		bIsPowerSource = false;
		DefaultPowerUsage = 1.0f; // Base power usage when active
		DefaultNoiseLevel = 0.1f; // Base noise level when active          
	}
	virtual FString GetTypeString() const override { return TEXT("SS_FMBTVent"); }

    virtual void Tick(float DeltaTime) override
    {
    	MSJ_Powered_OpenClose::Tick(DeltaTime);
    	
    	if (!SubState) return;
    	
        float CurrentVentRate = 1.0f;
        if (bIsBlowing) {
        	CurrentVentRate = -1.0f;
        	if (TickFlask(DeltaTime * 0.2f)) {
                HandleCommand("BLOW", "SET", "false");
                bIsBlowing = false;
                AddToNotificationQueue(FString::Printf(TEXT("%s flask %s"), *GetSystemName(), TEXT("empty")));
				UpdateChange();
        	}
        }
        if (OpenClosePart->IsOpen() || bIsBlowing)
        {
            float NewLevel = SubState->ForwardMBTLevel + CurrentVentRate * DeltaTime;
            if (NewLevel <= 0.0f)
            {
                HandleCommand("BLOW", "SET", "false");
                bIsBlowing = false;
                AddToNotificationQueue(FString::Printf(TEXT("%s tank %s"), *GetSystemName(), TEXT("empty")));
                SubState->ForwardMBTLevel = 0.0f;
				UpdateChange();
            }
            else if (NewLevel >= 1.0f)
            {
                HandleCommand("OPEN", "SET", "false");
                AddToNotificationQueue(FString::Printf(TEXT("%s tank %s"), *GetSystemName(), TEXT("full")));
                SubState->ForwardMBTLevel = 1.0f;
				UpdateChange();
            }
            else
            {
                SubState->ForwardMBTLevel = NewLevel;
            }
        }
    }

	virtual void Render(RenderingContext& Context) override
	{
		PWRJ_MultiSelectJunction::Render(Context);
		//
		int32 dx = 150-75-16-3-13+1 + 16;
		FBox2D r;
		r.Min.X = X-dx - 120;
		r.Min.Y = Y + H/2 - 30;
		r.Max.X = X-dx;
		r.Max.Y = Y + H/2 + 30;
		Context.DrawRectangle(r, FLinearColor::White, true);
		FBox2D r2 = r;
		r2.Min.Y += 60-60*SubState->ForwardMBTLevel;
		Context.DrawRectangle(r2, FLinearColor(0.85f, 0.85f, 1.0f), true);
		Context.DrawRectangle(r, FLinearColor::Black, false);
		//
		FVector2D Position = r.Min;
		Position.X += 1;
		Position.Y += 1;
		Context.DrawText(Position, "FMBT", FLinearColor::Black);
		Position.X += 75;
		if (SubState->ForwardMBTLevel == 1.0f) Position.X -= 7;
		Context.DrawText(Position, FString::Printf(TEXT("%d%%"), (int)(100*SubState->ForwardMBTLevel)), FLinearColor::Black);
    }

	virtual void RenderUnderline(RenderingContext& Context) override
	{
		FBox2D r;
		int32 dx = 150-75-16-3-13+1 + 16;
		r.Min.X = X-dx - 120;
		r.Min.Y = Y + H/2 - 30;
		r.Max.X = X-dx;
		r.Max.Y = Y + H/2 + 30;
		FVector2D a, b;
		a.X = r.Min.X + 75;
		a.Y = r.Min.Y + H/2;
		b.X = r.Min.X + 75;
		b.Y = r.Min.Y + H*2 + 20;

		if (bIsBlowing)
		{
			float d = 0.0f;
			d += 0.25f*FMath::Sin(FPlatformTime::Seconds() * 8.0f);
			if (d < 0) d = 0;
			Context.DrawLine(a, b, FLinearColor(d, 1.0f, d), 12.0f);
		}
		else
		{
			Context.DrawLine(a, b, FLinearColor(0.6f, 1.0f, 0.6f), 6.0f);
		}
	}

    virtual bool TickFlask(float ScaledDeltaTime) override
    {
    	SubState->Flask1Level -= ScaledDeltaTime;
    	if (SubState->Flask1Level < 0) {
    		SubState->Flask1Level = 0;
    		return true;
    	}
    	return false;
    }
};
