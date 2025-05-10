#pragma once

#include "CoreMinimal.h"
#include "SubmarineState.h"
#include "ICommandHandler.h"
#include "Engine/EngineTypes.h"
#include "VE_CommandButton.h"
#include "PWR_PowerSegment.h"

//#define ZERO_SLOT_ALWAYS_ENABLE

//NewObject with empty name can't be used to create default subobjects (inside of UObject derived class constructor) as it produces inconsistent object names. Use ObjectInitializer.CreateDefaultSubobject<> instead.

/**
 * PWRJ_MultiSelectJunction: Allows selection of one of several ports to connect to a main port
 */
class PWRJ_MultiSelectJunction : public ICH_PowerJunction
{
protected:
    int32 SelectedPort = -1;     // -1 means no port selected
    virtual void SetPortEnabled(int32 Port, bool bEnabled) override		// don't use this on a PWRJ_MultiSelectJunction
    {
    	if (bEnabled) {
    		SetAllPortsDisabled();			// setting any port enabled disables all the others
			SelectedPort = Port;
		}
        if (Port >= 0 && Port < EnabledPorts.Num())
        {
            EnabledPorts[Port] = bEnabled;
        }
    }

public:
    PWRJ_MultiSelectJunction(const FString& name, float InX, float InY, float InW=150, float InH=24)
        : ICH_PowerJunction(name, InX, InY, InW, InH)
        , SelectedPort(-1)
    {
    }
	virtual FString GetTypeString() const override { return TEXT("PWRJ_MultiSelectJunction"); }

    /**
     * Sets the currently selected port by its 1-based index.
     * -1 indicates no port is selected (disconnected).
     */
public:
    using ICH_PowerJunction::ICH_PowerJunction;

    void Tick(float DeltaTime) override {}

	virtual ECommandResult SetSelectedPort(int32 Index)
	{
    	SetAllPortsDisabled();			// setting any port selected disables all the others

		SelectedPort = Index;
        if (Index >= 0 && Index < EnabledPorts.Num())
        {
            EnabledPorts[Index] = true;
        }
		return ECommandResult::Handled;
	}

    virtual bool HasPower() const override
    {
//        if (SelectedPort < 0 || SelectedPort >= Ports.Num() || !Ports[SelectedPort])
//            UE_LOG(LogTemp, Warning, TEXT(" >>> No power for %s SelectedPort=%d"), *SystemName, SelectedPort);
        if (SelectedPort < 0 || SelectedPort >= Ports.Num() || !Ports[SelectedPort])
            return false;
//        if (IsShutdown())
//            UE_LOG(LogTemp, Warning, TEXT(" >>> Shutdown for %s SelectedPort=%d"), *SystemName, SelectedPort);
        if (IsShutdown()) return false;
//        if (Ports[SelectedPort]->GetPowerLevel() <= 0.0f)
//            UE_LOG(LogTemp, Warning, TEXT(" >>> Zero power available for %s SelectedPort=%d"), *SystemName, SelectedPort);
        return Ports[SelectedPort]->GetPowerLevel() > 0.0f;
    }

    virtual bool AddPort(PWR_PowerSegment* Segment, int32 InSideWhich, int32 InSideOffset) override   // side: 0=top, 1=left, 2=bottom, 3=right
    {
    	bool b = ICH_PowerJunction::AddPort(Segment, InSideWhich, InSideOffset);
    	SetPortEnabled(Ports.Num() - 1, true);
    	return b;
    }

    virtual void HandleTouchEventExtraFunction(int32 except)
    {
		if (!EnabledPorts[except]) SelectedPort = except;
		else SelectedPort = -1;
    	// multiselect would clear all enables, maybe force 0 enabled
#ifdef ZERO_SLOT_ALWAYS_ENABLE
    	if (EnabledPorts.Num() < 1) return;
    	EnabledPorts[0] = true;
    	for (int i=1; i<EnabledPorts.Num(); i++) if (i != except) EnabledPorts[i] = false;
#else // !ZERO_SLOT_ALWAYS_ENABLE
    	for (int i=0; i<EnabledPorts.Num(); i++) if (i != except) EnabledPorts[i] = false;
#endif // !ZERO_SLOT_ALWAYS_ENABLE
    }

public:	// this is an example of a ICommandHandler implementation that blocks out "POWERPORT" aspect from superclass and adds "POWERSELECT"
    ECommandResult HandleCommand(const FString& Aspect, const FString& Command, const FString& Value) override
    {
//UE_LOG(LogTemp, Warning, TEXT("PWRJ_MultiSelectJunction::HandleCommand: %s %s %s Ports.Num()=%d"), *Aspect, *Command, *Value, Ports.Num());
//        if (Ports.Num() <= 1) 
        if (Aspect == "POWERSELECT" && Command == "SET")
        {
			if (Value == "OFF")
			{
				SetPortEnabled(-1, true);
				return ECommandResult::Handled;
			}
            int32 Index = FCString::Atoi(*Value);
#ifdef ZERO_SLOT_ALWAYS_ENABLE
            if ((Index >= 0 && Index < Ports.Num()) || (Index == -1))
            {
				SetPortEnabled(Index, true);
                return ECommandResult::Handled;
            }
#else // !ZERO_SLOT_ALWAYS_ENABLE
            if ((Index >= 0 && Index < Ports.Num()) || (Index == -1))
            {
				SetPortEnabled(Index, true);
                return ECommandResult::Handled;
            }
#endif // !ZERO_SLOT_ALWAYS_ENABLE
            return ECommandResult::HandledWithError;
        }
        // If not handled here, delegate to the base ICH_PowerJunction
		if (Aspect.StartsWith(TEXT("POWERPORT"))) return ECommandResult::NotHandled;		// can't mess with underlying implementation
        return ICH_PowerJunction::HandleCommand(Aspect, Command, Value);
    }

    bool CanHandleCommand(const FString& Aspect, const FString& Command, const FString& Value) const override
    {
		if (Aspect.StartsWith(TEXT("POWERPORT"))) return false;		// can't mess with underlying implementation
    	if (ICH_PowerJunction::CanHandleCommand(Aspect, Command, Value)) return true;
//    	if (Ports.Num() <= 1) 
    	return false;		// dont handle POWERSELECT command until Ports.Num() >= 2
        return Aspect == "POWERSELECT" && Command == "SET";
    }

    /** Queries the current state, specifically the selected port. */
    virtual FString QueryState(const FString& Aspect) const override
    {
//    	if (Ports.Num() <= 1) 
    	if (Aspect.Equals(TEXT("POWERSELECT"))) return "";		// no queries until Ports.Num() >= 2
        // Check if the aspect is asking for the selected port (case-insensitive)
        if (Aspect.Equals(TEXT("POWERSELECT"), ESearchCase::IgnoreCase))
        {
            return FString::FromInt(SelectedPort);
        }
        
		if (Aspect.StartsWith(TEXT("POWERPORT"))) return "";		// can't mess with underlying implementation

        // If not handled here, pass to base class (ICH_PowerJunction)
        return ICH_PowerJunction::QueryState(Aspect);
    }

    TArray<FString> QueryEntireState() const override
    {
        // Start with the state from the base class (ICH_PowerJunction)
        // This now implicitly includes FAULT/SHUTDOWN status if handled by base
        TArray<FString> Out = ICH_PowerJunction::QueryEntireState();
        for (int i=Out.Num(); --i>=0; ) {
        	if (Out[i].StartsWith(TEXT("POWERPORT"))) {		// can't mess with underlying implementation
        		Out.RemoveAt(i);
        	}
        }
//        if (Ports.Num() > 1) {		// don't report queries until Ports.Num() >= 2
        	if (SelectedPort >= 0) Out.Add(FString::Printf(TEXT("POWERSELECT SET %d"), SelectedPort));
	        else Out.Add(FString::Printf(TEXT("POWERSELECT SET -1")));
//		}		// but this didn't record on/off for 1 port SS_* classes local connector
        return Out;
    }

    TArray<FString> GetAvailableCommands() const override
    {
        TArray<FString> Out = ICH_PowerJunction::QueryEntireState();
        for (int i=Out.Num(); --i>=0; ) {
        	if (Out[i].StartsWith(TEXT("POWERPORT"))) {		// can't mess with underlying implementation
        		Out.RemoveAt(i);
        	}
        }
//        if (Ports.Num() > 1) 
        Out.Add(TEXT("POWERSELECT SET <port>|-1|OFF"));		// WHY don't report commands until Ports.Num() >= 2
        return Out;
    }

    /** Returns a list of aspects that can be queried. */
    virtual TArray<FString> GetAvailableQueries() const override
    {
        TArray<FString> Queries = ICH_PowerJunction::GetAvailableQueries(); // Inherit base queries
        for (int i=Queries.Num(); --i>=0; ) {
        	if (Queries[i].StartsWith(TEXT("POWERPORT"))) {		// can't mess with underlying implementation
        		Queries.RemoveAt(i);
        	}
        }
//        if (Ports.Num() > 1) 
        Queries.Add("POWERSELECT");
        return Queries;
    }

#ifdef never
public:
    /** Renders the specific SPMT symbol and its owned visual elements. */
    virtual void Render(RenderingContext& Context) override
    {
        FVector2D MyPosition = GetPosition();
        FLinearColor DrawColor = IsFaulted() || IsShutdown() ? FLinearColor::Red : FLinearColor::Black;
        int NumPorts = Ports.Num();

        // Draw SPMT specific symbol
        float Radius = 6.0f;
        float TerminalLength = 15.0f;
        FVector2D CommonPoint = MyPosition - FVector2D(Radius * 1.5f, 0); // Common input point

        // Draw common terminal
        Context.DrawLine(CommonPoint - FVector2D(Radius*0.5f, 0), CommonPoint, DrawColor, 1.0f);

        // Calculate positions for selectable terminals (P1 to PN) in an arc
        TArray<FVector2D> TerminalPoints;
        if (NumPorts > 0) {
             float StartAngle = -PI / 3.0f; // Example arc range
             float EndAngle = PI / 3.0f;
             float AngleStep = (NumPorts > 1) ? (EndAngle - StartAngle) / (NumPorts - 1) : 0;

            for(int i = 0; i < NumPorts; ++i) {
                float Angle = StartAngle + i * AngleStep;
                FVector2D TerminalPos = MyPosition + FVector2D(TerminalLength * FMath::Cos(Angle), TerminalLength * FMath::Sin(Angle));
                TerminalPoints.Add(TerminalPos);
                // Draw terminal contact point
                Context.DrawCircle(TerminalPos, 2.0f, DrawColor, true);
            }
        }
        
        // Draw wiper line based on selection
        if (SelectedPort > 0 && SelectedPort <= TerminalPoints.Num())
        {
             Context.DrawLine(CommonPoint, TerminalPoints[SelectedPort - 1], DrawColor, 1.5f); 
        }
        // Else (SelectedPort == -1 or invalid), draw no connection

        // Update and Render owned visual elements (the command buttons)
        for (IVisualElement* Element : VisualElements)
        {
            if (Element)
            {
                Element->UpdateState(); // Ensure element reflects current state
                Element->Render(Context, MyPosition);
            }
        }
        
        // Draw name slightly offset
        Context.DrawText(MyPosition + FVector2D(Radius + 2, -4), SystemName, DrawColor); 
    }

    /** Creates the visual elements for a generic MultiSelect Junction. */
    virtual void InitializeVisualElements() override
    {
        ICH_PowerJunction::InitializeVisualElements(); // Call base 

        // Create N+1 command buttons for N ports + OFF
        int NumPorts = Ports.Num(); // Assuming GetPortCount() exists
        if (NumPorts <= 0) return; // No ports, no buttons needed

        float ButtonWidth = 25.0f;
        float ButtonHeight = 18.0f;
        float ButtonSpacing = 4.0f;
        int MaxButtonsPerRow = 5; // Example limit 
        float StartY = 10.0f; // Position below the symbol

        for (int i = 0; i <= NumPorts; ++i) // Iterate N ports + 1 for OFF
        {
            int PortIndex = (i < NumPorts) ? (i + 1) : -1; // Port index (1 to N, or -1 for OFF)
            FString CommandValue = FString::FromInt(PortIndex);
            FString ButtonText = (PortIndex == -1) ? TEXT("OFF") : FString::Printf(TEXT("P%d"), PortIndex);
            FString Command = FString::Printf(TEXT("POWERSELECT SET %s"), *CommandValue);

            // Calculate position (simple horizontal layout for now)
            int Row = i / MaxButtonsPerRow;
            int Col = i % MaxButtonsPerRow;
            float PosX = (Col - (MaxButtonsPerRow -1) * 0.5f) * (ButtonWidth + ButtonSpacing);
            float PosY = StartY + Row * (ButtonHeight + ButtonSpacing);

            FBox2D ButtonBounds(FVector2D(PosX - ButtonWidth/2.0f, PosY), 
                                FVector2D(PosX + ButtonWidth/2.0f, PosY + ButtonHeight));

            VisualElements.Add(new VE_CommandButton(this, ButtonBounds, TEXT("POWERSELECT"), Command, FString::FromInt(PortIndex), ButtonText));
        }
    }
#endif // never
};


