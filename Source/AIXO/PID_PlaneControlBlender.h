// PID_PlaneControlBlender - Combines outputs from pitch and depth rate PIDs
#pragma once

#include "SubmarineState.h"
#include "SS_Elevator.h"
#include "SS_BowPlanes.h"
#include "ICommandHandler.h"
#include <cmath>

class PID_PlaneControlBlender : public ICommandHandler {
public:
    PID_PlaneControlBlender(const FString& Name, ASubmarineState& InState, SS_Elevator* InElevator, SS_BowPlanes* InBowPlanes)
        : SubState(InState), Elevator(InElevator)
        , BowPlanes(InBowPlanes)
        , PitchOut(0.0f)
        , DepthOut(0.0f)
        , bPitchActive(false)
        , bDepthActive(false)
    {
    	SystemName = Name;
    }

    void SetPitchOutput(float Value) {
        PitchOut = Value;
        bPitchActive = true;
    }

    void SetDepthRateOutput(float Value) {
        DepthOut = Value;
        bDepthActive = true;
    }

    virtual void Tick(float DeltaTime) override {
        constexpr float MaxDeflection = 20.0f;

        float pitch = bPitchActive ? PitchOut : 0.0f;
        float depth = bDepthActive ? DepthOut : 0.0f;

        float Bow = -pitch + depth;
        float Elev = +pitch + depth;

        if (BowPlanes)
        {
            BowPlanes->SetTargetAngle(FMath::Clamp(Bow, -MaxDeflection, MaxDeflection));
        }
        if (Elevator)
        {
            Elevator->SetTargetAngle(FMath::Clamp(Elev, -MaxDeflection, MaxDeflection));
        }

        bPitchActive = false;
        bDepthActive = false;
    }

    // ICommandHandler stubs
    virtual ECommandResult HandleCommand(const FString& Aspect, const FString& Command, const FString& Value) override { return ECommandResult::NotHandled; }
    virtual bool CanHandleCommand(const FString& Aspect, const FString& Command, const FString& Value) const override { return false; }
    virtual FString QueryState(const FString& Aspect) const override { return TEXT(""); }
    virtual TArray<FString> QueryEntireState() const override { return {}; }
    virtual TArray<FString> GetAvailableCommands() const override { return {}; }

    /** Returns a list of aspects that can be queried (none for this class). */
    virtual TArray<FString> GetAvailableQueries() const override { return {}; }

    friend class PID_PlaneDepthRate;
    friend class PID_PlanePitch;

private:
    ASubmarineState& SubState;
    SS_Elevator* Elevator;
    SS_BowPlanes* BowPlanes;
    float PitchOut;
    float DepthOut;
    bool bPitchActive;
    bool bDepthActive;
};
