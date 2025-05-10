#include "PWR_PowerSegment.h"

bool PWR_PowerSegment::HandleTouchEvent(const TouchEvent& Event, CommandDistributor* Distributor)
{
	if (!JunctionA) return false;
	if (!JunctionB) return false;
	if (Event.Type != TouchEvent::EType::Up) return false;
	FVector2D p1 = JunctionA->GetPortConnection(PortA);
	FVector2D p2 = JunctionB->GetPortConnection(PortB);
	FVector2D oot;
	oot.X = (p1.X + p2.X) / 2;
	oot.Y = (p1.Y + p2.Y) / 2;
	float d = (oot.X-Event.Position.X)*(oot.X-Event.Position.X) + (oot.Y-Event.Position.Y)*(oot.Y-Event.Position.Y);
	if (d < 10*10) {
		switch (Status) {
			case EPowerSegmentStatus::NORMAL:
				SetStatus(EPowerSegmentStatus::SHORTED);
				return true;
				break;
			case EPowerSegmentStatus::SHORTED:
				SetStatus(EPowerSegmentStatus::OPENED);
				return true;
				break;
			case EPowerSegmentStatus::OPENED:
				SetStatus(EPowerSegmentStatus::NORMAL);
				return true;
				break;
		}
	}
	return false;
}

