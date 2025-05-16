// PID_PlanePitch - Uses PID_PlaneControlBlender to apply opposing plane angles
#pragma once

#include "ICommandHandler.h"
#include "SubmarineState.h"
#include "PID_PlaneControlBlender.h"
#include <cmath>

class PID_PlanePitch : public ICommandHandler {
public:
    PID_PlanePitch(const FString &name, ASubmarineState* InState, PID_PlaneControlBlender* InBlender)
        : SubState(InState), Blender(InBlender), TargetPitch(0.0f), bEnabled(false),
          Kp(1.0f), Ki(0.0f), Kd(0.5f), LastError(0.0f), Integral(0.0f),
          StableTime(0.0f), LastPitch(0.0f) { SystemName = name; }

    virtual ECommandResult HandleCommand(const FString& Aspect, const FString& Command, const FString& Value) override {
        if (Aspect == "PITCH" && Command == "SET") {
            TargetPitch = FCString::Atof(*Value);
            return ECommandResult::Handled;
        }
        if (Aspect == "ENABLED" && Command == "SET") {
            bEnabled = Value.ToBool();
            if (bEnabled) AddToNotificationQueue("PLANE PITCH PID engaged");
            else AddToNotificationQueue("PLANE PITCH PID manually disengaged");
            return ECommandResult::Handled;
        }
        return ECommandResult::NotHandled;
    }

    virtual bool CanHandleCommand(const FString& Aspect, const FString& Command, const FString& Value) const override {
        return Aspect == "PITCH" || Aspect == "ENABLED";
    }

    virtual FString QueryState(const FString& Aspect) const override {
        if (Aspect == "PITCH") return FString::SanitizeFloat(TargetPitch);
        if (Aspect == "ENABLED") return bEnabled ? TEXT("true") : TEXT("false");
        return TEXT("");
    }

    virtual TArray<FString> QueryEntireState() const override {
        return {
            FString::Printf(TEXT("PITCH SET %.2f"), TargetPitch),
            FString::Printf(TEXT("ENABLED SET %s"), bEnabled ? TEXT("true") : TEXT("false"))
        };
    }

    virtual TArray<FString> GetAvailableCommands() const override {
        return {
            TEXT("PITCH SET <float>"),
            TEXT("ENABLED SET <bool>")
        };
    }

    /** Returns a list of aspects that can be queried. */
    virtual TArray<FString> GetAvailableQueries() const override
    {
        return { "PITCH", "ENABLED" };
    }

    void Tick(float DeltaTime) {
        if (!bEnabled || !Blender) return;

        float Speed = SubState->Velocity.Size();
        if (Speed < 0.1f) {
            AddToNotificationQueue("PLANE PITCH PID disengaged: sub not moving");
            bEnabled = false;
            return;
        }

        float CurrentPitch = SubState->SubmarineRotation.Pitch;
        float Error = TargetPitch - CurrentPitch;

        Integral += Error * DeltaTime;
        float Derivative = (Error - LastError) / DeltaTime;
        float Output = Kp * Error + Ki * Integral + Kd * Derivative;
        LastError = Error;

        Blender->SetPitchOutput(Output);

        if (FMath::Abs(CurrentPitch - LastPitch) < 0.1f) {
            StableTime += DeltaTime;
            if (StableTime > 4.0f) {
                AddToNotificationQueue("PLANE PITCH PID disengaged: pitch unresponsive");
                bEnabled = false;
            }
        } else {
            StableTime = 0.0f;
        }
        LastPitch = CurrentPitch;
    }

private:
    ASubmarineState* SubState;
    PID_PlaneControlBlender* Blender;
    float TargetPitch;
    bool bEnabled;
    float Kp, Ki, Kd;
    float LastError;
    float Integral;
    float StableTime;
    float LastPitch;
};
