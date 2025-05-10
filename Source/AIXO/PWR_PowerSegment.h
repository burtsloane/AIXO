#pragma once

#include "CoreMinimal.h"
#include "ICH_PowerJunction.h"
#include "SubmarineState.h"
#include "ICommandHandler.h"
#include "IVisualElement.h"
#include "Engine/EngineTypes.h"

/**
 * Power segment status enumeration
 */
enum class EPowerSegmentStatus
{
    NORMAL,
    SHORTED,
    OPENED
};

/**
 * PWR_PowerSegment represents a connection between two junctions.
 */
class PWR_PowerSegment
{
private:
    int32 PortA;
    int32 PortB;
    ICH_PowerJunction* JunctionA;
    ICH_PowerJunction* JunctionB;
    EPowerSegmentStatus Status;
    FString SystemName;

protected:		// determined by power propagator
    bool bIsShorted;		// another segment connected to us is shorted
    bool bIsOverenergized;
    bool bIsUnderPowered;
    bool bIsSelected;
    float PowerLevel;
    float PowerFlowDirection;

public:
    PWR_PowerSegment(const FString& name) : SystemName(name) { Status = EPowerSegmentStatus::NORMAL; }

    // Accessors
    float GetPowerLevel() const { return PowerLevel; }
    void SetPowerLevel(float Level) { PowerLevel = Level; }
    float GetPowerFlowDirection() const { return PowerFlowDirection; }
    void SetPowerFlowDirection(float Direction) { PowerFlowDirection = Direction; }
    EPowerSegmentStatus GetStatus() const { return Status; }
    void SetStatus(EPowerSegmentStatus NewStatus) { Status = NewStatus; }
    bool IsShorted() const { return bIsShorted; }
    void SetShorted(bool bShorted) { bIsShorted = bShorted; }
    bool IsOverenergized() const { return bIsOverenergized; }
    void SetOverenergized(bool bOver) { bIsOverenergized = bOver; }
    bool IsUnderPowered() const { return bIsUnderPowered; }
    void SetUnderPowered(bool bUnder) { bIsUnderPowered = bUnder; }
    void SetJunctionA(int32 InPort, ICH_PowerJunction* InJunction) { PortA = InPort; JunctionA = InJunction; }
    void SetJunctionB(int32 InPort, ICH_PowerJunction* InJunction) { PortB = InPort; JunctionB = InJunction; }
    ICH_PowerJunction* GetJunctionA() const { return JunctionA; }
    ICH_PowerJunction* GetJunctionB() const { return JunctionB; }
    int32 GetPortA() const { return PortA; }
    int32 GetPortB() const { return PortB; }
    const FString& GetName() const { return SystemName; }

    virtual bool HandleTouchEvent(const TouchEvent& Event, CommandDistributor* Distributor);

    friend class PWR_PowerPropagation;
    friend class VisualizationManager;
    friend class UPowerGridLoader;
};

