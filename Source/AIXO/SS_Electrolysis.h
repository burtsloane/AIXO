#pragma once

#include "ICommandHandler.h"
#include "PWRJ_MultiSelectJunction.h"
#include "ICH_OnOff.h"
class SubmarineState; // Forward declaration

class SS_Electrolysis : public PWRJ_MultiSelectJunction
{
protected:
    ICH_OnOff OnOffPart;
    float HydrogenProductionRate = 0.05f;
    float LOXProductionRate = 0.05f;
    ASubmarineState* SubState;

public:
    SS_Electrolysis(const FString& Name, ASubmarineState* InSubState, float InX, float InY, float InW=150, float InH=24)
        : PWRJ_MultiSelectJunction(Name, InX, InY, InW, InH)
        , OnOffPart(Name, this)
        , SubState(InSubState)
    {
    	SubState = InSubState;
		DefaultPowerUsage = 2.0f; // Base power usage when active
		DefaultNoiseLevel = 0.3f; // Base noise level when active          
    }
	virtual FString GetTypeString() const override { return TEXT("SS_Electrolysis"); }

    virtual ECommandResult HandleCommand(const FString& Aspect, const FString& Command, const FString& Value) override
    {
//UE_LOG(LogTemp, Warning, TEXT("SS_Electrolysis::HandleCommand: %s %s %s"), *Aspect, *Command, *Value);
        auto Result = PWRJ_MultiSelectJunction::HandleCommand(Aspect, Command, Value);
        if (Result != ECommandResult::NotHandled) return Result;
        
        Result = OnOffPart.HandleCommand(Aspect, Command, Value);
        if (Result != ECommandResult::NotHandled) return Result;

        if (Aspect == "PRODUCTION")
        {
            if (Command == "SET")
            {
                float Rate = FCString::Atof(*Value);
                HydrogenProductionRate = Rate;
                LOXProductionRate = Rate;
                return ECommandResult::Handled;
            }
        }
        return ECommandResult::NotHandled;
    }

    virtual bool CanHandleCommand(const FString& Aspect, const FString& Command, const FString& Value) const override
    {
        return PWRJ_MultiSelectJunction::CanHandleCommand(Aspect, Command, Value) ||
               OnOffPart.CanHandleCommand(Aspect, Command, Value) ||
               Aspect == "PRODUCTION";
    }

    virtual FString QueryState(const FString& Aspect) const override
    {
//UE_LOG(LogTemp, Warning, TEXT("SS_Electrolysis::QueryState: %s"), *Aspect);
        FString Result = PWRJ_MultiSelectJunction::QueryState(Aspect);
        if (!Result.IsEmpty()) return Result;
        
        Result = OnOffPart.QueryState(Aspect);
        if (!Result.IsEmpty()) return Result;

        if (Aspect == "PRODUCTION") return FString::Printf(TEXT("%.2f"), HydrogenProductionRate);
        return TEXT("");
    }

    virtual TArray<FString> QueryEntireState() const override
    {
        TArray<FString> Out = PWRJ_MultiSelectJunction::QueryEntireState();
        Out.Append(OnOffPart.QueryEntireState());
        Out.Add(FString::Printf(TEXT("PRODUCTION SET %.2f"), HydrogenProductionRate));
        return Out;
    }

    virtual TArray<FString> GetAvailableCommands() const override
    {
        TArray<FString> Out = PWRJ_MultiSelectJunction::GetAvailableCommands();
        Out.Append(OnOffPart.GetAvailableCommands());
        Out.Add("PRODUCTION SET <float>");
        return Out;
    }

    /** Returns a list of aspects that can be queried. */
    virtual TArray<FString> GetAvailableQueries() const override
    {
        TArray<FString> Queries = PWRJ_MultiSelectJunction::GetAvailableQueries();
        Queries.Append(OnOffPart.GetAvailableQueries());
        Queries.Add("PRODUCTION");
        return Queries;
    }

    virtual float GetCurrentPowerUsage() const override
    {
        if (IsShutdown()) return 0.0f;
        return IsOn()?DefaultPowerUsage:0.1f;
    }

    virtual void Tick(float DeltaTime) override
    {
        if (OnOffPart.IsOn())
        {
            float NewHydrogen = SubState->H2Level + HydrogenProductionRate * DeltaTime;
            float NewLOX = SubState->LOXLevel + LOXProductionRate * DeltaTime;

            bool bHydrogenFull = NewHydrogen >= 1.0f;
            bool bLOXFull = NewLOX >= 1.0f;

            if (bHydrogenFull || bLOXFull)
            {
                OnOffPart.HandleCommand("ON", "SET", "false");
                PostHandleCommand();
                if (bHydrogenFull)
                {
                    AddToNotificationQueue(FString::Printf(TEXT("%s hydrogen tank full"), *SystemName));
                }
                if (bLOXFull)
                {
                    AddToNotificationQueue(FString::Printf(TEXT("%s LOX tank full"), *SystemName));
                }
                SubState->H2Level = 1.0f;
                SubState->LOXLevel = 1.0f;
            }
            else
            {
                SubState->H2Level = NewHydrogen;
                SubState->LOXLevel = NewLOX;
            }
        } else {
        	if (SubState->LOXLevel < 0.1f) {
                OnOffPart.HandleCommand("ON", "SET", "true");		// TODO: this is bad for silence
	            AddToNotificationQueue("ELECTROLYSIS ONLINE: LOX & H2 below 10%");
                PostHandleCommand();
        	}
        }
    }
    virtual bool IsOn() const override { return OnOffPart.IsOn(); }

	void RenderLabels(RenderingContext& Context) { return; }

	virtual void InitializeVisualElements() override
	{
		ICH_PowerJunction::InitializeVisualElements(); // Call base class if it does anything

		// Toggle Button Implementation
		FBox2D ToggleBounds(FVector2D(W-26, 2), FVector2D(W-2, 14)); 
		VisualElements.Add(new VE_ToggleButton(this, 
												 ToggleBounds,
												 TEXT("ON"),      // Query Aspect
												 TEXT("ON SET true"),    // Command On
												 TEXT("ON SET false"),   // Command Off
												 TEXT("true"),			// expected value for "ON"
												 TEXT("false"),			// expected value for "OFF"
												 TEXT("OFF"),          // Text On
												 TEXT("ON")          // Text Off
												 ));
	}

	virtual void RenderUnderlay(RenderingContext& Context) override
	{
		int32 erdy = 80;
		int32 fH = 32;
		int32 fW = W;
		FVector2D f, f2, t, t2;
		f.X = X + 18;
		f.Y = Y + erdy;
		t.X = f.X;
		t.Y = Y + fH/2;
		f2 = f;
		f2.X += 80;
		t2 = t;
		t2.X += 80;
		if (OnOffPart.IsOn())
		{
			float d = 0.0f;
			d += 0.25f*FMath::Sin(FPlatformTime::Seconds() * 8.0f);
			if (d < 0) d = 0;
			Context.DrawLine(f, t, FLinearColor(d, 1.0f, d), 12.0f);
			Context.DrawLine(f2, t2, FLinearColor(1.0f, d, d), 12.0f);
		}
		else
		{
			Context.DrawLine(f, t, FLinearColor(0.6f, 1.0f, 0.6f), 6.0f);
			Context.DrawLine(f2, t2, FLinearColor(1.0f, 0.6f, 0.6f), 6.0f);
		}
	}
};

