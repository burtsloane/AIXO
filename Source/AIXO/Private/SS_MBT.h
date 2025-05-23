#pragma once

#include "ICommandHandler.h"
#include "ICH_OpenClose.h"
#include "VE_ToggleButton.h"
#include "VE_MomentaryButton.h"

class SS_MBT : public PWRJ_MultiSelectJunction {
protected:
    ASubmarineState* SubState;
    ICH_OpenClose *OpenClosePart;
	bool bIsBlowing = false;
	bool bIsEBlowing = false;

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

    virtual float GetFlaskLevel()
    {
    	if (SystemName == "RMBT") return SubState->Flask2Level;
    	return SubState->Flask1Level;
    }

    virtual void SetFlaskLevel(float val)
    {
		if (SystemName == "RMBT") SubState->Flask2Level = val;
		else SubState->Flask1Level = val;
    }

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
        if (Aspect == "EBLOW" && Command == "SET") {
        	if (GetLevel() > 0.0f) bIsEBlowing = Value.ToBool();
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
        if (Aspect == "EBLOW" && Command == "SET") return true;
        return OpenClosePart->CanHandleCommand(Aspect, Command, Value) ||
               PWRJ_MultiSelectJunction::CanHandleCommand(Aspect, Command, Value);
    }

    virtual FString QueryState(const FString& Aspect) const override
    {
        if (Aspect == "EBLOW") {
        	return bIsEBlowing?"true":"false";
        }
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
        Out.Add(FString::Printf(TEXT("EBLOW SET %s"), bIsEBlowing ? TEXT("true") : TEXT("false")));
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
        Out.Add("EBLOW SET <bool>");
        Out.Append(OpenClosePart->GetAvailableCommands());
        Out.Append(PWRJ_MultiSelectJunction::GetAvailableCommands());
        Out.Sort();
        return Out;
    }

    virtual TArray<FString> GetAvailableQueries() const override
    {
        TArray<FString> Queries;
        Queries.Add("BLOW");
        Queries.Add("EBLOW");
        Queries.Append(OpenClosePart->GetAvailableQueries());
        Queries.Append(PWRJ_MultiSelectJunction::GetAvailableQueries());
        Queries.Sort();
        return Queries;
    }

    virtual bool TickFlask(float ScaledDeltaTime)
    {
		SetFlaskLevel(GetFlaskLevel() - ScaledDeltaTime);
    	if (GetFlaskLevel() < 0) {
    		SetFlaskLevel(0.0f);
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
				UpdateChange();
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
        if (bIsEBlowing) {
        	CurrentVentRate = -1.0f;
        	if (TickFlask(DeltaTime * 0.2f)) {
                HandleCommand("EBLOW", "SET", "false");
                bIsEBlowing = false;
                AddToNotificationQueue(FString::Printf(TEXT("%s flask %s"), *GetSystemName(), TEXT("empty")));
				UpdateChange();
        	}
        }
		float m = OpenClosePart->GetMovementAmount();
        if ((m > 0) || bIsBlowing || bIsEBlowing)
        {
        	float d = CurrentVentRate * DeltaTime;
			if (!bIsBlowing && !bIsEBlowing) d *= m;
            float NewLevel = GetLevel() + d;
            if (NewLevel <= 0.0f)
            {
                if (bIsBlowing) HandleCommand("BLOW", "SET", "false");
                if (bIsEBlowing) HandleCommand("EBLOW", "SET", "false");
                bIsBlowing = false;
                bIsEBlowing = false;
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
//RenderUnderlay(Context); return;		// just draw the underlay line on top for viz
        FLinearColor tc = FLinearColor::Black;
        FLinearColor c = RenderBGGetColor();
        bool showwater = true;
		if (bIsSelected) {
			if (((int32)(FPlatformTime::Seconds() / 0.5f)) & 1) {
				c = FLinearColor(0.0f, 0.5f, 0.0f);		// selected color
				tc = FLinearColor::White;
				showwater = false;
			}
		}
		//
		FBox2D r, r2;
		{
			r.Min.X = X - 80;
			r.Min.Y = Y + H - 4;
			r.Max.X = X;
			r.Max.Y = r.Min.Y;
			r2 = r;
			r2.Min.Y = Y + H - 4 - 12;
			r2.Max.Y = r2.Min.Y;
			float d = 5 * GetLevel();
			if (d > 1) d = 1;
			if (d > 0) {
				FBox2D rr = r;
				rr.Min = r2.Min;
				rr.Max = r.Max;
				rr.Min.Y = rr.Max.Y - (rr.Max.Y - rr.Min.Y) * d; 
				Context.DrawRectangle(rr, FLinearColor(0.85f, 0.85f, 1.0f), true);
			}
			Context.DrawLine(r.Min, r.Max, FLinearColor::Black, 1);
			Context.DrawLine(r2.Min, r2.Max, FLinearColor::Black, 1);
		}
		//
		r.Min.X = X;
		r.Min.Y = Y;
		r.Max.X = X + W;
		r.Max.Y = Y + H;
		Context.DrawRectangle(r, c, true);
		r2 = r;
		r2.Min.Y += 60-60*GetLevel();
		if (showwater) Context.DrawRectangle(r2, FLinearColor(0.85f, 0.85f, 1.0f), true);
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

	virtual void RenderUnderlay(RenderingContext& Context) override
	{
		int32 fH = 32;
		int32 fW = W;
		FVector2D f, f1, f2, t;
		if (SystemName == "RMBT") {
			f.X = X + W - 24;
			f.Y = Y;
		} else {
			f.X = X + W - 24;
			f.Y = Y + H + H/2 - 10;
		}
		t.X = f.X;
		t.Y = f.Y + fH/2;
		f2 = f;
		f2.Y = f.Y - 30;
		f1 = f;
		f1.Y = f.Y + fH/2;
		if (bIsBlowing || bIsEBlowing)
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
		FBox2D ToggleBounds(FVector2D(-42, 0), FVector2D(-4, 12)); 
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
		ToggleBounds2.Min.X -= 16;
		ToggleBounds2.Max.X = ToggleBounds2.Min.X + 12;
		VisualElements.Add(new VE_MomentaryButton(this, 
												 ToggleBounds2,
												 TEXT("EBLOW"),      // Query Aspect
												 TEXT("EBLOW SET true"),    // Command On
												 TEXT("EBLOW SET false"),   // Command Off
												 TEXT("true"),			// expected value for "ON"
												 TEXT("false"),			// expected value for "OFF"
												 TEXT(""),          // Text On
												 TEXT("E")          // Text Off
												 ));
	}
};
