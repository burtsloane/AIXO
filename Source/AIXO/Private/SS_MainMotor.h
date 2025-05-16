#pragma once

#include "ICommandHandler.h"
#include "PWRJ_MultiSelectJunction.h"
#include "VE_CommandButton.h"
#include "VE_SliderY.h"

class SS_MainMotor : public PWRJ_MultiSelectJunction
{
protected:
    ASubmarineState* SubState;
    float Throttle = 0.0f; // range 0.0 - 1.0
    bool bSilentRunning = false;
    float CavitationThreshold = 0.66f; // 2/3 throttle and up causes cavitation
    float PowerFactor; // Store the factor locally
    float NoiseFactorNormal;
    float NoiseFactorSilent;

public:
    SS_MainMotor(const FString& Name, ASubmarineState* InSubState, float InX, float InY, float InW=150, float InH=24)
        : PWRJ_MultiSelectJunction(Name, InX, InY, InW, InH), // Pass factors as defaults?
          SubState(InSubState)
    {
		DefaultPowerUsage = 5.0f; // Base power usage when active
		DefaultNoiseLevel = 0.5f; // Base noise level when active          
		PowerFactor = 10.0f;       
		NoiseFactorNormal = 0.5f;        
		NoiseFactorSilent = 0.1f; 
    }
	virtual FString GetTypeString() const override { return TEXT("SS_MainMotor"); }

    virtual ECommandResult HandleCommand(const FString& Aspect, const FString& Command, const FString& Value) override
    {
//UE_LOG(LogTemp, Warning, TEXT("SS_MainMotor::HandleCommand: %s %s %s"), *Aspect, *Command, *Value);
        if (Aspect == "THROTTLE" && Command == "SET") {
            float NewThrottle = FCString::Atof(*Value);
            Throttle = FMath::Clamp(NewThrottle, -1.0f, 1.0f);

            if (bSilentRunning && Throttle > CavitationThreshold) {
                AddToNotificationQueue("WARNING: CAVITATION LIKELY DURING SILENT RUNNING");
            }
            return ECommandResult::Handled;
        }
        else if (Aspect == "THROTTLE" && Command == "GET") {
            return ECommandResult::NotHandled; // handled via QueryState
        }
        else if (Aspect == "SILENT" && Command == "SET") {
            bSilentRunning = (Value == "TRUE" || Value == "true" || Value == "1");
            return ECommandResult::Handled;
        }

        return PWRJ_MultiSelectJunction::HandleCommand(Aspect, Command, Value);
    }

    virtual bool CanHandleCommand(const FString& Aspect, const FString& Command, const FString& Value) const override
    {
        return (Aspect == "THROTTLE" && Command == "SET") ||
               (Aspect == "SILENT" && Command == "SET") ||
               PWRJ_MultiSelectJunction::CanHandleCommand(Aspect, Command, Value);
    }

    virtual FString QueryState(const FString& Aspect) const override
    {
//UE_LOG(LogTemp, Warning, TEXT("SS_MainMotor::QueryState: %s"), *Aspect);
        if (Aspect == "THROTTLE")
            return FString::SanitizeFloat(Throttle);
        if (Aspect == "SILENT")
            return bSilentRunning ? TEXT("true") : TEXT("false");
        return PWRJ_MultiSelectJunction::QueryState(Aspect);
    }

    virtual TArray<FString> QueryEntireState() const override
    {
        TArray<FString> Out;
        Out.Add(FString::Printf(TEXT("THROTTLE SET %.2f"), Throttle));
        Out.Add(FString::Printf(TEXT("SILENT SET %s"), bSilentRunning ? TEXT("true") : TEXT("false")));
        Out.Append(PWRJ_MultiSelectJunction::QueryEntireState());
        return Out;
    }

    virtual TArray<FString> GetAvailableCommands() const override
    {
        TArray<FString> Out = {
            TEXT("THROTTLE SET <-1.0..1.0>"),
            TEXT("SILENT SET <bool>")
        };
        Out.Append(PWRJ_MultiSelectJunction::GetAvailableCommands());
        return Out;
    }

    /** Returns a list of aspects that can be queried. */
    virtual TArray<FString> GetAvailableQueries() const override
    {
        TArray<FString> Queries = PWRJ_MultiSelectJunction::GetAvailableQueries(); // Inherit base queries
        Queries.Append({ "THROTTLE", "SILENT", "POWERLEVEL", "NOISELEVEL" }); // Add specific queries
        return Queries;
    }

    /** Power usage depends on throttle */
    virtual float GetCurrentPowerUsage() const override 
    {
    	if (IsShutdown()) return 0.0f;
        float p = FMath::Abs(Throttle) * PowerFactor;
        if (p <= 0.0f) p = 0.1f;
        return p;
    }

    /** Noise depends on throttle and silent running mode */
    virtual float GetCurrentNoiseLevel() const override 
    {
        if (IsShutdown()) return 0.0f;
        float BaseNoise = bSilentRunning ? NoiseFactorSilent : NoiseFactorNormal;
        return FMath::Abs(Throttle) * BaseNoise;
    }

    float GetThrottle() const { return Throttle; }
    bool IsSilentRunning() const { return bSilentRunning; }

    virtual void Tick(float DeltaTime) override
    {
        if (!HasPower()) return;
        // Main motor functionality here
    }

	void RenderLabels(RenderingContext& Context)
	{
		return;
	}

    virtual FLinearColor RenderBGGetColor()
    {
        FLinearColor c = PWRJ_MultiSelectJunction::RenderBGGetColor();
    	if (FMath::Abs(Throttle) > (1 - 0.06f*2)) {
			if (((int32)(FPlatformTime::Seconds() / 0.5f)) & 1) {
				c = FLinearColor::Red;
			}
    	} else if (FMath::Abs(Throttle) > (1 - 0.14f*2)) {
			if (((int32)(FPlatformTime::Seconds() / 1.0f)) & 1) {
				c = FLinearColor(0.75f, 0.75f, 0.0f);
			}
    	}
		return c;
    }

	virtual void InitializeVisualElements() override
	{
		ICH_PowerJunction::InitializeVisualElements(); // Call base class if it does anything

		// Toggle Button Implementation
//		FBox2D ToggleBounds(FVector2D(W-26, 2), FVector2D(W-2, 2+12));
//		VisualElements.Add(new VE_CommandButton(this, 
//												 ToggleBounds,
//												 TEXT("SILENT"),      // Query Aspect
//												 TEXT("SILENT SET true"),    // Command On
//												 TEXT("true"),			// expected value for "SILENT"
//												 TEXT("SIL")          // Display
//												 ));
//		FBox2D ToggleBounds1(FVector2D(W-54, 2), FVector2D(W-28, 2+12));
//		VisualElements.Add(new VE_CommandButton(this, 
//												 ToggleBounds1,
//												 TEXT("SILENT"),      // Query Aspect
//												 TEXT("SILENT SET false"),    // Command On
//												 TEXT("false"),			// expected value for "SILENT"
//												 TEXT("LOU")          // Display
//												 ));
		FBox2D ToggleBounds2(FVector2D(12, 18), FVector2D(W-12, 18+12));
		VisualElements.Add(new VE_SliderY(this, 
												 ToggleBounds2,
												 VE_Slider::EOrientation::Horizontal,
												 -1.0f,
												 1.0f,
												 TEXT("THROTTLE"),      // Query Aspect Commanded
												 TEXT("THROTTLE"),    // Query Aspect Actual
												 0.06125f,					// Step Value
												 TEXT("SET")   // Command verb
												 ));
	}
};

