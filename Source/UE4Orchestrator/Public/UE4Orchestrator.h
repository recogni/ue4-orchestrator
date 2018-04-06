#pragma once

#include "Core.h"
#include "Engine.h"
#include "Tickable.h"

// Logging stuff ...
DECLARE_LOG_CATEGORY_EXTERN(LogUE4Orc, Log, All);

#include "types.h"
#include "mongoose.h"

class UE4ORCHESTRATOR_API URCHTTP : public FTickableGameObject
{
  public:
    URCHTTP();
    ~URCHTTP();

    static URCHTTP& Get();

    // FTickableObject interface ...
    virtual void    Tick(float dt)                  override;
    virtual TStatId GetStatId()             const   override;
    virtual bool    IsTickable()            const { return true; }
    virtual bool    IsTickableWhenPaused()  const { return true; }
    virtual bool    IsTickableInEditor()    const { return true; }

    // World / Player queries ...
    void            Init();
    void            WorldUpdated();
    APawn*          GetPawn();
    UWorld*         GetWorld();

  private:
    struct mg_mgr         mgr;
    struct mg_connection* conn;
    APawn*                pawn;
};

