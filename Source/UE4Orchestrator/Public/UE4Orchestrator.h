#pragma once

#include "Core.h"
#include "Engine.h"
#include "Tickable.h"

// Logging stuff ...
DECLARE_LOG_CATEGORY_EXTERN(LogUE4Orc, Log, All);

#include "mongoose.h"

class UE4ORCHESTRATOR_API URCHTTP : public FTickableGameObject
{
  public:
    URCHTTP();
    ~URCHTTP();

    void Init();

    static URCHTTP& Get();

    // FTickableObject interface
    virtual void    Tick(float dt)                  override;
    virtual TStatId GetStatId()             const   override;
    virtual bool    IsTickable()            const { return true; }
    virtual bool    IsTickableWhenPaused()  const { return true; }
    virtual bool    IsTickableInEditor()    const { return true; }

  private:
    struct mg_mgr         mgr;
    struct mg_connection* conn;
};

