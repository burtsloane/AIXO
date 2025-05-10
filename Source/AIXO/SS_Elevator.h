#pragma once

#include "SubmarineState.h"
#include "SS_Elevator.h"
#include "SS_BowPlanes.h"
#include "ICommandHandler.h"
#include "PWRJ_MultiSelectJunction.h"
#include "ICH_Actuator.h"
#include <cmath>

class SS_Elevator : public PWRJ_MultiSelectJunction
{
protected:
    ICH_Actuator ActuatorPart;
    ASubmarineState* SubState;
    bool bIsMoving;

public:
    SS_Elevator(const FString& Name, ASubmarineState* InSubState, float InX, float InY, float InW=150, float InH=24)
        : PWRJ_MultiSelectJunction(Name, InX, InY, InW, InH),
          ActuatorPart(Name + "_Actuator", this)
        , SubState(InSubState)
    {
		DefaultPowerUsage = 1.5f; // Base power usage when active
		DefaultNoiseLevel = 0.3f; // Base noise level when active          
		bIsMoving = false;        
    }
	virtual FString GetTypeString() const override { return TEXT("SS_Elevator"); }

    virtual ECommandResult HandleCommand(const FString& Aspect, const FString& Command, const FString& Value) override
    {
        auto Result = ActuatorPart.HandleCommand(Aspect, Command, Value);
        if (Result != ECommandResult::NotHandled)
            return Result;
        return PWRJ_MultiSelectJunction::HandleCommand(Aspect, Command, Value);
    }

    virtual bool CanHandleCommand(const FString& Aspect, const FString& Command, const FString& Value) const override
    {
        return ActuatorPart.CanHandleCommand(Aspect, Command, Value) || PWRJ_MultiSelectJunction::CanHandleCommand(Aspect, Command, Value);
    }

    virtual FString QueryState(const FString& Aspect) const override
    {
        FString Result = ActuatorPart.QueryState(Aspect);
        return !Result.IsEmpty() ? Result : PWRJ_MultiSelectJunction::QueryState(Aspect);
    }

    virtual TArray<FString> QueryEntireState() const override
    {
        TArray<FString> Out = ActuatorPart.QueryEntireState();
        Out.Append(PWRJ_MultiSelectJunction::QueryEntireState());
        Out.Sort();
        return Out;
    }

    virtual TArray<FString> GetAvailableCommands() const override
    {
        TArray<FString> Out = ActuatorPart.GetAvailableCommands();
        Out.Append(PWRJ_MultiSelectJunction::GetAvailableCommands());
        Out.Sort();
        return Out;
    }

    virtual TArray<FString> GetAvailableQueries() const override
    {
        TArray<FString> Queries = ActuatorPart.GetAvailableQueries();
        Queries.Append(PWRJ_MultiSelectJunction::GetAvailableQueries());
        Queries.Sort();
        return Queries;
    }

    virtual float GetCurrentPowerUsage() const override
    {
		if (IsShutdown()) return 0.0f;
        return (!ActuatorPart.IsActive() || !ActuatorPart.IsMoving()) ? 0.1f : DefaultPowerUsage;
    }

    virtual float GetCurrentNoiseLevel() const override
    {
        return (IsShutdown() || !ActuatorPart.IsActive() || !ActuatorPart.IsMoving()) ? 0.0f : DefaultNoiseLevel;
    }

    virtual void Tick(float DeltaTime) override
    {
        ActuatorPart.Tick(DeltaTime);
        if (ActuatorPart.GetCurrentAngle() != ActuatorPart.GetTargetAngle()) {
        	VisualElements[0]->UpdateState();
        	bIsMoving = true;
		} else if (bIsMoving) {
        	VisualElements[0]->UpdateState();
        	bIsMoving = false;
		}

        // The actual movement logic is now handled within ActuatorPart.Tick().
        // This class (SS_Elevator) might need to read the CurrentAngle from ActuatorPart
        // if other systems (like SubmarineState) need it directly from here.
        // Example:
        SubState->ElevatorAngle = ActuatorPart.GetCurrentAngle();
    }

	virtual void RenderUnderlay(RenderingContext& Context) override
	{
		float R = 30;
		float R2 = 36;
		FVector2D a, b, c;
		a.X = X + W - 2;				a.Y = Y + H/2;
        b = a;
		b.X += R*FMath::Cos(GetCurrentAngle() * 1.0f);
		b.Y -= R*FMath::Sin(GetCurrentAngle() * 1.0f);
		Context.DrawLine(a, b, FLinearColor::Black, 4.0f);

		for (int i=-7; i<7; i++) {
			float ang = i*10*3.14159265f/180 * 1.0f;
			FVector2D a1 = a;
			a1.X += R*FMath::Cos(ang);
			a1.Y -= R*FMath::Sin(ang);
			b = a;
			b.X += R2*FMath::Cos(ang);
			b.Y -= R2*FMath::Sin(ang);
			Context.DrawLine(a1, b, FLinearColor::Black, ((i&1)==0)?2.0f:1.0f);
		}
	}

    void SetTargetAngle(float Angle)
    {
        ActuatorPart.SetTargetAngle(Angle);
    }

    float GetCurrentAngle() const
    {
        return ActuatorPart.GetCurrentAngle();
    }

	void RenderLabels(RenderingContext& Context)
	{
		return;
	}

	virtual void InitializeVisualElements() override
	{
		ICH_PowerJunction::InitializeVisualElements(); // Call base class if it does anything

		FBox2D ToggleBounds(FVector2D(6, 18), FVector2D(W-6, 18+12));
		VisualElements.Add(new VE_Slider(this, 
												 ToggleBounds,
												 VE_Slider::EOrientation::Horizontal,
												 -1.0f,
												 1.0f,
												 TEXT("ANGLE"),      // Query Aspect Commanded
												 TEXT("ACTUAL"),    // Query Aspect Actual
												 0.06125f,					// Step Value
												 TEXT("SET")   // Command verb
												 ));
	}

    friend class PID_PlaneControlBlender;
};


