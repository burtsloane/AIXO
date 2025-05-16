#pragma once

#include "CoreMinimal.h"
#include "SubmarineState.h"
#include "ICommandHandler.h"

/**
 * PWR_PowerPropagation handles network-wide power distribution.
 * Step-by-step breakdown:
 * 1. Reset power levels and fault flags, set power level to 999999
 * 2a. Detect and shut down power sources that connect to shorted segments
 * 2b. Detect and shut down power sources that connect to another power source
 * 2c. Traverse and memoize segment paths to sources.
 * 3. Apply power load back to segments if valid path and not faulted.
 * 4. Reset unpowered segments power levels to 0 (still at 999999, not visited by above steps)
 * 5. Detect and shut down power sources that are overloaded by power sinks
 */
class PWR_PowerPropagation
{
private:
    static void RecursiveMarkShorted(ICH_PowerJunction* Junction, TSet<ICH_PowerJunction*>& Visited, PWR_PowerSegment* FromSegment = nullptr)
    {
//UE_LOG(LogTemp, Warning, TEXT("RecursiveMarkShorted at %s %hs"), *Junction->GetSystemName(), (!Junction || Visited.Contains(Junction))?"already visited":"");
        if (!Junction || Visited.Contains(Junction)) return;
        Visited.Add(Junction);
        Junction->SetShorted(true);
		Junction->SetShutdown(true);

        for (PWR_PowerSegment* Segment : Junction->GetConnectedSegments(FromSegment))
        {
            if (!Segment || Segment->GetStatus() == EPowerSegmentStatus::OPENED) continue;
            Segment->SetShorted(true);

            ICH_PowerJunction* Next = (Segment->GetJunctionA() == Junction) ? Segment->GetJunctionB() : Segment->GetJunctionA();
            int32 NextPort = (Segment->GetJunctionA() == Junction) ? Segment->GetPortB() : Segment->GetPortA();
            if (!Next->EnabledPorts[NextPort]) continue;
//UE_LOG(LogTemp, Warning, TEXT("      recurse %s -> %s"), *Segment->GetName(), *Next->GetSystemName());
            RecursiveMarkShorted(Next, Visited, Segment);
        }
    }

    static bool RecursiveDetectOtherPowerSource(ICH_PowerJunction* Junction, ICH_PowerJunction* Origin, TSet<ICH_PowerJunction*>& Visited, PWR_PowerSegment* FromSegment = nullptr)
    {
//FString s = FString::Printf(TEXT("RecursiveDetectOtherPowerSource at %s"), *Junction->GetSystemName());
//if (!Junction || Visited.Contains(Junction)) UE_LOG(LogTemp, Warning, TEXT("%s done"), *s);
        if (!Junction || Visited.Contains(Junction)) return false;
        Visited.Add(Junction);

        if (Junction != Origin && Junction->IsPowerSource() && !Junction->IsChargingPort(FromSegment)) return true;
//s+=" ->";
//for (PWR_PowerSegment* Segment : Junction->GetConnectedSegments(FromSegment)) s += " " + Segment->GetName();
//UE_LOG(LogTemp, Warning, TEXT("%s"), *s);

        for (PWR_PowerSegment* Segment : Junction->GetConnectedSegments(FromSegment))
        {
            if (!Segment || Segment->GetStatus() == EPowerSegmentStatus::OPENED) continue;
            ICH_PowerJunction* Next = (Segment->GetJunctionA() == Junction) ? Segment->GetJunctionB() : Segment->GetJunctionA();
            int32 NextPort = (Segment->GetJunctionA() == Junction) ? Segment->GetPortB() : Segment->GetPortA();
            if (!Next->EnabledPorts[NextPort]) continue;
            if (Next->IsChargingPort(NextPort)) continue;
//UE_LOG(LogTemp, Warning, TEXT("      recurse %s -> %s"), *Segment->GetName(), *Next->GetSystemName());
            if (RecursiveDetectOtherPowerSource(Next, Origin, Visited, Segment)) return true;
        }
        return false;
    }

    static void RecursiveMarkOverenergized(ICH_PowerJunction* Junction, TSet<ICH_PowerJunction*>& Visited, PWR_PowerSegment* FromSegment = nullptr)
    {
//UE_LOG(LogTemp, Warning, TEXT("RecursiveMarkOverenergized at %s %hs"), *Junction->GetSystemName(), (!Junction || Visited.Contains(Junction))?"already visited":"");
        if (!Junction || Visited.Contains(Junction)) return;
        Visited.Add(Junction);
		Junction->SetOverenergized(true);

        for (PWR_PowerSegment* Segment : Junction->GetConnectedSegments(FromSegment))
        {
            if (!Segment || Segment->GetStatus() == EPowerSegmentStatus::OPENED) continue;
            Segment->SetOverenergized(true);

            ICH_PowerJunction* Next = (Segment->GetJunctionA() == Junction) ? Segment->GetJunctionB() : Segment->GetJunctionA();
            int32 NextPort = (Segment->GetJunctionA() == Junction) ? Segment->GetPortB() : Segment->GetPortA();
            if (!Next->EnabledPorts[NextPort]) continue;
//UE_LOG(LogTemp, Warning, TEXT("      marked %s next %s"), *Segment->GetName(), *Next->GetSystemName());
            RecursiveMarkOverenergized(Next, Visited, Segment);
        }
    }

    static void RecursiveMemoizePaths(ICH_PowerJunction* Junction, TArray<ICH_PowerJunction*> JPath, TArray<PWR_PowerSegment*> Path, TSet<ICH_PowerJunction*>& Visited, PWR_PowerSegment* FromSegment = nullptr)
    {
//UE_LOG(LogTemp, Warning, TEXT("RecursiveMemoizePaths at %s %hs"), *Junction->GetSystemName(), (!Junction || Visited.Contains(Junction))?"already visited":"");
        if (!Junction || Visited.Contains(Junction)) return;
        TSet<ICH_PowerJunction*> NewVisited = Visited;		// gotta copy here to find all possible routes
        NewVisited.Add(Junction);
        JPath.Add(Junction);
//if (Junction->GetPathToSourceSegments().Num() == 0 || (Path.Num() > 0 && Path.Num() < Junction->GetPathToSourceSegments().Num()))
//UE_LOG(LogTemp, Warning, TEXT("   %s memoized path %d segments (was %d)"), *Junction->SystemName, Path.Num(), Junction->GetPathToSourceSegments().Num());
        if (Junction->GetPathToSourceSegments().Num() == 0 || (Path.Num() > 0 && Path.Num() < Junction->GetPathToSourceSegments().Num()))
			Junction->SetPathToSourceSegments(Path);
        if (Junction->GetPathToSourceJunction().Num() == 0 || (JPath.Num() > 0 && JPath.Num() < Junction->GetPathToSourceJunction().Num()))
			Junction->SetPathToSourceJunction(JPath);

//FString s = FString::Printf(TEXT("      %s has path: "), *Junction->GetSystemName());
//for (int i=0; i<Path.Num(); i++) {
//	if (i > 0) s += " -> ";
//	s += Path[i]->GetName();
//}
////for (int i=0; i<JPath.Num(); i++) {
////	if (i > 0) s += " -> ";
////	s += JPath[i]->GetSystemName();
////}
//UE_LOG(LogTemp, Warning, TEXT("%s"), *s);

        for (PWR_PowerSegment* Segment : Junction->GetConnectedSegments(FromSegment))
        {
            if (!Segment || Segment->GetStatus() != EPowerSegmentStatus::NORMAL) continue;

            ICH_PowerJunction* Next = (Segment->GetJunctionA() == Junction) ? Segment->GetJunctionB() : Segment->GetJunctionA();
            int32 NextPort = (Segment->GetJunctionA() == Junction) ? Segment->GetPortB() : Segment->GetPortA();
            if (!Next->EnabledPorts[NextPort]) continue;
            TArray<PWR_PowerSegment*> NewPath = Path;
            NewPath.Add(Segment);
            TArray<ICH_PowerJunction*> NewJPath = JPath;
//UE_LOG(LogTemp, Warning, TEXT("      recurse %s -> %s"), *Segment->GetName(), *Next->GetSystemName());
            RecursiveMemoizePaths(Next, NewJPath, NewPath, NewVisited, Segment);
        }
    }

    static void RecursiveMarkUnderpowered(ICH_PowerJunction* Junction, TSet<ICH_PowerJunction*>& Visited, PWR_PowerSegment* FromSegment = nullptr)
    {
//UE_LOG(LogTemp, Warning, TEXT("RecursiveMarkUnderpowered at %s%hs"), *Junction->GetSystemName(), (!Junction || Visited.Contains(Junction))?"already visited":"");
        if (!Junction || Visited.Contains(Junction)) return;
        Visited.Add(Junction);

        for (PWR_PowerSegment* Segment : Junction->GetConnectedSegments(FromSegment))
        {
            if (!Segment || Segment->GetStatus() == EPowerSegmentStatus::OPENED) continue;
            Segment->SetUnderPowered(true);

            ICH_PowerJunction* Next = (Segment->GetJunctionA() == Junction) ? Segment->GetJunctionB() : Segment->GetJunctionA();
            int32 NextPort = (Segment->GetJunctionA() == Junction) ? Segment->GetPortB() : Segment->GetPortA();
            if (!Next->EnabledPorts[NextPort]) continue;
//UE_LOG(LogTemp, Warning, TEXT("      at %s"), *Next->GetSystemName());
            RecursiveMarkUnderpowered(Next, Visited, Segment);
        }
    }

public:
    static void PropagatePower(TArray<PWR_PowerSegment*>& Segments, TArray<ICH_PowerJunction*>& Junctions)
    {
        // Step 1: Reset power levels and fault flags
        for (PWR_PowerSegment* Segment : Segments)
        {
            if (Segment)
            {
                Segment->SetPowerLevel(999999);
                Segment->SetShorted(false);
                Segment->SetOverenergized(false);
                Segment->SetUnderPowered(false);
            }
        }
        for (ICH_PowerJunction* Junction : Junctions)
        {
            if (Junction)
            {
                Junction->SetShutdown(false);
                Junction->SetShorted(false);
                Junction->SetOverenergized(false);
                Junction->SetUnderPowered(false);
				Junction->SetPathToSourceSegments({});
				Junction->SetPathToSourceJunction({});
            }
        }

        // Step 2a: Detect and shut down power sources that connect to shorted segments
        for (PWR_PowerSegment* Seg : Segments) {
        	if (Seg->GetStatus() == EPowerSegmentStatus::SHORTED) {
//UE_LOG(LogTemp, Warning, TEXT("Found Shorted Segment %s"), *Seg->GetName());
				TSet<ICH_PowerJunction*> MarkVisited;
        		ICH_PowerJunction* J;
        		J = Seg->GetJunctionA();
        		if (J->EnabledPorts[Seg->GetPortA()]) {
//UE_LOG(LogTemp, Warning, TEXT("      Short to %s"), *J->GetSystemName());
                    RecursiveMarkShorted(J, MarkVisited, Seg);
                    // TODO: no shutdown if battery charging pin is connected to a shorted segment, just no charging!
        		}
        		J = Seg->GetJunctionB();
        		if (J->EnabledPorts[Seg->GetPortB()]) {
//UE_LOG(LogTemp, Warning, TEXT("      short to %s"), *J->GetSystemName());
                    RecursiveMarkShorted(J, MarkVisited, Seg);
                    // TODO: no shutdown if battery charging pin is connected to a shorted segment, just no charging!
        		}
        	}
        }

        // Step 2b: Detect and shut down power sources that connect to another power source
        for (ICH_PowerJunction* Junction : Junctions)
        {
            if (Junction && Junction->IsPowerSource() && !Junction->IsShutdown())
            {
//UE_LOG(LogTemp, Warning, TEXT("Step 2b: Starting at %s checking for duplicated power"), *Junction->GetSystemName());
                TSet<ICH_PowerJunction*> Visited;
				// TODO: no shutdown if battery charging pin is connected to a different power source!
                if (RecursiveDetectOtherPowerSource(Junction, Junction, Visited))
                {
//UE_LOG(LogTemp, Warning, TEXT("      duplicated power found at %s"), *Junction->GetSystemName());
                    Junction->SetShutdown(true);
                    TSet<ICH_PowerJunction*> MarkVisited;
                    RecursiveMarkOverenergized(Junction, MarkVisited);
                }
            }
        }

        // Step 2c: Traverse and memoize segment paths to sources
        for (ICH_PowerJunction* Junction : Junctions)
        {
            if (Junction && Junction->IsPowerSource() && !Junction->IsShutdown() && (Junction->GetPowerAvailable()>0))
            {
                // TODO: don't exit a battery node through a charging pin
//UE_LOG(LogTemp, Warning, TEXT("Step 2c: Starting at %s memoize"), *Junction->GetSystemName());
                TSet<ICH_PowerJunction*> Visited;
                TArray<PWR_PowerSegment*> Path;
                TArray<ICH_PowerJunction*> JPath;
                RecursiveMemoizePaths(Junction, JPath, Path, Visited);
            }
        }

        // Step 3: Apply power load back to segments if valid path and not faulted
        for (ICH_PowerJunction* Junction : Junctions)
        {
//UE_LOG(LogTemp, Warning, TEXT("Step 3: Starting at %s add up power"), *Junction->GetSystemName());
			// TODO: battery node consumes current through a charging pin
            if (!Junction->IsPowerSource())
            {
            	// add Junction->GetPowerLevel() to all segments and junctions in the path to the source
				for (PWR_PowerSegment* PathSegment : Junction->GetPathToSourceSegments())
				{
					if (PathSegment->GetPowerLevel() != 999999)
					{
						PathSegment->SetPowerLevel(Junction->GetCurrentPowerUsage() + PathSegment->GetPowerLevel());
					}
					else
					{
						PathSegment->SetPowerLevel(Junction->GetCurrentPowerUsage());
					}
				}
            } else {
				PWR_PowerSegment* PathSegmentCharge = Junction->GetChargingSegment();
//if (PathSegment) UE_LOG(LogTemp, Warning, TEXT("Step 3: power usage for charging: %.2f"), Junction->GetCurrentPowerUsage());
				if (PathSegmentCharge) {
					for (PWR_PowerSegment* PathSegment : Junction->GetPathToSourceSegments())
					{
						if (PathSegment->GetPowerLevel() != 999999)
						{
							PathSegment->SetPowerLevel(Junction->GetCurrentPowerUsage() + PathSegment->GetPowerLevel());
						}
						else
						{
							PathSegment->SetPowerLevel(Junction->GetCurrentPowerUsage());
						}
					}
				}
            }
        }

        // Step 4: Reset unpowered segment power levels to 0
        for (PWR_PowerSegment* Segment : Segments)
        {
            if (Segment && Segment->GetPowerLevel() == 999999)
            {
                Segment->SetPowerLevel(0);
            }
        }

        // Step 5: Detect and shut down power sources that are overloaded by power sinks
        for (ICH_PowerJunction* Junction : Junctions)
        {
//UE_LOG(LogTemp, Warning, TEXT("Step 5: Starting at %s detect overloads"), *Junction->GetSystemName());
            if (Junction && Junction->IsPowerSource() && !Junction->IsShutdown())
            {
                float TotalPowerDraw = 0.0f;
                for (PWR_PowerSegment* Segment : Junction->GetConnectedSegments())
                {
                    if (Segment && Segment->GetStatus() == EPowerSegmentStatus::NORMAL)
                    {
                        TotalPowerDraw += Segment->GetPowerLevel();
                    }
                }

                // Check if total power draw exceeds source capacity
                if (TotalPowerDraw > 0.0f && TotalPowerDraw > Junction->GetPowerAvailable())
                {
//UE_LOG(LogTemp, Warning, TEXT("      at %s %.2f > %.2f"), *Junction->GetSystemName(), TotalPowerDraw, Junction->GetPowerAvailable());
                    Junction->SetShutdown(true);
                    TSet<ICH_PowerJunction*> MarkVisited;
	                // TODO: don't exit a battery node through a charging pin
                    RecursiveMarkUnderpowered(Junction, MarkVisited);
                }
            }
        }
    }
};


