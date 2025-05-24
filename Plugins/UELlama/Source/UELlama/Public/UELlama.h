// Copyright (c) 2023 Mika Pi

#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"

class FUELlamaModule : public IModuleInterface
{
public:
    virtual void StartupModule() override;
    virtual void ShutdownModule() override;
};
