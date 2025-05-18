#include "PWR_PowerSegment.h"
#include "ICH_PowerJunction.h"

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

bool PWR_PowerSegment::IsPointNear(const FVector2D& Point) const
{
	if (!JunctionA || !JunctionB) return false;

	// Get segment endpoints
	FVector2D Start(JunctionA->X + JunctionA->W/2, JunctionA->Y + JunctionA->H/2);
	FVector2D End(JunctionB->X + JunctionB->W/2, JunctionB->Y + JunctionB->H/2);

	// Calculate distance from point to line segment
	const float Margin = 5.0f; // pixels
	FVector2D Segment = End - Start;
	float SegmentLength = Segment.Size();
	if (SegmentLength < 0.0001f) return false;

	FVector2D NormalizedSegment = Segment / SegmentLength;
	FVector2D ToPoint = Point - Start;
	float Projection = FVector2D::DotProduct(ToPoint, NormalizedSegment);
	
	// Clamp projection to segment
	Projection = FMath::Clamp(Projection, 0.0f, SegmentLength);
	
	// Calculate closest point on segment
	FVector2D ClosestPoint = Start + NormalizedSegment * Projection;
	
	// Check distance to closest point
	return (Point - ClosestPoint).Size() <= Margin;
}

