/* -*- mode: c; tab-width: 4; indent-tabs-mode: nil; -*- */

#include "CoreMinimal.h"
#include "ModuleManager.h"
#include "ModuleInterface.h"
#include "IPlatformFilePak.h"

#include "UE4Orchestrator.h"
#include "UE4OrchestratorPrivate.h"

DEFINE_LOG_CATEGORY(LogUE4Orc);


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

bool
FUE4OrchestratorPlugin::LoadObject(FString &Path)
{
    return (URCHTTP::Get().loadObject(Path) == 0);
}

bool
FUE4OrchestratorPlugin::UnLoadObject(FString &Path)
{
    return (URCHTTP::Get().unloadObject(Path) == 0);
}
