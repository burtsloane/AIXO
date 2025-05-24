#include "SubmarineState.h"
#include "PWR_PowerSegment.h"
#include "PWR_BusTapJunction.h"

ASubmarineState::ASubmarineState() {
}
void ASubmarineState::BeginPlay() {
	Super::BeginPlay();
}
void ASubmarineState::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const {
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
}

// Implementation of physics functions
void ASubmarineState::UpdateBuoyancy()
{
    if (!GetRootComponent() || !GetRootComponent()->IsSimulatingPhysics())
        return;

    float ForwardBuoyancy = ForwardMBTLevel * 20.0f + ForwardTBTLevel * 12.5f;
    float RearBuoyancy = RearMBTLevel * 20.0f + RearTBTLevel * 12.5f;
    float TotalBuoyancy = ForwardBuoyancy + RearBuoyancy;

    FVector BuoyancyForce = FVector(0.0f, 0.0f, (2.0f - TotalBuoyancy) * 500.0f);
	if (UPrimitiveComponent* Primitive = Cast<UPrimitiveComponent>(GetRootComponent()))
	{
		Primitive->AddForce(BuoyancyForce * Mass, NAME_None, true);
	}

    float BuoyancyTorque = (RearBuoyancy - ForwardBuoyancy) * 10.0f;
	if (UPrimitiveComponent* Primitive = Cast<UPrimitiveComponent>(GetRootComponent()))
	{
		Primitive->AddTorqueInRadians(
			FVector(BuoyancyTorque, 0.0f, 0.0f) * MomentOfInertia,
			NAME_None,
			true // bAccelChange: applies as angular acceleration, not world moment
		);
	}
}

void ASubmarineState::ApplyPhysics()
{
    if (!GetRootComponent() || !GetRootComponent()->IsSimulatingPhysics())
        return;

    float PitchTorque = ElevatorAngle * 24.0f + (LeftBowPlanesAngle+RightBowPlanesAngle) * 10.0f;
    float YawTorque = RudderAngle * 24.0f;

	if (UPrimitiveComponent* Primitive = Cast<UPrimitiveComponent>(GetRootComponent()))
	{
		Primitive->AddTorqueInRadians(
			FVector(PitchTorque, YawTorque, 0.0f) * MomentOfInertia,
			NAME_None,
			true // bAccelChange: applies as angular acceleration, not world moment
		);
	}
}

void ASubmarineState::UpdateRendering()
{
    if (bSonarPingActive)
    {
        // Trigger sonar ping VFX
    }

    if (bEngineRunning)
    {
        // Trigger engine wake effects
    }
}

/*
void ASubmarineState::PrintPowerGraph(PWR_PowerSegment* root, int depth, TSet<const void*>* visited)
{
    if (!root) return;

    if (!visited) visited = new TSet<const void*>();
    if (visited->Contains(root)) return;
    visited->Add(root);

    FString prefix = FString::ChrN(depth * 2, ' ');
    LogMessage(prefix + TEXT("Segment: ") + root->GetName());

    for (auto* tap : root->ConnectedBusTaps)
    {
        LogMessage(prefix + TEXT("  → Tap: ") + tap->GetName());
        for (int i = 0; i < 3; ++i)
        {
            if (PWR_PowerSegment* seg = tap->GetConnectedSegment(i))
            {
                if (seg != root)
                    PrintPowerGraph(seg, depth + 1, visited);
            }
        }
    }
}
*/

void ASubmarineState::Tick(float DeltaTime)
{
    Super::Tick(DeltaTime);

    for (auto& Handler : Systems)
    {
        Handler->Tick(DeltaTime);
    }
}

FString ASubmarineState::GetStateAsText() const
{
//    if (SubState)
//    {
////        return SubState->ToDisplayString();
//    }
    return "<no state>";
}

void ASubmarineState::LogMessage(const FString& Message)
{
    LogBuffer.Insert(Message, 0);
    if (LogBuffer.Num() > 100)
        LogBuffer.SetNum(100);
}
