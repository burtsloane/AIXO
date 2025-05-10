#pragma once

#include "ICommandHandler.h"
#include "PWRJ_MultiSelectJunction.h"
#include "ICH_Actuator.h"
#include "VE_Slider.h"

class SS_BowPlanes : public PWRJ_MultiSelectJunction
{
protected:
    // Has-a relationship with ICH_Actuator for its movement logic
    ICH_Actuator ActuatorPart;
    ASubmarineState* SubState;
    bool bIsMoving;

public:
    SS_BowPlanes(const FString& Name, ASubmarineState* InSubState, float InX, float InY, float InW=150, float InH=24)
        : PWRJ_MultiSelectJunction(Name, InX, InY, InW, InH), // Pass power/noise to PWR base
          ActuatorPart(Name + "_Actuator", this), // Initialize composed ActuatorPart
          SubState(InSubState)
    {
        // SystemName, X, Y are set by PWRJ_MultiSelectJunction base
		DefaultPowerUsage = 0.75f; // Base power usage when active
		DefaultNoiseLevel = 0.3f; // Base noise level when active  
		bIsMoving = false;        
    }
	virtual FString GetTypeString() const override { return TEXT("SS_BowPlanes"); }

    // --- ICommandHandler Overrides: Delegate to ActuatorPart or PWR base ---

    virtual ECommandResult HandleCommand(const FString& Aspect, const FString& Command, const FString& Value) override
    {
//UE_LOG(LogTemp, Warning, TEXT("SS_BowPlanes::HandleCommand %s %s %s"), *Aspect, *Command, *Value);
        // Try actuator-specific commands (ANGLE SET)
        auto Result = ActuatorPart.HandleCommand(Aspect, Command, Value);
        if (Result != ECommandResult::NotHandled)
            return Result;
        // If not handled by actuator, try power-related commands via base class
        return PWRJ_MultiSelectJunction::HandleCommand(Aspect, Command, Value);
    }

    virtual bool CanHandleCommand(const FString& Aspect, const FString& Command, const FString& Value) const override
    {
        // Check if either the actuator part or the power part can handle it
        return ActuatorPart.CanHandleCommand(Aspect, Command, Value) || PWRJ_MultiSelectJunction::CanHandleCommand(Aspect, Command, Value);
    }

    virtual FString QueryState(const FString& Aspect) const override
    {
//UE_LOG(LogTemp, Warning, TEXT("SS_BowPlanes::QueryState %s"), *Aspect);
        // Try actuator-specific queries (ANGLE, ACTUATOR state)
        FString Result = ActuatorPart.QueryState(Aspect);
        if (!Result.IsEmpty()) 
            return Result;
        // If not handled by actuator, try power-related queries via base class
        return PWRJ_MultiSelectJunction::QueryState(Aspect);
    }

    virtual TArray<FString> QueryEntireState() const override
    {
        // Get state from actuator part (ANGLE SET, ACTUATOR ACTIVATE/DEACTIVATE)
        TArray<FString> Out = ActuatorPart.QueryEntireState();
        Out.Append(PWRJ_MultiSelectJunction::QueryEntireState());
        Out.Sort();
        return Out;
    }

    virtual TArray<FString> GetAvailableCommands() const override
    {
        // Get commands from actuator part
        TArray<FString> Out = ActuatorPart.GetAvailableCommands();
        // Append commands from power part (base class)
        Out.Append(PWRJ_MultiSelectJunction::GetAvailableCommands());
        Out.Sort();
        return Out;
    }

    virtual TArray<FString> GetAvailableQueries() const override
    {
        // Get queries from actuator part
        TArray<FString> Queries = ActuatorPart.GetAvailableQueries();
        // Append queries from power part (base class)
        Queries.Append(PWRJ_MultiSelectJunction::GetAvailableQueries());
        Queries.Sort();
        return Queries;
    }

    // Override GetCurrentPowerUsage/GetCurrentNoiseLevel based on actuator activity
    virtual float GetCurrentPowerUsage() const override
    {
         // Use power if actuator is active and moving (requires accessors on ICH_Actuator)
         if (IsShutdown()) return 0.0f;
         return (!ActuatorPart.IsActive() || !ActuatorPart.IsMoving()) ? 0.1f : DefaultPowerUsage;
    }

    virtual float GetCurrentNoiseLevel() const override
    {
         // Make noise if actuator is active and moving
         return (IsShutdown() || !ActuatorPart.IsActive() || !ActuatorPart.IsMoving()) ? 0.0f : DefaultNoiseLevel;
    }

    virtual void Tick(float DeltaTime) override
    {
        // Call Tick on composed part
        ActuatorPart.Tick(DeltaTime); 
        if (ActuatorPart.GetCurrentAngle() != ActuatorPart.GetTargetAngle()) {
        	VisualElements[0]->UpdateState();
        	bIsMoving = true;
		} else if (bIsMoving) {
        	VisualElements[0]->UpdateState();
        	bIsMoving = false;
		}
        // Base class Tick is empty

        // The actual movement logic is now handled within ActuatorPart.Tick().
        // This class (SS_BowPlanes) might need to read the CurrentAngle from ActuatorPart
        // if other systems (like SubmarineState) need it directly from here.
        // Example: 
        //  SubState->BowPlanesAngle = ActuatorPart.GetCurrentAngle();
        if (SystemName == "RBP") {
        	SubState->RightBowPlanesAngle = ActuatorPart.GetCurrentAngle();
        } else if (SystemName == "LBP") {
        	SubState->LeftBowPlanesAngle = ActuatorPart.GetCurrentAngle();
        } 
    }

	virtual void RenderUnderlay(RenderingContext& Context) override
	{
		float R = 22;
		float R2 = 28;
		FVector2D a, a1, b, c;
        if (SystemName == "RBP") {
			a.X = X + W - 2;				a.Y = Y + H/2;
			b = a;
			b.X += R*FMath::Cos(GetCurrentAngle() * 1.0f);
			b.Y -= R*FMath::Sin(GetCurrentAngle() * 1.0f);
			Context.DrawLine(a, b, FLinearColor::Black, 4.0f);
			
			for (int i=-7; i<7; i++) {
				if (i&1) continue;
				float ang = i*10*3.14159265f/180 * 1.0f;
				a1 = a;
				a1.X += R*FMath::Cos(ang);
				a1.Y -= R*FMath::Sin(ang);
				b = a;
				b.X += R2*FMath::Cos(ang);
				b.Y -= R2*FMath::Sin(ang);
				Context.DrawLine(a1, b, FLinearColor::Black, ((i&1)==0)?2.0f:1.0f);
			}
        } else if (SystemName == "LBP") {
			a.X = X + 2;				a.Y = Y + H/2;
			b = a;
			b.X -= R*FMath::Cos(GetCurrentAngle() * 1.0f);
			b.Y -= R*FMath::Sin(GetCurrentAngle() * 1.0f);
			Context.DrawLine(a, b, FLinearColor::Black, 4.0f);
			
			for (int i=-7; i<7; i++) {
				if (i&1) continue;
				float ang = i*10*3.14159265f/180 * 1.0f;
				a1 = a;
				a1.X -= R*FMath::Cos(ang);
				a1.Y -= R*FMath::Sin(ang);
				b = a;
				b.X -= R2*FMath::Cos(ang);
				b.Y -= R2*FMath::Sin(ang);
				Context.DrawLine(a1, b, FLinearColor::Black, ((i&1)==0)?2.0f:1.0f);
			}
        } 
	}

    // Expose ActuatorPart's TargetAngle if needed by PID_PlaneControlBlender
    // Option 1: Direct access (requires friend or public ActuatorPart)
    // Option 2: Setter method
    void SetTargetAngle(float Angle)
    {
         // Delegate to ActuatorPart using its HandleCommand or a direct setter
        ActuatorPart.SetTargetAngle(Angle);
    }

    float GetCurrentAngle() const
    {
         return ActuatorPart.GetCurrentAngle(); // Requires getter in ICH_Actuator
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

    friend class PID_PlaneControlBlender; // Still needed if PID needs access
};

