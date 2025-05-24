#pragma once

#include "CoreMinimal.h"
#include "ICommandHandler.h"

class PWR_PowerSegment;

/**
 * Command Distributor for processing commands through multiple handlers.
 */
class CommandDistributor
{
public:
    TArray<ICommandHandler*> CommandHandlers;
private:
    TMap<FString, ICommandHandler*> HandlerMap;
    TArray<PWR_PowerSegment*> Segments;

public:
    TArray<ICommandHandler*> GetCommandHandlers() const { return CommandHandlers; }
    TArray<PWR_PowerSegment*> GetSegments() const { return Segments; }

    /**
     * Registers a command handler.
     */
    void RegisterHandler(ICommandHandler* Handler)
    {
        CommandHandlers.Add(Handler);
        if (Handler)
        {
            HandlerMap.Add(Handler->GetSystemName(), Handler);
        }
    }

    void RegisterSegment(PWR_PowerSegment* Segment)
    {
        Segments.Add(Segment);
	}

    /**
     * Finds a command handler by its system name.
     * @param Name The name of the system to find.
     * @return Pointer to the ICommandHandler if found, nullptr otherwise.
     */
    ICommandHandler* FindCommandHandler(const FString& Name) const
    {
        return HandlerMap.FindRef(Name);
    }

    /**
     * Processes a command by iterating through handlers until handled.
     */
    ECommandResult ProcessCommand(const FString& FullCommand)
    {
        // Ignore C++ style comments
        if (FullCommand.StartsWith("//"))
        {
            return ECommandResult::NotHandled;
        }

        FString Destination, Aspect, Command, Value;

        // Parse the command into 4 components
        FString RemainingCommand = FullCommand;
        if (!RemainingCommand.Split(".", &Destination, &RemainingCommand))
        {
            Destination = RemainingCommand;
            RemainingCommand = "";
        }

        if (!RemainingCommand.Split(" ", &Aspect, &RemainingCommand))
        {
            Aspect = "";
        }

        if (!RemainingCommand.Split(" ", &Command, &Value))
        {
            Command = RemainingCommand;
            Value = "";
        }

        // Find the correct handler and process the command
        ICommandHandler* Handler = FindCommandHandler(Destination);
        if (Handler)
        {
            ECommandResult r = Handler->HandleCommand(Aspect, Command, Value);
            Handler->PostHandleCommand();
            return r;
        }
        return ECommandResult::NotHandled;
    }

    /**
     * Retrieves all available commands from all handlers, including system names.
     */
    TArray<FString> GetAvailableQueries() const
    {
        TArray<FString> AllQueries;
        for (const ICommandHandler* Handler : CommandHandlers)
        {
            if (Handler)
            {
                TArray<FString> HandlerQueries = Handler->GetAvailableQueries();
                for (FString& Cmd : HandlerQueries)
                {
                    Cmd = Handler->GetSystemName() + "." + Cmd;
                }
                AllQueries.Append(HandlerQueries);
            }
        }
        AllQueries.Add(TEXT("End of available Queries"));
        return AllQueries;
    }

    TArray<FString> GetAvailableCommands() const
    {
        TArray<FString> AllCommands;
        for (const ICommandHandler* Handler : CommandHandlers)
        {
            if (Handler)
            {
                TArray<FString> HandlerCommands = Handler->GetAvailableCommands();
                for (FString& Cmd : HandlerCommands)
                {
                    Cmd = Handler->GetSystemName() + "." + Cmd;
                }
                AllCommands.Append(HandlerCommands);
            }
        }
        AllCommands.Add(TEXT("End of available commands"));
        return AllCommands;
    }

    /**
     * Retrieves the entire state from all handlers, including system names.
     */
    TArray<FString> GenerateCommandsFromEntireState() const
    {
        TArray<FString> FullState;
        for (const ICommandHandler* Handler : CommandHandlers)
        {
            if (Handler)
            {
                TArray<FString> HandlerState = Handler->QueryEntireState();
                for (const FString& StateLine : HandlerState)
                {
                    FullState.Add(Handler->GetSystemName() + "." + StateLine);
                }
            }
        }
        FullState.Add(TEXT("End of entire state"));
        return FullState;
    }

    /**
     * Calls Tick() on all registered command handlers.
     */
    void TickAll(float DeltaTime)
    {
        for (ICommandHandler* Handler : CommandHandlers)
        {
            if (Handler)
            {
                Handler->Tick(DeltaTime);
            }
        }
    }

    /**
     * Processes a block of commands by splitting on newlines and calling ProcessCommand.
     */
    void ProcessCommandBlock(const FString& CommandBlock)
    {
        TArray<FString> CommandLines;
        CommandBlock.ParseIntoArray(CommandLines, TEXT("\n"), true);

        for (const FString& Line : CommandLines)
        {
            ProcessCommand(Line);
        }
    }

    TArray<FString> GetSystemNotifications() const
    {
        TArray<FString> Notifications;
        for (ICommandHandler* Handler : CommandHandlers)
        {
            if (Handler)
            {
                TArray<FString> HandlerNotifications = Handler->RetrieveNotificationQueue();
                for (const FString& StateLine : HandlerNotifications)
                {
                    Notifications.Add(StateLine);
                }
            }
        }
        Notifications.Add(TEXT("End of notifications"));
        return Notifications;
    }

    /** Clears all registered handlers and segments. Needed before loading from JSON. */
    void ClearHandlersAndSegments()
    {
        // NOTE: This does NOT delete the objects, assumes ownership is handled elsewhere (e.g., UPowerGridLoader or the Actor)
        CommandHandlers.Empty();
        HandlerMap.Empty();
        Segments.Empty();
    }
    
	friend class ULlamaComponent;
};





