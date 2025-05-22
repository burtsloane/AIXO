#include "UPowerGridLoader.h"
#include "ICH_PowerJunction.h"
#include "PWR_PowerSegment.h"
#include "SubmarineState.h" // Include necessary class definitions

// Include headers for specific junction types needed for registration
#include "SS_Sonar.h"
#include "SS_BowPlanes.h"
#include "SS_FMBTVent.h"
#include "SS_TorpedoTube.h"
#include "SS_FTBTPump.h"
#include "SS_Battery.h" // Includes SS_Battery1, SS_Battery2
#include "SS_ControlRoom.h"
#include "SS_Airlock.h"
#include "SS_XTBTPump.h"
#include "SS_CO2Scrubber.h"
#include "SS_Degaussing.h"
#include "SS_AirCompressor.h"
#include "SS_Dehumidifier.h"
#include "SS_O2Generator.h"
#include "SS_Electrolysis.h"
#include "SS_AIP.h"
#include "SS_MainMotor.h"
#include "SS_RTBTPump.h"
#include "SS_ROVCharging.h"
#include "SS_MBT.h"
#include "SS_RMBTVent.h"
#include "SS_Rudder.h"
#include "SS_Elevator.h"
#include "SS_GPS.h"
#include "SS_Camera.h"
#include "SS_Antenna.h"
#include "SS_Radar.h"
#include "SS_Hatch.h"
#include "SS_Countermeasures.h"
#include "SS_SolarPanels.h"
#include "SS_TowedSonarArray.h"
#include "PWRJ_MultiConnJunction.h"
// Add any other required SS_*, MC_*, PWR_* headers here

// For JSON parsing
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Dom/JsonObject.h"
#include "Misc/FileHelper.h"
#include "HAL/PlatformFileManager.h" // Required for directory creation

// Include base classes needed for status enums
#include "ICH_PowerJunction.h"
#include "PWR_PowerSegment.h"

// Helper function to convert EPowerSegmentStatus to FString
FString UPowerGridLoader::SegmentStatusToString(EPowerSegmentStatus Status)
{
	switch (Status)
	{
		case EPowerSegmentStatus::NORMAL: return TEXT("NORMAL");
		case EPowerSegmentStatus::SHORTED: return TEXT("SHORTED");
		case EPowerSegmentStatus::OPENED: return TEXT("OPEN"); // Assuming OPENED maps to "OPEN"
		default: return TEXT("UNKNOWN");
	}
}

// Helper function to convert EPowerJunctionStatus to FString
FString UPowerGridLoader::JunctionStatusToString(EPowerJunctionStatus Status)
{
	switch (Status)
	{
		case EPowerJunctionStatus::NORMAL: return TEXT("NORMAL");
		case EPowerJunctionStatus::DAMAGED50: return TEXT("DAMAGED50");
		case EPowerJunctionStatus::DAMAGED100: return TEXT("DAMAGED100");
		case EPowerJunctionStatus::DESTROYED: return TEXT("DESTROYED");
		default: return TEXT("UNKNOWN");
	}
}

// Helper function to convert FString to EPowerJunctionStatus
EPowerJunctionStatus UPowerGridLoader::StringToJunctionStatus(const FString& StatusStr)
{
	if (StatusStr.Equals(TEXT("NORMAL"), ESearchCase::IgnoreCase)) return EPowerJunctionStatus::NORMAL;
	if (StatusStr.Equals(TEXT("DAMAGED50"), ESearchCase::IgnoreCase)) return EPowerJunctionStatus::DAMAGED50;
	if (StatusStr.Equals(TEXT("DAMAGED100"), ESearchCase::IgnoreCase)) return EPowerJunctionStatus::DAMAGED100;
	if (StatusStr.Equals(TEXT("DESTROYED"), ESearchCase::IgnoreCase)) return EPowerJunctionStatus::DESTROYED;
	UE_LOG(LogTemp, Warning, TEXT("StringToJunctionStatus: Unknown status string '%s'. Defaulting to NORMAL."), *StatusStr);
	return EPowerJunctionStatus::NORMAL;
}

// Helper function to convert FString to EPowerSegmentStatus
EPowerSegmentStatus UPowerGridLoader::StringToSegmentStatus(const FString& StatusStr)
{
	if (StatusStr.Equals(TEXT("NORMAL"), ESearchCase::IgnoreCase)) return EPowerSegmentStatus::NORMAL;
	if (StatusStr.Equals(TEXT("SHORTED"), ESearchCase::IgnoreCase)) return EPowerSegmentStatus::SHORTED;
	if (StatusStr.Equals(TEXT("OPEN"), ESearchCase::IgnoreCase)) return EPowerSegmentStatus::OPENED; // Assuming "OPEN" maps to OPENED
	UE_LOG(LogTemp, Warning, TEXT("StringToSegmentStatus: Unknown status string '%s'. Defaulting to NORMAL."), *StatusStr);
	return EPowerSegmentStatus::NORMAL;
}

// Static variable definitions
TMap<FString, JunctionFactoryFuncStateful> UPowerGridLoader::JunctionFactoryRegistry;
bool UPowerGridLoader::bIsFactoryRegistered = false;

void UPowerGridLoader::RegisterJunctionTypes()
{
    if (bIsFactoryRegistered) return;

//    UE_LOG(LogTemp, Log, TEXT("Registering Junction Types..."));

    // Register stateful junctions (those needing ASubmarineState*)
    JunctionFactoryRegistry.Add("SS_Sonar", [](const FString& Name, ASubmarineState* State, float X, float Y, float W, float H) -> ICH_PowerJunction* {
        return new SS_Sonar(Name, State, X, Y, W, H);
    });
    JunctionFactoryRegistry.Add("SS_BowPlanes", [](const FString& Name, ASubmarineState* State, float X, float Y, float W, float H) -> ICH_PowerJunction* {
        return new SS_BowPlanes(Name, State, X, Y, W, H);
    });
    JunctionFactoryRegistry.Add("SS_FMBTVent", [](const FString& Name, ASubmarineState* State, float X, float Y, float W, float H) -> ICH_PowerJunction* {
        return new SS_FMBTVent(Name, State, X, Y, W, H);
    });
    JunctionFactoryRegistry.Add("SS_TorpedoTube", [](const FString& Name, ASubmarineState* State, float X, float Y, float W, float H) -> ICH_PowerJunction* {
        return new SS_TorpedoTube(Name, State, X, Y, W, H);
    });
    JunctionFactoryRegistry.Add("SS_FTBTPump", [](const FString& Name, ASubmarineState* State, float X, float Y, float W, float H) -> ICH_PowerJunction* {
        return new SS_FTBTPump(Name, State, X, Y, W, H);
    });
    JunctionFactoryRegistry.Add("SS_Battery1", [](const FString& Name, ASubmarineState* State, float X, float Y, float W, float H) -> ICH_PowerJunction* {
        return new SS_Battery1(Name, State, X, Y, W, H);
    });
     JunctionFactoryRegistry.Add("SS_Battery2", [](const FString& Name, ASubmarineState* State, float X, float Y, float W, float H) -> ICH_PowerJunction* {
        return new SS_Battery2(Name, State, X, Y, W, H);
    });
    // ... Add ALL other SS_* types here ...
    JunctionFactoryRegistry.Add("SS_ControlRoom", [](const FString& Name, ASubmarineState* State, float X, float Y, float W, float H) -> ICH_PowerJunction* { return new SS_ControlRoom(Name, State, X, Y, W, H); });
    JunctionFactoryRegistry.Add("SS_Airlock", [](const FString& Name, ASubmarineState* State, float X, float Y, float W, float H) -> ICH_PowerJunction* { return new SS_Airlock(Name, State, X, Y, W, H); });
    JunctionFactoryRegistry.Add("SS_XTBTPump", [](const FString& Name, ASubmarineState* State, float X, float Y, float W, float H) -> ICH_PowerJunction* { return new SS_XTBTPump(Name, State, X, Y, W, H); });
    JunctionFactoryRegistry.Add("SS_CO2Scrubber", [](const FString& Name, ASubmarineState* State, float X, float Y, float W, float H) -> ICH_PowerJunction* { return new SS_CO2Scrubber(Name, State, X, Y, W, H); });
    JunctionFactoryRegistry.Add("SS_Degaussing", [](const FString& Name, ASubmarineState* State, float X, float Y, float W, float H) -> ICH_PowerJunction* { return new SS_Degaussing(Name, State, X, Y, W, H); });
    JunctionFactoryRegistry.Add("SS_AirCompressor", [](const FString& Name, ASubmarineState* State, float X, float Y, float W, float H) -> ICH_PowerJunction* { return new SS_AirCompressor(Name, State, X, Y, W, H); });
    JunctionFactoryRegistry.Add("SS_Dehumidifier", [](const FString& Name, ASubmarineState* State, float X, float Y, float W, float H) -> ICH_PowerJunction* { return new SS_Dehumidifier(Name, State, X, Y, W, H); });
    JunctionFactoryRegistry.Add("SS_O2Generator", [](const FString& Name, ASubmarineState* State, float X, float Y, float W, float H) -> ICH_PowerJunction* { return new SS_O2Generator(Name, State, X, Y, W, H); });
    JunctionFactoryRegistry.Add("SS_Electrolysis", [](const FString& Name, ASubmarineState* State, float X, float Y, float W, float H) -> ICH_PowerJunction* { return new SS_Electrolysis(Name, State, X, Y, W, H); });
    JunctionFactoryRegistry.Add("SS_AIP", [](const FString& Name, ASubmarineState* State, float X, float Y, float W, float H) -> ICH_PowerJunction* { return new SS_AIP(Name, State, X, Y, W, H); });
    JunctionFactoryRegistry.Add("SS_MainMotor", [](const FString& Name, ASubmarineState* State, float X, float Y, float W, float H) -> ICH_PowerJunction* { return new SS_MainMotor(Name, State, X, Y, W, H); });
    JunctionFactoryRegistry.Add("SS_RTBTPump", [](const FString& Name, ASubmarineState* State, float X, float Y, float W, float H) -> ICH_PowerJunction* { return new SS_RTBTPump(Name, State, X, Y, W, H); });
    JunctionFactoryRegistry.Add("SS_ROVCharging", [](const FString& Name, ASubmarineState* State, float X, float Y, float W, float H) -> ICH_PowerJunction* { return new SS_ROVCharging(Name, State, X, Y, W, H); });
    JunctionFactoryRegistry.Add("SS_RMBTVent", [](const FString& Name, ASubmarineState* State, float X, float Y, float W, float H) -> ICH_PowerJunction* { return new SS_RMBTVent(Name, State, X, Y, W, H); });
    JunctionFactoryRegistry.Add("SS_MBT", [](const FString& Name, ASubmarineState* State, float X, float Y, float W, float H) -> ICH_PowerJunction* { return new SS_MBT(Name, State, X, Y, W, H); });
    JunctionFactoryRegistry.Add("SS_Rudder", [](const FString& Name, ASubmarineState* State, float X, float Y, float W, float H) -> ICH_PowerJunction* { return new SS_Rudder(Name, State, X, Y, W, H); });
    JunctionFactoryRegistry.Add("SS_Elevator", [](const FString& Name, ASubmarineState* State, float X, float Y, float W, float H) -> ICH_PowerJunction* { return new SS_Elevator(Name, State, X, Y, W, H); });
    JunctionFactoryRegistry.Add("SS_GPS", [](const FString& Name, ASubmarineState* State, float X, float Y, float W, float H) -> ICH_PowerJunction* { return new SS_GPS(Name, State, X, Y, W, H); });
    JunctionFactoryRegistry.Add("SS_Camera", [](const FString& Name, ASubmarineState* State, float X, float Y, float W, float H) -> ICH_PowerJunction* { return new SS_Camera(Name, State, X, Y, W, H); });
    JunctionFactoryRegistry.Add("SS_Antenna", [](const FString& Name, ASubmarineState* State, float X, float Y, float W, float H) -> ICH_PowerJunction* { return new SS_Antenna(Name, State, X, Y, W, H); });
    JunctionFactoryRegistry.Add("SS_Radar", [](const FString& Name, ASubmarineState* State, float X, float Y, float W, float H) -> ICH_PowerJunction* { return new SS_Radar(Name, State, X, Y, W, H); });
    JunctionFactoryRegistry.Add("SS_Hatch", [](const FString& Name, ASubmarineState* State, float X, float Y, float W, float H) -> ICH_PowerJunction* { return new SS_Hatch(Name, State, X, Y, W, H); });
    JunctionFactoryRegistry.Add("SS_Countermeasures", [](const FString& Name, ASubmarineState* State, float X, float Y, float W, float H) -> ICH_PowerJunction* { return new SS_Countermeasures(Name, State, X, Y, W, H); });
    JunctionFactoryRegistry.Add("SS_SolarPanels", [](const FString& Name, ASubmarineState* State, float X, float Y, float W, float H) -> ICH_PowerJunction* { return new SS_SolarPanels(Name, State, X, Y, W, H); });
    JunctionFactoryRegistry.Add("SS_TowedSonarArray", [](const FString& Name, ASubmarineState* State, float X, float Y, float W, float H) -> ICH_PowerJunction* { return new SS_TowedSonarArray(Name, State, X, Y, W, H); });


    // Register stateless junctions (if any - example assumes PWRJ_MultiConnJunction might be)
    // Note: Using the Stateful signature here, just ignoring the State param inside lambda.
    // If constructors truly differ, a separate map might be cleaner.
    JunctionFactoryRegistry.Add("PWRJ_MultiConnJunction", [](const FString& Name, ASubmarineState* State, float X, float Y, float W, float H) -> ICH_PowerJunction* {
        // Assuming PWRJ_MultiConnJunction constructor doesn't *require* State, but accepts X,Y,W,H
        // Adjust constructor call as needed based on its actual definition.
        return new PWRJ_MultiConnJunction(Name, State, X, Y, W, H); // Pass State even if ignored, or adjust signature/map
    });

    bIsFactoryRegistered = true;
//    UE_LOG(LogTemp, Log, TEXT("Junction Types Registered. Count: %d"), JunctionFactoryRegistry.Num());
}

// Helper to parse "MARKER +/- CONSTANT" or just "MARKER" or just "CONSTANT"
bool UPowerGridLoader::ParseExpression(const FString& Expression, const TMap<FString, float>& Markers, float& OutValue)
{
    FString ExprTrimmed = Expression.TrimStartAndEnd();
    if (ExprTrimmed.IsNumeric())
    {
        OutValue = FCString::Atof(*ExprTrimmed);
        return true;
    }

    FString MarkerName;
    FString Operator;
    FString ConstantStr;
    float ConstantVal = 0.0f;

    FString LeftSide, RightSide;

    if (ExprTrimmed.Split(TEXT("+"), &LeftSide, &RightSide))
    {
        MarkerName = LeftSide.TrimStartAndEnd();
        ConstantStr = RightSide.TrimStartAndEnd();
        Operator = TEXT("+");
    }
    else if (ExprTrimmed.Split(TEXT("-"), &LeftSide, &RightSide))
    {
        MarkerName = LeftSide.TrimStartAndEnd();
        ConstantStr = RightSide.TrimStartAndEnd();
        Operator = TEXT("-");
    }
    else
    {
        // Assume it's just a marker name
        MarkerName = ExprTrimmed;
        ConstantStr = TEXT("");
        Operator = TEXT("");
    }

    // Validate Constant if an operator was found
    if (!Operator.IsEmpty())
    {
        if (ConstantStr.IsNumeric())
        {
            ConstantVal = FCString::Atof(*ConstantStr);
        }
        else
        {
             UE_LOG(LogTemp, Error, TEXT("ParseExpression: Invalid constant value '%s' after operator in expression '%s'"), *ConstantStr, *Expression);
            return false; // Invalid format if operator exists but constant is not numeric
        }
    }
    
    if (!MarkerName.IsEmpty())
    {
        const float* MarkerValuePtr = Markers.Find(MarkerName);
        if (MarkerValuePtr)
        {
            if (Operator == "+")
            {
                OutValue = *MarkerValuePtr + ConstantVal;
                return true;
            }
            else if (Operator == "-")
            {
                OutValue = *MarkerValuePtr - ConstantVal;
                return true;
            }
            else // No operator, just the marker
            {
                OutValue = *MarkerValuePtr;
                return true;
            }
        }
        else
        {
             UE_LOG(LogTemp, Error, TEXT("ParseExpression: Unknown marker '%s' in expression '%s'"), *MarkerName, *Expression);
             return false;
        }
    }

    // Case where it wasn't purely numeric but also didn't resolve to a marker expression
    UE_LOG(LogTemp, Error, TEXT("ParseExpression: Failed to parse expression '%s'"), *Expression);
    return false;
}

bool UPowerGridLoader::ParseCoordinateValue(const FJsonValue* JsonVal, const TMap<FString, float>& Markers, float& OutValue)
{
    if (!JsonVal)
    {
        return false;
    }

    if (JsonVal->Type == EJson::Number)
    {
        OutValue = JsonVal->AsNumber();
        return true;
    }
    else if (JsonVal->Type == EJson::String)
    {
        return ParseExpression(JsonVal->AsString(), Markers, OutValue);
    }

    return false;
}

bool UPowerGridLoader::ParseVector2D(const FJsonObject* JsonObj, const TMap<FString, float>& Markers, float& OutX, float& OutY)
{
    if (!JsonObj)
    {
        return false;
    }

    bool bXSuccess = ParseCoordinateValue(JsonObj->TryGetField(TEXT("X")).Get(), Markers, OutX);
    bool bYSuccess = ParseCoordinateValue(JsonObj->TryGetField(TEXT("Y")).Get(), Markers, OutY);

    if (!bXSuccess)
    {
        UE_LOG(LogTemp, Error, TEXT("ParseVector2D: Failed to parse 'X' coordinate."));
    }
    if (!bYSuccess)
    {
        UE_LOG(LogTemp, Error, TEXT("ParseVector2D: Failed to parse 'Y' coordinate."));
    }

    return bXSuccess && bYSuccess;
}

bool UPowerGridLoader::ParseCommandString(const FString& CommandStr, FString& OutAspect, FString& OutCommand, FString& OutValue)
{
    TArray<FString> Parts;
    CommandStr.ParseIntoArray(Parts, TEXT(" "), true); // Split by space, keep empty parts relevant?

    if (Parts.Num() >= 2)
    {
        OutAspect = Parts[0];
        OutCommand = Parts[1];
        if (Parts.Num() >= 3)
        {
            // Join remaining parts back together in case value has spaces
            TArray<FString> ValueParts;
            for(int32 i = 2; i < Parts.Num(); ++i)
            {
                ValueParts.Add(Parts[i]);
            }
            OutValue = FString::Join(ValueParts, TEXT(" "));
        }
        else
        {
            OutValue = TEXT(""); // No value provided
        }
        return true;
    }
    else
    {
        UE_LOG(LogTemp, Error, TEXT("ParseCommandString: Failed to parse command string '%s'. Expected at least '<aspect> <command>'."), *CommandStr);
        return false;
    }
}

bool UPowerGridLoader::LoadPowerGridFromJson(
    const FString& JsonFilePath,
    ASubmarineState* SubState,
    TArray<TUniquePtr<ICH_PowerJunction>>& OutJunctions,
    TArray<TUniquePtr<PWR_PowerSegment>>& OutSegments,
    TMap<FString, ICH_PowerJunction*>& OutJunctionMap,
    TMap<FString, float>& OutMarkerDefinitions
)
{
    RegisterJunctionTypesIfNeeded(); // Ensure factory is populated

    OutJunctions.Empty();
    OutSegments.Empty();
    OutJunctionMap.Empty();

    // Will store InitialCommands for each junction until after segments are wired
    TMap<ICH_PowerJunction*, TArray<FString>> PendingCommands;

    FString JsonString;
    if (!FFileHelper::LoadFileToString(JsonString, *JsonFilePath))
    {
        UE_LOG(LogTemp, Error, TEXT("LoadPowerGridFromJson: Failed to load file: %s"), *JsonFilePath);
        return false;
    }

    TSharedPtr<FJsonObject> RootJsonObject;
    TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonString);

    if (!FJsonSerializer::Deserialize(Reader, RootJsonObject) || !RootJsonObject.IsValid())
    {
        UE_LOG(LogTemp, Error, TEXT("LoadPowerGridFromJson: Failed to parse JSON file: %s. Error: %s"), *JsonFilePath, *Reader->GetErrorMessage());
        return false;
    }

    // 1. Parse Coordinate Markers
    TMap<FString, float> Markers;
    const TSharedPtr<FJsonObject>* MarkersObjectPtr;
    if (RootJsonObject->TryGetObjectField(TEXT("Markers"), MarkersObjectPtr)) // Match generated name
    {
        for (const auto& Pair : (*MarkersObjectPtr)->Values)
        {
            if (Pair.Value.IsValid() && Pair.Value->Type == EJson::Number)
            {
                Markers.Add(Pair.Key, Pair.Value->AsNumber());
                OutMarkerDefinitions.Add(Pair.Key, Pair.Value->AsNumber());
            }
            else
            {
                 UE_LOG(LogTemp, Warning, TEXT("LoadPowerGridFromJson: Invalid value for marker '%s'. Expected number."), *Pair.Key);
            }
        }
    }
    UE_LOG(LogTemp, Log, TEXT("LoadPowerGridFromJson: Loaded %d coordinate markers."), Markers.Num());

    // 2. Parse Junctions
    const TArray<TSharedPtr<FJsonValue>>* JunctionsJsonArray;
    if (!RootJsonObject->TryGetArrayField(TEXT("Junctions"), JunctionsJsonArray))
    {
        UE_LOG(LogTemp, Error, TEXT("LoadPowerGridFromJson: 'Junctions' array not found in JSON."));
        return false;
    }

    for (const TSharedPtr<FJsonValue>& JunctionValue : *JunctionsJsonArray)
    {
        const TSharedPtr<FJsonObject>& JunctionObject = JunctionValue->AsObject();
        if (!JunctionObject.IsValid()) { UE_LOG(LogTemp, Warning, TEXT("LoadPowerGridFromJson: Invalid item in 'Junctions' array.")); continue; }

        FString JName, JType;
        if (!JunctionObject->TryGetStringField(TEXT("Name"), JName) || !JunctionObject->TryGetStringField(TEXT("Type"), JType)) { UE_LOG(LogTemp, Error, TEXT("LoadPowerGridFromJson: Junction missing 'Name' or 'Type'.")); continue; }

        const TSharedPtr<FJsonObject>* PosObjectPtr;
        const TSharedPtr<FJsonObject>* SizeObjectPtr;
        float X_offset = 0.f, Y_offset = 0.f, W = 150.f, H = 24.f;
        FString MarkerX_Name, MarkerY_Name;
        float FinalX = 0.f, FinalY = 0.f; // Store final calculated coordinates

        if (JunctionObject->TryGetObjectField(TEXT("Position"), PosObjectPtr))
        {
            // Read X, Y (offsets), mX, mY (marker names)
            if (!(*PosObjectPtr)->TryGetNumberField(TEXT("X"), X_offset)) { UE_LOG(LogTemp, Error, TEXT("LoadPowerGridFromJson: Junction '%s' missing 'X' in Position."), *JName); continue; }
            if (!(*PosObjectPtr)->TryGetNumberField(TEXT("Y"), Y_offset)) { UE_LOG(LogTemp, Error, TEXT("LoadPowerGridFromJson: Junction '%s' missing 'Y' in Position."), *JName); continue; }
            (*PosObjectPtr)->TryGetStringField(TEXT("mX"), MarkerX_Name);
            (*PosObjectPtr)->TryGetStringField(TEXT("mY"), MarkerY_Name);

            // Calculate Final Position
            FinalX = X_offset; // Start with the offset
            FinalY = Y_offset;
            if (!MarkerX_Name.IsEmpty())
            {
                const float* MarkerValue = Markers.Find(MarkerX_Name);
                if (MarkerValue) { FinalX = *MarkerValue + X_offset; }
                else { UE_LOG(LogTemp, Warning, TEXT("LoadPowerGridFromJson: Junction '%s' uses undefined X marker '%s'. Using offset as absolute X."), *JName, *MarkerX_Name); }
            }
            if (!MarkerY_Name.IsEmpty())
            {
                const float* MarkerValue = Markers.Find(MarkerY_Name);
                if (MarkerValue) { FinalY = *MarkerValue + Y_offset; }
                else { UE_LOG(LogTemp, Warning, TEXT("LoadPowerGridFromJson: Junction '%s' uses undefined Y marker '%s'. Using offset as absolute Y."), *JName, *MarkerY_Name); }
            }
        }
        else { UE_LOG(LogTemp, Error, TEXT("LoadPowerGridFromJson: Junction '%s' missing 'Position' object."), *JName); continue; }

        if (JunctionObject->TryGetObjectField(TEXT("Size"), SizeObjectPtr))
        {
            if (!(*SizeObjectPtr)->TryGetNumberField(TEXT("W"), W)) { UE_LOG(LogTemp, Warning, TEXT("LoadPowerGridFromJson: Junction '%s' missing/invalid 'W' in Size. Using default."), *JName); W = 150.f; }
            if (!(*SizeObjectPtr)->TryGetNumberField(TEXT("H"), H)) { UE_LOG(LogTemp, Warning, TEXT("LoadPowerGridFromJson: Junction '%s' missing/invalid 'H' in Size. Using default."), *JName); H = 24.f; }
        }
        else { UE_LOG(LogTemp, Warning, TEXT("LoadPowerGridFromJson: Junction '%s' missing 'Size'. Using defaults."), *JName); }

        // Create Junction using Factory with final coordinates
        JunctionFactoryFuncStateful* FactoryFunc = JunctionFactoryRegistry.Find(JType);
        if (!FactoryFunc) { UE_LOG(LogTemp, Error, TEXT("LoadPowerGridFromJson: Unknown junction type '%s' for junction '%s'."), *JType, *JName); continue; }

        ICH_PowerJunction* NewJunction = (*FactoryFunc)(JName, SubState, FinalX, FinalY, W, H);
        if (!NewJunction)
        {
            UE_LOG(LogTemp, Error, TEXT("LoadPowerGridFromJson: Factory failed to create junction '%s' of type '%s'."), *JName, *JType);
            continue;
        }

        /*  Immediately transfer ownership to OutJunctions so the pointer
            remains valid even if we 'continue' later in this loop.          */
        TUniquePtr<ICH_PowerJunction> JunctionPtr(NewJunction);
        OutJunctions.Add(MoveTemp(JunctionPtr));
        ICH_PowerJunction* CurrentJunction = OutJunctions.Last().Get();

        // Set Marker names on the instance
        CurrentJunction->MarkerX = MarkerX_Name;
        CurrentJunction->MarkerY = MarkerY_Name;

        // Set Status
        FString StatusString;
        if (JunctionObject->TryGetStringField(TEXT("Status"), StatusString))
        {
            CurrentJunction->SetStatus(StringToJunctionStatus(StatusString));
        }

        // Ownership already transferred to OutJunctions above.
        OutJunctionMap.Add(JName, CurrentJunction);

        // Defer initial commands until all segments have been created
        const TArray<TSharedPtr<FJsonValue>>* CommandsJsonArray;
        if (JunctionObject->TryGetArrayField(TEXT("InitialCommands"), CommandsJsonArray))
        {
            for (const TSharedPtr<FJsonValue>& CommandValue : *CommandsJsonArray)
            {
                if (CommandValue->Type == EJson::String)
                {
                    PendingCommands.FindOrAdd(CurrentJunction).Add(CommandValue->AsString());
                }
            }
        }
    }
    UE_LOG(LogTemp, Log, TEXT("LoadPowerGridFromJson: Successfully created %d junctions."), OutJunctions.Num());

    // 3. Parse Segments
    const TArray<TSharedPtr<FJsonValue>>* SegmentsJsonArray;
    if (!RootJsonObject->TryGetArrayField(TEXT("Segments"), SegmentsJsonArray))
    {
        UE_LOG(LogTemp, Error, TEXT("LoadPowerGridFromJson: 'Segments' array not found in JSON."));
        return false;
    }

     for (const TSharedPtr<FJsonValue>& SegmentValue : *SegmentsJsonArray)
    {
        const TSharedPtr<FJsonObject>& SegmentObject = SegmentValue->AsObject();
        if (!SegmentObject.IsValid()) { UE_LOG(LogTemp, Warning, TEXT("LoadPowerGridFromJson: Invalid item in 'Segments' array.")); continue; }

        FString SegName, JuncAName, JuncBName, StatusString;
        int32 PortA = -1, PortB = -1, SideA = -1, OffsetA = -1, SideB = -1, OffsetB = -1;

        // Read all segment fields including Status, SideA/B, OffsetA/B
        if (!SegmentObject->TryGetStringField(TEXT("Name"), SegName) ||
            !SegmentObject->TryGetStringField(TEXT("JunctionA"), JuncAName) ||
            !SegmentObject->TryGetStringField(TEXT("JunctionB"), JuncBName) ||
            !SegmentObject->TryGetNumberField(TEXT("PortA"), PortA) ||
            !SegmentObject->TryGetNumberField(TEXT("PortB"), PortB) ||
            !SegmentObject->TryGetStringField(TEXT("Status"), StatusString) || // Read Status string
            !SegmentObject->TryGetNumberField(TEXT("SideA"), SideA) ||       // Read Side/Offset
            !SegmentObject->TryGetNumberField(TEXT("OffsetA"), OffsetA) ||
            !SegmentObject->TryGetNumberField(TEXT("SideB"), SideB) ||
            !SegmentObject->TryGetNumberField(TEXT("OffsetB"), OffsetB) )
        {
             UE_LOG(LogTemp, Error, TEXT("LoadPowerGridFromJson: Segment missing required field (Name, JunctionA/B, PortA/B, Status, SideA/B, OffsetA/B). Segment: %s"), *SegName);
             continue;
        }

        ICH_PowerJunction** JuncAPtr = OutJunctionMap.Find(JuncAName);
        ICH_PowerJunction** JuncBPtr = OutJunctionMap.Find(JuncBName);

        if (!JuncAPtr || !*JuncAPtr) { UE_LOG(LogTemp, Error, TEXT("LoadPowerGridFromJson: Segment '%s' refers to unknown JunctionA '%s'."), *SegName, *JuncAName); continue; }
        if (!JuncBPtr || !*JuncBPtr) { UE_LOG(LogTemp, Error, TEXT("LoadPowerGridFromJson: Segment '%s' refers to unknown JunctionB '%s'."), *SegName, *JuncBName); continue; }

        ICH_PowerJunction* JunctionA = *JuncAPtr;
        ICH_PowerJunction* JunctionB = *JuncBPtr;

        // Create Segment
        PWR_PowerSegment* NewSegmentRawPtr = new PWR_PowerSegment(SegName);

        // Set Status
        NewSegmentRawPtr->SetStatus(StringToSegmentStatus(StatusString));

        // Connect Segment to Junctions using explicit Side/Offset from JSON
        NewSegmentRawPtr->SetJunctionA(PortA, JunctionA); // Use PortA index from JSON
        if (!JunctionA->AddPort(NewSegmentRawPtr, SideA, OffsetA)) { // Use SideA/OffsetA from JSON
            UE_LOG(LogTemp, Error, TEXT("LoadPowerGridFromJson: Failed to add port %d (Side:%d, Offset:%d) for segment '%s' to junction '%s'."), PortA, SideA, OffsetA, *SegName, *JuncAName);
            delete NewSegmentRawPtr;
            continue;
        }

        NewSegmentRawPtr->SetJunctionB(PortB, JunctionB); // Use PortB index from JSON
        if (!JunctionB->AddPort(NewSegmentRawPtr, SideB, OffsetB)) { // Use SideB/OffsetB from JSON
             UE_LOG(LogTemp, Error, TEXT("LoadPowerGridFromJson: Failed to add port %d (Side:%d, Offset:%d) for segment '%s' to junction '%s'."), PortB, SideB, OffsetB, *SegName, *JuncBName);
             // Consider removing port from JunctionA if B fails? More complex cleanup needed.
             delete NewSegmentRawPtr;
             continue;
        }

        OutSegments.Emplace(NewSegmentRawPtr); // TUniquePtr takes ownership
//        OutSegments.Add(NewSegment);
    }
    UE_LOG(LogTemp, Log, TEXT("LoadPowerGridFromJson: Successfully created %d segments."), OutSegments.Num());

    // --- 4. Apply deferred InitialCommands now that all segments exist ---
    for (auto& Pair : PendingCommands)
    {
        ICH_PowerJunction* Junction = Pair.Key;
        const TArray<FString>& CmdList = Pair.Value;

        for (const FString& CmdStr : CmdList)
        {
            FString Aspect, Command, Value;
            if (ParseCommandString(CmdStr, Aspect, Command, Value))
            {
                ECommandResult Result = Junction->HandleCommand(Aspect, Command, Value);
                if (Result == ECommandResult::NotHandled)
                {
                    UE_LOG(LogTemp, Warning, TEXT("LoadPowerGridFromJson: Deferred command '%s' not handled by junction '%s'."), *CmdStr, *Junction->GetSystemName());
                }
                Junction->PostHandleCommand();
            }
            else
            {
                UE_LOG(LogTemp, Error, TEXT("LoadPowerGridFromJson: Failed to parse deferred command '%s' for junction '%s'."), *CmdStr, *Junction->GetSystemName());
            }
        }
    }

    UE_LOG(LogTemp, Log, TEXT("LoadPowerGridFromJson: Finished loading grid from %s"), *JsonFilePath);
    return true;
}

// --- JSON Generation ---

bool UPowerGridLoader::GenerateJsonFromGrid(
	const CommandDistributor& GridState,
	const FString& FilePath,
	const TMap<FString, float>& MarkerDefinitions)
{
//	UE_LOG(LogTemp, Log, TEXT("GenerateJsonFromGrid: Generating Power Grid JSON from GridState to %s"), *FilePath);
//	UE_LOG(LogTemp, Log, TEXT("GenerateJsonFromGrid: Received %d marker definitions."), MarkerDefinitions.Num()); // Log number of markers received

	TSharedPtr<FJsonObject> RootObject = MakeShareable(new FJsonObject);

	// 1. Add Markers
	TSharedPtr<FJsonObject> MarkersObject = MakeShareable(new FJsonObject);
	for (const auto& Pair : MarkerDefinitions)
	{
		MarkersObject->SetNumberField(Pair.Key, Pair.Value);
		UE_LOG(LogTemp, Log, TEXT("GenerateJsonFromGrid: Adding marker '%s' = %f"), *Pair.Key, Pair.Value); // Log each marker being added
	}
	if(MarkersObject->Values.Num() > 0) // Check if we actually added anything
    {
	    RootObject->SetObjectField(TEXT("Markers"), MarkersObject);
        UE_LOG(LogTemp, Log, TEXT("GenerateJsonFromGrid: Added Markers object to root JSON.")); // Confirm Markers object addition
    } else {
        UE_LOG(LogTemp, Warning, TEXT("GenerateJsonFromGrid: MarkersObject is empty. Not adding to root JSON.")); // Log if marker object was empty
    }

	// 2. Add Junctions
	TArray<TSharedPtr<FJsonValue>> JunctionsArray;
	const TArray<ICommandHandler*>& Handlers = GridState.GetCommandHandlers();
	TSet<ICH_PowerJunction*> ProcessedJunctions; // Avoid duplicates if handler list has them somehow

	for (ICommandHandler* Handler : Handlers)
	{
		if (!Handler) continue;

		ICH_PowerJunction* Junction = Handler->GetAsPowerJunction();
		if (!Junction || ProcessedJunctions.Contains(Junction)) continue;

		ProcessedJunctions.Add(Junction);

		TSharedPtr<FJsonObject> JunctionObject = MakeShareable(new FJsonObject);

		// Basic Info
		JunctionObject->SetStringField(TEXT("Name"), Junction->GetSystemName());
		FString TypeString = Junction->GetTypeString();
		if (TypeString.IsEmpty())
		{
			UE_LOG(LogTemp, Warning, TEXT("GenerateJsonFromGrid: Junction '%s' has empty TypeString! Skipping."), *Junction->GetSystemName());
			continue;
		}
		JunctionObject->SetStringField(TEXT("Type"), TypeString);

		// Position (with marker offset calculation)
		TSharedPtr<FJsonObject> PositionObject = MakeShareable(new FJsonObject);
		FVector2D CurrentPos = Junction->GetPosition();
		float OutputX = CurrentPos.X;
		float OutputY = CurrentPos.Y;

		if (!Junction->MarkerX.IsEmpty())
		{
//			UE_LOG(LogTemp, Log, TEXT("GenerateJsonFromGrid: Junction '%s' has MarkerX: '%s'. CurrentPos.X: %f"), *Junction->GetSystemName(), *Junction->MarkerX, CurrentPos.X); // Log marker info
			const float* MarkerValue = MarkerDefinitions.Find(Junction->MarkerX);
			if (MarkerValue)
			{
//				UE_LOG(LogTemp, Log, TEXT("GenerateJsonFromGrid: Found MarkerX value: %f. Calculating offset..."), *MarkerValue);
				OutputX = CurrentPos.X - *MarkerValue;
//				UE_LOG(LogTemp, Log, TEXT("GenerateJsonFromGrid: Calculated OutputX (offset): %f"), OutputX);
			}
			else
			{
				UE_LOG(LogTemp, Warning, TEXT("GenerateJsonFromGrid: Junction '%s' uses undefined X marker '%s'. Using absolute X: %f."), *Junction->GetSystemName(), *Junction->MarkerX, OutputX);
			}
		}
		if (!Junction->MarkerY.IsEmpty())
		{
//			UE_LOG(LogTemp, Log, TEXT("GenerateJsonFromGrid: Junction '%s' has MarkerY: '%s'. CurrentPos.Y: %f"), *Junction->GetSystemName(), *Junction->MarkerY, CurrentPos.Y); // Log marker info
			const float* MarkerValue = MarkerDefinitions.Find(Junction->MarkerY);
			if (MarkerValue)
			{
//				UE_LOG(LogTemp, Log, TEXT("GenerateJsonFromGrid: Found MarkerY value: %f. Calculating offset..."), *MarkerValue);
				OutputY = CurrentPos.Y - *MarkerValue;
//				UE_LOG(LogTemp, Log, TEXT("GenerateJsonFromGrid: Calculated OutputY (offset): %f"), OutputY);
			}
			else
			{
				UE_LOG(LogTemp, Warning, TEXT("GenerateJsonFromGrid: Junction '%s' uses undefined Y marker '%s'. Using absolute Y: %f."), *Junction->GetSystemName(), *Junction->MarkerY, OutputY);
			}
		}

		PositionObject->SetNumberField(TEXT("X"), OutputX);
		PositionObject->SetNumberField(TEXT("Y"), OutputY);
		PositionObject->SetStringField(TEXT("mX"), Junction->MarkerX);
		PositionObject->SetStringField(TEXT("mY"), Junction->MarkerY);
		JunctionObject->SetObjectField(TEXT("Position"), PositionObject);

		// Size
		TSharedPtr<FJsonObject> SizeObject = MakeShareable(new FJsonObject);
		SizeObject->SetNumberField(TEXT("W"), Junction->GetW());
		SizeObject->SetNumberField(TEXT("H"), Junction->GetH());
		JunctionObject->SetObjectField(TEXT("Size"), SizeObject);

		// Junction Status
		JunctionObject->SetStringField(TEXT("Status"), JunctionStatusToString(Junction->Status));

		// Initial Commands (from QueryEntireState)
		TArray<FString> StateCommands = Junction->QueryEntireState();
        if (!StateCommands.IsEmpty()) // Only add the array if there's state to store
        {
            TArray<TSharedPtr<FJsonValue>> JsonCommandsArray;
            for (const FString& Cmd : StateCommands)
            {
                JsonCommandsArray.Add(MakeShareable(new FJsonValueString(Cmd)));
            }
            JunctionObject->SetArrayField(TEXT("InitialCommands"), JsonCommandsArray);
        }

		JunctionsArray.Add(MakeShareable(new FJsonValueObject(JunctionObject)));
	}
	RootObject->SetArrayField(TEXT("Junctions"), JunctionsArray);

	// 3. Add Segments
	TArray<TSharedPtr<FJsonValue>> SegmentsArray;
	const TArray<PWR_PowerSegment*>& Segments = GridState.GetSegments();
	for (const PWR_PowerSegment* Segment : Segments)
	{
		if (!Segment) continue;

		TSharedPtr<FJsonObject> SegmentObject = MakeShareable(new FJsonObject);
		SegmentObject->SetStringField(TEXT("Name"), Segment->GetName());

		// Segment Status
		SegmentObject->SetStringField(TEXT("Status"), SegmentStatusToString(Segment->GetStatus()));

		// Connections (Using Names)
		ICH_PowerJunction* JctA = Segment->GetJunctionA();
		ICH_PowerJunction* JctB = Segment->GetJunctionB();
		int32 PortAIndex = Segment->GetPortA();
		int32 PortBIndex = Segment->GetPortB();

		if (JctA && JctB)
		{
			SegmentObject->SetStringField(TEXT("JunctionA"), JctA->GetSystemName());
			SegmentObject->SetNumberField(TEXT("PortA"), PortAIndex); // Storing index might be less robust than querying port info, but matches C++ init

			SegmentObject->SetStringField(TEXT("JunctionB"), JctB->GetSystemName());
			SegmentObject->SetNumberField(TEXT("PortB"), PortBIndex);

			// Include side/offset info for clarity (redundant if loader uses AddPort logic correctly)
			int32 SideA, OffsetA, SideB, OffsetB;
			if (JctA->GetPortInfo(PortAIndex, SideA, OffsetA))
			{
				SegmentObject->SetNumberField(TEXT("SideA"), SideA);
				SegmentObject->SetNumberField(TEXT("OffsetA"), OffsetA);
			}
			if (JctB->GetPortInfo(PortBIndex, SideB, OffsetB))
			{
				SegmentObject->SetNumberField(TEXT("SideB"), SideB);
				SegmentObject->SetNumberField(TEXT("OffsetB"), OffsetB);
			}

			SegmentsArray.Add(MakeShareable(new FJsonValueObject(SegmentObject)));
		}
		else
		{
			UE_LOG(LogTemp, Warning, TEXT("GenerateJsonFromGrid: Segment '%s' has incomplete connections. Skipping."), *Segment->GetName());
		}
	}
	RootObject->SetArrayField(TEXT("Segments"), SegmentsArray);

	// 4. Write to File
	FString OutputString;
	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&OutputString);
	FJsonSerializer::Serialize(RootObject.ToSharedRef(), Writer);
//	UE_LOG(LogTemp, Log, TEXT("GenerateJsonFromGrid: Serialized JSON content to be written:\n%s"), *OutputString); // Log the final JSON string

	if (FFileHelper::SaveStringToFile(OutputString, *FilePath))
	{
//		UE_LOG(LogTemp, Log, TEXT("Successfully wrote JSON to %s"), *FilePath);
		return true;
	}
	else
	{
		UE_LOG(LogTemp, Error, TEXT("Failed to write JSON to %s"), *FilePath);
		return false;
	}
}

// Ensure RegisterJunctionTypes is called at least once, e.g., in a module startup or relevant actor constructor.
// If called from multiple places, the bIsFactoryRegistered flag prevents redundant work.
// Example: Could be called from AVisualTestHarnessActor constructor or BeginPlay if appropriate.
// UPowerGridLoader::RegisterJunctionTypesIfNeeded(); // Call this before loading/using the factory
void UPowerGridLoader::RegisterJunctionTypesIfNeeded()
{
	if (!bIsFactoryRegistered)
	{
		RegisterJunctionTypes();
	}
} 
