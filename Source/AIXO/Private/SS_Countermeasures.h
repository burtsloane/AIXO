// CountermeasuresSystem - Command Handler Implementations
#pragma once

#include "ICommandHandler.h"
#include "PWRJ_MultiSelectJunction.h"
#include "SubmarineState.h"
//#include <map>
//#include <vector>
//#include <string>
//#include <algorithm>

class SS_Countermeasures : public PWRJ_MultiSelectJunction {
public:
    SS_Countermeasures(const FString& Name, ASubmarineState* InSubState, float X, float Y, float InW=150, float InH=24)
        : PWRJ_MultiSelectJunction(Name, X, Y, InW, InH), // Pass rough max power/noise?
          SubState(InSubState)
    {
        for (int i = 0; i < 8; ++i) EMPStatus[i] = false;
        AudioMasking = "OFF";
        Jamming = false;
        Degauss = true;
		JammingPower = 1.2f; // Base power usage when active
		DegaussPower = 0.5f; // Base power usage when active
		MaskPower = 0.8f; // Base power usage when active
		DefaultPowerUsage = JammingPower+DegaussPower+MaskPower; // Base power usage when active
		BaseNoise = 0.05f; // Base noise level when active          
		DefaultNoiseLevel = BaseNoise; // Base noise level when active          
//        Expendables["DECOY"] = 4;
//        Expendables["SPINNER"] = 2;
//        Expendables["CHAFF"] = 6;
    }
	virtual FString GetTypeString() const override { return TEXT("SS_Countermeasures"); }

    virtual ECommandResult HandleCommand(const FString& Aspect, const FString& Command, const FString& Value) override {
        FString UAspect = Aspect.ToUpper();
        if (UAspect.StartsWith("EMP") && Command == "FIRE") {
            int idx = FCString::Atoi(*UAspect.RightChop(3));
            if (idx >= 0 && idx < 8) {
                EMPStatus[idx] = true; // Assume instantaneous for now
                return ECommandResult::Handled;
            }
        }
        if (UAspect == "JAMMING" && Command == "SET") {
            Jamming = Value.ToBool();
            return ECommandResult::Handled;
        }
        if (UAspect == "DEGAUSS" && Command == "SET") {
            Degauss = Value.ToBool();
            return ECommandResult::Handled;
        }
        if (UAspect == "MASK" && Command == "SET") {
            AudioMasking = Value.ToUpper();
            return ECommandResult::Handled;
        }
        if (UAspect == "EXPEND" && Command == "SET") {
            FString Type = Value.ToUpper();
            if (Expendables.Contains(Type) && Expendables[Type] > 0) {
                Expendables[Type]--;
                return ECommandResult::Handled;
            }
        }
        return PWRJ_MultiSelectJunction::HandleCommand(Aspect, Command, Value);
    }

    virtual bool CanHandleCommand(const FString& Aspect, const FString& Command, const FString& Value) const override {
        FString UAspect = Aspect.ToUpper();
        return (UAspect.StartsWith("EMP") && Command == "FIRE") ||
               (UAspect == "JAMMING" && Command == "SET") ||
               (UAspect == "DEGAUSS" && Command == "SET") ||
               (UAspect == "MASK" && Command == "SET") ||
               (UAspect == "EXPEND" && Command == "SET") ||
               PWRJ_MultiSelectJunction::CanHandleCommand(Aspect, Command, Value);
    }

    virtual FString QueryState(const FString& Aspect) const override {
        FString UAspect = Aspect.ToUpper();
        if (UAspect.StartsWith("EMP")) {
            int idx = FCString::Atoi(*UAspect.RightChop(3));
            return (idx >= 0 && idx < 8) ? (EMPStatus[idx] ? TEXT("true") : TEXT("false")) : TEXT("");
        }
        if (UAspect == "JAMMING") return Jamming ? TEXT("true") : TEXT("false");
        if (UAspect == "DEGAUSS") return Degauss ? TEXT("true") : TEXT("false");
        if (UAspect == "MASK") return AudioMasking;
        if (UAspect == "EXPENDABLES") {
            FString Summary;
            for (const auto& Elem : Expendables)
                Summary += Elem.Key + ":" + FString::FromInt(Elem.Value) + TEXT(" ");
            return Summary;
        }
        return PWRJ_MultiSelectJunction::QueryState(Aspect);
    }

    virtual TArray<FString> QueryEntireState() const override {
        // Get restorable state (Category 5) from base class
        TArray<FString> Out = PWRJ_MultiSelectJunction::QueryEntireState();
        
        // Add local restorable state (Category 5)
        Out.Add(FString::Printf(TEXT("JAMMING SET %s"), Jamming ? TEXT("true") : TEXT("false")));
        Out.Add(FString::Printf(TEXT("DEGAUSS SET %s"), Degauss ? TEXT("true") : TEXT("false")));
        Out.Add(FString::Printf(TEXT("MASK SET %s"), *AudioMasking));

        // EMP status (Category 3) and Expendable counts (Category 3) are not included.
        // EMP<n> FIRE and EXPEND SET (Category 6) are actions, not state.

        return Out;
    }

    virtual TArray<FString> GetAvailableCommands() const override {
        TArray<FString> Out = PWRJ_MultiSelectJunction::GetAvailableCommands();
        for (int i = 0; i < 8; ++i)
            Out.Add(FString::Printf(TEXT("EMP%d FIRE"), i));
        Out.Add("JAMMING SET <bool>");
        Out.Add("DEGAUSS SET <bool>");
        Out.Add("MASK SET <mode>");
        Out.Add("EXPEND SET <type>");
        return Out;
    }

    /** Returns a list of aspects that can be queried. */
    virtual TArray<FString> GetAvailableQueries() const override
    {
        TArray<FString> Queries = PWRJ_MultiSelectJunction::GetAvailableQueries(); // Inherit base queries
        // Add specific queries
        for (int i = 0; i < 8; ++i)
        {
            Queries.Add(FString::Printf(TEXT("EMP%d"), i));
        }
        Queries.Append({ "JAMMING", "DEGAUSS", "MASK", "EXPENDABLES" });
        return Queries;
    }

    /** Power usage depends on active countermeasures */
    virtual float GetCurrentPowerUsage() const override 
    {
        if (IsShutdown()) return 0.0f;
        float P = 0.1f;
        if (Jamming) P += JammingPower;
        if (Degauss) P += DegaussPower;
        if (AudioMasking != "OFF") P += MaskPower;
        return P;
    }

    /** Noise level depends on active countermeasures (simplified) */
    virtual float GetCurrentNoiseLevel() const override 
    {
        // Assuming noise is proportional to power usage, scaled by base noise level
        return GetCurrentPowerUsage() > 0.0f ? BaseNoise : 0.0f;
    }

private:
    ASubmarineState* SubState;
    bool EMPStatus[8];
    TMap<FString, int> Expendables;
    bool Jamming;
    bool Degauss;
    FString AudioMasking;
    // Store power/noise contributions
    float JammingPower;
    float DegaussPower;
    float MaskPower;
    float BaseNoise;
};

