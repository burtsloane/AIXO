#pragma once

#include "IVisualElement.h"
#include "ICommandHandler.h" // Base class for owner, QueryState
#include "CommandDistributor.h"
#include "Components/ActorComponent.h" // For FMath if needed

/**
 * Base class for a slider visual element.
 * Handles dragging, value clamping, state querying, command sending,
 * and provides virtual methods for custom rendering.
 */
class VE_Slider : public IVisualElement
{
public:
    enum class EOrientation { Horizontal, Vertical };

protected:
    FBox2D RelativeBounds; // Bounds for the entire slider control (track + potential padding)
    EOrientation Orientation;
    float MinValue;
    float MaxValue;
    float StepValue = 0.0f; // 0 means continuous

    // State Query & Command Config
    FString QueryAspectTarget;      // Aspect to query/set the target value (e.g., "TARGET_ANGLE")
    FString QueryAspectActual;      // Optional aspect to query the actual value (e.g., "CURRENT_ANGLE")
    FString CommandVerb = TEXT("SET"); // Verb used for sending commands

    // Internal State
    float CurrentTargetValue = 0.0f; // Last queried target value
    float CurrentActualValue = 0.0f; // Last queried actual value
    float DraggingThumbValue = 0.0f; // Value corresponding to current touch drag position
    bool bIsDragging = false;

public:
    VE_Slider(ICH_PowerJunction* InOwner, // Correct Owner type
              const FBox2D& InRelativeBounds,
              EOrientation InOrientation,
              float InMinValue,
              float InMaxValue,
              const FString& InQueryAspectTarget,
              const FString& InQueryAspectActual = TEXT(""), // Optional
              float InStepValue = 0.0f,
              const FString& InCommandVerb = TEXT("SET"))
        : IVisualElement(InOwner), // Pass correct type to base
          RelativeBounds(InRelativeBounds),
          Orientation(InOrientation),
          MinValue(InMinValue),
          MaxValue(InMaxValue),
          StepValue(InStepValue),
          QueryAspectTarget(InQueryAspectTarget),
          QueryAspectActual(InQueryAspectActual),
          CommandVerb(InCommandVerb)
    {
        UpdateState();
        DraggingThumbValue = CurrentTargetValue; // Initialize drag value
    }

    /** Updates internal state by querying the owner. */
    virtual void UpdateState() override
    {
        if (!Owner) return;

        // Query Target Value
        FString TargetValueStr = Owner->QueryState(QueryAspectTarget);
        CurrentTargetValue = FCString::Atof(*TargetValueStr); // Convert string from query to float
        CurrentTargetValue = FMath::Clamp(CurrentTargetValue, MinValue, MaxValue);

        // Query Actual Value (if aspect is configured)
        if (!QueryAspectActual.IsEmpty())
        { 
            FString ActualValueStr = Owner->QueryState(QueryAspectActual);
            CurrentActualValue = FCString::Atof(*ActualValueStr);
            CurrentActualValue = FMath::Clamp(CurrentActualValue, MinValue, MaxValue);
        }
        else
        {
            CurrentActualValue = CurrentTargetValue; // Default to target if actual not queried
        }

        // If not currently dragging, keep the dragging thumb synced
        if (!bIsDragging)
        { 
            DraggingThumbValue = CurrentTargetValue;
        }
    }

    /** Renders the base slider track, thumb, and text. Calls RenderSecondaryIndicator. */
    virtual void Render(RenderingContext& Context, const FVector2D& OwnerPosition) override
    {
        FBox2D WorldBounds = GetWorldBounds(OwnerPosition);
        Context.DrawRectangle(WorldBounds, FLinearColor::Black, false);

        // Calculate track and thumb positions based on orientation
		RenderTrackAndThumb(Context, OwnerPosition);
    }

    /** Renders the base slider track, thumb, and text. Calls RenderSecondaryIndicator. */
    virtual void RenderTrackAndThumb(RenderingContext& Context, const FVector2D& OwnerPosition)
    {
        FBox2D WorldBounds = GetWorldBounds(OwnerPosition);
        float ValueRange = MaxValue - MinValue;
        float CurrentRatio = (ValueRange > KINDA_SMALL_NUMBER) ? FMath::Clamp((DraggingThumbValue - MinValue) / ValueRange, 0.0f, 1.0f) : 0.5f;
        FVector2D TrackStart, TrackEnd, ThumbCenter;
        float ThumbSize = 5.0f;
        if (Orientation == EOrientation::Horizontal)
        { 
            TrackStart = FVector2D(WorldBounds.Min.X, WorldBounds.GetCenter().Y);
            TrackEnd = FVector2D(WorldBounds.Max.X, WorldBounds.GetCenter().Y);
            ThumbCenter = FMath::Lerp(TrackStart, TrackEnd, CurrentRatio);
        }
        else // Vertical
        { 
            TrackStart = FVector2D(WorldBounds.GetCenter().X, WorldBounds.Max.Y); // Bottom is MinValue
            TrackEnd = FVector2D(WorldBounds.GetCenter().X, WorldBounds.Min.Y);   // Top is MaxValue
            ThumbCenter = FMath::Lerp(TrackStart, TrackEnd, CurrentRatio);
        }

        // Draw Track
        Context.DrawLine(TrackStart, TrackEnd, FLinearColor(0.4f, 0.4f, 0.4f), 2.0f);

        // Draw Thumb
        Context.DrawCircle(ThumbCenter, ThumbSize, FLinearColor::Black, true);

        // Draw Value Text (example position)
//        FString ValueText = FString::Printf(TEXT("%.1f"), DraggingThumbValue);
//        Context.DrawText(WorldBounds.Min + FVector2D(5, -15), ValueText, FLinearColor::Black);

        // Call virtual function for subclasses to draw secondary indicators (like actual value)
        RenderSecondaryIndicator(Context, OwnerPosition, CurrentActualValue);
    }

    /** Virtual function for derived classes to draw additional indicators (e.g., actual value). */
    virtual void RenderSecondaryIndicator(RenderingContext& Context, const FVector2D& OwnerPosition, float ActualValue) const
    {
        // Base implementation does nothing
        FBox2D WorldBounds = GetWorldBounds(OwnerPosition);
        FVector2D TrackStart, TrackEnd, ThumbCenter;
        float ThumbSize = 5.0f;
        float ValueRange = MaxValue - MinValue;
        float CurrentRatio = (ValueRange > KINDA_SMALL_NUMBER) ? FMath::Clamp((ActualValue - MinValue) / ValueRange, 0.0f, 1.0f) : 0.5f;

        // Calculate track and thumb positions based on orientation
        if (Orientation == EOrientation::Horizontal)
        { 
            TrackStart = FVector2D(WorldBounds.Min.X, WorldBounds.GetCenter().Y);
            TrackEnd = FVector2D(WorldBounds.Max.X, WorldBounds.GetCenter().Y);
            ThumbCenter = FMath::Lerp(TrackStart, TrackEnd, CurrentRatio);
        }
        else // Vertical
        { 
            TrackStart = FVector2D(WorldBounds.GetCenter().X, WorldBounds.Max.Y); // Bottom is MinValue
            TrackEnd = FVector2D(WorldBounds.GetCenter().X, WorldBounds.Min.Y);   // Top is MaxValue
            ThumbCenter = FMath::Lerp(TrackStart, TrackEnd, CurrentRatio);
        }

        Context.DrawCircle(ThumbCenter, ThumbSize, FLinearColor::Black, false);
    }

    /** Returns the configured relative bounds. */
    virtual FBox2D GetRelativeBounds() const override
    {
        return RelativeBounds;
    }

    /** Handles touch events for dragging the slider thumb. */
    virtual bool HandleTouch(const TouchEvent& Event, CommandDistributor* Distributor) override
    {
//if (!Owner) UE_LOG(LogTemp, Warning, TEXT("VE_Slider::HandleTouch(): Owner is null"));
//if (!Distributor) UE_LOG(LogTemp, Warning, TEXT("VE_Slider::HandleTouch(): Distributor is null"));
        if (!Owner || !Distributor) return false;

		FBox2D WorldBounds(RelativeBounds.Min + Owner->GetPosition(), RelativeBounds.Max + Owner->GetPosition());
		WorldBounds.Min.X -= 4;
		WorldBounds.Min.Y -= 4;
		WorldBounds.Max.X += 4;
		WorldBounds.Max.Y += 4;
//UE_LOG(LogTemp, Warning, TEXT("VE_Slider::HandleTouch(): start %d %.2f,%.2f"), (int)Event.Type, Event.Position.X, Event.Position.Y);
//UE_LOG(LogTemp, Warning, TEXT("                        : rect %.2f,%.2f %.2f,%.2f"), WorldBounds.Min.X, WorldBounds.Min.Y, WorldBounds.Max.X, WorldBounds.Max.Y);
        bool bHandled = false;

        if (Event.Type == TouchEvent::EType::Down)
        {
            if (WorldBounds.IsInside(Event.Position)) // Check touch is within overall bounds
            {
                bIsDragging = true;
                UpdateDraggingValue(Event.Position, WorldBounds);
                SendValueCommand(Distributor, DraggingThumbValue);
                bHandled = true;
//UE_LOG(LogTemp, Warning, TEXT("VE_Slider::HandleTouch(): * isDragging=true val=%.2f"), DraggingThumbValue);
            }
//else UE_LOG(LogTemp, Warning, TEXT("VE_Slider::HandleTouch(): not inside"));
        }
        else if (Event.Type == TouchEvent::EType::Move && bIsDragging)
        {
            UpdateDraggingValue(Event.Position, WorldBounds);
            // Optional: Throttle command sending on move?
            SendValueCommand(Distributor, DraggingThumbValue);
            UpdateState(); // Refresh state from owner after final command
//UE_LOG(LogTemp, Warning, TEXT("VE_Slider::HandleTouch(): isDragging=true val=%.2f"), DraggingThumbValue);
            bHandled = true;
        }
        else if (Event.Type == TouchEvent::EType::Up && bIsDragging)
        {
            bIsDragging = false;
//UE_LOG(LogTemp, Warning, TEXT("VE_Slider::HandleTouch(): isDragging=false val=%.2f"), DraggingThumbValue);
            UpdateDraggingValue(Event.Position, WorldBounds);
            SendValueCommand(Distributor, DraggingThumbValue);
            UpdateState(); // Refresh state from owner after final command
            bHandled = true;
        }
        else if (Event.Type == TouchEvent::EType::Cancel && bIsDragging)
        {
             bIsDragging = false;
//UE_LOG(LogTemp, Warning, TEXT("VE_Slider::HandleTouch(): isDragging=false"));
             // Optional: Revert DraggingThumbValue to CurrentTargetValue?
             DraggingThumbValue = CurrentTargetValue;
             bHandled = true;
        }

        return bHandled;
    }

protected:
    /** Calculates the value based on touch position along the slider track. */
    void UpdateDraggingValue(const FVector2D& TouchPosition, const FBox2D& WorldBounds)
    {
        float Ratio = 0.0f;
        if (Orientation == EOrientation::Horizontal)
        { 
            float TrackWidth = WorldBounds.GetSize().X;
            Ratio = (TrackWidth > KINDA_SMALL_NUMBER) ? FMath::Clamp((TouchPosition.X - WorldBounds.Min.X) / TrackWidth, 0.0f, 1.0f) : 0.5f;
        }
        else // Vertical
        { 
            float TrackHeight = WorldBounds.GetSize().Y;
            Ratio = (TrackHeight > KINDA_SMALL_NUMBER) ? FMath::Clamp((WorldBounds.Max.Y - TouchPosition.Y) / TrackHeight, 0.0f, 1.0f) : 0.5f; // Inverted Y
        }

        DraggingThumbValue = FMath::Lerp(MinValue, MaxValue, Ratio);

        // Apply snapping if StepValue is set
        if (StepValue > KINDA_SMALL_NUMBER)
        {
            DraggingThumbValue = FMath::RoundToFloat(DraggingThumbValue / StepValue) * StepValue;
        }
        
        // Final clamp to ensure validity
        DraggingThumbValue = FMath::Clamp(DraggingThumbValue, MinValue, MaxValue);
    }

    /** Sends the command to set the owner's state. */
    void SendValueCommand(CommandDistributor* Distributor, float Value)
    {
        if (!Owner || !Distributor) return;
        FString ValueString = FString::Printf(TEXT("%f"), Value); // Format value
        
        // Construct command fragment: "<QueryAspectTarget> <CommandVerb> <ValueString>"
        FString CommandFragment = FString::Printf(TEXT("%s %s %s"), 
                                               *QueryAspectTarget, 
                                               *CommandVerb, 
                                               *ValueString);

        // Prepend system name and send
        FString FullCommand = FString::Printf(TEXT("%s.%s"), 
                                            *Owner->GetSystemName(), 
                                            *CommandFragment);
                                            
        Distributor->ProcessCommand(FullCommand);
    }

    /** Helper to get world bounds */
    FBox2D GetWorldBounds(const FVector2D& OwnerPosition) const {
         return FBox2D(RelativeBounds.Min + OwnerPosition, RelativeBounds.Max + OwnerPosition);
    }
}; 
