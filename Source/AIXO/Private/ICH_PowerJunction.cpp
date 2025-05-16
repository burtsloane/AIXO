#include "ICH_PowerJunction.h"
#include "PWR_PowerSegment.h"

void ICH_PowerJunction::RenderLabels(RenderingContext& Context)
{
//return;	// labels
	FVector2D Position;
	Position.X = X+W+1;
	Position.Y = Y-1;
	FString s = "";
	if (bIsPowerSource) s += "S ";
	if (IsShorted()) s += "Shorted";
	else if (IsOverenergized()) s += "OvrPwr";
	else if (IsUnderPowered()) s += "LowPwr";
	else s += "Normal";
//	Context.DrawText(Position, s, FLinearColor::Black);
	Position.X = X+10;
	Position.Y += 12;
	FString f = FString::Printf(TEXT("U%dN%d"), (int)(10*DefaultPowerUsage), (int)(10*DefaultNoiseLevel));
	if (f != "U0N0") if (!bIsPowerSource)
		Context.DrawTinyText(Position, f, FLinearColor::Black);
//	s = ">";
//	for (int i=0; i<PathToSourceSegments.Num(); i++) {
//		if (i > 0) s += ">";
//		s += PathToSourceJunction[i]->GetSystemName();
//		s += ">";
//		s += PathToSourceSegments[i]->GetName();
//	}
//	Position.Y += 12;
//	Context.DrawText(Position, s, FLinearColor::Black);
}

