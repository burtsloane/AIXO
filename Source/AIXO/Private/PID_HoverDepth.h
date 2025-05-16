// PID_HoverDepth - Maintains target depth using FTBT and RTBT pumps
#pragma once

#include "ICommandHandler.h"
#include "SubmarineState.h"
#include "SS_FTBTPump.h"
#include "SS_RTBTPump.h"
#include <cmath>

class PID_HoverDepth : public ICommandHandler {
public:
    PID_HoverDepth(const FString &name, ASubmarineState& InState, SS_FTBTPump* InFrontPump, SS_RTBTPump* InRearPump)
        : SubState(InState), FTBT(InFrontPump), RTBT(InRearPump),
          TargetDepth(0.0f), bEnabled(false),
          Kp(0.5f), Ki(0.0f), Kd(0.3f),
          LastError(0.0f), Integral(0.0f),
          StableTime(0.0f), LastDepth(0.0f) { SystemName = name; }

    virtual ECommandResult HandleCommand(const FString& Aspect, const FString& Command, const FString& Value) override {
        if (Aspect == "DEPTH" && Command == "SET") {
            TargetDepth = FCString::Atof(*Value);
            return ECommandResult::Handled;
        }
        if (Aspect == "ENABLED" && Command == "SET") {
            bEnabled = Value.ToBool();
            if (bEnabled) AddToNotificationQueue("HOVER PID engaged");
            else AddToNotificationQueue("HOVER PID manually disengaged");
            return ECommandResult::Handled;
        }
        return ECommandResult::NotHandled;
    }

    virtual bool CanHandleCommand(const FString& Aspect, const FString& Command, const FString& Value) const override {
        return Aspect == "DEPTH" || Aspect == "ENABLED";
    }

    virtual FString QueryState(const FString& Aspect) const override {
        if (Aspect == "DEPTH") return FString::SanitizeFloat(TargetDepth);
        if (Aspect == "ENABLED") return bEnabled ? TEXT("true") : TEXT("false");
        return TEXT("");
    }

    virtual TArray<FString> QueryEntireState() const override {
        return {
            FString::Printf(TEXT("DEPTH SET %.2f"), TargetDepth),
            FString::Printf(TEXT("ENABLED SET %s"), bEnabled ? TEXT("true") : TEXT("false"))
        };
    }

    virtual TArray<FString> GetAvailableCommands() const override {
        return {
            TEXT("DEPTH SET <float>"),
            TEXT("ENABLED SET <bool>")
        };
    }

    /** Returns a list of aspects that can be queried. */
    virtual TArray<FString> GetAvailableQueries() const override
    {
        return { "DEPTH", "ENABLED" };
    }

    void Tick(float DeltaTime) {
        if (!bEnabled || !FTBT || !RTBT) return;

        // Detect plane-based control conflict
        if (FMath::Abs(SubState.LeftBowPlanesAngle) > 1.0f || 
        	FMath::Abs(SubState.RightBowPlanesAngle) > 1.0f || 
        	FMath::Abs(SubState.ElevatorAngle) > 1.0f) {
            if (SubState.Velocity.Size() > 0.2f) {
                AddToNotificationQueue("HOVER PID conflict: plane-based depth control detected");
                bEnabled = false;
                return;
            }
        }

        float CurrentDepth = SubState.SubmarineLocation.Z;
        float Error = TargetDepth - CurrentDepth;

        Integral += Error * DeltaTime;
        float Derivative = (Error - LastError) / DeltaTime;
        float Output = Kp * Error + Ki * Integral + Kd * Derivative;
        LastError = Error;

        // Clamp pump rate within safety margin (0.1 to 0.9 fill level assumed)
        bool LimitedFront = SubState.ForwardTBTLevel < 0.1f || SubState.ForwardTBTLevel > 0.9f;
        bool LimitedRear = SubState.RearTBTLevel < 0.1f || SubState.RearTBTLevel > 0.9f;

        float SafeOutput = FMath::Clamp(Output, -1.0f, 1.0f);
        FTBT->SetPumpRate(LimitedFront ? 0.0f : SafeOutput);
        RTBT->SetPumpRate(LimitedRear ? 0.0f : SafeOutput);

        // Detect failure to change depth
        if (FMath::Abs(CurrentDepth - LastDepth) < 0.05f) {
            StableTime += DeltaTime;
            if (StableTime > 5.0f) {
                AddToNotificationQueue("HOVER PID disengaged: depth unresponsive");
                bEnabled = false;
            }
        } else {
            StableTime = 0.0f;
        }
        LastDepth = CurrentDepth;
    }

private:
    ASubmarineState& SubState;
    SS_FTBTPump* FTBT;
    SS_RTBTPump* RTBT;
    float TargetDepth;
    bool bEnabled;
    float Kp, Ki, Kd;
    float LastError;
    float Integral;
    float StableTime;
    float LastDepth;
};

// SS_FTBTPump and SS_RTBTPump must declare:
// friend class PID_HoverDepth;
