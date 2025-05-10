#pragma once

#include "ICommandHandler.h"
#include "PWRJ_MultiSelectJunction.h"
#include "PWRJ_MultiFeederJunction.h"

class SS_Battery : public PWRJ_MultiFeederJunction
{
protected:
    PWRJ_MultiSelectJunction *InputPart;    // For charging input
    int32 iChargingPin = -1;
    float DischargeRate = 0.01f; // per second when discharging
    float ChargeRate = 0.02f;    // per second when charging
    ASubmarineState* SubState;
    bool bBattery1;

public:
    SS_Battery(const FString& Name, ASubmarineState* InSubState, float InX, float InY, float InW=150, float InH=24)
        : PWRJ_MultiFeederJunction(Name+"_Output", InX, InY, InW, InH),
          SubState(InSubState)
    {
        SystemName = Name;
        InputPart = new PWRJ_MultiSelectJunction(Name+"_Input", InX, InY);
        InputPart->bIsPowerSource = false;
        bBattery1 = true;
    }

    virtual PWR_PowerSegment *GetChargingSegment() const override {
    	if (iChargingPin == -1) return nullptr;
    	return Ports[iChargingPin];
    }

    virtual bool IsChargingPort(int n) override {
//if (iChargingPin != -1) UE_LOG(LogTemp, Warning, TEXT("        test port %d =?= chargingpin %d"), n, iChargingPin);
    	return n == iChargingPin;
    }
    virtual bool IsChargingPort(PWR_PowerSegment *seg) override {
//    	int j = -1;
//    	for (int i=0; i<Ports.Num(); i++) if (Ports[i] == seg) if (i == iChargingPin) j = i;
//if (iChargingPin != -1) UE_LOG(LogTemp, Warning, TEXT("        test port %d (%s) =?= chargingpin %d"), j, *seg->GetName(), iChargingPin);
    	for (int i=0; i<Ports.Num(); i++) if (Ports[i] == seg) if (i == iChargingPin) return true;
    	return false;
	}

    virtual TArray<PWR_PowerSegment*> GetConnectedSegments(PWR_PowerSegment* IgnoreSegment = nullptr) const
    {
        TArray<PWR_PowerSegment*> Result;
		for (int32 i = 0; i < Ports.Num(); ++i)
		{
			if ((IgnoreSegment == Ports[i]) && (!EnabledPorts[i])) return Result;
		}
        for (int32 i = 0; i < Ports.Num(); ++i)
        {
            if (EnabledPorts[i] && Ports[i] && Ports[i] != IgnoreSegment && i != iChargingPin)
            {
                Result.Add(Ports[i]);
            }
        }
        return Result;
    }

    virtual ECommandResult HandleCommand(const FString& Aspect, const FString& Command, const FString& Value) override
    {
        if (Aspect == "CHARGE") {
            if (Command == "ENABLE") {
                if (iChargingPin != -1) SetPortEnabled(iChargingPin, false);
                iChargingPin = (int)FCString::Atof(*Value);
                if (iChargingPin != -1) SetPortEnabled(iChargingPin, true);
                return ECommandResult::Handled;
            } else if (Command == "DISABLE") {
                if (iChargingPin != -1) SetPortEnabled(iChargingPin, false);
                iChargingPin = -1;
//UE_LOG(LogTemp, Warning, TEXT("        CHARGE DISABLE"));
                return ECommandResult::Handled;
            } else {
                AddToNotificationQueue("BATTERY.CHARGE UNKNOWNCOMMAND");
                return ECommandResult::HandledWithError;
            }
        }
        auto Result = InputPart->HandleCommand(Aspect, Command, Value);
        if (Result != ECommandResult::NotHandled) return Result;
        return PWRJ_MultiFeederJunction::HandleCommand(Aspect, Command, Value);
    }

    virtual bool CanHandleCommand(const FString& Aspect, const FString& Command, const FString& Value) const override
    {
        return ((Aspect == "CHARGE" && (Command == "ENABLE" || Command == "DISABLE"))) || 
               InputPart->CanHandleCommand(Aspect, Command, Value) ||
               PWRJ_MultiFeederJunction::CanHandleCommand(Aspect, Command, Value);
    }

    virtual FString QueryState(const FString& Aspect) const override
    {
        if (Aspect == "LEVEL")
            return FString::SanitizeFloat(GetBatteryLevel());
        if (Aspect == "CHARGE")
            return FString::Printf(TEXT("%d"), iChargingPin);
        FString Result = InputPart->QueryState(Aspect);
        return !Result.IsEmpty() ? Result : PWRJ_MultiFeederJunction::QueryState(Aspect);
    }

    virtual TArray<FString> QueryEntireState() const override
    {
        TArray<FString> Out;
        // Add command to restore charging state
        if (iChargingPin == -1) {
	        Out.Add(TEXT("CHARGE DISABLE"));
        } else {
	        Out.Add(FString::Printf(TEXT("CHARGE ENABLE %d"), iChargingPin));
        }
        
        // LEVEL is not directly settable via command, so we don't add it.
        // Out.Add(FString::Printf(TEXT("LEVEL SET %.3f"), GetBatteryLevel())); // REMOVED
        
        // Add state from composed parts
        Out.Append(InputPart->QueryEntireState());
        Out.Append(PWRJ_MultiFeederJunction::QueryEntireState());
        return Out;
    }

    virtual TArray<FString> GetAvailableCommands() const override
    {
        TArray<FString> Out = { "CHARGE ENABLE <pin>", "CHARGE DISABLE" };
        Out.Append(InputPart->GetAvailableCommands());
        Out.Append(PWRJ_MultiFeederJunction::GetAvailableCommands());
        return Out;
    }

    /** Returns a list of aspects that can be queried. */
    virtual TArray<FString> GetAvailableQueries() const override
    {
        TArray<FString> Queries = { "LEVEL", "CHARGE" };
        // Add queries from composed parts
        Queries.Append(InputPart->GetAvailableQueries());
        Queries.Append(PWRJ_MultiFeederJunction::GetAvailableQueries());
        return Queries;
    }

    virtual void Tick(float DeltaTime) override
    {
        // Handle charging
		float OldLevel = GetBatteryLevel();
        if ((iChargingPin != -1) && OldLevel < 1.0f)
        {
        	OldLevel += ChargeRate * DeltaTime;
            if (OldLevel > 1.0f)
            {
                SetBatteryLevel(1.0f);
                SetPortEnabled(iChargingPin, false);
//UE_LOG(LogTemp, Warning, TEXT("        FULLY CHARGED"));
                iChargingPin = -1;
                AddToNotificationQueue(FString::Printf(TEXT("%s fully charged"), *SystemName));
				PostHandleCommand();
            } else {
                SetBatteryLevel(OldLevel);
            }
        }

        // Handle discharging
        OldLevel = GetBatteryLevel();
        if (OldLevel > 0.0f)
        {
            float NewLevel = OldLevel - DischargeRate * DeltaTime;
            
            // Check for level thresholds
            if (OldLevel > 0.2f && NewLevel <= 0.2f)
            {
                AddToNotificationQueue(FString::Printf(TEXT("%s at 20%%"), *SystemName));
            }
            else if (OldLevel > 0.1f && NewLevel <= 0.1f)
            {
                AddToNotificationQueue(FString::Printf(TEXT("%s at 10%%"), *SystemName));
            }
            else if (OldLevel > 0.0f && NewLevel <= 0.0f)
            {
                NewLevel = 0.0f;
                AddToNotificationQueue(FString::Printf(TEXT("%s depleted"), *SystemName));
				PostHandleCommand();
            }
			SetBatteryLevel(NewLevel);
        }
    }

    virtual void RenderOnePin(RenderingContext& Context, int i, FBox2D b)
    {
    	if (i != iChargingPin) {
    		PWRJ_MultiFeederJunction::RenderOnePin(Context, i, b);
    		return;
    	}
    	FLinearColor c = FLinearColor::Green;
		if (((int32)(FPlatformTime::Seconds() / 0.5f)) & 1) c = FLinearColor::White;
		Context.DrawRectangle(b, FLinearColor::Green, true);
		Context.DrawRectangle(b, FLinearColor::Black, false);
    }

	virtual void Render(RenderingContext& Context) override
	{
		PWRJ_MultiFeederJunction::Render(Context);
		//
        FBox2D bar;
        float DX = 16;
        bar.Min.X = X+DX;
        bar.Min.Y = Y+18;
        bar.Max.X = bar.Min.X + (W-2*DX)*GetBatteryLevel();
        bar.Max.Y = bar.Min.Y+12;
        Context.DrawRectangle(bar, FLinearColor::Green, true);
        bar.Min.X = X+DX;
        bar.Min.Y = Y+18;
        bar.Max.X = bar.Min.X-2*DX + W;
        bar.Max.Y = bar.Min.Y+12;
        Context.DrawRectangle(bar, FLinearColor::Black, false);
    }

    void SetBatteryLevel(float NewLevel) {
    	if (bBattery1) SubState->Battery1Level = FMath::Clamp(NewLevel, 0.0f, 1.0f);
    	else SubState->Battery2Level = FMath::Clamp(NewLevel, 0.0f, 1.0f);
	}

    float GetBatteryLevel() const { return bBattery1?SubState->Battery1Level:SubState->Battery2Level; }
    virtual float GetPowerAvailable() const override { return GetBatteryLevel() > 0.0f?400.0f:0.0f; }

    virtual float GetCurrentPowerUsage() const override
    {
        if (IsShutdown()) return 0.0f;
    	if (iChargingPin == -1) return 0.0f;
        return 6.0f;			// active charging
    }

	virtual void InitializeVisualElements() override
	{
		ICH_PowerJunction::InitializeVisualElements(); // Call base class if it does anything

		// Toggle Button Implementation
		FBox2D ToggleBounds(FVector2D(W-14, 18), FVector2D(W-2, 18+12)); 
		VisualElements.Add(new VE_ToggleButton(this, 
												 ToggleBounds,
												 TEXT("CHARGE"),      // Query Aspect
												 TEXT("CHARGE ENABLE 2"),    // Command On
												 TEXT("CHARGE DISABLE"),   // Command Off
												 TEXT("2"),			// expected value for "ON"
												 TEXT("-1"),			// expected value for "OFF"
												 TEXT(""),          // Text On
												 TEXT("C")          // Text Off
												 ));
		FBox2D ToggleBounds2(FVector2D(2, 18), FVector2D(14, 18+12)); 
		VisualElements.Add(new VE_ToggleButton(this, 
												 ToggleBounds2,
												 TEXT("CHARGE"),      // Query Aspect
												 TEXT("CHARGE ENABLE 1"),    // Command On
												 TEXT("CHARGE DISABLE"),   // Command Off
												 TEXT("1"),			// expected value for "ON"
												 TEXT("-1"),			// expected value for "OFF"
												 TEXT(""),          // Text On
												 TEXT("C")          // Text Off
												 ));
	}
};

class SS_Battery1 : public SS_Battery
{
public:
    SS_Battery1(const FString& Name, ASubmarineState* InSubState, float InX, float InY, float InW=150, float InH=24)
        : SS_Battery(Name, InSubState, InX, InY, InW, InH)
    {
        bBattery1 = true;
    }
	virtual FString GetTypeString() const override { return TEXT("SS_Battery1"); }
};

class SS_Battery2 : public SS_Battery
{
public:
    SS_Battery2(const FString& Name, ASubmarineState* InSubState, float InX, float InY, float InW=150, float InH=24)
        : SS_Battery(Name, InSubState, InX, InY, InW, InH)
    {
        bBattery1 = false;
    }
	virtual FString GetTypeString() const override { return TEXT("SS_Battery2"); }
};
