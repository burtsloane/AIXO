// PID_RudderHeading - Maintains true heading adjusted for local current
#pragma once

#include "ICommandHandler.h"
#include "SubmarineState.h"
#include "SS_Rudder.h"
#include <cmath>

class PID_RudderHeading : public ICommandHandler {
public:
    PID_RudderHeading(const FString &name, ASubmarineState* InState, SS_Rudder* InRudder)
        : SubState(InState)
        , Rudder(InRudder)
        , Kp(2.0f)
        , Ki(0.0f)
        , Kd(1.0f)
        , LastError(0.0f)
        , Integral(0.0f)
        , bEnabled(false)
        , DesiredHeading(0.0f)
        , HeadingStableTime(0.0f)
        , LastHeading(0.0f)
    {
		SystemName = name;
    }

    virtual ECommandResult HandleCommand(const FString& Aspect, const FString& Command, const FString& Value) override {
        if (Aspect == "HEADING" && Command == "SET") {
            DesiredHeading = FCString::Atof(*Value);
            return ECommandResult::Handled;
        }
        if (Aspect == "ENABLED" && Command == "SET") {
            bEnabled = Value.ToBool();
            if (bEnabled) AddToNotificationQueue("RUDDER PID engaged");
            else AddToNotificationQueue("RUDDER PID manually disengaged");
            return ECommandResult::Handled;
        }
        if (Aspect == "ANGLE" && Command == "SET") {
            bEnabled = false;
            AddToNotificationQueue("RUDDER PID disengaged due to manual override");
        }
        return ECommandResult::NotHandled;
    }

    virtual bool CanHandleCommand(const FString& Aspect, const FString& Command, const FString& Value) const override {
        return Aspect == "HEADING" || Aspect == "ENABLED" || Aspect == "ANGLE";
    }

    virtual FString QueryState(const FString& Aspect) const override {
        if (Aspect == "HEADING") return FString::SanitizeFloat(DesiredHeading);
        if (Aspect == "ENABLED") return bEnabled ? TEXT("true") : TEXT("false");
        return TEXT("");
    }

    virtual TArray<FString> QueryEntireState() const override {
        return {
            FString::Printf(TEXT("HEADING SET %.2f"), DesiredHeading),
            FString::Printf(TEXT("ENABLED SET %s"), bEnabled ? TEXT("true") : TEXT("false"))
        };
    }

    virtual TArray<FString> GetAvailableCommands() const override {
        return {
            TEXT("HEADING SET <float>"),
            TEXT("ENABLED SET <bool>")
        };
    }

    /** Returns a list of aspects that can be queried. */
    virtual TArray<FString> GetAvailableQueries() const override
    {
        return { "HEADING", "ENABLED" };
    }

    virtual void Tick(float DeltaTime) override {
        if (!bEnabled || !Rudder) return;

// TODO:        FVector TrueVelocity = SubState->Velocity - SubState->CurrentVector;
        FVector TrueVelocity = SubState->Velocity;
        float ActualHeading = FMath::RadiansToDegrees(FMath::Atan2(TrueVelocity.Y, TrueVelocity.X));
        float Error = FMath::UnwindDegrees(DesiredHeading - ActualHeading);

        Integral += Error * DeltaTime;
        float Derivative = (Error - LastError) / DeltaTime;
        float Output = Kp * Error + Ki * Integral + Kd * Derivative;
        LastError = Error;

        Rudder->SetTargetAngle(FMath::Clamp(Output, -30.0f, 30.0f));

        // Heading response detection
        if (FMath::Abs(ActualHeading - LastHeading) < 0.5f) {
            HeadingStableTime += DeltaTime;
            if (HeadingStableTime > 3.0f) {
                AddToNotificationQueue("RUDDER PID disengaged: heading unresponsive");
                bEnabled = false;
            }
        } else {
            HeadingStableTime = 0.0f;
        }
        LastHeading = ActualHeading;
    }

private:
    ASubmarineState* SubState;
    SS_Rudder* Rudder;
    float Kp, Ki, Kd;
    float LastError;
    float Integral;
    bool bEnabled;
    float DesiredHeading;
    float HeadingStableTime;
    float LastHeading;
};

// SS_Rudder class must declare:
// friend class PID_RudderHeading;
