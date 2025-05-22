#pragma once

#include "CoreMinimal.h"
#include "ICH_PowerJunction.h"
#include "VE_ToggleButton.h"

/**
 * PWRJ_MultiFeederJunction: Allows multiple power segments to be connected and powered simultaneously
 * Used by power sources to distribute power to multiple consumers
 */
class PWRJ_MultiFeederJunction : public ICH_PowerJunction
{
private:

public:
    PWRJ_MultiFeederJunction(const FString& name, float InX, float InY, float InW=150, float InH=24)
        : ICH_PowerJunction(name, InX, InY, InW, InH)
    {
    	bIsPowerSource = true;  // Always a power source
    }
	virtual FString GetTypeString() const override { return TEXT("PWRJ_MultiFeederJunction"); }

//    // Command handling for enabling/disabling ports
//    ECommandResult HandleCommand(const FString& Aspect, const FString& Command, const FString& Value) override
//    {
//        return ICH_PowerJunction::HandleCommand(Aspect, Command, Value);
//    }
//
//    bool CanHandleCommand(const FString& Aspect, const FString& Command, const FString& Value) const override
//    {
//        return ICH_PowerJunction::CanHandleCommand(Aspect, Command, Value);
//    }
//
//    FString QueryState(const FString& Aspect) const override
//    {
//        return ICH_PowerJunction::QueryState(Aspect);
//    }
//
//    TArray<FString> QueryEntireState() const override
//    {
//        TArray<FString> Out = ICH_PowerJunction::QueryEntireState();
//        return Out;
//    }
//
//    TArray<FString> GetAvailableCommands() const override
//    {
//        TArray<FString> Out = ICH_PowerJunction::QueryEntireState();
//        return Out;
//    }
//
//    /** Returns a list of aspects that can be queried. */
//    virtual TArray<FString> GetAvailableQueries() const override
//    {
//        TArray<FString> Queries = ICH_PowerJunction::GetAvailableQueries(); // Inherit base queries
//        return Queries;
//    }

//

#ifdef never
    /** Creates toggle buttons for each input port. */
    virtual void InitializeVisualElements() override
    { 
        ICH_PowerJunction::InitializeVisualElements(); // Call base

        int NumInputs = EnabledPorts.Num();
        if (NumInputs <= 0) return;

        float ButtonSize = 15.0f;
        float Radius = 25.0f; // Radius from center to place buttons

        for (int i = 0; i < NumInputs; ++i)
        {
            // Calculate button position around the center
            float Angle = (float)i / NumInputs * 2.0f * PI; // Distribute evenly
            float PosX = Radius * FMath::Cos(Angle);
            float PosY = Radius * FMath::Sin(Angle);
            FBox2D ButtonBounds(FVector2D(PosX - ButtonSize/2.0f, PosY - ButtonSize/2.0f),
                                FVector2D(PosX + ButtonSize/2.0f, PosY + ButtonSize/2.0f));

            // Create the properly configured toggle button
            FString QueryAspectStr = FString::Printf(TEXT("POWERPORT_%d"), i); // e.g., "POWERPORT_0", "POWERPORT_1"
            FString CmdOn = FString::Printf(TEXT("POWERPORT_%d ENABLE"), i);
            FString CmdOff = FString::Printf(TEXT("POWERPORT_%d DISABLE"), i);
            FString DisplayText = FString::Printf(TEXT("%d"), i); // Show port index

            VisualElements.Add(new VE_ToggleButton(this, 
                                                     ButtonBounds, 
                                                     QueryAspectStr,      // Query: "POWERPORT_n"
                                                     CmdOn,               // Command for ON: "POWERPORT ENABLE n"
                                                     CmdOff,              // Command for OFF: "POWERPORT DISABLE n"
                                                     TEXT("true"),        // State is ON if QueryState returns "1"
												 	 TEXT("false"),			// expected value for "OFF"
                                                     DisplayText,         // Text ON
                                                     DisplayText          // Text OFF (same for port number)
                                                     ));
        }
    }

    /** Renders the MultiFeeder symbol and input toggles. */
    virtual void Render(RenderingContext& Context) override
    {
        FVector2D MyPosition = GetPosition();
        FLinearColor BaseColor = IsFaulted() || IsShutdown() ? FLinearColor::Red : FLinearColor::Black;

        // Draw base circle symbol (or custom feeder symbol)
        float SymbolRadius = 8.0f;
        Context.DrawCircle(MyPosition, SymbolRadius, BaseColor, true);
        Context.DrawText(MyPosition + FVector2D(SymbolRadius + 2, -4), SystemName, BaseColor);

        // Update and Render owned visual elements (input toggles)
        int NumInputs = Ports.Num();
        float Radius = 10.0f;
        for (int i = 0; i < VisualElements.Num() && i < NumInputs; ++i)
        { 
            IVisualElement* Element = VisualElements[i];
            if (Element)
            {
                // Manually update visual state based on owner here if UpdateState isn't flexible enough
                // TODO: Need a way to set the visual state of VE_ToggleButton externally or adapt UpdateState.
                // For now, we draw the connecting line based on state:
                float Angle = (float)i / NumInputs * 2.0f * PI;
                FVector2D OuterPoint = MyPosition + FVector2D(Radius * FMath::Cos(Angle), Radius * FMath::Sin(Angle));
                FLinearColor LineColor = EnabledPorts[i]  ? FLinearColor::Green : FLinearColor(0.5f, 0.5f, 0.5f);
                Context.DrawLine(MyPosition, OuterPoint, LineColor, EnabledPorts[i] ? 1.5f : 1.0f);

                Element->Render(Context, MyPosition);
            }
        }
    }
#endif // never
};
