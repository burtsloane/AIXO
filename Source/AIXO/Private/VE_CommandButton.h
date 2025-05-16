#pragma once

#include "IVisualElement.h"
#include "ICH_PowerJunction.h"
#include "PWRJ_MultiSelectJunction.h"
#include "CommandDistributor.h"

/**
 * Represents a button that sends a specific command when tapped.
 * It can visually indicate if the owner's state matches the state this button represents.
 */
class VE_CommandButton : public IVisualElement
{
protected:
    FBox2D RelativeBounds;       // Bounds relative to the owner
    bool bRepresentsCurrentState = false; // Should this button appear highlighted/active?

    // Configuration
    FString CommandToSend;        // The exact command string fragment (e.g., "POWERSELECT 1")
    FString StateValueRepresented;  // The state value this button corresponds to (e.g., 1 for ON, -1 for OFF)
    FString DisplayText;          // Text to display on the button (e.g., "ON", "OFF")
    FString QueryAspect;

public:
    VE_CommandButton(ICH_PowerJunction* InOwner, 
                     const FBox2D& InRelativeBounds,
                     const FString& InQueryAspect,
                     const FString& InCommandToSend, // e.g., "POWERSELECT 1"
                     const FString& InStateValueRepresented, // e.g., 1
                     const FString& InDisplayText)
        : IVisualElement(InOwner),
          RelativeBounds(InRelativeBounds),
          CommandToSend(InCommandToSend),
          StateValueRepresented(InStateValueRepresented),
          DisplayText(InDisplayText),
          QueryAspect(InQueryAspect)
    {
        UpdateState(); // Get initial state active status
    }

    /** Updates whether this button represents the owner's current state. */
    virtual void UpdateState() override
    {
        if (Owner)
        {
            // instead, query aspect "POWERSELECT", and compare to a string
            FString q = Owner->QueryState(QueryAspect);
            bRepresentsCurrentState = q == StateValueRepresented;
        }
        else
        {
            bRepresentsCurrentState = false;
        }
    }

    /** Renders the button, highlighting if it represents the current state. */
    virtual void Render(RenderingContext& Context, const FVector2D& OwnerPosition) override
    {
        FBox2D WorldBounds(RelativeBounds.Min + OwnerPosition, RelativeBounds.Max + OwnerPosition);
        // Highlight if it represents the current state, dim otherwise
        FLinearColor FillColor = bRepresentsCurrentState ? FLinearColor::Green : FLinearColor::White;
        FLinearColor BorderColor = FLinearColor::Black;

        Context.DrawRectangle(WorldBounds, FillColor, true);
        Context.DrawRectangle(WorldBounds, BorderColor, false); // Outline
        Context.DrawText(WorldBounds.GetCenter() - FVector2D(10, 6), DisplayText, BorderColor); // Adjust text position
    }

    /** Returns the configured relative bounds. */
    virtual FBox2D GetRelativeBounds() const override
    {
        return RelativeBounds;
    }

    /** Handles touch events to send the configured command. */
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
						// Send the command(s)
						TArray<FString> CommandLines;
						CommandToSend.ParseIntoArray(CommandLines, TEXT("\n"), true);

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
        return false; 
    }
}; 
