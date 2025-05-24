#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "ILLMGameInterface.h"
#include "ICommandHandler.h"
#include "SubmarineGameManager.generated.h"

class AVisualTestHarnessActor;

UCLASS()
class AIXO_API ASubmarineGameManager : public AActor, public ILLMGameInterface
{
    GENERATED_BODY()

public:
    ASubmarineGameManager();

    // ILLMGameInterface implementation
    virtual FString GetSystemsContextBlock() const override;
    virtual FString GetStaticWorldInfoBlock() const override;
    virtual FString GetLowFrequencyStateBlock() const override;
    virtual FString HandleGetSystemInfo(const FString& QueryString) override;
    virtual FString HandleCommandSubmarineSystem(const FString& CommandString) override;
    virtual FString HandleQuerySubmarineSystem(const FString& QueryString) override;

    // uplink
    UPROPERTY()
    class AVisualTestHarnessActor* HarnessActor;

protected:
    virtual void BeginPlay() override;

private:
    // Helper functions
    FString MakeCommandHandlerString(ICommandHandler* Handler) const;
    ICommandHandler* FindHandlerByName(const FString& SystemName) const;
    FString FormatJsonResponse(const FString& ErrorMessage) const;
    FString FormatJsonResponse(const FString& Key, const FString& Value) const;
}; 
