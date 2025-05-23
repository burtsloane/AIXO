#pragma once

#include "CoreMinimal.h"
#include "ICommandHandler.h"

namespace AIXO {

/**
 * Utility functions for generating string representations of command handlers
 */
class CommandHandlerStringUtils
{
public:
    /**
     * Generates a string representation of a command handler's state and capabilities.
     * This string is formatted for inclusion in LLM context.
     * 
     * @param Handler The command handler to generate a string for
     * @return A formatted string containing the handler's state and capabilities
     */
    static FString MakeCommandHandlerString(ICommandHandler* Handler);
};

} // namespace AIXO 