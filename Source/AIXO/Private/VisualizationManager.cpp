#include "VisualizationManager.h"
#include "ICH_PowerJunction.h"
#include "PWR_PowerSegment.h"
#include "PWR_PowerPropagation.h"
// #include "RenderingContext.h" // REMOVE THIS
#include "IVisualElement.h" // Includes RenderingContext definition
#include "CommandDistributor.h"
#include "LlamaComponent.h"          // For friend declaration
#include "VisualTestHarnessActor.h"  // For friend declaration

// Add friend declarations at the top of the file
//class AVisualTestHarnessActor;
//class ULlamaComponent;

VisualizationManager::VisualizationManager()
{
	ClickedOnJunction = nullptr;
}

VisualizationManager::~VisualizationManager()
{
    // Note: This manager doesn't own the junctions/segments themselves,
    // only pointers to them. Their cleanup is handled elsewhere (e.g., TestHarnessActor).
    Junctions.Empty();
    Segments.Empty();
}

void VisualizationManager::AddJunction(ICH_PowerJunction* Junction)
{
    if (Junction)
    {
        Junctions.AddUnique(Junction);
    }
}

void VisualizationManager::AddSegment(PWR_PowerSegment* Segment)
{
    if (Segment)
    {
        Segments.AddUnique(Segment);
    }
}

void VisualizationManager::Render(RenderingContext& Context)
{
    // Render Junctions underlays
    for (ICH_PowerJunction* Junction : Junctions)
    {
        if (Junction)
        {
            Junction->RenderUnderlay(Context);
        }
    }

    // Render Segments first (typically drawn behind junctions)
    for (PWR_PowerSegment* Segment : Segments)
    { 
        if (Segment)
        {
            if (Segment->GetJunctionA() && Segment->GetJunctionB())
            {
				FString s = "";
				FLinearColor c(0.4f, 0.8f, 0.4f);
				float linewidth = 6.5f;
				switch (Segment->GetStatus()) {
					case EPowerSegmentStatus::NORMAL:
						if (Segment->IsShorted()) s += "X";
						else if (Segment->IsOverenergized()) s += "+";
//						else if (Segment->IsUnderPowered()) s += "-";
//						else s += "N/"; // drop the N/ etc, its visible from color
						if (Segment->IsShorted()) c = FLinearColor(0.4f, 0.0f, 0.0f);
						else if (Segment->IsOverenergized()) c = FLinearColor(1.0f, 0.5f, 0.5f);
						// if there is power, set color to black, compute line width
						else if (Segment->GetPowerLevel() > 0) {
							if (Segment->IsUnderPowered()) c = FLinearColor(0.0f, 0.0f, 0.4f);
							else c = FLinearColor::Black;
							linewidth = 3.0f + 3*FMath::Sqrt(Segment->GetPowerLevel());
						}
						break;
					case EPowerSegmentStatus::SHORTED:			s += "S";	c = FLinearColor(0.4f, 0.0f, 0.0f);		break;
					case EPowerSegmentStatus::OPENED:			s += "O";	c = FLinearColor(1.0f, 0.8f, 0.8f);		break;
				}
				if (Segment->bIsSelected) {
					if (((int32)(FPlatformTime::Seconds() / 0.5f)) & 1) c = FLinearColor(0.0f, 0.5f, 0.0f);		// selected color
				}
            	FVector2D ptfrom = Segment->GetJunctionA()->GetPortConnection(Segment->GetPortA());
            	FVector2D ptto = Segment->GetJunctionB()->GetPortConnection(Segment->GetPortB());
                Context.DrawLine(ptfrom, 
                                 ptto, 
                                 c,
//                                 (Segment->GetPowerLevel()>0) ? FLinearColor::Yellow : FLinearColor(0.4f, 0.4f, 0.4f), // Example color based on power
                                 linewidth);
				// draw the little current box if more than 2 units are used
				int32 d = (int)(10*Segment->GetPowerLevel());
				if (d > 2) {
					FVector2D Position;
					Position.X = (ptfrom.X + ptto.X)/2 - 8;
					Position.Y = (ptfrom.Y + ptto.Y)/2 - 8;
					if (FMath::Abs(ptfrom.X - ptto.X) > FMath::Abs(ptfrom.Y - ptto.Y)) Position.Y += 10;
					else Position.X += 10;
					if (s.Len() == 0) s += FString::Printf(TEXT("%d"), d);
					FBox2D r;
					r.Min.X = Position.X - 1;
					r.Min.Y = 4 + Position.Y - 3;
					r.Max.X = Position.X + 15;
					r.Max.Y = 4 + Position.Y + 9;
					// adjust for string length
					if (s.Len() < 2) r.Max.X -= 6;
					if (s.Len() > 2) { r.Min.X -= 2; r.Max.X += 4; }

// labels
					Context.DrawRectangle(r, FLinearColor::White, true);
					Context.DrawRectangle(r, FLinearColor::Black, false);
					Position.Y += 1;
					Context.DrawTinyText(Position, s, FLinearColor::Black);
				}
            }
        }
    }

    // Render Junctions (which will also render their owned IVisualElements)
    for (ICH_PowerJunction* Junction : Junctions)
    {
        if (Junction)
        {
            Junction->Render(Context); // Junction::Render handles drawing itself and its children
        }
    }
}

void VisualizationManager::ClearSelections()
{
    for (int i = Junctions.Num() - 1; i >= 0; --i)
    { 
        ICH_PowerJunction* Junction = Junctions[i];
        if (Junction)
        {
        	Junction->bIsSelected = false;
        }
	}
	//
    for (PWR_PowerSegment* Segment : Segments)
    {
        if (Segment)
        {
        	Segment->bIsSelected = false;
        }
	}
}

void VisualizationManager::SetupSelection(ICH_PowerJunction* Junction)
{
	Junction->bIsSelected = true;
	const TArray<PWR_PowerSegment*>& segs = Junction->GetPathToSourceSegments();
	for (int j=0; j<segs.Num(); j++) segs[j]->bIsSelected = true;
	const TArray<ICH_PowerJunction*>& juncs = Junction->GetPathToSourceJunction();
	for (int j=0; j<juncs.Num(); j++) juncs[j]->bIsSelected = true;
}

void VisualizationManager::RefreshSelection()
{
	// might have been subtle re-routing changes, so refresh the current route (if any)
	ICH_PowerJunction* CurrentSelectedJunction = nullptr;
	int32 longestPathFound = -1;
	// find the Junction with the longest GetPathToSourceJunction() -> CurrentSelectedJunction
	for (int ii = Junctions.Num() - 1; ii >= 0; --ii)
	{ 
		ICH_PowerJunction* Junction2 = Junctions[ii];
		if (Junction2 && Junction2->bIsSelected)
		{
			int32 ln = Junction2->GetPathToSourceJunction().Num();
			if (longestPathFound < ln) {
				longestPathFound = ln;
				CurrentSelectedJunction = Junction2;
			}
		}
	}
	ClearSelections();
	// run one cycle in case things changed
	PWR_PowerPropagation::PropagatePower(Segments, Junctions);
	// show the (new) route
	if (CurrentSelectedJunction) SetupSelection(CurrentSelectedJunction);
}

bool VisualizationManager::HandleTouchEvent(const TouchEvent& Event, CommandDistributor* Distributor)
{
	switch (Event.Type) {
		case TouchEvent::EType::Down:
			// Iterate junctions in reverse order - assumes junctions drawn last might be 'on top'
			// for overlapping elements, though element order within a junction also matters.
			for (int i = Junctions.Num() - 1; i >= 0; --i)
			{ 
				ICH_PowerJunction* Junction = Junctions[i];
				if (Junction)
				{ 
					// Let the junction handle the event, which includes checking its children
					int32 r = Junction->HandleJunctionTouchEvent(Event, Distributor);
					if (r == 1)
					{
						ClickedOnJunction = Junction;
						return true; // Event was handled by the junction or one of its elements
					}
					if (r == 2)
					{
						ClickedOnJunction = Junction;
						ClearSelections();
						// this node is the [new] selection
						SetupSelection(ClickedOnJunction);
						return true;	// Event was interpreted as a selection action
					}
				}
			}

			for (PWR_PowerSegment* Segment : Segments)
			{
				if (Segment)
				{
					if (Segment->HandleTouchEvent(Event, Distributor))
					{
						RefreshSelection();
						return true; // Event was handled by the segment
					}
				}
			}

			// no node is selected, deselect all
			ClearSelections();
			break;
		case TouchEvent::EType::Move:
			if (ClickedOnJunction) {
            	int32 r = ClickedOnJunction->HandleJunctionTouchEvent(Event, Distributor);
			}
			break;
		case TouchEvent::EType::Up:
			if (ClickedOnJunction) {
            	int32 r = ClickedOnJunction->HandleJunctionTouchEvent(Event, Distributor);
            	ClearSelections();
			}
			ClickedOnJunction = nullptr;
			break;
		case TouchEvent::EType::Cancel:
			if (ClickedOnJunction) {
            	int32 r = ClickedOnJunction->HandleJunctionTouchEvent(Event, Distributor);
            	ClearSelections();
			}
			ClickedOnJunction = nullptr;
			break;
	}

    return false; // No component handled the event
} 
