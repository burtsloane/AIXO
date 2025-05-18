#pragma once

#include "CoreMinimal.h"
#include "IVisualElement.h" // For TouchEvent

// Forward Declarations
class ICH_PowerJunction;
class PWR_PowerSegment;
class RenderingContext;
class CommandDistributor;

/**
 * Manages the collection of visual elements for the power grid,
 * handling rendering orchestration and touch input dispatch.
 */
class VisualizationManager
{
private:
    // Lists of top-level drawable objects
    TArray<ICH_PowerJunction*> Junctions;
    TArray<PWR_PowerSegment*> Segments;
    
    // Potentially add lists for other top-level drawable things if needed

public:
    VisualizationManager();
    ~VisualizationManager();

    void AddJunction(ICH_PowerJunction* Junction);
    void AddSegment(PWR_PowerSegment* Segment);
    // Add Remove methods if needed

    /** Renders all managed visual components onto the given context. */
    void Render(RenderingContext& Context);

    /** 
     * Dispatches a touch event to the appropriate visual component.
     * @param Event The touch event data.
     * @param Distributor The command distributor for elements to use.
     * @return True if the event was handled, false otherwise.
     */
    bool HandleTouchEvent(const TouchEvent& Event, CommandDistributor* Distributor);
    
    void ClearSelections();
    void SetupSelection(ICH_PowerJunction* Junction);
	void RefreshSelection();

	ICH_PowerJunction* ClickedOnJunction;

    friend class AVisualTestHarnessActor;
    friend class ULlamaComponent;
}; 
