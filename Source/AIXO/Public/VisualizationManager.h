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
    // Constructor/Destructor - moved to cpp
    VisualizationManager();
    ~VisualizationManager();

    // Core functionality - moved to cpp
    void AddJunction(ICH_PowerJunction* Junction);
    void AddSegment(PWR_PowerSegment* Segment);
    void Render(RenderingContext& Context);
    bool HandleTouchEvent(const TouchEvent& Event, CommandDistributor* Distributor);
    void ClearSelections();
    void SetupSelection(ICH_PowerJunction* Junction);
    void RefreshSelection();

    // Public state
    ICH_PowerJunction* ClickedOnJunction;

    friend class AVisualTestHarnessActor;
}; 
