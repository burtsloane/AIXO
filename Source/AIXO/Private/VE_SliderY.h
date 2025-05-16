#pragma once

#include "IVisualElement.h"
#include "ICommandHandler.h" // Base class for owner, QueryState
#include "CommandDistributor.h"
#include "Components/ActorComponent.h" // For FMath if needed
#include "VE_Slider.h"

/**
 * Base class for a slider visual element.
 * Handles dragging, value clamping, state querying, command sending,
 * and provides virtual methods for custom rendering.
 */
class VE_SliderY : public VE_Slider
{
public:
    VE_SliderY(ICH_PowerJunction* InOwner, // Correct Owner type
              const FBox2D& InRelativeBounds,
              EOrientation InOrientation,
              float InMinValue,
              float InMaxValue,
              const FString& InQueryAspectTarget,
              const FString& InQueryAspectActual = TEXT(""), // Optional
              float InStepValue = 0.0f,
              const FString& InCommandVerb = TEXT("SET"))
        : VE_Slider(InOwner, InRelativeBounds, InOrientation, InMinValue, InMaxValue, InQueryAspectTarget, InQueryAspectActual, InStepValue, InCommandVerb)
    {
    }

    virtual void Render(RenderingContext& Context, const FVector2D& OwnerPosition) override
    {
        FBox2D WorldBounds;
//        WorldBounds.Min.X = Owner->X;
//        WorldBounds.Min.Y = Owner->Y;
//        WorldBounds.Max = WorldBounds.Min;
//        WorldBounds.Max.X += Owner->W;
//        WorldBounds.Max.Y += Owner->H;
//    	if (FMath::Abs(CurrentActualValue) > (1 - 0.06f*2)) {
//			if (((int32)(FPlatformTime::Seconds() / 0.5f)) & 1) {
//	        	Context.DrawRectangle(WorldBounds, FLinearColor::Red, true);
//	        	RenderName(Context, OwnerPosition);
//	        	Context.DrawRectangle(WorldBounds, FLinearColor::Black, false);
//			}
//    	} else if (FMath::Abs(CurrentActualValue) > (1 - 0.14f*2)) {
//			if (((int32)(FPlatformTime::Seconds() / 1.0f)) & 1) {
//	        	Context.DrawRectangle(WorldBounds, FLinearColor(0.75f, 0.75f, 0.0f), true);
//	        	RenderName(Context, OwnerPosition);
//	        	Context.DrawRectangle(WorldBounds, FLinearColor::Black, false);
//			}
//    	}
        WorldBounds = GetWorldBounds(OwnerPosition);
        Context.DrawRectangle(WorldBounds, FLinearColor::White, true);
		FBox2D r;
		r = WorldBounds;
		float w = r.Max.X - r.Min.X;
		r.Max.X = r.Min.X + w * 0.14f;
        Context.DrawRectangle(r, FLinearColor(0.75f, 0.75f, 0.0f), true);
		r = WorldBounds;
		r.Max.X = r.Min.X + w * 0.06f;
        Context.DrawRectangle(r, FLinearColor::Red, true);
		r = WorldBounds;
		r.Min.X = r.Max.X - w * 0.14f;
        Context.DrawRectangle(r, FLinearColor(0.75f, 0.57f, 0.0f), true);
		r = WorldBounds;
		r.Min.X = r.Max.X - w * 0.06f;
        Context.DrawRectangle(r, FLinearColor::Red, true);

// maybe? track temperature, increases in red, decreases <70%
//   do we need a visible temperature bar? does extended high temperature cause damage?

        Context.DrawRectangle(WorldBounds, FLinearColor::Black, false);			// border

        // Calculate track and thumb positions based on orientation
		RenderTrackAndThumb(Context, OwnerPosition);
    }
}; 
