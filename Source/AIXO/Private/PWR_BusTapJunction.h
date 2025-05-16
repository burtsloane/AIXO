#pragma once

#include "CoreMinimal.h"
#include "SubmarineState.h"
#include "ICommandHandler.h"
#include "PWR_PowerSegment.h"
#include "ICH_PowerJunction.h"
#include "Engine/EngineTypes.h"
#include "VE_ToggleButton.h"

/**
 * PWR_BusTapJunction: Connects three ports in configurable ways
 */
class PWR_BusTapJunction : public ICH_PowerJunction
{
private:
    PWR_PowerSegment* PortA = nullptr;
    PWR_PowerSegment* PortB = nullptr;
    PWR_PowerSegment* PortC = nullptr;

    bool bConnectAB = true;
    bool bConnectAC = true;
    bool bConnectBC = true;

public:
	PWR_BusTapJunction(const FString& name, float InX, float InY, float InW=150, float InH=24) :
		ICH_PowerJunction(name, InX, InY, InW, InH) { } 

    void SetPorts(PWR_PowerSegment* InA, PWR_PowerSegment* InB, PWR_PowerSegment* InC)
    {
        PortA = InA; PortB = InB; PortC = InC;
    }

    void SetConnections(bool AB, bool AC, bool BC)
    {
        bConnectAB = AB; bConnectAC = AC; bConnectBC = BC;
    }

    void ConnectPort(int p, PWR_PowerSegment* seg)
    {
    	switch (p) {
    		case 0:		PortA = seg;		break;
    		case 1:		PortB = seg;		break;
    		case 2:		PortC = seg;		break;
    	}
    }

    TArray<PWR_PowerSegment*> GetConnectedSegments(PWR_PowerSegment* IgnoreSegment = nullptr) const override
    {
        TArray<PWR_PowerSegment*> Result;
        if (bConnectAB && PortA && PortA != IgnoreSegment) Result.Add(PortA);
        if (bConnectAC && PortA && PortA != IgnoreSegment && PortC && PortC != IgnoreSegment) Result.Add(PortC);
        if (bConnectBC && PortB && PortB != IgnoreSegment && PortC && PortC != IgnoreSegment) Result.Add(PortB);
        return Result;
    }

public:
    ECommandResult HandleCommand(const FString& Aspect, const FString& Command, const FString& Value) override
    {
        bool bValue = Value.ToBool();
        if (Aspect == "AB" && Command == "SET") { bConnectAB = bValue; return ECommandResult::Handled; }
        if (Aspect == "AC" && Command == "SET") { bConnectAC = bValue; return ECommandResult::Handled; }
        if (Aspect == "BC" && Command == "SET") { bConnectBC = bValue; return ECommandResult::Handled; }
        return ECommandResult::NotHandled;
    }

    bool CanHandleCommand(const FString& Aspect, const FString& Command, const FString& Value) const override
    {
        return Command == "SET" && (Aspect == "AB" || Aspect == "AC" || Aspect == "BC");
    }

    FString QueryState(const FString& Aspect) const override
    {
        if (Aspect == "AB") return bConnectAB ? "true" : "false";
        if (Aspect == "AC") return bConnectAC ? "true" : "false";
        if (Aspect == "BC") return bConnectBC ? "true" : "false";
        return "";
    }

    TArray<FString> QueryEntireState() const override
    {
        return {
            FString::Printf(TEXT("AB SET %s"), bConnectAB ? TEXT("true") : TEXT("false")),
            FString::Printf(TEXT("AC SET %s"), bConnectAC ? TEXT("true") : TEXT("false")),
            FString::Printf(TEXT("BC SET %s"), bConnectBC ? TEXT("true") : TEXT("false"))
        };
    }

    TArray<FString> GetAvailableCommands() const override
    {
        return { "AB SET <bool>", "AC SET <bool>", "BC SET <bool>" };
    }

    void Tick(float DeltaTime) override {}

    /** Creates toggle buttons for AB, AC, BC connections. */
    virtual void InitializeVisualElements() override
    { 
        ICH_PowerJunction::InitializeVisualElements(); // Call base

        // Define button layouts (example)
        float ButtonWidth = 30.0f;
        float ButtonHeight = 18.0f;
        float VSpacing = 5.0f;
        float HPos = 20.0f; // Position to the right of the symbol
        
        // Button AB (Larger, more important)
        FBox2D BoundsAB(FVector2D(HPos, -ButtonHeight * 1.5f - VSpacing), 
                        FVector2D(HPos + ButtonWidth * 1.2f, -VSpacing)); // Slightly larger
        VisualElements.Add(new VE_ToggleButton(this,
                                                 BoundsAB,
                                                 TEXT("AB"),           // Query Aspect
                                                 TEXT("AB SET true"),  // Command On
                                                 TEXT("AB SET false"), // Command Off
                                                 TEXT("true"),         // Expected State On
												 TEXT("false"),		   // expected value for "OFF"
                                                 TEXT("A-B"),          // Text On
                                                 TEXT("A-B")           // Text Off
                                                 ));

        // Button AC
        FBox2D BoundsAC(FVector2D(HPos, VSpacing), 
                        FVector2D(HPos + ButtonWidth, VSpacing + ButtonHeight));
        VisualElements.Add(new VE_ToggleButton(this,
                                                 BoundsAC,
                                                 TEXT("AC"),           // Query Aspect
                                                 TEXT("AC SET true"),  // Command On
                                                 TEXT("AC SET false"), // Command Off
                                                 TEXT("true"),         // Expected State On
												 TEXT("false"),		   // expected value for "OFF"
                                                 TEXT("A-C"),          // Text On
                                                 TEXT("A-C")           // Text Off
                                                 ));

        // Button BC
        FBox2D BoundsBC(FVector2D(HPos, VSpacing * 2 + ButtonHeight), 
                        FVector2D(HPos + ButtonWidth, VSpacing * 2 + ButtonHeight * 2));
        VisualElements.Add(new VE_ToggleButton(this,
                                                 BoundsBC,
                                                 TEXT("BC"),           // Query Aspect
                                                 TEXT("BC SET true"),  // Command On
                                                 TEXT("BC SET false"), // Command Off
                                                 TEXT("true"),         // Expected State On
												 TEXT("false"),		   // expected value for "OFF"
                                                 TEXT("B-C"),          // Text On
                                                 TEXT("B-C")           // Text Off
                                                 ));
    }

    /** Renders the BusTap symbol and connection toggles. */
    virtual void Render(RenderingContext& Context) override
    {
        FVector2D MyPosition = GetPosition();
        FLinearColor BaseColor = IsFaulted() || IsShutdown() ? FLinearColor::Red : FLinearColor::Black;
        // Get state using direct access or QueryState
        bool bABConnected = QueryState(TEXT("AB")).ToBool(); // Using QueryState now
        bool bACConnected = QueryState(TEXT("AC")).ToBool();
        bool bBCConnected = QueryState(TEXT("BC")).ToBool();

        // Draw the main bus line (vertical example)
        float BusLength = 30.0f;
        FVector2D A_Point = MyPosition + FVector2D(0, -BusLength / 2.0f);
        FVector2D B_Point = MyPosition + FVector2D(0, BusLength / 2.0f);
        Context.DrawLine(A_Point, B_Point, BaseColor, 2.0f);

        // Draw tap point C (horizontal example)
        float TapLength = 15.0f;
        FVector2D C_TapPoint = MyPosition; // Tap off the center
        FVector2D C_EndPoint = MyPosition + FVector2D(TapLength, 0); // Tapping right
        Context.DrawLine(C_TapPoint, C_EndPoint, BaseColor, 1.0f);
        Context.DrawCircle(C_EndPoint, 2.0f, BaseColor, true); // Terminal for C

        // Indicate A-B connection status 
        if (bABConnected)
        {
             Context.DrawLine(A_Point, B_Point, FLinearColor::Yellow, 3.0f); // Thicker/different color
        }

        // Indicate C connection status - Simplified drawing
        FLinearColor C_Color = FLinearColor(0.5f, 0.5f, 0.5f);
        if (bACConnected && bBCConnected && bABConnected) { // C connected to unified A&B
             Context.DrawLine(C_TapPoint, MyPosition + FVector2D(5,0), FLinearColor::Yellow, 1.5f);
             C_Color = FLinearColor::Yellow;
        } else if (bACConnected) { // C connected only to A
            Context.DrawLine(C_TapPoint, A_Point + FVector2D(5,0), FLinearColor::Green, 1.5f);
            C_Color = FLinearColor::Green;
        } else if (bBCConnected) { // C connected only to B
            Context.DrawLine(C_TapPoint, B_Point + FVector2D(5,0), FLinearColor::Green, 1.5f);
            C_Color = FLinearColor::Green;
        }
        // Recolor C terminal based on connection
         Context.DrawCircle(C_EndPoint, 2.0f, C_Color, true); 

        // Update and Render owned visual elements (AB, AC, BC toggles)
        for (IVisualElement* Element : VisualElements)
        { 
            if (Element)
            {
                Element->UpdateState(); // Now uses QueryState correctly
                Element->Render(Context, MyPosition);
            }
        }

        Context.DrawText(MyPosition + FVector2D(TapLength + 2, -4), SystemName, BaseColor);
    }
};
