#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "Templates/Function.h" // For TFunction
#include "Containers/Map.h"
#include "Containers/Array.h"

// Include headers defining the Status enums used in helper function declarations
#include "ICH_PowerJunction.h"
#include "PWR_PowerSegment.h"

#include "UPowerGridLoader.generated.h"

// Forward Declarations
class ICH_PowerJunction;
class PWR_PowerSegment;
class ASubmarineState;
class FJsonObject;
class FJsonValue;

// Factory function signatures
// For junctions requiring SubmarineState access
using JunctionFactoryFuncStateful = TFunction<ICH_PowerJunction*(const FString& /*Name*/, ASubmarineState* /*State*/, float /*X*/, float /*Y*/, float /*W*/, float /*H*/)>;
// For junctions NOT requiring SubmarineState access (adjust signature if needed)
using JunctionFactoryFuncStateless = TFunction<ICH_PowerJunction*(const FString& /*Name*/, float /*X*/, float /*Y*/, float /*W*/, float /*H*/)>;

UCLASS()
class UPowerGridLoader : public UObject
{
    GENERATED_BODY()

public:
    /**
     * Loads the power grid definition from a JSON file.
     * @param JsonFilePath Full path to the JSON definition file.
     * @param SubState Pointer to the ASubmarineState instance.
     * @param OutJunctions TArray to populate with created junctions.
     * @param OutSegments TArray to populate with created segments.
     * @param OutJunctionMap TMap to populate with created junctions, keyed by name.
     * @return True if loading and parsing were successful, false otherwise.
     */
    static bool LoadPowerGridFromJson(
        const FString& JsonFilePath,
        ASubmarineState* SubState,
        TArray<TUniquePtr<ICH_PowerJunction>>& OutJunctions,
        TArray<TUniquePtr<PWR_PowerSegment>>& OutSegments,
        TMap<FString, ICH_PowerJunction*>& OutJunctionMap,
		TMap<FString, float>& OutMarkerDefinitions
    );

    /**
     * Generates a JSON definition file based on the current state of a CommandDistributor.
     * Call this *after* the grid has been initialized via C++.
     * @param Distributor Reference to the populated CommandDistributor.
     * @param OutputJsonFilePath Full path where the JSON file should be saved.
     * @return True if JSON generation and saving were successful, false otherwise.
     */
    static bool GenerateJsonFromGrid(
        const class CommandDistributor& GridState,
        const FString& FilePath,
        const TMap<FString, float>& MarkerDefinitions
    );

private:
    // Factory registry map - maps type string from JSON to creation function
    // We might need two maps or a way to differentiate stateful/stateless if signatures differ significantly
    // For simplicity, let's try one map and assume stateless ones just ignore the State param for now.
    static TMap<FString, JunctionFactoryFuncStateful> JunctionFactoryRegistry;
    static bool bIsFactoryRegistered;

    /** Registers all known junction types with the factory. */
    static void RegisterJunctionTypes();

    /** Helper to parse coordinates, potentially handling markers and expressions. */
    static bool ParseCoordinateValue(const FJsonValue* JsonVal, const TMap<FString, float>& Markers, float& OutValue);

    /** Helper to parse a Position or Size object. */
    static bool ParseVector2D(const FJsonObject* JsonObj, const TMap<FString, float>& Markers, float& OutX, float& OutY);

    /** Helper to parse a simple expression like "MARKER +/- CONSTANT". */
    static bool ParseExpression(const FString& Expression, const TMap<FString, float>& Markers, float& OutValue);

     /** Helper to parse an aspect/command/value string */
    static bool ParseCommandString(const FString& CommandStr, FString& OutAspect, FString& OutCommand, FString& OutValue);

    // Status Enum -> String Helpers (Needed for Generation)
    static FString JunctionStatusToString(EPowerJunctionStatus Status);
    static FString SegmentStatusToString(EPowerSegmentStatus Status);

    // String -> Status Enum Helpers (Needed for Loading)
    static EPowerJunctionStatus StringToJunctionStatus(const FString& StatusStr);
    static EPowerSegmentStatus StringToSegmentStatus(const FString& StatusStr);

	static void RegisterJunctionTypesIfNeeded();
}; 
