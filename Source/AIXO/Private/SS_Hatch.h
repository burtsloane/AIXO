#pragma once

#include "MSJ_Powered_OpenClose.h"
#include "SubmarineState.h" // Included as constructor uses it

class SS_Hatch : public MSJ_Powered_OpenClose {
public: 
    SS_Hatch(const FString& Name, ASubmarineState* InSubState, float X, float Y, float InW=150, float InH=24)
	    : MSJ_Powered_OpenClose(Name, this, InSubState, X, Y, InW, InH) 
    {
		bIsPowerSource = false;
		DefaultPowerUsage = 1.0f; // Base power usage when active
		DefaultNoiseLevel = 0.1f; // Base noise level when active          
	}
	virtual FString GetTypeString() const override { return TEXT("SS_Hatch"); }
	void RenderLabels(RenderingContext& Context) { return; }
}; 
