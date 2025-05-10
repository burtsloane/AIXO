// PID_PlaneDepthRate - Uses PID_PlaneControlBlender to apply depth rate adjustments
#pragma once

#include "ICommandHandler.h"
#include "SubmarineState.h"
#include "PID_PlaneControlBlender.h"
#include <cmath>

class PID_PlaneDepthRate : public ICommandHandler {
public:
    PID_PlaneDepthRate(const FString &name, ASubmarineState& InState, PID_PlaneControlBlender* InBlender)
        : SubState(InState), Blender(InBlender), TargetRate(0.0f), bEnabled(false),
          Kp(1.0f), Ki(0.0f), Kd(0.4f), LastError(0.0f), Integral(0.0f),
          StableTime(0.0f), LastDepth(0.0f) { SystemName = name; }

    virtual ECommandResult HandleCommand(const FString& Aspect, const FString& Command, const FString& Value) override {
        if (Aspect == "RATE" && Command == "SET") {
            TargetRate = FCString::Atof(*Value);
            return ECommandResult::Handled;
        }
        if (Aspect == "ENABLED" && Command == "SET") {
            bEnabled = Value.ToBool();
            if (bEnabled) AddToNotificationQueue("PLANE DEPTH RATE PID engaged");
            else AddToNotificationQueue("PLANE DEPTH RATE PID manually disengaged");
            return ECommandResult::Handled;
        }
        return ECommandResult::NotHandled;
    }

    virtual bool CanHandleCommand(const FString& Aspect, const FString& Command, const FString& Value) const override {
        return Aspect == "RATE" || Aspect == "ENABLED";
    }

    virtual FString QueryState(const FString& Aspect) const override {
        if (Aspect == "RATE") return FString::SanitizeFloat(TargetRate);
        if (Aspect == "ENABLED") return bEnabled ? TEXT("true") : TEXT("false");
        return TEXT("");
    }

    virtual TArray<FString> QueryEntireState() const override {
        return {
            FString::Printf(TEXT("RATE SET %.2f"), TargetRate),
            FString::Printf(TEXT("ENABLED SET %s"), bEnabled ? TEXT("true") : TEXT("false"))
        };
    }

    virtual TArray<FString> GetAvailableCommands() const override {
        return {
            TEXT("RATE SET <float>"),
            TEXT("ENABLED SET <bool>")
        };
    }

    /** Returns a list of aspects that can be queried. */
    virtual TArray<FString> GetAvailableQueries() const override
    {
        return { "RATE", "ENABLED" };
    }

    void Tick(float DeltaTime) {
        if (!bEnabled || !Blender) return;

        float Speed = SubState.Velocity.Size();
        if (Speed < 0.1f) {
            AddToNotificationQueue("PLANE DEPTH RATE PID disengaged: sub not moving");
            bEnabled = false;
            return;
        }

        float DepthChange = (SubState.SubmarineLocation.Z - LastDepth) / DeltaTime;
        float Error = TargetRate - DepthChange;

        Integral += Error * DeltaTime;
        float Derivative = (Error - LastError) / DeltaTime;
        float Output = Kp * Error + Ki * Integral + Kd * Derivative;
        LastError = Error;

        Blender->SetDepthRateOutput(Output);

        if (FMath::Abs(DepthChange) < 0.05f) {
            StableTime += DeltaTime;
            if (StableTime > 4.0f) {
                AddToNotificationQueue("PLANE DEPTH RATE PID disengaged: no depth change");
                bEnabled = false;
            }
        } else {
            StableTime = 0.0f;
        }
        LastDepth = SubState.SubmarineLocation.Z;
    }

private:
    ASubmarineState& SubState;
    PID_PlaneControlBlender* Blender;
    float TargetRate;
    bool bEnabled;
    float Kp, Ki, Kd;
    float LastError;
    float Integral;
    float StableTime;
    float LastDepth;
};
