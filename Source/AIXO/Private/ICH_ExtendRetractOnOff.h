#pragma once

#include "ICommandHandler.h"
#include "PWRJ_MultiSelectJunction.h"
#include "ICH_ExtendRetract.h"
#include "ICH_OnOff.h"

class ICH_ExtendRetractOnOff : public ICommandHandler {
protected:
    ICH_ExtendRetract *ExtendPart;
    ICH_OnOff *PowerPart;
    ICH_PowerJunction* Owner = nullptr;

public:
    ICH_ExtendRetractOnOff(const FString& Name, ICH_PowerJunction* InOwner)
    {
        SystemName = Name;
        ExtendPart = new ICH_ExtendRetract(Name+"_Extend", InOwner);
        PowerPart = new ICH_OnOff(Name+"_OnOff", InOwner);
        Owner = InOwner;
    }

    virtual void UpdateChange() {
    	PostHandleCommand();
    	if (Owner) Owner->PostHandleCommand();
    }

    virtual ECommandResult HandleCommand(const FString& Aspect, const FString& Command, const FString& Value) override {
        auto Result = ExtendPart->HandleCommand(Aspect, Command, Value);
        if (Result == ECommandResult::Handled) UpdateChange();
        if (Result != ECommandResult::NotHandled) return Result;
        Result = PowerPart->HandleCommand(Aspect, Command, Value);
        if (Result == ECommandResult::Handled) UpdateChange();
        return Result;
    }

    virtual bool CanHandleCommand(const FString& Aspect, const FString& Command, const FString& Value) const override {
        return ExtendPart->CanHandleCommand(Aspect, Command, Value) ||
        	PowerPart->CanHandleCommand(Aspect, Command, Value);
    }

    virtual FString QueryState(const FString& Aspect) const override {
        FString Result = ExtendPart->QueryState(Aspect);
        return !Result.IsEmpty() ? Result : PowerPart->QueryState(Aspect);
    }

    virtual TArray<FString> QueryEntireState() const override {
        TArray<FString> Out = ExtendPart->QueryEntireState();
        Out.Append(PowerPart->QueryEntireState());
        return Out;
    }

    virtual TArray<FString> GetAvailableCommands() const override {
        TArray<FString> Out = ExtendPart->GetAvailableCommands();
        Out.Append(PowerPart->GetAvailableCommands());
        return Out;
    }

    /** Returns a list of aspects that can be queried. */
    virtual TArray<FString> GetAvailableQueries() const override
    {
        TArray<FString> Queries = ExtendPart->GetAvailableQueries();
        Queries.Append(PowerPart->GetAvailableQueries());
        return Queries;
    }

    virtual void Tick(float DeltaTime) override
    {
		if (Owner && !Owner->HasPower()) return;
        PowerPart->Tick(DeltaTime);
        ExtendPart->Tick(DeltaTime);
    }

    bool IsOn() const { return PowerPart->IsOn(); }
    bool IsExtended() const { return ExtendPart->IsExtended(); }
    bool IsMoving() const { return ExtendPart->IsMoving(); }
    float GetExtensionLevel() const { return ExtendPart->GetExtensionLevel(); }
};

