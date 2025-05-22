#pragma once

#include "CoreMinimal.h"
#include "PWR_PowerSegment.h"
#include "SubmarineState.h"
#include "ICommandHandler.h"
#include "Engine/EngineTypes.h"
#include "IVisualElement.h"

/**
 * Power junction status enumeration
 */
enum class EPowerJunctionStatus
{
	NORMAL,
	DAMAGED50,
	DAMAGED100,
	DESTROYED
};

/**
 * Base class for power junctions
 */
class ICH_PowerJunction : public ICommandHandler
{
public:
    float X, Y, W, H; // Location for drawing the network visually
	const int32 B = 14;	// extra border around the box, length of a pin
	const int32 P = 12;	// width of a pin
    bool bIsPowerSource; // Indicates if this junction supplies power
    FString MarkerX = TEXT(""); // Name of the marker X is relative to (if any)
    FString MarkerY = TEXT(""); // Name of the marker Y is relative to (if any)
protected:
    float DefaultPowerUsage = 0.1f; // Base power usage when active
    float DefaultNoiseLevel = 0.0f; // Base noise level when active

    // Visual Elements owned by this junction
    TArray<IVisualElement*> VisualElements;
    IVisualElement* CurrentVisualElement;

protected:		// ports
    TArray<PWR_PowerSegment*> Ports;  // All ports can be active simultaneously
    TArray<bool> EnabledPorts;        // Track which ports are enabled
    TArray<int32> SideWhich;          // 0=top, 1=left, 2=bottom, 3=right
    TArray<int32> SideOffset;         // how far down the side

protected:		// determined by power propagator
    TArray<PWR_PowerSegment*> PathToSourceSegments;
    TArray<ICH_PowerJunction*> PathToSourceJunction;
    bool bIsShorted;
    bool bIsOverenergized;
    bool bIsUnderPowered;
    bool bIsShutdown;
    bool bIsSelected;

    EPowerJunctionStatus Status = EPowerJunctionStatus::NORMAL; // Junction status
	FBox2D ActualExtent;	// can be bigger if VE_* is outside the basic box

public:
    ICH_PowerJunction(const FString& name, float InX, float InY, float InW = 150.f, float InH = 24.f)
        : X(InX), Y(InY), W(InW), H(InH),
          bIsPowerSource(false),
          DefaultPowerUsage(0.0f), DefaultNoiseLevel(0.0f)
    { 
        SystemName = name; 
        CurrentVisualElement = nullptr;
        ActualExtent.Min.X = InX - B;
        ActualExtent.Min.Y = InY - B;
        ActualExtent.Max.X = InX + InW + B;
        ActualExtent.Max.Y = InY + InH + B;
    }

    virtual ~ICH_PowerJunction()
    {
        // Clean up owned visual elements
        for (IVisualElement* Element : VisualElements)
        {
            delete Element;
        }
        VisualElements.Empty();
    }

    // --- Status Accessors ---
    bool IsPowerSource() const { return bIsPowerSource; }
    bool IsFaulted() const { return bIsShutdown; }
    bool IsShutdown() const { return bIsShutdown; }
    void SetShutdown(bool bShutdown) { bIsShutdown = bShutdown; }
    void SetPathToSourceSegments(const TArray<PWR_PowerSegment*>& Path) { PathToSourceSegments = Path; }
    void SetPathToSourceJunction(const TArray<ICH_PowerJunction*>& Path) { PathToSourceJunction = Path; }
    const TArray<PWR_PowerSegment*>& GetPathToSourceSegments() const { return PathToSourceSegments; }
    const TArray<ICH_PowerJunction*>& GetPathToSourceJunction() const { return PathToSourceJunction; }
    virtual PWR_PowerSegment *GetChargingSegment() const { return nullptr; }
    virtual bool IsChargingPort(int n) { return false; }
    virtual bool IsChargingPort(PWR_PowerSegment *seg) { return false; }

    /** Returns the current operational power usage of the junction. Can be overridden by derived classes. */
    virtual float GetCurrentPowerUsage() const
    {
        // Base implementation returns default usage if not shutdown/faulted
        if (IsShutdown()) return 0.0f;
        return DefaultPowerUsage;
    }

    /** Returns the  operational noise level of the junction. Can be overridden by derived classes. */
    virtual float GetCurrentNoiseLevel() const
    {
        // Base implementation returns default noise if not shutdown/faulted
        return IsShutdown() ? 0.0f : DefaultNoiseLevel;
    }
    
    float GetW() const { return W; }
    float GetH() const { return H; }

    virtual int32 GetNumPorts() { return Ports.Num(); }

    // Add a new port to the junction
    virtual bool AddPort(PWR_PowerSegment* Segment, int32 InSideWhich, int32 InSideOffset)   // side: 0=top, 1=left, 2=bottom, 3=right
    {
        Ports.Add(Segment);
        EnabledPorts.Add(true);  // Ports are enabled by default to support AVisualTestHarnessActor::InitializeCommandHandlers()
        SideWhich.Add(InSideWhich);
        SideOffset.Add(InSideOffset);
        return true;
    }

    // Enable or disable a specific port
    virtual bool IsPortEnabled(int32 Port) const
    {
        if (Port >= 0 && Port < EnabledPorts.Num())
        {
        	return EnabledPorts[Port];
        }
        return false;
    }

    // Enable or disable a specific port
    virtual void SetPortEnabled(int32 Port, bool bEnabled)
    {
        if (Port >= 0 && Port < EnabledPorts.Num())
        {
            EnabledPorts[Port] = bEnabled;
        }
    }

    virtual void SetAllPortsDisabled()
    {
    	for (int i = 0; i<EnabledPorts.Num(); i++) EnabledPorts[i] = false;
    }

    virtual void PostHandleCommand() override
    {
    	for (IVisualElement* Element : VisualElements)
        {
        	Element->UpdateState();
		}
	}

    virtual bool HasPower() const 
    {
        return true;
    }

    bool IsShorted() const { return bIsShorted; }
    void SetShorted(bool bShorted) { bIsShorted = bShorted; }
    bool IsOverenergized() const { return bIsOverenergized; }
    void SetOverenergized(bool bOver) { bIsOverenergized = bOver; }
    bool IsUnderPowered() const { return bIsUnderPowered; }
    void SetUnderPowered(bool bUnder) { bIsUnderPowered = bUnder; }

public:
    // Override to return all enabled ports
    virtual TArray<PWR_PowerSegment*> GetConnectedSegments(PWR_PowerSegment* IgnoreSegment = nullptr) const
    {
        TArray<PWR_PowerSegment*> Result;
		for (int32 i = 0; i < Ports.Num(); ++i)
		{
			if ((IgnoreSegment == Ports[i]) && (!EnabledPorts[i])) return Result;
		}
        for (int32 i = 0; i < Ports.Num(); ++i)
        {
            if (EnabledPorts[i] && Ports[i] && Ports[i] != IgnoreSegment)
            {
                Result.Add(Ports[i]);
            }
        }
        return Result;
    }

    /** Needs to be called after construction to set up visuals */
    virtual void InitializeVisualElements()
    {
        // Base implementation does nothing; derived classes override to add elements.
    }

    // --- ICommandHandler Implementation ---
    virtual ECommandResult HandleCommand(const FString& Aspect, const FString& Command, const FString& Value) override
    {
//UE_LOG(LogTemp, Warning, TEXT("ICH_PowerJunction::HandleCommand: %s %s %s %s"), *SystemName, *Aspect, *Command, *Value);
        if (Aspect.StartsWith("POWERPORT_") && (Command == "ENABLE" || Command == "DISABLE"))
        {
            FString PortStr;
            if (Aspect.Split("_", nullptr, &PortStr))
            {
                int32 Port = FCString::Atoi(*PortStr);
                if (Port >= 0 && Port < Ports.Num())
                {
                    SetPortEnabled(Port, Command == "ENABLE");
                    return ECommandResult::Handled;
                }
//else UE_LOG(LogTemp, Warning, TEXT("                                : port %d out of range 0..%d"), Port, Ports.Num());
            }
//else UE_LOG(LogTemp, Warning, TEXT("                                : _ not found"));
        }
        return ECommandResult::NotHandled; 
    }

    virtual bool CanHandleCommand(const FString& Aspect, const FString& Command, const FString& Value) const override
    {
        if (Aspect.StartsWith("POWERPORT_"))
        {
            FString PortStr;
            if (Aspect.Split("_", nullptr, &PortStr))
            {
                int32 Port = FCString::Atoi(*PortStr);
                if (Port >= 0 && Port < Ports.Num())
                {
                    return true;
                }
            }
        }
        return false;
    }

    virtual TArray<FString> GetAvailableCommands() const override
    {
        return { "POWERPORT_<n> ENABLE/DISABLE" };
    }

    /** Returns a list of aspects that can be queried. */
    virtual TArray<FString> GetAvailableQueries() const override
    {
        // Return aspects handled by QueryState
        return { "POWERPORT_<n>", "POWERFAULT", "POWERLEVEL", "NOISELEVEL" };
    }

    /** Returns the state related to fault/shutdown status */
    virtual TArray<FString> QueryEntireState() const override
    {
        TArray<FString> Out;
        for (int32 i = 0; i < Ports.Num(); ++i)
        {
            Out.Add(FString::Printf(TEXT("POWERPORT_%d %s"), i, 
                EnabledPorts[i] ? TEXT("ENABLE") : TEXT("DISABLE")));
        }
        return Out;
    }

    virtual FString QueryState(const FString& Aspect) const override
    {
        if (Aspect.StartsWith("POWERPORT_"))
        {
            FString PortStr;
            if (Aspect.Split("_", nullptr, &PortStr))
            {
                int32 Port = FCString::Atoi(*PortStr);
                if (Port >= 0 && Port < Ports.Num())
                {
                    return EnabledPorts[Port] ? TEXT("true") : TEXT("false");
                }
            }
        }
        if (Aspect.Equals(TEXT("POWERFAULT"), ESearchCase::IgnoreCase))
        {
            return (bIsShutdown ? TEXT("true") : TEXT("false"));
        }
        if (Aspect.Equals(TEXT("POWERLEVEL"), ESearchCase::IgnoreCase))
        {
            return FString::Printf(TEXT("%.2f"), GetCurrentPowerUsage());
        }
        if (Aspect.Equals(TEXT("NOISELEVEL"), ESearchCase::IgnoreCase))
        {
            return FString::Printf(TEXT("%.2f"), GetCurrentNoiseLevel());
        }
        return TEXT(""); // Return empty string if aspect is not handled by base class
    }

    /** Performs per-frame updates (currently none needed for base junction). */
    virtual void Tick(float DeltaTime) override
    {
        // Base power junction has no tick logic by itself
    }

    // --- Accessors ---
    virtual FVector2D GetPosition() const { return FVector2D(X, Y); }
    virtual float GetPowerAvailable() const { return 0; }
    virtual EPowerJunctionStatus GetStatus() const { return Status; } // Getter for status
    virtual void SetStatus(EPowerJunctionStatus NewStatus) { Status = NewStatus; } // Setter for status

    // --- Visualization Methods ---

    /** Renders the junction and its associated visual elements. */
    virtual void Render(RenderingContext& Context)
    {
        RenderBG(Context);
        RenderName(Context);
        RenderPins(Context);
        RenderLabels(Context);
        RenderVEs(Context);
    }

    virtual void RenderUnderlay(RenderingContext& Context)
    {
    }

    virtual FLinearColor RenderBGGetColor()
    {
        FLinearColor c = FLinearColor::White;
        if (IsFaulted() || IsShutdown()) c = FLinearColor::Red;
		else if (IsShorted()) c = FLinearColor(0.4f, 0.0f, 0.0f);
		else if (IsOverenergized()) c = FLinearColor(1.0f, 0.5f, 0.5f);
		else if (IsUnderPowered()) {
			if (((int32)(FPlatformTime::Seconds() / 0.5f)) & 1) {
				c = FLinearColor(0.0f, 0.0f, 1.0f);		// underpowered color
			}
		}
		else if (!HasPower()) c = FLinearColor(0.5f, 0.5f, 0.5f);
//		else if (IsOn()) {
//			if (((int32)(FPlatformTime::Seconds() / 0.5f)) & 1) {
//				c = FLinearColor(1.0f, 1.0f, 0.0f);		// active color
//			}
//		}
		return c;
    }

    virtual void RenderBG(RenderingContext& Context)
    {
        FVector2D MyPosition = GetPosition();
        FLinearColor tc = FLinearColor::Black;
        FLinearColor c = RenderBGGetColor();
		if (bIsSelected) {
			if (((int32)(FPlatformTime::Seconds() / 0.5f)) & 1) {
				c = FLinearColor(0.0f, 0.5f, 0.0f);		// selected color
				tc = FLinearColor::White;
			}
		}
        //
//        FBox2D outer;
//        outer.Min.X = X-B;
//        outer.Min.Y = Y-B;
//        outer.Max.X = X+W+B-1;
//        outer.Max.Y = Y+H+B-1;
//        Context.DrawRectangle(outer, FLinearColor::Gray, true);
        FBox2D border;
        FBox2D inner;
        inner.Min.X = X;
        inner.Min.Y = Y;
        inner.Max.X = X+W-1;
        inner.Max.Y = Y+H-1;
		Context.DrawRectangle(inner, c, true);
        if (bIsPowerSource) {
			border.Min.X = X-2;
			border.Min.Y = Y-2;
			border.Max.X = X+W+2;
			border.Max.Y = Y+H+2;
			Context.DrawRectangle(border, tc, false);
		}
		border.Min.X = X;
		border.Min.Y = Y;
		border.Max.X = X+W;
		border.Max.Y = Y+H;
		Context.DrawRectangle(border, tc, false);
    }

    virtual void RenderName(RenderingContext& Context)
    {
        FVector2D Position;
        FLinearColor tc = FLinearColor::Black;
        Position.X = X+2;
        Position.Y = Y+1;
		if (bIsSelected) {
			if (((int32)(FPlatformTime::Seconds() / 0.5f)) & 1) {
				tc = FLinearColor::White;
			}
		}
        Context.DrawText(Position, SystemName, tc);
    }

	virtual void RenderLabels(RenderingContext& Context);

    virtual void RenderOnePin(RenderingContext& Context, int i, FBox2D b)
    {
		if (EnabledPorts[i]) {
			FString s = FString::Printf(TEXT("%d"), i);
			Context.DrawRectangle(b, FLinearColor::Black, true);
			FVector2D p = b.Min;
			p.X+=3;
			p.Y++;
// labels
			if (Ports.Num() > 1) Context.DrawTinyText(p, s, FLinearColor::White);
		} else {
			FString s = FString::Printf(TEXT("%d"), i);
			Context.DrawRectangle(b, FLinearColor::White, true);
//			if ((SideWhich[i] == 0) || (SideWhich[i] == 1)) {
//				b.Min.X++;
//				b.Min.Y++;
//				b.Max.X++;
//				b.Max.Y++;
//			}
			Context.DrawRectangle(b, FLinearColor::Black, false);
			FVector2D p = b.Min;
			p.X+=3;
			p.Y++;
// labels
			if (Ports.Num() > 1) Context.DrawTinyText(p, s, FLinearColor::Black);
		}
    }

    virtual void RenderPins(RenderingContext& Context)
    {
        for (int i=0; i<Ports.Num(); i++) {
	        FBox2D b;
        	b.Min.X = X;
        	b.Min.Y = Y;
        	switch (SideWhich[i]) {
        		case 0:
        			b.Min.X += SideOffset[i] - P/2;// - 1;
        			b.Min.Y -= B;
        			b.Max.X = b.Min.X + P;
        			b.Max.Y = b.Min.Y + B;// - 1;
        			break;
        		case 1:
        			b.Min.X -= B;
        			b.Min.Y += SideOffset[i] - P/2;// - 1;
        			b.Max.X = b.Min.X + B;// - 1;
        			b.Max.Y = b.Min.Y + P;
        			break;
        		case 2:
        			b.Min.X += SideOffset[i] - P/2;// - 1;
        			b.Min.Y += H;
        			b.Max.X = b.Min.X + P;
        			b.Max.Y = b.Min.Y + B;// - 1;
        			break;
        		case 3:
        			b.Min.X += W;
        			b.Min.Y += SideOffset[i] - P/2;// - 1;
        			b.Max.X = b.Min.X + B;// - 1;
        			b.Max.Y = b.Min.Y + P;
        			break;
				default:
					continue;
        	}
        	RenderOnePin(Context, i, b);
        }
    }

    virtual void RenderVEs(RenderingContext& Context)
    {
        FVector2D MyPosition = GetPosition();
        // Render owned visual elements (relative to this junction's position)
        for (IVisualElement* Element : VisualElements)
        {
            if (Element)
            {
                // Element->UpdateState(); // Let element pull state from owner
                Element->Render(Context, MyPosition);
            }
        }
    }

    virtual FVector2D GetPortConnection(int32 Port)
    {
    	FVector2D ret;
    	ret.X = X;
    	ret.Y = Y;
    	int32 offs = SideOffset[Port] - 1;
		switch (SideWhich[Port]) {
			case 0:		// top
				ret.X += offs;
				ret.Y -= B;
				break;
			case 1:		// left
				ret.X -= B;
				ret.Y += offs;
				break;
			case 2:		// bottom
				ret.X += offs;
				ret.Y += H + B;
				break;
			case 3:		// right
				ret.X += W + B;
				ret.Y += offs;
				break;
		}
    	return ret;
    }

    virtual void HandleTouchEventExtraFunction(int32 except)
    {
    	// multiselect would clear all enables
    }

    /** Handles touch events, checking self and owned visual elements. */
    virtual int32 HandleJunctionTouchEvent(const TouchEvent& Event, CommandDistributor* Distributor)
    {
//		switch (Event.Type) {
//			case TouchEvent::EType::Down:
//				break;
//			case TouchEvent::EType::Move:
//				break;
//			case TouchEvent::EType::Up:
//				break;
//			case TouchEvent::EType::Cancel:
//				break;
//		}
    	// test ports for click
    	int32 besti = -1;
    	int32 bestd = 999999;
    	int32 bestmin = B*B;
    	for (int i = 0; i<Ports.Num(); i++) {
    		FVector2D p = GetPortConnection(i);
    		// compute distance squared to click
    		float d = (p.X-Event.Position.X)*(p.X-Event.Position.X) + (p.Y-Event.Position.Y)*(p.Y-Event.Position.Y);
    		if (d >= bestmin) continue;
    		if (d < bestd) {
    			bestd = d;
    			besti = i;
    		}
    	}
		switch (Event.Type) {
			case TouchEvent::EType::Down:
				// Check visual elements first (in reverse order for potential overlap)
				for (int i = VisualElements.Num() - 1; i >= 0; --i)
				{ 
					IVisualElement* Element = VisualElements[i];
					if (Element)
					{
						FBox2D RelativeBounds = Element->GetRelativeBounds();
						FBox2D WorldBounds(RelativeBounds.Min + GetPosition(), RelativeBounds.Max + GetPosition());

//UE_LOG(LogTemp, Warning, TEXT("HandleTouchEvent: checking %g,%g against [%s]"), Event.Position.X, Event.Position.Y, *WorldBounds.ToString());

						if (WorldBounds.IsInside(Event.Position))
						{ 
//UE_LOG(LogTemp, Warning, TEXT("               calling HandleTouch(%d)"), (int)Event.Type);
							if (Element->HandleTouch(Event, Distributor))
							{
								CurrentVisualElement = Element;
								return 1; // Event handled by a child element
							}
						}
					}
				}
				if (besti >= 0) {
					return 1;		// make this the current PowerJunction so we get the up-click
				}
				{
					FBox2D SelfBounds(GetPosition(), GetPosition() + FVector2D(W, H)); // Example bounds
					if (SelfBounds.IsInside(Event.Position))
					{
						// Handle direct touch on junction? Maybe show info popup?
//UE_LOG(LogTemp, Warning, TEXT("               touched junction: %s"), *SystemName);
						return 2; 		// new selection
					}
				}
				break;
			case TouchEvent::EType::Move:
				if (CurrentVisualElement) {
					CurrentVisualElement->HandleTouch(Event, Distributor);
					return 1; // Event handled by a child element
				}
				break;
			case TouchEvent::EType::Up:
				if (CurrentVisualElement) {
					CurrentVisualElement->HandleTouch(Event, Distributor);
					CurrentVisualElement = nullptr;
					return 1; // Event handled by a child element
				} else {
					if (besti >= 0) {
						HandleTouchEventExtraFunction(besti);		// - ___: MultiSelect clear all enables (maybe auto-enable port 0)
						EnabledPorts[besti] ^= true;
						return 1;
					}
				}
				break;
			case TouchEvent::EType::Cancel:
				CurrentVisualElement = nullptr;
				return 1; // Event handled by a child element
				break;
		}
        return 0; // Event not handled by this junction or its elements
    }

    // --- RTTI-Replacement for Casting a ICommandHandler ---
    virtual class ICH_PowerJunction* GetAsPowerJunction() override { return this; }
    virtual const class ICH_PowerJunction* GetAsPowerJunction() const override { return this; }

    // --- Type Information for JSON Generation ---
    // Derived classes MUST implement this to return their specific type string (e.g., "SS_Battery1")
    virtual FString GetTypeString() const = 0;

    virtual bool IsOn() const { return true; }		// subclasses can default to on, or relay a on/off switch

    // Helper to get port side/offset information
    bool GetPortInfo(int32 PortIndex, int32& OutSide, int32& OutOffset) const
    {
        if (PortIndex >= 0 && PortIndex < SideWhich.Num() && PortIndex < SideOffset.Num())
        {
            OutSide = SideWhich[PortIndex];
            OutOffset = SideOffset[PortIndex];
            return true;
        }
        return false;
    }

    // Hit testing
    virtual bool IsPointNear(const FVector2D& Point) const
    {
        // Check if point is within junction bounds plus a small margin
//        const float Margin = 10.0f; // pixels
//        return Point.X >= X - Margin && Point.X <= X + W + Margin &&
//               Point.Y >= Y - Margin && Point.Y <= Y + H + Margin;
		if (ActualExtent.Min.X > Point.X) return false;
		if (ActualExtent.Min.Y > Point.Y) return false;
		if (ActualExtent.Max.X < Point.X) return false;
		if (ActualExtent.Max.Y < Point.Y) return false;
		return true;
    }

    void CalculateExtentsVisualElements() {
		for (IVisualElement* Element : VisualElements) {
			FBox2D r = Element->GetRelativeBounds();
			float x = X + r.Min.X;
			float y = Y + r.Min.Y;
			float x1 = X + r.Max.X;
			float y1 = Y + r.Max.Y;
			if (ActualExtent.Min.X > x) ActualExtent.Min.X = x;
			if (ActualExtent.Min.Y > y) ActualExtent.Min.Y = y;
			if (ActualExtent.Max.X < x1) ActualExtent.Max.X = x1;
			if (ActualExtent.Max.Y < y1) ActualExtent.Max.Y = y1;
		}
	}

	void RenderHighlights(RenderingContext& Context) const {
		Context.DrawRectangle(ActualExtent, FLinearColor::Gray, true);
		FBox2D r;
		r.Min.X = X - P;
		r.Max.X = X+W + P;
		r.Min.Y = Y - P;
		r.Max.Y = Y+H + P;
		Context.DrawRectangle(r, FLinearColor::Yellow, true);
	}

    friend class PWR_PowerSegment;
    friend class PWR_PowerPropagation;
    friend class VisualizationManager;
    friend class UPowerGridLoader;
    friend class ULlamaComponent;
};
