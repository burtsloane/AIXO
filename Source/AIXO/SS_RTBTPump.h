#pragma once

#include "ICommandHandler.h"
#include "PWRJ_MultiSelectJunction.h"
#include "ICH_Pump.h"
#include "VE_ToggleButton.h"

class SS_RTBTPump : public PWRJ_MultiSelectJunction
{
protected:
    ICH_Pump *PumpPart;
    ASubmarineState* SubState;
    bool bTargetingLevel;
    float bTargetLevel;

public:
    SS_RTBTPump(const FString& Name, ASubmarineState* InSubState, float InX, float InY, float InW=150, float InH=24)
        : PWRJ_MultiSelectJunction(Name, InX, InY, InW, InH),
          SubState(InSubState)
    {
    	PumpPart = new ICH_Pump(Name+"_Pump", this);
    	bTargetingLevel = false;
		DefaultPowerUsage = 1.0f; // Base power usage when active
		DefaultNoiseLevel = 0.2f; // Base noise level when active          
    }
	virtual FString GetTypeString() const override { return TEXT("SS_RTBTPump"); }

    virtual float GetLevel()
    {
    	return SubState->RearTBTLevel;
    }

    virtual void SetLevel(float val)
    {
		SubState->RearTBTLevel = val;
    }

    virtual float GetDY()
    {
    	return 10;
    }

    virtual ECommandResult HandleCommand(const FString& Aspect, const FString& Command, const FString& Value) override
    {
//UE_LOG(LogTemp, Warning, TEXT("SS_*TBTPump::HandleCommand: %s %s %s"), *Aspect, *Command, *Value);
        if ((Aspect == "AUTOFILL") && (Command == "SET")) {
        	bTargetLevel = FMath::Clamp(FCString::Atof(*Value) / 100.0f, 0.0f, 1.0f);
        	bTargetingLevel = true;
			PumpPart->SetPumpRate((bTargetLevel < GetLevel())?-1.0f:1.0f);
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
        if ((Aspect == "AUTOFILL") && (Command == "SET")) return true;
        return PumpPart->CanHandleCommand(Aspect, Command, Value) || PWRJ_MultiSelectJunction::CanHandleCommand(Aspect, Command, Value);
    }

    virtual FString QueryState(const FString& Aspect) const override
    {
        if (Aspect == "AUTOFILL") return bTargetingLevel?"true":"false";
        FString Result = PumpPart->QueryState(Aspect);
        return !Result.IsEmpty() ? Result : PWRJ_MultiSelectJunction::QueryState(Aspect);
    }

    virtual TArray<FString> QueryEntireState() const override
    {
        TArray<FString> Out = PumpPart->QueryEntireState();
        Out.Append(PWRJ_MultiSelectJunction::QueryEntireState());
        if (bTargetingLevel) Out.Add(FString::Printf(TEXT("AUTOFILL SET %d"), (int)(100*bTargetingLevel)));
        Out.Sort();
        return Out;
    }

    virtual TArray<FString> GetAvailableCommands() const override
    {
        TArray<FString> Out = PumpPart->GetAvailableCommands();
        Out.Append(PWRJ_MultiSelectJunction::GetAvailableCommands());
        Out.Add("AUTOFILL SET <level 0-100>");
        Out.Sort();
        return Out;
    }

    virtual TArray<FString> GetAvailableQueries() const override
    {
        TArray<FString> Queries = PumpPart->GetAvailableQueries();
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

    virtual void Tick(float DeltaTime) override
    {
        if (!HasPower()) {
        	if ((PumpPart->GetPumpRate() != 0) || bTargetingLevel) {
        		PumpPart->SetPumpRate(0.0f);
				bTargetingLevel = false;
        		PostHandleCommand();
        	}
        	return;
		}
        float CurrentPumpRate = PumpPart->GetPumpRate();
        if (FMath::Abs(CurrentPumpRate) > 0.01f && SubState)
        {
            float NewLevel = GetLevel() + CurrentPumpRate * DeltaTime;
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

	const int32 DX = 150 + 16;
	virtual void RenderUnderlay(RenderingContext& Context) override
	{
        FVector2D Position;
        Position.X = X + H/2;
        Position.Y = Y + H/2;
		FVector2D Position2 = Position;
		FVector2D Position3 = Position;
		Position2.X = X-DX;        	Position2.Y -= 30 + GetDY();
		Position3.X = X-DX;        	Position3.Y += 30 - GetDY();
		Context.DrawTriangle(Position, Position2, Position3, FLinearColor(0.85f, 0.85f, 1.0f));
    }

	virtual void Render(RenderingContext& Context) override
	{
		PWRJ_MultiSelectJunction::Render(Context);
		//
        FVector2D Position;
		FBox2D r;
		r.Min.X = X-DX - 120;
		r.Min.Y = Y + H/2 - 30 - GetDY();
		r.Max.X = X-DX;
		r.Max.Y = Y + H/2 + 30 - GetDY();
		Context.DrawRectangle(r, FLinearColor::White, true);
		FBox2D r2 = r;
		r2.Min.Y += 60-60*GetLevel();
		Context.DrawRectangle(r2, FLinearColor(0.85f, 0.85f, 1.0f), true);
		Context.DrawRectangle(r, FLinearColor::Black, false);
		//
		Position = r.Min;
		Position.X += 1;
		Position.Y += 1;
		Context.DrawText(Position, *SystemName, FLinearColor::Black);
		Position.X += 75;
		if (GetLevel() == 1.0f) Position.X -= 7;
		Context.DrawText(Position, FString::Printf(TEXT("%d%%"), (int)(100*GetLevel())), FLinearColor::Black);
	}

	void RenderLabels(RenderingContext& Context)
	{
		return;
	}

	virtual void InitializeVisualElements() override
	{
		ICH_PowerJunction::InitializeVisualElements(); // Call base class if it does anything

		FBox2D ToggleBounds(FVector2D(W-16, 2), FVector2D(W-2, 14)); 
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
		FBox2D ToggleBounds2(FVector2D(W-16-18, 2), FVector2D(W-2-18, 14)); 
		VisualElements.Add(new VE_ToggleButton(this, 
												 ToggleBounds2,
												 TEXT("AUTOFILL"),      // Query Aspect
												 TEXT("AUTOFILL SET 50"),    // Command On
												 TEXT(""),   // Command Off
												 TEXT("true"),			// expected value for "ON"
												 TEXT("false"),			// expected value for "OFF"
												 TEXT("/"),          // Text On
												 TEXT("/")          // Text Off
												 ));
		FBox2D ToggleBounds3(FVector2D(W-16-18-18, 2), FVector2D(W-2-18-18, 14)); 
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
	}

	friend class PID_HoverDepth;
};
