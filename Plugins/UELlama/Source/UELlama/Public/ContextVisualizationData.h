// ContextVisualizationData.h
// This file can go in YourPlugin/Source/YourPluginModule/Public/
// or YourGameModule/Source/YourGameModuleName/Public/
// Ensure it's accessible to both ULlamaComponent and your Slate/UMG widgets.

#pragma once

#include "CoreMinimal.h"
#include "Styling/SlateColor.h" // For FLinearColor
#include "ContextVisualizationData.generated.h" // For UENUM/USTRUCT if exposed to BP

// Enum to categorize different parts of the LLM context for visualization
UENUM(BlueprintType)
enum class EContextVisBlockType : uint8
{
    SystemPrompt        UMETA(DisplayName = "System Prompt"),
    StaticWorldInfo     UMETA(DisplayName = "Static World Info"),
    LowFrequencyState   UMETA(DisplayName = "Low Frequency State"),
    ConversationTurnUser UMETA(DisplayName = "Conversation: User"),
    ConversationTurnAssistant UMETA(DisplayName = "Conversation: Assistant"),
    ConversationTurnToolResponse UMETA(DisplayName = "Conversation: Tool Response"),
    HighFrequencyState  UMETA(DisplayName = "High Frequency State"),
    // You could add more specific types if needed, e.g., FocusInstruction
    FreeSpace           UMETA(DisplayName = "Free Context Space"),
    Unknown             UMETA(DisplayName = "Unknown Block")
};

// Represents a single colored segment in the visualization bar
USTRUCT(BlueprintType)
struct FContextVisBlock
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ContextVis")
    EContextVisBlockType BlockType = EContextVisBlockType::Unknown;

    // Start position of this block, normalized (0.0 at bottom, 1.0 at top of total capacity)
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ContextVis")
    float NormalizedStart = 0.0f;

    // Height of this block, normalized (as a fraction of total capacity)
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ContextVis")
    float NormalizedHeight = 0.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ContextVis")
    FLinearColor BlockColor = FLinearColor::Gray;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ContextVis")
    FText TooltipText; // Optional: Text to show on hover

    FContextVisBlock() = default;

    FContextVisBlock(EContextVisBlockType InType, float InStart, float InHeight, FLinearColor InColor, FText InTooltip = FText())
        : BlockType(InType), NormalizedStart(InStart), NormalizedHeight(InHeight), BlockColor(InColor), TooltipText(InTooltip)
    {}
};

// Data structure broadcasted from LlamaComponent to update the visualizer
USTRUCT(BlueprintType)
struct FContextVisPayload
{
    GENERATED_BODY()

    // All the colored blocks to draw, in order from bottom to top
    UPROPERTY(BlueprintReadWrite, Category = "ContextVis")
    TArray<FContextVisBlock> Blocks;

    // The total token capacity of the LLM's context window (e.g., llama_n_ctx())
    UPROPERTY(BlueprintReadWrite, Category = "ContextVis")
    int32 TotalTokenCapacity = 32768;

    // The number of tokens currently processed and stored in the KV cache
    // This is where the black line will be drawn.
    UPROPERTY(BlueprintReadWrite, Category = "ContextVis")
    int32 KvCacheDecodedTokenCount = 0;

    // Optional: Total tokens currently represented by the blocks (Fixed + Convo + HFS)
    // This might be different from KvCacheDecodedTokenCount if not all of it is in KV yet.
    // For simplicity, we can derive this from summing block token counts if needed,
    // or assume KvCacheDecodedTokenCount also reflects the "active" prompt part.
    // Let's keep it simple and rely on KvCacheDecodedTokenCount for the line.
};

