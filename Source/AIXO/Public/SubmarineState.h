#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "CommandDistributor.h"
#include "ICommandHandler.h"
#include "Engine/EngineTypes.h"
#include "SubmarineState.generated.h"

UCLASS(Blueprintable)
class ASubmarineState : public AActor
{
    GENERATED_BODY()

public:
    ASubmarineState();

    UFUNCTION(BlueprintCallable)
    FString GetStateAsText() const;

    UFUNCTION(BlueprintCallable)
    void LogMessage(const FString& Message);

protected:
    virtual void BeginPlay() override;

public:
    virtual void Tick(float DeltaTime) override;

    /** Network Replication */
public:
    virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

    /** Position & Orientation */
public:
    UPROPERTY(Replicated, VisibleAnywhere, BlueprintReadOnly, Category = "Submarine|State")
    FVector SubmarineLocation;

    UPROPERTY(Replicated, VisibleAnywhere, BlueprintReadOnly, Category = "Submarine|State")
    FRotator SubmarineRotation;

    UPROPERTY(Replicated, VisibleAnywhere, BlueprintReadOnly, Category = "Submarine|State")
    FVector Velocity;

    UPROPERTY(Replicated, VisibleAnywhere, BlueprintReadOnly, Category = "Submarine|State")
    float H2Level = 1.0f; // 0.0 (empty) to 1.0 (full of H2 @3000PSI)

    UPROPERTY(Replicated, VisibleAnywhere, BlueprintReadOnly, Category = "Submarine|State")
    float LOXLevel = 1.0f; // 0.0 (empty) to 1.0 (full of LOX @3000PSI)

    UPROPERTY(Replicated, VisibleAnywhere, BlueprintReadOnly, Category = "Submarine|State")
    float Flask1Level = 1.0f; // 0.0 (empty) to 1.0 (full of air @3000PSI)

    UPROPERTY(Replicated, VisibleAnywhere, BlueprintReadOnly, Category = "Submarine|State")
    float Flask2Level = 1.0f; // 0.0 (empty) to 1.0 (full of air @3000PSI)

    UPROPERTY(Replicated, VisibleAnywhere, BlueprintReadOnly, Category = "Submarine|State")
    float Battery1Level = 1.0f; // 0.0 (empty) to 1.0 (full)

    UPROPERTY(Replicated, VisibleAnywhere, BlueprintReadOnly, Category = "Submarine|State")
    float Battery2Level = 1.0f; // 0.0 (empty) to 1.0 (full)

    UPROPERTY(Replicated, VisibleAnywhere, BlueprintReadOnly, Category = "Submarine|State")
    FString AlertLevel;

    /** Control Surfaces */
public:
    UPROPERTY(Replicated, VisibleAnywhere, BlueprintReadOnly, Category = "Submarine|Controls")
    float RudderAngle;

    UPROPERTY(Replicated, VisibleAnywhere, BlueprintReadOnly, Category = "Submarine|Controls")
    float ElevatorAngle;

    UPROPERTY(Replicated, VisibleAnywhere, BlueprintReadOnly, Category = "Submarine|Controls")
    float RightBowPlanesAngle;

    UPROPERTY(Replicated, VisibleAnywhere, BlueprintReadOnly, Category = "Submarine|Controls")
    float LeftBowPlanesAngle;

    /** Main Ballast Tanks (MBTs) - External, Open to Sea Pressure */
public:
    UPROPERTY(Replicated, VisibleAnywhere, BlueprintReadOnly, Category = "Submarine|Buoyancy")
    float ForwardMBTLevel; // 0.0 (empty) to 1.0 (full of water)

    UPROPERTY(Replicated, VisibleAnywhere, BlueprintReadOnly, Category = "Submarine|Buoyancy")
    float RearMBTLevel; // 0.0 (empty) to 1.0 (full of water)

    /** Trim Ballast Tanks (TBTs) - Internal, Controlled by Pumps */
public:
    UPROPERTY(Replicated, VisibleAnywhere, BlueprintReadOnly, Category = "Submarine|Buoyancy")
    float ForwardTBTLevel = 0.5f; // 0.0 (empty) to 1.0 (full)

    UPROPERTY(Replicated, VisibleAnywhere, BlueprintReadOnly, Category = "Submarine|Buoyancy")
    float RearTBTLevel = 0.5f; // 0.0 (empty) to 1.0 (full)

    /** Airlock */
public:
    UPROPERTY(Replicated, VisibleAnywhere, BlueprintReadOnly, Category = "Submarine|Airlock")
    bool Occupied;

    UPROPERTY(Replicated, VisibleAnywhere, BlueprintReadOnly, Category = "Submarine|Airlock")
    float WaterLevel;

    UPROPERTY(Replicated, VisibleAnywhere, BlueprintReadOnly, Category = "Submarine|Airlock")
    float Pressure;

    UPROPERTY(Replicated, VisibleAnywhere, BlueprintReadOnly, Category = "Submarine|Airlock")
    float OuterHatchOpen;

    UPROPERTY(Replicated, VisibleAnywhere, BlueprintReadOnly, Category = "Submarine|Airlock")
    float InnerDoorOpen;

    /** Structural Integrity & Damage */
public:
    UPROPERTY(Replicated, VisibleAnywhere, BlueprintReadOnly, Category = "Submarine|Damage")
    float HullIntegrity; // 1.0 = fully intact, 0.0 = destroyed

    UPROPERTY(Replicated, VisibleAnywhere, BlueprintReadOnly, Category = "Submarine|Damage")
    bool bIsFlooding;

    UPROPERTY(Replicated, VisibleAnywhere, BlueprintReadOnly, Category = "Submarine|Damage")
    float FloodingRate; // How quickly water is entering (if flooding)

    /** External Effects */
public:
    UPROPERTY(Replicated, VisibleAnywhere, BlueprintReadOnly, Category = "Submarine|Sensors")
    bool bSonarPingActive;

    UPROPERTY(Replicated, VisibleAnywhere, BlueprintReadOnly, Category = "Submarine|Sensors")
    bool bEngineRunning;

    /** Physics Properties */
public:
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Submarine|Physics")
    float Mass = 10000.0f; // Arbitrary mass in kg

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Submarine|Physics")
    FVector MomentOfInertia = FVector(50000.0f, 100000.0f, 150000.0f); // Approximated values for roll, pitch, yaw

    /** Functionality */
public:
    void UpdateBuoyancy();
    void ApplyPhysics();
    void UpdateRendering();
    
public:
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    TArray<FString> LogBuffer;

//    CommandDistributor* CmdDistributor;

    TArray<ICommandHandler*> Systems;

    void InitializeCommandHandlers();
};


