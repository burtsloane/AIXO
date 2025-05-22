#pragma once

#include "ICommandHandler.h"
#include "MSJ_Powered_OpenClose.h"
#include "VE_ToggleButton.h"

class SS_FMBTVent : public MSJ_Powered_OpenClose {
public:
	SS_FMBTVent(const FString& Name, ASubmarineState* InSubState, float X, float Y, float InW=150, float InH=24)
	: MSJ_Powered_OpenClose(Name, this, InSubState, X, Y, InW, InH) {
		bIsPowerSource = false;
		DefaultPowerUsage = 1.0f; // Base power usage when active
		DefaultNoiseLevel = 0.1f; // Base noise level when active          
	}
	virtual FString GetTypeString() const override { return TEXT("SS_FMBTVent"); }

    virtual float GetLevel()
    {
    	return SubState->ForwardMBTLevel;
    }

    virtual void SetLevel(float val)
    {
		SubState->ForwardMBTLevel = val;
    }

	bool bIsBlowing = false;

    virtual ECommandResult HandleCommand(const FString& Aspect, const FString& Command, const FString& Value) override
    {
//UE_LOG(LogTemp, Warning, TEXT("SS_RMBTVent::HandleCommand: %s %s %s"), *Aspect, *Command, *Value);
        if (Aspect == "BLOW" && Command == "SET") {
        	bIsBlowing = Value.ToBool();
        	return ECommandResult::Handled;
        }
        // If not handled, try PWR base part
        return MSJ_Powered_OpenClose::HandleCommand(Aspect, Command, Value);
    }

    virtual bool CanHandleCommand(const FString& Aspect, const FString& Command, const FString& Value) const override
    {
        return ((Aspect == "BLOW" && Command == "SET")) ||
               MSJ_Powered_OpenClose::CanHandleCommand(Aspect, Command, Value);
    }

    virtual FString QueryState(const FString& Aspect) const override
    {
        if (Aspect == "BLOW") {
        	return bIsBlowing?"true":"false";
        }
        // If not handled, try PWR base part
        return MSJ_Powered_OpenClose::QueryState(Aspect);
    }

    virtual TArray<FString> QueryEntireState() const override
    {
        TArray<FString> Out = { FString::Printf(TEXT("BLOW SET %s"), bIsBlowing ? TEXT("true") : TEXT("false")) };
        Out.Append(MSJ_Powered_OpenClose::QueryEntireState());
        
        Out.Sort();
        return Out;
    }

    virtual TArray<FString> GetAvailableCommands() const override
    {
        TArray<FString> Out = { "BLOW SET <bool>" };
        Out.Append(MSJ_Powered_OpenClose::GetAvailableCommands());
        Out.Sort();
        return Out;
    }

    virtual TArray<FString> GetAvailableQueries() const override
    {
        TArray<FString> Queries = { "BLOW" };
        Queries.Append(MSJ_Powered_OpenClose::GetAvailableQueries());
        Queries.Sort();
        return Queries;
    }

    virtual bool TickFlask(float ScaledDeltaTime)
    {
		SetLevel(GetLevel() - ScaledDeltaTime);
    	if (GetLevel() < 0) {
    		SetLevel(0.0f);
    		return true;
    	}
    	return false;
    }

    virtual void Tick(float DeltaTime) override
    {
    	MSJ_Powered_OpenClose::Tick(DeltaTime);
    	
    	if (!SubState) return;
    	if (!HasPower()) {
    		if (bIsBlowing) {
                HandleCommand("BLOW", "SET", "false");
			}
    		return;
    	}
    	
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
            float NewLevel = GetLevel() + CurrentVentRate * DeltaTime;
            if (NewLevel <= 0.0f)
            {
                HandleCommand("BLOW", "SET", "false");
                bIsBlowing = false;
                AddToNotificationQueue(FString::Printf(TEXT("%s tank %s"), *GetSystemName(), TEXT("empty")));
                SetLevel(0.0f);
				UpdateChange();
            }
            else if (NewLevel >= 1.0f)
            {
                HandleCommand("OPEN", "SET", "false");
                AddToNotificationQueue(FString::Printf(TEXT("%s tank %s"), *GetSystemName(), TEXT("full")));
                SetLevel(1.0f);
				UpdateChange();
            }
            else
            {
                SetLevel(NewLevel);
            }
        }
    }

	virtual void Render(RenderingContext& Context) override
	{
		PWRJ_MultiSelectJunction::Render(Context);
return;		// old
		//
		int32 dx = 150-75-16-3-13+1 + 16;
		FBox2D r;
		r.Min.X = X-dx - 120;
		r.Min.Y = Y + H/2 - 30;
		r.Max.X = X-dx;
		r.Max.Y = Y + H/2 + 30;
		Context.DrawRectangle(r, FLinearColor::White, true);
		FBox2D r2 = r;
		r2.Min.Y += 60-60*GetLevel();
		Context.DrawRectangle(r2, FLinearColor(0.85f, 0.85f, 1.0f), true);
		Context.DrawRectangle(r, FLinearColor::Black, false);
		//
		FVector2D Position = r.Min;
		Position.X += 1;
		Position.Y += 1;
		Context.DrawText(Position, "FMBT", FLinearColor::Black);
		Position.X += 75;
		if (GetLevel() == 1.0f) Position.X -= 7;
		Context.DrawText(Position, FString::Printf(TEXT("%d%%"), (int)(100*GetLevel())), FLinearColor::Black);
    }

	virtual void RenderUnderline(RenderingContext& Context)
	{
return;		// old
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

	virtual void RenderUnderlay(RenderingContext& Context) override
	{
return;		// old
        FVector2D Position;
        Position.X = X + W/2;
        Position.Y = Y + H/2;
		FVector2D Position2 = Position;
		FVector2D Position3 = Position;
		int32 dx = 150-75-16-3-13+1 + 16;
		Position2.X = X-dx;        	Position2.Y -= 30;
		Position3.X = X-dx;        	Position3.Y += 30;
		Context.DrawTriangle(Position, Position2, Position3, FLinearColor(0.85f, 0.85f, 1.0f));
		
		RenderUnderline(Context);
	}

	void RenderLabels(RenderingContext& Context)
	{
		return;
	}

	virtual void InitializeVisualElements() override
	{
		MSJ_Powered_OpenClose::InitializeVisualElements();

		// Toggle Button Implementation
		FBox2D ToggleBounds(FVector2D(W-40, 16+2), FVector2D(W-2, 16+14)); 
		VisualElements.Add(new VE_ToggleButton(this, 
												 ToggleBounds,
												 TEXT("BLOW"),      // Query Aspect
												 TEXT("BLOW SET true"),    // Command On
												 TEXT("BLOW SET false"),   // Command Off
												 TEXT("true"),			// expected value for "ON"
												 TEXT("false"),			// expected value for "OFF"
												 TEXT(""),          // Text On
												 TEXT("BLOW")          // Text Off
												 ));
	}
};
