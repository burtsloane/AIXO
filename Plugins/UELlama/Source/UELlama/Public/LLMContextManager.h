#pragma once

#include "CoreMinimal.h"
#include "UObject/NoExportTypes.h"
#include "LLMContextManager.generated.h"

// Enum for context block types
UENUM(BlueprintType)
enum class ELLMContextBlockType : uint8
{
    SystemPrompt,        // Overall instructions, personality, tool definitions
    StaticWorldInfo,     // Grid topology, GetSystemInfo() notes, SOPs
    LowFrequencyState,   // Geopolitical updates, mission phase (changes rarely)
    ConversationHistory, // Managed internally
    HighFrequencyState,  // Passed directly with each input
    COUNT UMETA(Hidden)
};

UCLASS()
class UELLAMA_API ULLMContextManager : public UObject
{
    GENERATED_BODY()

public:
    ULLMContextManager();

    // Block management
    void UpdateBlock(ELLMContextBlockType BlockType, const FString& NewContent);
    FString GetBlockContent(ELLMContextBlockType BlockType) const;
    
    // Block ordering and change tracking
    void SetBlockOrder(const TArray<ELLMContextBlockType>& NewOrder);
    bool HasBlockChanged(ELLMContextBlockType BlockType) const;
    void MarkBlockUnchanged(ELLMContextBlockType BlockType);
    
    // Context assembly
    FString AssembleFullContext() const;
    FString AssembleContextFromBlock(ELLMContextBlockType BlockType) const;
    
    // Change notification
    DECLARE_MULTICAST_DELEGATE_OneParam(FOnBlockChanged, ELLMContextBlockType);
    FOnBlockChanged OnBlockChanged;

    // Block access for visualization
    const TMap<ELLMContextBlockType, FString>& GetBlocks() const { return Blocks; }
    const TArray<ELLMContextBlockType>& GetBlockOrder() const { return BlockOrder; }

    // Convenience functions for common block types
    FString GetSystemPrompt() const { return GetBlockContent(ELLMContextBlockType::SystemPrompt); }
    FString GetSystemsInfo() const { return GetBlockContent(ELLMContextBlockType::StaticWorldInfo); }
    FString GetLowFreqState() const { return GetBlockContent(ELLMContextBlockType::LowFrequencyState); }

private:
    struct FBlockInfo {
        FString Content;
        bool bHasChanged = false;
        int32 LastUpdateTimestamp = 0;
    };
    
    UPROPERTY()
    TMap<ELLMContextBlockType, FString> Blocks;
    
    TArray<ELLMContextBlockType> BlockOrder;  // Order by change frequency
}; 