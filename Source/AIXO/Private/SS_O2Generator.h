#pragma once

#include "MSJ_Powered_OnOff.h"
#include "SubmarineState.h" // Included as constructor uses it

class SS_O2Generator : public MSJ_Powered_OnOff {
public: 
    SS_O2Generator(const FString& Name, ASubmarineState* InSubState, float X, float Y, float InW=150, float InH=24)
	    : MSJ_Powered_OnOff(Name, this, InSubState, X, Y, InW, InH) 
    {
		bIsPowerSource = false;
		DefaultPowerUsage = 1.0f; // Base power usage when active
		DefaultNoiseLevel = 0.1f; // Base noise level when active          
	}
	virtual FString GetTypeString() const override { return TEXT("SS_O2Generator"); }
	void RenderLabels(RenderingContext& Context) { return; }
}; 
