#pragma once

#include "ICommandHandler.h"

/**
 * ICH_CommandTeeHandler - Routes commands to a primary handler first, then to a secondary handler if not handled.
 * Useful for coordinating between controllers (like PIDs) and their controlled systems.
 */
class ICH_CommandTeeHandler : public ICommandHandler
{
private:
    ICommandHandler* PrimaryHandler;
    ICommandHandler* SecondaryHandler;

public:
    ICH_CommandTeeHandler(const FString& Name, ICommandHandler* InPrimaryHandler, ICommandHandler* InSecondaryHandler)
        : PrimaryHandler(InPrimaryHandler)
        , SecondaryHandler(InSecondaryHandler)
        , SystemName(Name)
    {
    }

    virtual ECommandResult HandleCommand(const FString& Aspect, const FString& Command, const FString& Value) override
    {
        // First try the primary handler (e.g., PID)
        if (PrimaryHandler)
        {
            ECommandResult Result = PrimaryHandler->HandleCommand(Aspect, Command, Value);
            if (Result != ECommandResult::NotHandled)
            {
                return Result;
            }
        }

        // If not handled, try the secondary handler (e.g., actual system)
        if (SecondaryHandler)
        {
            return SecondaryHandler->HandleCommand(Aspect, Command, Value);
        }

        return ECommandResult::NotHandled;
    }

    virtual bool CanHandleCommand(const FString& Aspect, const FString& Command, const FString& Value) const override
    {
        return (PrimaryHandler && PrimaryHandler->CanHandleCommand(Aspect, Command, Value)) ||
               (SecondaryHandler && SecondaryHandler->CanHandleCommand(Aspect, Command, Value));
    }

    virtual FString QueryState(const FString& Aspect) const override
    {
        if (PrimaryHandler)
        {
            FString Result = PrimaryHandler->QueryState(Aspect);
            if (!Result.IsEmpty())
            {
                return Result;
            }
        }

        if (SecondaryHandler)
        {
            return SecondaryHandler->QueryState(Aspect);
        }

        return TEXT("");
    }

    virtual TArray<FString> QueryEntireState() const override
    {
        TArray<FString> State;
        
        if (PrimaryHandler)
        {
            State.Append(PrimaryHandler->QueryEntireState());
        }
        
        if (SecondaryHandler)
        {
            State.Append(SecondaryHandler->QueryEntireState());
        }
        
        return State;
    }

    virtual TArray<FString> GetAvailableCommands() const override
    {
        TArray<FString> Commands;
        
        if (PrimaryHandler)
        {
            Commands.Append(PrimaryHandler->GetAvailableCommands());
        }
        
        if (SecondaryHandler)
        {
            Commands.Append(SecondaryHandler->GetAvailableCommands());
        }
        
        return Commands;
    }

    virtual void Tick(float DeltaTime) override
    {
        if (PrimaryHandler)
        {
            PrimaryHandler->Tick(DeltaTime);
        }
        
        if (SecondaryHandler)
        {
            SecondaryHandler->Tick(DeltaTime);
        }
    }
};

// // Instead of:
// CommandDistributor.RegisterHandler(new SS_Rudder("RUDDER", X, Y));

// // You would do:
// auto Rudder = new SS_Rudder("RUDDER", X, Y);
// auto RudderPID = new PID_RudderHeading(State, Rudder);
// auto RudderTee = new ICH_CommandTeeHandler("RUDDER", RudderPID, Rudder);
// CommandDistributor.RegisterHandler(RudderTee);
