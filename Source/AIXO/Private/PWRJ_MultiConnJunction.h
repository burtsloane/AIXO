#pragma once

#include "CoreMinimal.h"
#include "ICH_PowerJunction.h"

class PWRJ_MultiConnJunction : public ICH_PowerJunction
{
private:

public:
    PWRJ_MultiConnJunction(const FString& name, ASubmarineState* InSubState, float InX, float InY, float InW=150, float InH=24)
        : ICH_PowerJunction(name, InX, InY, InW, InH, false)  // not a power source
    {
    }
	virtual FString GetTypeString() const override { return TEXT("PWRJ_MultiConnJunction"); }
};
