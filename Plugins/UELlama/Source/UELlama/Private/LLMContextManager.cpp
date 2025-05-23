#include "LLMContextManager.h"

ULLMContextManager::ULLMContextManager()
{
    // Initialize default block order (most stable to least stable)
    BlockOrder = {
        ELLMContextBlockType::SystemPrompt,
        ELLMContextBlockType::StaticWorldInfo,
        ELLMContextBlockType::LowFrequencyState,
        ELLMContextBlockType::ConversationHistory,
        ELLMContextBlockType::HighFrequencyState
    };
    
    // Initialize blocks with empty content
    for (int32 i = 0; i < (int32)ELLMContextBlockType::COUNT; ++i)
    {
        Blocks.Add((ELLMContextBlockType)i, FString());
    }
}

void ULLMContextManager::UpdateBlock(ELLMContextBlockType BlockType, const FString& NewContent)
{
    if (BlockType >= ELLMContextBlockType::COUNT)
    {
        return;
    }
    
    // Update block content
    Blocks[BlockType] = NewContent;
    
    // Notify listeners
    OnBlockChanged.Broadcast(BlockType);
}

FString ULLMContextManager::GetBlockContent(ELLMContextBlockType BlockType) const
{
    if (const FString* Content = Blocks.Find(BlockType))
    {
        return *Content;
    }
    return FString();
}

void ULLMContextManager::SetBlockOrder(const TArray<ELLMContextBlockType>& NewOrder)
{
    // Validate new order
    TSet<ELLMContextBlockType> ValidTypes;
    for (int32 i = 0; i < (int32)ELLMContextBlockType::COUNT; ++i)
    {
        ValidTypes.Add((ELLMContextBlockType)i);
    }
    
    // Check that all required types are present
    for (const auto& Type : NewOrder)
    {
        if (!ValidTypes.Contains(Type))
        {
            return;  // Invalid type in new order
        }
        ValidTypes.Remove(Type);
    }
    
    if (!ValidTypes.IsEmpty())
    {
        return;  // Missing required types
    }
    
    BlockOrder = NewOrder;
}

bool ULLMContextManager::HasBlockChanged(ELLMContextBlockType BlockType) const
{
    // For now, we consider a block changed if it has content
    // In the future, we could track actual changes with FBlockInfo
    return !GetBlockContent(BlockType).IsEmpty();
}

void ULLMContextManager::MarkBlockUnchanged(ELLMContextBlockType BlockType)
{
    // In the future, we could update FBlockInfo here
    // For now, this is a no-op as we're not tracking changes
}

FString ULLMContextManager::AssembleFullContext() const
{
    FString FullContext;
    
    // Assemble blocks in order
    for (const auto& BlockType : BlockOrder)
    {
        const FString& Content = GetBlockContent(BlockType);
        if (!Content.IsEmpty())
        {
            // Add block header
            switch (BlockType)
            {
                case ELLMContextBlockType::SystemPrompt:
                    FullContext += TEXT("=== System Instructions ===\n");
                    break;
                case ELLMContextBlockType::StaticWorldInfo:
                    FullContext += TEXT("=== Static World Information ===\n");
                    break;
                case ELLMContextBlockType::LowFrequencyState:
                    FullContext += TEXT("=== Low Frequency State ===\n");
                    break;
                case ELLMContextBlockType::ConversationHistory:
                    FullContext += TEXT("=== Conversation History ===\n");
                    break;
                case ELLMContextBlockType::HighFrequencyState:
                    FullContext += TEXT("=== High Frequency State ===\n");
                    break;
                default:
                    break;
            }
            
            // Add content
            FullContext += Content;
            FullContext += TEXT("\n\n");
        }
    }
    
    return FullContext;
}

FString ULLMContextManager::AssembleContextFromBlock(ELLMContextBlockType BlockType) const
{
    return GetBlockContent(BlockType);
} 