#pragma once

#include "CoreMinimal.h"
#include "ICommandHandler.h"

// =====================================
// ICH_OnOff - adds ON aspect commands
// =====================================
class ICH_OnOff : public ICommandHandler
{
protected:
    bool bIsOn = false;
    ICH_PowerJunction* Owner = nullptr;

public:
    ICH_OnOff(const FString& Name, ICH_PowerJunction* InOwner)
    { 
        SystemName = Name; 
        Owner = InOwner;
    }

    virtual void UpdateChange() {
    	PostHandleCommand();
    	if (Owner) Owner->PostHandleCommand();
    }

    virtual ECommandResult HandleCommand(const FString& Aspect, const FString& Command, const FString& Value) override
    {
        if (Aspect == "ON")
        {
//        	if (Owner && !Owner->HasPower()) {
//UE_LOG(LogTemp, Warning, TEXT("ICH_OnOff::HandleCommand: %s %s %s returns %d does not have power"), *Aspect, *Command, *Value, (int)ECommandResult::NotHandled);
//        		return ECommandResult::NotHandled;
//			}
            if (Command == "SET") {
            	bIsOn = Value.ToBool();
            	UpdateChange();
//UE_LOG(LogTemp, Warning, TEXT("ICH_OnOff::HandleCommand: %s %s %s returns %d"), *Aspect, *Command, *Value, (int)ECommandResult::Handled);
            	return ECommandResult::Handled; }
        }
//UE_LOG(LogTemp, Warning, TEXT("ICH_OnOff::HandleCommand:: %s %s %s returns %d"), *Aspect, *Command, *Value, (int)ECommandResult::NotHandled);
        return ECommandResult::NotHandled;
    }

    /** Checks if the handler can process ON SET commands. */
    virtual bool CanHandleCommand(const FString& Aspect, const FString& Command, const FString& Value) const override
    {
        return Aspect == "ON" && Command == "SET";
    }

    virtual FString QueryState(const FString& Aspect) const override
    {
        if (Aspect == "ON") return bIsOn ? "true" : "false";
        return "";
    }

    virtual TArray<FString> QueryEntireState() const override
    {
        return { FString::Printf(TEXT("ON SET %s"), bIsOn ? TEXT("true") : TEXT("false")) };
    }

    virtual TArray<FString> GetAvailableCommands() const override
    {
        return { "ON SET <bool>" };
    }

    /** Returns a list of aspects that can be queried. */
    virtual TArray<FString> GetAvailableQueries() const override
    {
        return { "ON" };
    }

    /** Performs per-frame updates (none needed for base OnOff). */
    virtual void Tick(float DeltaTime) override
    {
        // Base OnOff has no tick logic
    }

    bool IsOn() const { return bIsOn; }
};

