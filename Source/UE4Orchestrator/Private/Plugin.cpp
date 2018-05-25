/* -*- mode: c; tab-width: 4; indent-tabs-mode: nil; -*- */

#include "UE4Orchestrator.h"

#include "CoreMinimal.h"
#include "ModuleManager.h"
#include "ModuleInterface.h"

DEFINE_LOG_CATEGORY(LogUE4Orc);

class FUE4OrchestratorPlugin : public IModuleInterface
{
    virtual void    StartupModule()     override;
    virtual void    ShutdownModule()    override;
};

IMPLEMENT_MODULE(FUE4OrchestratorPlugin, UE4Orchestrator);

void
FUE4OrchestratorPlugin::StartupModule()
{
    UE_LOG(LogUE4Orc, Log, TEXT("UE4Orchestrator::StartupModule"));

    URCHTTP &server = URCHTTP::Get();
    server.Init();
}

void
FUE4OrchestratorPlugin::ShutdownModule()
{
    UE_LOG(LogUE4Orc, Log, TEXT("UE4Orchestrator::ShutdownModule"));
}
