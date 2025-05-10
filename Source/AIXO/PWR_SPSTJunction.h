#pragma once

#include "CoreMinimal.h"
#include "PWRJ_MultiSelectJunction.h"
#include "PWR_PowerSegment.h"
#include "ICommandHandler.h"
#include "VE_ToggleButton.h"
#include "VE_CommandButton.h"

/**
 * Specialization of MultiSelectJunction representing a Single Pole Single Throw switch.
 * Allows connection to a single port (index 1) or disconnection (index -1).
 */
class PWR_SPSTJunction : public PWRJ_MultiSelectJunction
{
public:
	/**
	 * Constructor
	 */
	PWR_SPSTJunction(const FString& InSystemName = TEXT("SPSTJunction"), float InX = 0, float InY = 0)
		: PWRJ_MultiSelectJunction(InSystemName, InX, InY)
	{
		// Note: Visual Elements should be initialized *after* construction via InitializeVisualElements()
	}

	/** Creates the visual elements specific to an SPST Junction. */
	virtual void InitializeVisualElements() override
	{
		ICH_PowerJunction::InitializeVisualElements(); // Call base class if it does anything

		/*
		// Define bounds for the ON button (e.g., left side below symbol)
		FBox2D OnButtonBounds(FVector2D(-35, 10), FVector2D(-5, 30)); 
		VisualElements.Add(new VE_CommandButton(this, 
												OnButtonBounds, 
												TEXT("POWERSELECT"),
												TEXT("POWERSELECT SET 1"), // Command to send
												"1",                // State value this represents
												TEXT("ON")     // Display text
												));
		
		// Define bounds for the OFF button (e.g., right side below symbol)
		FBox2D OffButtonBounds(FVector2D(5, 10), FVector2D(35, 30)); 
		VisualElements.Add(new VE_CommandButton(this, 
												 OffButtonBounds, 
												 TEXT("POWERSELECT"),
												 TEXT("POWERSELECT SET -1"), // Command to send
												 "-1",               // State value this represents
												 TEXT("OFF")    // Display text
												 ));
		*/

		// --- Previous Toggle Button Implementation (commented out) ---
		FBox2D ToggleBounds(FVector2D(-15, 10), FVector2D(15, 30)); 
		VisualElements.Add(new VE_ToggleButton(this, 
												 ToggleBounds,
												 TEXT("POWERSELECT"),      // Query Aspect
												 TEXT("POWERSELECT SET 1"),    // Command On
												 TEXT("POWERSELECT SET -1"),   // Command Off
												 TEXT("1"),           // Expected State On is "1"
												 TEXT("0"),			// expected value for "OFF"
												 TEXT("ON"),          // Text On
												 TEXT("OFF")          // Text Off
												 ));
	}

	/**
	 * Adds a port, enforcing the SPST limit (max 1 port).
	 * @param Segment The power segment to connect to the new port.
	 * @return True if the port was added successfully, false otherwise (e.g., limit reached).
	 */
	virtual bool AddPort(PWR_PowerSegment* Segment, int32 InSideWhich, int32 InSideOffset) override
	{
		if (Ports.Num() >= 2)
		{
			// Log error or warning: Cannot add more than one port to an SPST junction.
			UE_LOG(LogTemp, Warning, TEXT("Attempted to add more than one port to SPST Junction: %s"), *SystemName);
			return false;
		}
		return PWRJ_MultiSelectJunction::AddPort(Segment, InSideWhich, InSideOffset);
	}

	/**
	 * Sets the selected port, enforcing SPST limits (only port 1 or -1 allowed).
	 * If not, we might need to override HandleCommand instead.
	 * @param NewPortIndex The desired port index (should be 1 or -1).
	 * @return Command result indicating success or failure.
	 */
//	virtual ECommandResult SetSelectedPort(int32 NewPortIndex) override
//	{
//		if (NewPortIndex != -1 && NewPortIndex != 1)
//		{
//			UE_LOG(LogTemp, Warning, TEXT("Invalid port index %d for SPST Junction: %s. Only 1 or -1 allowed."), NewPortIndex, *SystemName);
//			return ECommandResult::HandledWithError;
//		}
//        // Check if port 1 actually exists before allowing selection
//        if (NewPortIndex == 1 && Ports.Num() < 1)
//        {
//            UE_LOG(LogTemp, Warning, TEXT("Attempted to select port 1 on SPST Junction %s which has no ports."), *SystemName);
//            return ECommandResult::HandledWithError;
//        }
//		return PWRJ_MultiSelectJunction::SetSelectedPort(NewPortIndex);
//	}

	/** Renders the specific SPST symbol and its owned visual elements. */
	virtual void Render(RenderingContext& Context) override
	{
		FVector2D MyPosition = GetPosition();
		FLinearColor DrawColor = IsFaulted() || IsShutdown() ? FLinearColor::Red : FLinearColor::Black;
		bool bIsOn = (SelectedPort == 1);

		// Draw SPST specific symbol (example: circle with a line indicating state)
		float Radius = 6.0f;
		Context.DrawCircle(MyPosition, Radius, DrawColor, false);
		Context.DrawLine(MyPosition - FVector2D(Radius, 0), MyPosition + FVector2D(Radius, 0), DrawColor, 1.0f); // Base line
		if (bIsOn)
		{
			 // Draw line angled up to show connection
			 Context.DrawLine(MyPosition - FVector2D(Radius, 0), MyPosition + FVector2D(0, -Radius * 1.5f), DrawColor, 1.0f); 
		}
		else
		{
			// Draw line angled down to show disconnection
			Context.DrawLine(MyPosition - FVector2D(Radius, 0), MyPosition + FVector2D(0, Radius * 1.5f), DrawColor, 1.0f);
		}

		// Update and Render owned visual elements (like the toggle button)
		for (IVisualElement* Element : VisualElements)
		{
			if (Element)
			{
				Element->UpdateState(); // Ensure element reflects current state
				Element->Render(Context, MyPosition);
			}
		}
		
		// Draw name slightly offset from symbol/elements
		Context.DrawText(MyPosition + FVector2D(Radius + 2, -4), SystemName, DrawColor); 
	}
};
