#pragma once

#include "ICommandHandler.h"
#include "PWRJ_MultiSelectJunction.h"
#include "SubmarineState.h"

class SS_ControlRoom : public PWRJ_MultiSelectJunction
{
protected:
    ASubmarineState* SubState;

public:
    SS_ControlRoom(const FString& Name, ASubmarineState* InSubState, float InX, float InY, float InW=150, float InH=24)
        : PWRJ_MultiSelectJunction(Name, InX, InY, InW, InH), SubState(InSubState)
    {
    	DefaultPowerUsage = 0.1f;
    }
	virtual FString GetTypeString() const override { return TEXT("SS_ControlRoom"); }

    virtual ECommandResult HandleCommand(const FString& Aspect, const FString& Command, const FString& Value) override
    {
        if (Aspect == "ALERT" && Command == "SET") {
            if (SubState)
            {
                SubState->AlertLevel = Value;
                return ECommandResult::Handled;
            }
        }
        return PWRJ_MultiSelectJunction::HandleCommand(Aspect, Command, Value);
    }

    virtual bool CanHandleCommand(const FString& Aspect, const FString& Command, const FString& Value) const override
    {
        return (Aspect == "ALERT" && Command == "SET") || PWRJ_MultiSelectJunction::CanHandleCommand(Aspect, Command, Value);
    }

    virtual FString QueryState(const FString& Aspect) const override
    {
        if (Aspect == "ALERT") return SubState ? SubState->AlertLevel : TEXT("UNKNOWN");
        return PWRJ_MultiSelectJunction::QueryState(Aspect);
    }

    virtual TArray<FString> QueryEntireState() const override
    {
        TArray<FString> Out = {
            FString::Printf(TEXT("ALERT SET %s"), SubState ? *SubState->AlertLevel : TEXT("UNKNOWN"))
        };
        Out.Append(PWRJ_MultiSelectJunction::QueryEntireState());
        return Out;
    }

    virtual TArray<FString> GetAvailableCommands() const override
    {
        TArray<FString> Out = {
            TEXT("ALERT SET NORMAL"),
            TEXT("ALERT SET SILENTRUNNING"),
            TEXT("ALERT SET EMERGENCYDIVE"),
            TEXT("ALERT SET MISSILEWARNING")
        };
        Out.Append(PWRJ_MultiSelectJunction::GetAvailableCommands());
        return Out;
    }

    /** Returns a list of aspects that can be queried. */
    virtual TArray<FString> GetAvailableQueries() const override
    {
        TArray<FString> Queries = PWRJ_MultiSelectJunction::GetAvailableQueries(); // Inherit base queries
        Queries.Add("ALERT"); // Add specific query
        return Queries;
    }
};
