#pragma once

#include "CoreMinimal.h"
#include "LlamaTypes.generated.h"

// Enum for context block types
UENUM(BlueprintType)
enum class ELlamaContextBlockType : uint8
{
    SystemPrompt UMETA(DisplayName = "System Prompt"),
    Systems UMETA(DisplayName = "Systems"),
    LowFrequencyState UMETA(DisplayName = "Low Frequency State"),
    HighFrequencyState UMETA(DisplayName = "High Frequency State"),
    ConversationHistory UMETA(DisplayName = "Conversation History"),
    StaticWorldInfo,     // Grid topology, GetSystemInfo() notes, SOPs
    // ConversationHistory is managed internally by ProcessInputAndGenerate
    // HighFrequencyState is passed directly with ProcessInputAndGenerate
    // UserInput and ToolResponse are also part of ProcessInputAndGenerate
    COUNT UMETA(Hidden) // For iterating if needed
}; 