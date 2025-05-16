#pragma once

#include "ICommandHandler.h"
#include "PWRJ_MultiSelectJunction.h"
#include "SS_TorpedoTube.h"
#include "SubmarineState.h"

class SS_TorpedoLoader : public PWRJ_MultiSelectJunction
{
public:
    enum class ETorpedoType {
        None,
        Standard,
        AntennaBuoy,
        CameraBuoy,
        ROV,
        PD,
        Nuke,
        Swarm
    };

    struct Stack {
        TArray<ETorpedoType> Torpedoes;
    };

private:
    static constexpr int NumStacks = 6;
    static constexpr int MaxStackHeight = 10;

    TArray<Stack> Stacks;
    int CentralConveyorPosition = -1; // -1 = empty, 0..5 = over stack, 6 = over load conveyor, 7 = hidden slot
    ETorpedoType ConveyorTorpedo = ETorpedoType::None;
    ETorpedoType LoadConveyorTorpedo = ETorpedoType::None;
    float LoadProgress = 0.0f; // 0.0 .. 1.0

    ASubmarineState* SubState;

public:
    SS_TorpedoLoader(const FString& Name, ASubmarineState* InSub, float InX, float InY, float InW=150, float InH=24)
        : PWRJ_MultiSelectJunction(Name, InX, InY, InW, InH), SubState(InSub)
    {
        Stacks.SetNum(NumStacks);
        for (int i = 0; i < NumStacks; ++i)
        {
            for (int j = 0; j < MaxStackHeight; ++j)
            {
                Stacks[i].Torpedoes.Add(ETorpedoType::Standard);
            }
        }
    }
	virtual FString GetTypeString() const override { return TEXT("SS_TorpedoLoader"); }

    virtual void Tick(float DeltaTime) override
    {
        if (!HasPower()) return;
        if (LoadConveyorTorpedo != ETorpedoType::None)
        {
            LoadProgress += DeltaTime;
            if (LoadProgress > 1.0f)
                LoadProgress = 1.0f;
        }
    }

    virtual ECommandResult HandleCommand(const FString& Aspect, const FString& Command, const FString& Value) override
    {
        FString UAspect = Aspect.ToUpper();
        FString UCommand = Command.ToUpper();

        if (UAspect == "CONVEYOR") {
            if (UCommand == "MOVE_LEFT") { MoveConveyorLeft(); return ECommandResult::Handled; }
            if (UCommand == "MOVE_RIGHT") { MoveConveyorRight(); return ECommandResult::Handled; }
            if (UCommand == "GET_FROM_STACK") {
                int StackIdx = FCString::Atoi(*Value);
                MoveTorpedoFromStack(StackIdx); // Assumes FromTop=true default
                return ECommandResult::Handled; // Note: Doesn't check for errors/block conditions here
            }
            if (UCommand == "PUT_ON_STACK") {
                 int StackIdx = FCString::Atoi(*Value);
                 PutTorpedoBackOnStack(StackIdx); // Assumes OnTop=true default
                 return ECommandResult::Handled; // Note: Doesn't check for errors/block conditions here
            }
            if (UCommand == "LOAD_TUBE") { MoveTorpedoToLoadConveyor(); return ECommandResult::Handled; }
            if (UCommand == "GET_HIDDEN") { MoveHiddenTorpedoToConveyor(); return ECommandResult::Handled; }
        }

        // Delegate to base class if not handled
        return PWRJ_MultiSelectJunction::HandleCommand(Aspect, Command, Value);
    }

    virtual bool CanHandleCommand(const FString& Aspect, const FString& Command, const FString& Value) const override
    {
        FString UAspect = Aspect.ToUpper();
        FString UCommand = Command.ToUpper();
        if (UAspect == "CONVEYOR" && (
                UCommand == "MOVE_LEFT" || UCommand == "MOVE_RIGHT" ||
                UCommand == "GET_FROM_STACK" || UCommand == "PUT_ON_STACK" ||
                UCommand == "LOAD_TUBE" || UCommand == "GET_HIDDEN")) {
            return true;
        }

        // Delegate check to base class
        return PWRJ_MultiSelectJunction::CanHandleCommand(Aspect, Command, Value);
    }

    virtual FString QueryState(const FString& Aspect) const override
    {
        FString UAspect = Aspect.ToUpper();
        if (UAspect == "CONVEYOR_POS") return FString::FromInt(CentralConveyorPosition);
        if (UAspect == "CONVEYOR_ITEM") return TorpedoTypeToString(ConveyorTorpedo);
        if (UAspect == "LOAD_CONVEYOR_ITEM") return TorpedoTypeToString(LoadConveyorTorpedo);
        if (UAspect == "LOAD_PROGRESS") return FString::SanitizeFloat(LoadProgress);
        if (UAspect.StartsWith("STACK_")) {
            int StackIdx = FCString::Atoi(*UAspect.RightChop(6)); // After "STACK_"
            if (StackIdx >= 0 && StackIdx < Stacks.Num()) {
                FString Contents;
                for(int i = Stacks[StackIdx].Torpedoes.Num() - 1; i >= 0; --i) { // Top first
                    Contents += TorpedoTypeToString(Stacks[StackIdx].Torpedoes[i]);
                    if (i > 0) Contents += ",";
                }
                return Contents;
            }
        }

        // Delegate to base class if not handled
        return PWRJ_MultiSelectJunction::QueryState(Aspect);
    }

    virtual TArray<FString> QueryEntireState() const override
    {
        // Get restorable state (Category 5) from base class
        TArray<FString> Out = PWRJ_MultiSelectJunction::QueryEntireState();

        // This system seems entirely action-driven based on the header.
        // There are no obvious Category 5 variables to restore.
        // The state of the stacks might be considered restorable, but complex to represent
        // as simple commands without dedicated LOAD_STACK commands.
        // TODO: Add LOAD_STACK commands to restore stacks.

        return Out;
    }

    virtual TArray<FString> GetAvailableCommands() const override
    {
        TArray<FString> Out = PWRJ_MultiSelectJunction::GetAvailableCommands();
        Out.Append({
            TEXT("CONVEYOR MOVE_LEFT"),
            TEXT("CONVEYOR MOVE_RIGHT"),
            TEXT("CONVEYOR GET_FROM_STACK <0-5>"),
            TEXT("CONVEYOR PUT_ON_STACK <0-5>"),
            TEXT("CONVEYOR LOAD_TUBE"),
            TEXT("CONVEYOR GET_HIDDEN")
        });
        return Out;
    }

    virtual TArray<FString> GetAvailableQueries() const override
    {
        TArray<FString> Queries = PWRJ_MultiSelectJunction::GetAvailableQueries();
        Queries.Append({
            TEXT("CONVEYOR_POS"),
            TEXT("CONVEYOR_ITEM"),
            TEXT("LOAD_CONVEYOR_ITEM"),
            TEXT("LOAD_PROGRESS")
            // Add STACK_n queries dynamically or as pattern
        });
         for(int i=0; i<NumStacks; ++i) {
             Queries.Add(FString::Printf(TEXT("STACK_%d"), i));
         }
        return Queries;
    }

    FString TorpedoTypeToString(ETorpedoType Type) const
    {
        switch (Type)
        {
        case ETorpedoType::Standard: return "Standard";
        case ETorpedoType::AntennaBuoy: return "AntennaBuoy";
        case ETorpedoType::CameraBuoy: return "CameraBuoy";
        case ETorpedoType::ROV: return "ROV";
        case ETorpedoType::PD: return "PD";
        case ETorpedoType::Nuke: return "Nuke";
        case ETorpedoType::Swarm: return "Swarm";
        default: return "None";
        }
    }

    void MoveConveyorLeft()
    {
        if (CentralConveyorPosition > 0 && CentralConveyorPosition <= 6)
            CentralConveyorPosition--;
    }

    void MoveConveyorRight()
    {
        if (CentralConveyorPosition >= 0 && CentralConveyorPosition < 6)
            CentralConveyorPosition++;
    }

    void MoveTorpedoToLoadConveyor()
    {
        if (CentralConveyorPosition == 6 && ConveyorTorpedo != ETorpedoType::None && LoadConveyorTorpedo == ETorpedoType::None)
        {
            LoadConveyorTorpedo = ConveyorTorpedo;
            ConveyorTorpedo = ETorpedoType::None;
            LoadProgress = 0.0f;
        }
    }

    void MoveTorpedoFromStack(int StackIndex, bool FromTop = true)
    {
        if (StackIndex < 0 || StackIndex >= NumStacks) return;
        if (Stacks[StackIndex].Torpedoes.Num() == 0) return;
        if (ConveyorTorpedo != ETorpedoType::None) return;

        ETorpedoType T = FromTop ? Stacks[StackIndex].Torpedoes.Pop() : Stacks[StackIndex].Torpedoes[0];
        if (!FromTop) Stacks[StackIndex].Torpedoes.RemoveAt(0);
        ConveyorTorpedo = T;
        CentralConveyorPosition = StackIndex;
    }

    void PutTorpedoBackOnStack(int StackIndex, bool OnTop = true)
    {
        if (StackIndex < 0 || StackIndex >= NumStacks) return;
        if (ConveyorTorpedo == ETorpedoType::None) return;

        if (OnTop)
            Stacks[StackIndex].Torpedoes.Add(ConveyorTorpedo);
        else
            Stacks[StackIndex].Torpedoes.Insert(ConveyorTorpedo, 0);

        ConveyorTorpedo = ETorpedoType::None;
        CentralConveyorPosition = -1;
    }

    void MoveHiddenTorpedoToConveyor()
    {
        if (ConveyorTorpedo == ETorpedoType::None)
        {
            ConveyorTorpedo = ETorpedoType::Nuke;
            CentralConveyorPosition = 7;
        }
    }

    ETorpedoType GetLoadConveyorTorpedo() const { return LoadConveyorTorpedo; }
    float GetLoadProgress() const { return LoadProgress; }
    ETorpedoType GetConveyorTorpedo() const { return ConveyorTorpedo; }
    int GetConveyorPosition() const { return CentralConveyorPosition; }
    const TArray<Stack>& GetStacks() const { return Stacks; }
};

