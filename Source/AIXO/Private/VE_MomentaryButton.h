#pragma once

#include "IVisualElement.h"
#include "ICH_PowerJunction.h" // Owner type
#include "ICommandHandler.h" // For QueryState and ECommandResult
#include "CommandDistributor.h" // To send commands

/**
 * A concrete IVisualElement representing a toggle button (e.g., On/Off).
 * Queries its owner's state via QueryState(QueryAspect) and sends specific commands on touch.
 */
class VE_MomentaryButton : public IVisualElement
{
protected:
    FBox2D RelativeBounds; // Bounds relative to the owner
    bool bIsCurrentlyOn = false; // Locally cached state for rendering
    int32 iCurrentState = 0;	// state 0 is off, 1 is on, and -1 is other

    // --- Configuration --- 
    FString QueryAspect;          // Aspect string used to query the owner's state (e.g., "AB", "PORT_3")
    FString ExpectedStateOn = "true"; // The string value returned by QueryState when considered ON (case-insensitive)
    FString ExpectedStateOff = "false"; // The string value returned by QueryState when considered OFF (case-insensitive)
    
    FString CommandToSendOn;      // Command fragment to send for turning ON (e.g., "AB SET true", "PORT ENABLE 3")
    FString CommandToSendOff;     // Command fragment to send for turning OFF (e.g., "AB SET false", "PORT DISABLE 3")

    FString TextOn = "ON";        // Display text when ON
    FString TextOff = "OFF";      // Display text when OFF

public:
    VE_MomentaryButton(ICH_PowerJunction* InOwner, 
                     const FBox2D& InRelativeBounds,
                     const FString& InQueryAspect,
                     const FString& InCommandToSendOn,
                     const FString& InCommandToSendOff,
                     const FString& InExpectedStateOn = "true",
                     const FString& InExpectedStateOff = "false",
                     const FString& InTextOn = "ON",
                     const FString& InTextOff = "OFF")
        : IVisualElement(InOwner),
          RelativeBounds(InRelativeBounds),
          QueryAspect(InQueryAspect),
          ExpectedStateOn(InExpectedStateOn),
          ExpectedStateOff(InExpectedStateOff),
          TextOn(InTextOn),
          TextOff(InTextOff)
    {
    	CommandToSendOn = InCommandToSendOn;
    	CommandToSendOff = InCommandToSendOff;
        UpdateState(); // Get initial state
//if (InTextOn.Len() != InTextOff.Len()) UE_LOG(LogTemp, Warning, TEXT("VE_MomentaryButton: string length mismatch '%s' '%s'"), *InTextOn, *InTextOff);
    }

    /** Updates the locally cached state by querying the owner via QueryState. */
    virtual void UpdateState() override
    {
        if (Owner)
        {
            FString CurrentStateValue = Owner->QueryState(QueryAspect); // Use ICommandHandler interface
//UE_LOG(LogTemp, Warning, TEXT("UpdateState(): query(%s)=%s (on=%s)"), *QueryAspect, *CurrentStateValue, *ExpectedStateOn);
            bIsCurrentlyOn = CurrentStateValue.Equals(ExpectedStateOn, ESearchCase::IgnoreCase);
            iCurrentState = -1;
            if (CurrentStateValue.Equals(ExpectedStateOn, ESearchCase::IgnoreCase)) iCurrentState = 1;
            if (CurrentStateValue.Equals(ExpectedStateOff, ESearchCase::IgnoreCase)) iCurrentState = 0;
        }
        else
        {
            bIsCurrentlyOn = false;
            iCurrentState = 0;
        }
    }

    /** Renders the button, showing its current state. */
    virtual void Render(RenderingContext& Context, const FVector2D& OwnerPosition) override
    {
        FBox2D WorldBounds(RelativeBounds.Min + OwnerPosition, RelativeBounds.Max + OwnerPosition);
        FLinearColor FillColor;
        FString DisplayText;
        FLinearColor BorderColor = FLinearColor::Black;
        switch (iCurrentState) {
        	case -1:		// unrecognized state
        		FillColor = FLinearColor::White;
				DisplayText = "";
        		break;
        	case 0:			// off
        		FillColor = FLinearColor::White;
        		DisplayText = TextOff;
        		break;
        	case 1:			// on
				if (((int32)(FPlatformTime::Seconds() / 0.5f)) & 1) {
					FillColor = FLinearColor::Green;
					DisplayText = TextOn;
				} else {
					FillColor = FLinearColor::Green;
					DisplayText = "";
				}
        		DisplayText = TextOn;
        		break;
        }

        Context.DrawRectangle(WorldBounds, FillColor, true);
        Context.DrawRectangle(WorldBounds, BorderColor, false);
        Context.DrawText(WorldBounds.GetCenter() - FVector2D(DisplayText.Len() * 4, 6), DisplayText, BorderColor); 
    }

    /** Returns the configured relative bounds. */
    virtual FBox2D GetRelativeBounds() const override
    {
        return RelativeBounds;
    }

    /** Handles touch events to toggle the state by sending the configured command. */
    virtual bool HandleTouch(const TouchEvent& Event, CommandDistributor* Distributor) override
    {
        if (!Owner || !Distributor) return false;
//UE_LOG(LogTemp, Warning, TEXT("HandleTouch(): type=%d"), (int32)Event.Type);
		FBox2D WorldBounds(RelativeBounds.Min + Owner->GetPosition(), RelativeBounds.Max + Owner->GetPosition());
		WorldBounds.Min.X -= 4;
		WorldBounds.Min.Y -= 4;
		WorldBounds.Max.X += 4;
		WorldBounds.Max.Y += 4;
		bool isInside = WorldBounds.IsInside(Event.Position);

		switch (Event.Type) {
			case TouchEvent::EType::Down:
				{
					if (isInside)
					{
						// Determine the command to send (toggle to the *opposite* state)
						const FString& CommandFragment = CommandToSendOn;
						
						// Send the command(s)
						TArray<FString> CommandLines;
						CommandFragment.ParseIntoArray(CommandLines, TEXT("\n"), true);

						ECommandResult Result = ECommandResult::NotHandled;
						for (const FString& Line : CommandLines)
						{
							FString OneCommand = FString::Printf(TEXT("%s.%s"), *Owner->GetSystemName(), *Line);
							Result = Distributor->ProcessCommand(OneCommand);
							if (Result == ECommandResult::HandledWithError) {
								return true; // Handled the touch event
							}
						}

						// Update visual state immediately if command was likely successful
						if(Result != ECommandResult::HandledWithError && Result != ECommandResult::NotHandled)
						{
							 UpdateState(); // Re-query state after sending command
						}

						return true; // grab attention for this VE
					}
//else UE_LOG(LogTemp, Warning, TEXT("     %g,%g not in %s"), Event.Position.X, Event.Position.Y, *WorldBounds.ToString());
				}
				break;
			case TouchEvent::EType::Move:
				break;
			case TouchEvent::EType::Up:
				{
					if (isInside)
					{
						// Determine the command to send (toggle to the *opposite* state)
						const FString& CommandFragment = CommandToSendOff;
						
						// Send the command(s)
						TArray<FString> CommandLines;
						CommandFragment.ParseIntoArray(CommandLines, TEXT("\n"), true);

						ECommandResult Result = ECommandResult::NotHandled;
						for (const FString& Line : CommandLines)
						{
							FString OneCommand = FString::Printf(TEXT("%s.%s"), *Owner->GetSystemName(), *Line);
							Result = Distributor->ProcessCommand(OneCommand);
							if (Result == ECommandResult::HandledWithError) {
								return true; // Handled the touch event
							}
						}

						// Update visual state immediately if command was likely successful
						if(Result != ECommandResult::HandledWithError && Result != ECommandResult::NotHandled)
						{
							 UpdateState(); // Re-query state after sending command
						}

						return true; // Handled the touch event
					}
				}
				break;
			case TouchEvent::EType::Cancel:
				break;
		}

        return false; // Didn't handle this event type or touch was outside bounds
    }
}; 
