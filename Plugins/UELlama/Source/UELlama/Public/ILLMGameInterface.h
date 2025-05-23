#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "ILLMGameInterface.generated.h"

// This interface defines the contract for the game to provide data and handle tool calls
// for the Llama component. The game (AIXO) implements this interface and registers it
// with the Llama component.
UINTERFACE(MinimalAPI, Blueprintable)
class ULLMGameInterface : public UInterface
{
    GENERATED_BODY()
};

class UELLAMA_API ILLMGameInterface
{
    GENERATED_BODY()

public:
    // Context block providers - these return the current state of various game systems
    virtual FString GetSystemsContextBlock() const = 0;  // Current state of all systems
    virtual FString GetStaticWorldInfoBlock() const = 0; // Grid topology, SOPs, etc.
    virtual FString GetLowFrequencyStateBlock() const = 0; // Geopolitical updates, mission phase

    // Tool call handlers - these handle tool calls from the LLM and return results
    virtual FString HandleGetSystemInfo(const FString& QueryString) = 0;
    virtual FString HandleCommandSubmarineSystem(const FString& CommandString) = 0;
    virtual FString HandleQuerySubmarineSystem(const FString& QueryString) = 0;
}; 