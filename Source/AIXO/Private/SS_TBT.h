#pragma once

#include "ICommandHandler.h"
#include "PWRJ_MultiSelectJunction.h"
#include "ICH_Pump.h"
#include "VE_ToggleButton.h"
#include "VE_MomentaryButton.h"

class SS_TBT : public PWRJ_MultiSelectJunction
{
protected:
    ICH_Pump *PumpPart;
    ASubmarineState* SubState;
    bool bTargetingLevel;
    float bTargetLevel;
	bool bIsBlowing = false;

public:
    SS_TBT(const FString& Name, ASubmarineState* InSubState, float InX, float InY, float InW=120, float InH=60)
        : PWRJ_MultiSelectJunction(Name, InX, InY, InW, InH),
          SubState(InSubState)
    {
    	PumpPart = new ICH_Pump(Name+"_Pump", this);
    	bTargetingLevel = false;
		DefaultPowerUsage = 1.0f; // Base power usage when active
		DefaultNoiseLevel = 0.2f; // Base noise level when active          
    }
	virtual FString GetTypeString() const override { return TEXT("SS_TBT"); }

    virtual float GetLevel()
    {
    	if (SystemName == "RTBT") return SubState->RearTBTLevel;
    	else return SubState->ForwardTBTLevel;
    }

    virtual void SetLevel(float val)
    {
		if (SystemName == "RTBT") SubState->RearTBTLevel = val;
		else SubState->ForwardTBTLevel = val;
    }

    virtual float GetFlaskLevel()
    {
    	if (SystemName == "RTBT") return SubState->Flask2Level;
    	return SubState->Flask1Level;
    }

    virtual void SetFlaskLevel(float val)
    {
		if (SystemName == "RTBT") SubState->Flask2Level = val;
		else SubState->Flask1Level = val;
    }

    virtual float GetDY()
    {
    	if (SystemName == "RTBT") return 0;
    	else return -0;
    }

    virtual ECommandResult HandleCommand(const FString& Aspect, const FString& Command, const FString& Value) override
    {
//UE_LOG(LogTemp, Warning, TEXT("SS_TBT::HandleCommand: %s %s %s"), *Aspect, *Command, *Value);
        if (Aspect == "EBLOW" && Command == "SET") {
        	if (GetLevel() > 0.0f) bIsBlowing = Value.ToBool();
        	return ECommandResult::Handled;
        }
        if ((Aspect == "AUTOFILL") && (Command == "SET")) {
        	bTargetLevel = FMath::Clamp(FCString::Atof(*Value) / 100.0f, 0.0f, 1.0f);
        	bTargetingLevel = true;
        	if (bTargetLevel < GetLevel()) {
				if (GetLevel() > 0) PumpPart->SetPumpRate(-1.0f);
        	} else {
				if (GetLevel() < 1) PumpPart->SetPumpRate(1.0f);
        	}
			return ECommandResult::Handled;
        }
        auto Result = PumpPart->HandleCommand(Aspect, Command, Value);
        if (Result != ECommandResult::NotHandled)
            return Result;
        return PWRJ_MultiSelectJunction::HandleCommand(Aspect, Command, Value);
    }

    virtual void SetPumpRate(float InPumpRate) { PumpPart->SetPumpRate(InPumpRate); }

    virtual bool CanHandleCommand(const FString& Aspect, const FString& Command, const FString& Value) const override
    {
        if (Aspect == "EBLOW" && Command == "SET") return true;
        if ((Aspect == "AUTOFILL") && (Command == "SET")) return true;
        return PumpPart->CanHandleCommand(Aspect, Command, Value) || PWRJ_MultiSelectJunction::CanHandleCommand(Aspect, Command, Value);
    }

    virtual FString QueryState(const FString& Aspect) const override
    {
        if (Aspect == "EBLOW") {
        	return bIsBlowing?"true":"false";
        }
        if (Aspect == "AUTOFILL") return bTargetingLevel?"true":"false";
        FString Result = PumpPart->QueryState(Aspect);
        return !Result.IsEmpty() ? Result : PWRJ_MultiSelectJunction::QueryState(Aspect);
    }

    virtual TArray<FString> QueryEntireState() const override
    {
        TArray<FString> Out;
        Out.Add(FString::Printf(TEXT("EBLOW SET %s"), bIsBlowing ? TEXT("true") : TEXT("false")));
        Out.Append(PumpPart->QueryEntireState());
        Out.Append(PWRJ_MultiSelectJunction::QueryEntireState());
        if (bTargetingLevel) Out.Add(FString::Printf(TEXT("AUTOFILL SET %d"), (int)(100*bTargetingLevel)));
        Out.Sort();
        return Out;
    }

    virtual TArray<FString> GetAvailableCommands() const override
    {
        TArray<FString> Out;
        Out.Add("EBLOW SET <bool>");
        Out.Append(PumpPart->GetAvailableCommands());
        Out.Append(PWRJ_MultiSelectJunction::GetAvailableCommands());
        Out.Add("AUTOFILL SET <level 0-100>");
        Out.Sort();
        return Out;
    }

    virtual TArray<FString> GetAvailableQueries() const override
    {
        TArray<FString> Queries;
        Queries.Add("EBLOW");
        Queries.Append(PumpPart->GetAvailableQueries());
        Queries.Append(PWRJ_MultiSelectJunction::GetAvailableQueries());
        Queries.Add("AUTOFILL");
        Queries.Sort();
        return Queries;
    }

    virtual float GetCurrentPowerUsage() const override
    {
    	if (IsShutdown()) return 0.0f;
        return (FMath::IsNearlyZero(PumpPart->GetPumpRate())) ? 0.1f : DefaultPowerUsage;
    }

    virtual float GetCurrentNoiseLevel() const override
    {
        return (IsShutdown() || FMath::IsNearlyZero(PumpPart->GetPumpRate())) ? 0.0f : DefaultNoiseLevel;
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
		PumpPart->Tick(DeltaTime);

    	if (!SubState) return;
		if (bIsBlowing && GetLevel() <= 0.0f) {
			HandleCommand("EBLOW", "SET", "false");
			bIsBlowing = false;
			PostHandleCommand();
		}
        if (!HasPower()) {
        	if ((PumpPart->GetPumpRate() != 0) || bTargetingLevel) {
        		PumpPart->SetPumpRate(0.0f);
				bTargetingLevel = false;
        		PostHandleCommand();
        	}
        	return;
		}

        if (bIsBlowing) {
        	if (TickFlask(DeltaTime * 0.04f)) {
                HandleCommand("EBLOW", "SET", "false");
                bIsBlowing = false;
                AddToNotificationQueue(FString::Printf(TEXT("%s flask %s"), *GetSystemName(), TEXT("empty")));
				PostHandleCommand();
        	}
        	float d = -0.2f * DeltaTime;
            float NewLevel = GetLevel() + d;
            if (NewLevel <= 0.0f)
            {
                HandleCommand("EBLOW", "SET", "false");
                bIsBlowing = false;
                AddToNotificationQueue(FString::Printf(TEXT("%s tank %s"), *GetSystemName(), TEXT("empty")));
                NewLevel = 0.0f;
				PostHandleCommand();
            }
            SetLevel(NewLevel);
        }

        float CurrentPumpRate = PumpPart->GetPumpRate();
        if (FMath::Abs(CurrentPumpRate) > 0.01f && SubState)
        {
            float NewLevel = GetLevel() + CurrentPumpRate * DeltaTime * 0.2f;
            if (bTargetingLevel && (((CurrentPumpRate > 0) && (NewLevel >= bTargetLevel)) ||
									((CurrentPumpRate < 0) && (NewLevel <= bTargetLevel)))) {
				PumpPart->SetPumpRate(0.0f);
				AddToNotificationQueue(FString::Printf(TEXT("%s tank at %%%d"), *GetSystemName(), (int)(bTargetLevel * 100)));
				bTargetingLevel = false;
				SetLevel(bTargetLevel);
				PostHandleCommand();
            } else {
				if (NewLevel <= 0.0f)
				{
					PumpPart->SetPumpRate(0.0f);
					AddToNotificationQueue(FString::Printf(TEXT("%s tank %s"), *GetSystemName(), TEXT("empty")));
					SetLevel(0.0f);
					PostHandleCommand();
				}
				else if (NewLevel >= 1.0f)
				{
					PumpPart->SetPumpRate(0.0f);
					AddToNotificationQueue(FString::Printf(TEXT("%s tank %s"), *GetSystemName(), TEXT("full")));
					SetLevel(1.0f);
					PostHandleCommand();
				}
				else
				{
					SetLevel(NewLevel);
				}
            }
        }
    }

	virtual void RenderUnderlay(RenderingContext& Context) override
	{
		int32 fH = 32;
		int32 fW = W;
		FVector2D f, f1, f2, t;
		if (SystemName == "RTBT") {
			f.X = X + 25;
			f.Y = Y + H + H/2 + 5;
		} else {
			f.X = X + 25;
			f.Y = Y - 22;
		}
		t.X = f.X;
		t.Y = f.Y + 2;
		f2 = f;
		f2.Y = f.Y - 10;
		f1 = f;
		f1.Y = f.Y + 2;
		if (bIsBlowing)
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

	virtual void RenderPump(RenderingContext& Context, float x, float y, float rate, bool horiz)
	{
		FBox2D r;
		x -= 5;
		y -= 5;
		if (horiz) {
			r.Min.X = x - 5;//X - 50;
			r.Min.Y = y;//Y + H - 20;
			r.Max.X = r.Min.X + 30;
			r.Max.Y = y + 20;//Y + H;
		} else {
			r.Min.X = x;//X - 45;
			r.Min.Y = y - 5;//Y + H - 20 - 5;
			r.Max.X = r.Min.X + 20;
			r.Max.Y = y + 30 - 5;//Y + H + 5;
		}
		Context.DrawRectangle(r, FLinearColor::White, true);
		Context.DrawRectangle(r, FLinearColor::Black, false);
		//
		float R = 10;
		r.Min.X = x + 10;
		r.Min.Y = y + 10;
		Context.DrawCircle(r.Min, 10, FLinearColor::Black, false);
		//
		FVector2D a = r.Min;
		FVector2D b = r.Min;
		float dx = R*FMath::Sin(3.14156f/4 + rate * FPlatformTime::Seconds() * 5.0f);
		float dy = R*FMath::Cos(3.14156f/4 + rate * FPlatformTime::Seconds() * 5.0f);
		a.X += dx;
		a.Y -= dy;
		b.X -= dx;
		b.Y += dy;
		Context.DrawLine(a, b, FLinearColor::Black, 1.0f);
    }

    virtual void RenderName(RenderingContext& Context)
    {
        FVector2D Position;
        FLinearColor tc = FLinearColor::Black;
        Position.X = X+2;
        Position.Y = Y+1 - GetDY();
		if (bIsSelected) {
			if (((int32)(FPlatformTime::Seconds() / 0.5f)) & 1) {
				tc = FLinearColor::White;
			}
		}
        Context.DrawText(Position, SystemName, tc);
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
		// render the little pump
		FBox2D r, r2;
		{
			r.Min.X = X - 80;
			r.Min.Y = Y + H - 4 - GetDY();
			r.Max.X = X;
			r.Max.Y = r.Min.Y;
			r2 = r;
			r2.Min.Y = Y + H - 4 - 12 - GetDY();
			r2.Max.Y = r2.Min.Y;
			float d = 5 * GetLevel();
			if (d > 1) d = 1;
			{
				FBox2D rr = r;
				rr.Min = r2.Min;
				rr.Max = r.Max;
				rr.Min.Y = rr.Max.Y - (rr.Max.Y - rr.Min.Y) * d; 
				Context.DrawRectangle(rr, FLinearColor(0.85f, 0.85f, 1.0f), true);
				rr.Min = r2.Min;
				rr.Max.X = X - 35;
				Context.DrawRectangle(rr, FLinearColor(0.85f, 0.85f, 1.0f), true);
			}
			Context.DrawLine(r.Min, r.Max, FLinearColor::Black, 1);
			Context.DrawLine(r2.Min, r2.Max, FLinearColor::Black, 1);
			RenderPump(Context, X - 40, Y + H - 15 - GetDY(), PumpPart->GetPumpRate(), true);

			float basey, basey2;
			if (SystemName=="FTBT") {
				basey = Y + 2 - 24 - GetDY();
			} else {
				basey = Y + H + 12 - GetDY();
			}
			basey2 = basey + 15;
			r.Min.X = X - 80;
			r.Min.Y = basey + 11;
			r.Max.X = X + 15;
			r.Max.Y = r.Min.Y;
			r2 = r;
			r2.Min.Y = basey - 1;
			r2.Max.Y = r2.Min.Y;
			d = 5 * GetLevel();
			if (d > 1) d = 1;
			{
				FBox2D rr = r;
				rr.Min = r2.Min;
				rr.Max = r.Max;
				rr.Min.Y = rr.Max.Y - (rr.Max.Y - rr.Min.Y); 
				Context.DrawRectangle(rr, FLinearColor(0.85f, 0.85f, 1.0f), true);
			}
			Context.DrawLine(r.Min, r.Max, FLinearColor::Black, 1);
			Context.DrawLine(r2.Min, r2.Max, FLinearColor::Black, 1);
			//
			r.Min.X = X + 20 - 1;
			r.Min.Y = basey;
			r.Max.X = r.Min.X;
			r.Max.Y = basey + 8;
			if (SystemName=="FTBT") {
				r.Min.Y += 14;
				r.Max.Y += 14;
			} else {
				r.Min.Y += -13;
				r.Max.Y += -13;
			}
			r2 = r;
			r2.Min.X += 12;
			r2.Max.X = r2.Min.X;
			{
				FBox2D rr = r;
				rr.Min.X = r.Min.X;
				rr.Min.Y = r2.Min.Y;
				rr.Max.X = r2.Max.X;
				rr.Max.Y = r.Max.Y;
				rr.Min.Y = rr.Max.Y - (rr.Max.Y - rr.Min.Y) * d; 
				Context.DrawRectangle(rr, FLinearColor(0.85f, 0.85f, 1.0f), true);
			}
			Context.DrawLine(r.Min, r.Max, FLinearColor::Black, 1);
			Context.DrawLine(r2.Min, r2.Max, FLinearColor::Black, 1);
			//
			RenderPump(Context, X + 20, basey, bIsBlowing?-1:0, true);
		}
		//
		r.Min.X = X;
		r.Min.Y = Y - GetDY();
		r.Max.X = X + W;
		r.Max.Y = Y + H - GetDY();
		Context.DrawRectangle(r, c, true);
		r2 = r;
		r2.Min.Y += 60-60*GetLevel();
		if (showwater) Context.DrawRectangle(r2, FLinearColor(0.85f, 0.85f, 1.0f), true);
		Context.DrawRectangle(r, tc, false);
		//
		FVector2D Position = r.Min;
		Position.X += 1;
		Position.Y += 1;
//		Context.DrawText(Position, *SystemName, tc);
		Position.X += 75;
		if (GetLevel() == 1.0f) Position.X -= 7;
		Context.DrawText(Position, FString::Printf(TEXT("%d%%"), (int)(100*GetLevel())), tc);
	}

	void RenderLabels(RenderingContext& Context)
	{
		return;
	}

	virtual void InitializeVisualElements() override
	{
		ICH_PowerJunction::InitializeVisualElements(); // Call base class if it does anything

		float by = 24 - GetDY();
		FBox2D ToggleBounds(FVector2D(3-36, by), FVector2D(3-22, by+12)); 
		VisualElements.Add(new VE_ToggleButton(this, 
												 ToggleBounds,
												 TEXT("PUMPRATE"),      // Query Aspect
												 TEXT("PUMPRATE SET 1"),    // Command On
												 TEXT("PUMPRATE SET 0"),   // Command Off
												 TEXT("1.00"),			// expected value for "ON"
												 TEXT("0.00"),			// expected value for "OFF"
												 TEXT("+"),          // Text On
												 TEXT("+")          // Text Off
												 ));
//		FBox2D ToggleBounds2(FVector2D(-16-18, 2), FVector2D(-2-18, 14)); 
//		VisualElements.Add(new VE_ToggleButton(this, 
//												 ToggleBounds2,
//												 TEXT("AUTOFILL"),      // Query Aspect
//												 TEXT("AUTOFILL SET 50"),    // Command On
//												 TEXT(""),   // Command Off
//												 TEXT("true"),			// expected value for "ON"
//												 TEXT("false"),			// expected value for "OFF"
//												 TEXT("/"),          // Text On
//												 TEXT("/")          // Text Off
//												 ));
		FBox2D ToggleBounds3(FVector2D(3-36-18, by), FVector2D(3-22-18, by+12)); 
		VisualElements.Add(new VE_ToggleButton(this, 
												 ToggleBounds3,
												 TEXT("PUMPRATE"),      // Query Aspect
												 TEXT("PUMPRATE SET -1"),    // Command On
												 TEXT("PUMPRATE SET 0"),   // Command Off
												 TEXT("-1.00"),			// expected value for "ON"
												 TEXT("0.00"),			// expected value for "OFF"
												 TEXT("-"),          // Text On
												 TEXT("-")          // Text Off
												 ));
		FBox2D ToggleBounds4;
		if (SystemName == "RTBT") {
			ToggleBounds4 = FBox2D(FVector2D(45, H + 18 - GetDY() - 12 + 4), FVector2D(45+12, H + 18 - GetDY() + 4)); 
		} else {
			ToggleBounds4 = FBox2D(FVector2D(45, 14-20 - GetDY() - 12 - 4), FVector2D(45+14, 12-20 - GetDY() - 4)); 
		}
		VisualElements.Add(new VE_MomentaryButton(this, 
												 ToggleBounds4,
												 TEXT("EBLOW"),      // Query Aspect
												 TEXT("EBLOW SET true"),    // Command On
												 TEXT("EBLOW SET false"),   // Command Off
												 TEXT("true"),			// expected value for "ON"
												 TEXT("false"),			// expected value for "OFF"
												 TEXT(""),          // Text On
												 TEXT("E")          // Text Off
												 ));
	}

	friend class PID_HoverDepth;
};
