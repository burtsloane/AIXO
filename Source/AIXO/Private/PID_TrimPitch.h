// PID_TrimPitch - Maintains submarine pitch by controlling cross-TBT water balance
#pragma once

#include "ICommandHandler.h"
#include "SubmarineState.h"
#include "SS_XTBTPump.h"
#include <cmath>

class PID_TrimPitch : public ICommandHandler {
public:
    PID_TrimPitch(const FString &name, ASubmarineState& InState, SS_XTBTPump* InPump)
        : SubState(InState), Pump(InPump), TargetPitch(0.0f), bEnabled(false),
          Kp(0.5f), Ki(0.0f), Kd(0.2f), LastError(0.0f), Integral(0.0f),
          StableTime(0.0f), LastPitch(0.0f) { SystemName = name; }

    virtual ECommandResult HandleCommand(const FString& Aspect, const FString& Command, const FString& Value) override {
        if (Aspect == "TRIM" && Command == "SET") {
            TargetPitch = FCString::Atof(*Value);
            return ECommandResult::Handled;
        }
        if (Aspect == "ENABLED" && Command == "SET") {
            bEnabled = Value.ToBool();
            if (bEnabled) AddToNotificationQueue("TRIM PID engaged");
            else AddToNotificationQueue("TRIM PID manually disengaged");
            return ECommandResult::Handled;
        }
        return ECommandResult::NotHandled;
    }

    virtual bool CanHandleCommand(const FString& Aspect, const FString& Command, const FString& Value) const override {
        return Aspect == "TRIM" || Aspect == "ENABLED";
    }

    virtual FString QueryState(const FString& Aspect) const override {
        if (Aspect == "TRIM") return FString::SanitizeFloat(TargetPitch);
        if (Aspect == "ENABLED") return bEnabled ? TEXT("true") : TEXT("false");
        return TEXT("");
    }

    virtual TArray<FString> QueryEntireState() const override {
        return {
            FString::Printf(TEXT("TRIM SET %.2f"), TargetPitch),
            FString::Printf(TEXT("ENABLED SET %s"), bEnabled ? TEXT("true") : TEXT("false"))
        };
    }

    virtual TArray<FString> GetAvailableCommands() const override {
        return {
            TEXT("TRIM SET <float>"),
            TEXT("ENABLED SET <bool>")
        };
    }

    /** Returns a list of aspects that can be queried. */
    virtual TArray<FString> GetAvailableQueries() const override
    {
        return { "TRIM", "ENABLED" };
    }

    void Tick(float DeltaTime) {
        if (!bEnabled || !Pump) return;

        FVector Velocity = SubState.Velocity;
        float Speed = Velocity.Size();
        if (Speed > 0.05f) {
            AddToNotificationQueue("TRIM PID aborted: submarine in motion");
            bEnabled = false;
            return;
        }

        float CurrentPitch = SubState.SubmarineRotation.Pitch;
        float Error = TargetPitch - CurrentPitch;

        Integral += Error * DeltaTime;
        float Derivative = (Error - LastError) / DeltaTime;
        float Output = Kp * Error + Ki * Integral + Kd * Derivative;
        LastError = Error;

        Pump->SetPumpRate(FMath::Clamp(Output, -1.0f, 1.0f));

        // Detect lack of pitch correction
        if (FMath::Abs(CurrentPitch - LastPitch) < 0.1f) {
            StableTime += DeltaTime;
            if (StableTime > 5.0f) {
                AddToNotificationQueue("TRIM PID disengaged: pitch unresponsive");
                bEnabled = false;
            }
        } else {
            StableTime = 0.0f;
        }
        LastPitch = CurrentPitch;
    }

private:
    ASubmarineState& SubState;
    SS_XTBTPump* Pump;
    float TargetPitch;
    bool bEnabled;
    float Kp, Ki, Kd;
    float LastError;
    float Integral;
    float StableTime;
    float LastPitch;
};

// SS_XTBTPump must declare:
// friend class PID_TrimPitch;
