#pragma once

#include "CoreMinimal.h"
#include "PWRJ_MultiSelectJunction.h"
#include "PWR_PowerSegment.h"
#include "ICommandHandler.h"
#include "VE_CommandButton.h"

/**
 * Specialization of MultiSelectJunction representing a Single Pole Double Throw switch.
 * Allows connection to one of two ports (index 1 or 2) or disconnection (index -1).
 */
class PWR_SPDTJunction : public PWRJ_MultiSelectJunction
{
public:
	/**
	 * Constructor
	 */
	PWR_SPDTJunction(const FString& InSystemName = TEXT("SPDTJunction"), float InX = 0, float InY = 0)
		: PWRJ_MultiSelectJunction(InSystemName, InX, InY)
	{
		// Visual Elements initialized via InitializeVisualElements()
	}

	/** Creates the visual elements specific to an SPDT Junction. */
	virtual void InitializeVisualElements() override
	{
		ICH_PowerJunction::InitializeVisualElements(); 

		// Define bounds and create three command buttons
		float ButtonWidth = 30.0f;
		float ButtonHeight = 20.0f;
		float ButtonSpacing = 5.0f;
		float TotalWidth = 3 * ButtonWidth + 2 * ButtonSpacing;
		float StartX = -TotalWidth / 2.0f;
		float PosY = 10.0f; // Position below the symbol

		// Button for Port 1
		FBox2D Port1Bounds(FVector2D(StartX, PosY), FVector2D(StartX + ButtonWidth, PosY + ButtonHeight));
		VisualElements.Add(new VE_CommandButton(this, Port1Bounds, TEXT("POWERSELECT"), TEXT("POWERSELECT SET 1"), "1", TEXT("P1")));

		// Button for Port 2
		StartX += ButtonWidth + ButtonSpacing;
		FBox2D Port2Bounds(FVector2D(StartX, PosY), FVector2D(StartX + ButtonWidth, PosY + ButtonHeight));
		VisualElements.Add(new VE_CommandButton(this, Port2Bounds, TEXT("POWERSELECT"), TEXT("POWERSELECT SET 2"), "2", TEXT("P2")));

		// Button for Off (-1)
		StartX += ButtonWidth + ButtonSpacing;
		FBox2D OffBounds(FVector2D(StartX, PosY), FVector2D(StartX + ButtonWidth, PosY + ButtonHeight));
		VisualElements.Add(new VE_CommandButton(this, OffBounds, TEXT("POWERSELECT"), TEXT("POWERSELECT SET -1"), "-1", TEXT("OFF")));
	}

	/**
	 * Adds a port, enforcing the SPDT limit (max 2 ports).
	 * @param Segment The power segment to connect to the new port.
	 * @return True if the port was added successfully, false otherwise (e.g., limit reached).
	 */
	virtual bool AddPort(PWR_PowerSegment* Segment, int32 InSideWhich, int32 InSideOffset) override
	{
		if (Ports.Num() >= 3)
		{
			// Log error or warning: Cannot add more than two ports to an SPDT junction.
			UE_LOG(LogTemp, Warning, TEXT("Attempted to add more than two ports to SPDT Junction: %s"), *SystemName);
			return false;
		}
		return PWRJ_MultiSelectJunction::AddPort(Segment, InSideWhich, InSideOffset);
	}

	/**
	 * Sets the selected port, enforcing SPDT limits (only port 1, 2 or -1 allowed).
	 * If not, we might need to override HandleCommand instead.
	 * @param NewPortIndex The desired port index (should be 1, 2 or -1).
	 * @return Command result indicating success or failure.
	 */
//	virtual ECommandResult SetSelectedPort(int32 NewPortIndex) override
//	{
//		if (NewPortIndex != -1 && NewPortIndex != 1 && NewPortIndex != 2)
//		{
//			UE_LOG(LogTemp, Warning, TEXT("Invalid port index %d for SPDT Junction: %s. Only 1, 2 or -1 allowed."), NewPortIndex, *SystemName);
//			return ECommandResult::HandledWithError;
//		}
//        // Check if selected port actually exists before allowing selection
//        if ((NewPortIndex == 1 && Ports.Num() < 1) || (NewPortIndex == 2 && Ports.Num() < 2))
//        {
//            UE_LOG(LogTemp, Warning, TEXT("Attempted to select non-existent port %d on SPDT Junction %s."), NewPortIndex, *SystemName);
//            return ECommandResult::HandledWithError; // Or InvalidParameter
//        }
//		return PWRJ_MultiSelectJunction::SetSelectedPort(NewPortIndex);
//	}

	/** Renders the specific SPDT symbol and its owned visual elements. */
	virtual void Render(RenderingContext& Context) override
	{
		FVector2D MyPosition = GetPosition();
		FLinearColor DrawColor = IsFaulted() || IsShutdown() ? FLinearColor::Red : FLinearColor::Black;
		int32 CurrentSelection = SelectedPort;

		// Draw SPDT specific symbol (example: circle with line to P1, P2, or Off)
		float Radius = 6.0f;
		Context.DrawCircle(MyPosition, Radius, DrawColor, false);
		FVector2D CommonPoint = MyPosition - FVector2D(Radius, 0); // Left side of circle
		FVector2D P1Point = MyPosition + FVector2D(Radius * 0.5f, -Radius * 1.5f); // Top-right ish
		FVector2D P2Point = MyPosition + FVector2D(Radius * 0.5f, Radius * 1.5f);  // Bottom-right ish

		// Draw terminals
		Context.DrawLine(CommonPoint - FVector2D(Radius*0.5f, 0), CommonPoint, DrawColor, 1.0f);
		Context.DrawLine(P1Point, P1Point + FVector2D(Radius*0.5f, 0), DrawColor, 1.0f);
		Context.DrawLine(P2Point, P2Point + FVector2D(Radius*0.5f, 0), DrawColor, 1.0f);
		
		// Draw switch line based on selection
		if (CurrentSelection == 1)
		{
			Context.DrawLine(CommonPoint, P1Point, DrawColor, 1.5f); 
		}
		else if (CurrentSelection == 2)
		{
			Context.DrawLine(CommonPoint, P2Point, DrawColor, 1.5f);
		}
		// Else (SelectedPort == -1 or invalid), draw no connection (or maybe dashed line?)

		// Update and Render owned visual elements (the command buttons)
		for (IVisualElement* Element : VisualElements)
		{
			if (Element)
			{
				Element->UpdateState(); // Ensure element reflects current state
				Element->Render(Context, MyPosition);
			}
		}
		
		// Draw name slightly offset
		Context.DrawText(MyPosition + FVector2D(Radius + 2, -4), SystemName, DrawColor); 
	}
}; 
