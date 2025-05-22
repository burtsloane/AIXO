#pragma once

#include "ICommandHandler.h"
#include "ICH_OpenClose.h"
#include "VE_ToggleButton.h"

class SS_MBT : public PWRJ_MultiSelectJunction {
protected:
    ASubmarineState* SubState;

    ICH_OpenClose *OpenClosePart;

public:
	SS_MBT(const FString& Name, ASubmarineState* InSubState, float X, float Y, float InW=120, float InH=60)
	: PWRJ_MultiSelectJunction(Name, X, Y, InW, InH),
	  SubState(InSubState),
	  OpenClosePart(new ICH_OpenClose(Name + "_OpenClose", this)) // Initialize composed OpenClosePart
	 {
		bIsPowerSource = false;
		DefaultPowerUsage = 0.1f; // Base power usage when active
		DefaultNoiseLevel = 0.1f; // Base noise level when active  
		OpenClosePart->MoveDuration = 0.5f;        
	}
	virtual FString GetTypeString() const override { return TEXT("SS_MBT"); }

    virtual float GetLevel()
    {
    	if (SystemName == "RMBT") return SubState->RearMBTLevel;
    	return SubState->ForwardMBTLevel;
    }

    virtual void SetLevel(float val)
    {
		if (SystemName == "RMBT") SubState->RearMBTLevel = val;
		else SubState->ForwardMBTLevel = val;
    }

	bool bIsBlowing = false;

    virtual void UpdateChange() {
    	PostHandleCommand();
    }

    virtual ECommandResult HandleCommand(const FString& Aspect, const FString& Command, const FString& Value) override
    {
//UE_LOG(LogTemp, Warning, TEXT("SS_MBT::HandleCommand: %s %s %s"), *Aspect, *Command, *Value);
        if (Aspect == "BLOW" && Command == "SET") {
        	if (GetLevel() > 0.0f) bIsBlowing = Value.ToBool();
        	return ECommandResult::Handled;
        }
        // Try OpenClose part first
        ECommandResult Result = ECommandResult::Handled;
        if (GetLevel() < 1.0f) Result = OpenClosePart->HandleCommand(Aspect, Command, Value);
        if (Result == ECommandResult::Handled) UpdateChange();
        if (Result != ECommandResult::NotHandled) 
        {
            return Result;
        }
        // If not handled, try PWR base part
        Result = PWRJ_MultiSelectJunction::HandleCommand(Aspect, Command, Value);
        if (Result == ECommandResult::Handled) UpdateChange();
        return Result;
    }

    virtual bool CanHandleCommand(const FString& Aspect, const FString& Command, const FString& Value) const override
    {
        if (Aspect == "BLOW" && Command == "SET") return true;
        return OpenClosePart->CanHandleCommand(Aspect, Command, Value) ||
               PWRJ_MultiSelectJunction::CanHandleCommand(Aspect, Command, Value);
    }

    virtual FString QueryState(const FString& Aspect) const override
    {
        if (Aspect == "BLOW") {
        	return bIsBlowing?"true":"false";
        }
        // Try OpenClose part first
        FString Result = OpenClosePart->QueryState(Aspect);
        if (!Result.IsEmpty()) 
        {
            return Result;
        }
        // If not handled, try PWR base part
        return PWRJ_MultiSelectJunction::QueryState(Aspect);
    }

    virtual TArray<FString> QueryEntireState() const override
    {
        TArray<FString> Out;
        Out.Add(FString::Printf(TEXT("BLOW SET %s"), bIsBlowing ? TEXT("true") : TEXT("false")));
        
        // Get state from OpenClose part (Category 5)
        Out.Append(OpenClosePart->QueryEntireState()); // Gets ON SET ...
        // Append state from PWR base part (Category 5)
        Out.Append(PWRJ_MultiSelectJunction::QueryEntireState());

        Out.Sort();
        return Out;
    }

    virtual TArray<FString> GetAvailableCommands() const override
    {
        TArray<FString> Out;
        Out.Add("BLOW SET <bool>");
        Out.Append(OpenClosePart->GetAvailableCommands());
        Out.Append(PWRJ_MultiSelectJunction::GetAvailableCommands());
        Out.Sort();
        return Out;
    }

    virtual TArray<FString> GetAvailableQueries() const override
    {
        TArray<FString> Queries;
        Queries.Add("BLOW");
        Queries.Append(OpenClosePart->GetAvailableQueries());
        Queries.Append(PWRJ_MultiSelectJunction::GetAvailableQueries());
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
		OpenClosePart->Tick(DeltaTime);
    	
        float CurrentVentRate = 1.0f;
    	if (!SubState) return;
    	if (!HasPower()) {
    		if (bIsBlowing) {
                HandleCommand("BLOW", "SET", "false");
                bIsBlowing = false;
                CurrentVentRate = 0.0f;
			}
    		return;
    	}
    	
        if (bIsBlowing) {
        	CurrentVentRate = -1.0f;
        	if (TickFlask(DeltaTime * 0.2f)) {
                HandleCommand("BLOW", "SET", "false");
                bIsBlowing = false;
                AddToNotificationQueue(FString::Printf(TEXT("%s flask %s"), *GetSystemName(), TEXT("empty")));
				UpdateChange();
        	}
        }
		float m = OpenClosePart->GetMovementAmount();
        if ((m > 0) || bIsBlowing)
        {
        	float d = CurrentVentRate * DeltaTime;
			if (!bIsBlowing) d *= m;
            float NewLevel = GetLevel() + d;
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

	virtual void RenderBG(RenderingContext& Context) override
	{
        FLinearColor tc = FLinearColor::Black;
        FLinearColor c = RenderBGGetColor();
		if (bIsSelected) {
			if (((int32)(FPlatformTime::Seconds() / 0.5f)) & 1) {
				c = FLinearColor(0.0f, 0.5f, 0.0f);		// selected color
				tc = FLinearColor::White;
			}
		}
		//
		FBox2D r;
		r.Min.X = X;
		r.Min.Y = Y;
		r.Max.X = X + W;
		r.Max.Y = Y + H;
		Context.DrawRectangle(r, c, true);
		FBox2D r2 = r;
		r2.Min.Y += 60-60*GetLevel();
		Context.DrawRectangle(r2, FLinearColor(0.85f, 0.85f, 1.0f), true);
		Context.DrawRectangle(r, tc, false);
		float m = OpenClosePart->GetMovementAmount();
		{
			FBox2D r3 = r;
			r3.Max.X = (r3.Min.X + r3.Max.X) / 2;
			float d = r3.Max.X - r3.Min.X;
			d /= 2;
			r3.Max.X = r3.Min.X + d;
			r3.Max.Y = r3.Min.Y;
			Context.DrawLine(r3.Min, r3.Max, FLinearColor::White, 1.0f);
			float dy = d * FMath::Sin(m * 0.4f);
			float dx = d * FMath::Cos(m * 0.4f);
			r3.Min.X = r3.Max.X - dx;
			r3.Min.Y = r3.Max.Y - dy;
			Context.DrawLine(r3.Min, r3.Max, tc, 1.0f);
		}
		//
		FVector2D Position = r.Min;
		Position.X += 1;
		Position.Y += 1;
//		Context.DrawText(Position, "RMBT", tc);
		Position.X += 75;
		if (GetLevel() == 1.0f) Position.X -= 7;
		Context.DrawText(Position, FString::Printf(TEXT("%d%%"), (int)(100*GetLevel())), tc);
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
		a.Y = r.Min.Y - 20;
		b.X = r.Min.X + 75;
		b.Y = r.Min.Y + H/2;

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

    virtual float GetCurrentPowerUsage() const override
    {
        // Base implementation returns default usage if not shutdown/faulted
        if (IsShutdown()) return 0.0f;
        if (OpenClosePart->IsMoving()) return 0.5f;
        return DefaultPowerUsage;
    }

    /** Returns the  operational noise level of the junction. Can be overridden by derived classes. */
    virtual float GetCurrentNoiseLevel() const override
    {
        // Base implementation returns default noise if not shutdown/faulted
        if (OpenClosePart->IsMoving()) return DefaultNoiseLevel;
        return 0.0f;
    }

	virtual void InitializeVisualElements() override
	{
		ICH_PowerJunction::InitializeVisualElements(); // Call base class if it does anything

		// Toggle Button Implementation
		FBox2D ToggleBounds(FVector2D(-40, 0), FVector2D(-2, 12)); 
		VisualElements.Add(new VE_ToggleButton(this, 
												 ToggleBounds,
												 TEXT("OPEN"),      // Query Aspect
												 TEXT("OPEN SET true"),    // Command On
												 TEXT("OPEN SET false"),   // Command Off
												 TEXT("OPEN"),			// expected value for "ON"
												 TEXT("CLOSED"),			// expected value for "OFF"
												 TEXT(""),          // Text On
												 TEXT("VENT")          // Text Off
												 ));

		// Toggle Button Implementation
		FBox2D ToggleBounds2(FVector2D(W-42, -18), FVector2D(W-4, -6)); 
		if (SystemName != "RMBT") {
			ToggleBounds2.Min.Y += 12 + 8 + 6 + H;
			ToggleBounds2.Max.Y += 12 + 8 + 6 + H;
		}
		VisualElements.Add(new VE_ToggleButton(this, 
												 ToggleBounds2,
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
