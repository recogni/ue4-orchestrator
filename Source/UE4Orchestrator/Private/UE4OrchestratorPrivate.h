/* -*- mode: c; tab-width: 4; indent-tabs-mode: nil; -*- */

#pragma once

// Logging stuff ...
DECLARE_LOG_CATEGORY_EXTERN(LogUE4Orc, Log, All);

#include "mongoose.h"

// UE4
#include "IPlatformFilePak.h"
#include "FileManagerGeneric.h"
#include "StreamingNetworkPlatformFile.h"
#include "Runtime/AssetRegistry/Public/AssetRegistryModule.h"
#include "Modules/ModuleInterface.h"

#if WITH_EDITOR
#  include "LevelEditor.h"
#  include "Editor.h"
#  include "Editor/LevelEditor/Public/ILevelViewport.h"
#  include "Editor/LevelEditor/Public/LevelEditorActions.h"
#  include "Editor/UnrealEd/Public/LevelEditorViewport.h"
#endif

#include "UE4OrchestratorPrivate.generated.h"

typedef struct mg_str               mg_str_t;
typedef struct http_message         http_message_t;
typedef FModuleManager              FManager;
typedef FActorComponentTickFunction FTickFn;

#if WITH_EDITOR
  typedef FLevelEditorModule        FLvlEditor;
#endif

#define T                   TEXT
#define LOG(fmt, ...)       UE_LOG(LogUE4Orc, Log, TEXT(fmt), __VA_ARGS__)

////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////

class FUE4OrchestratorPlugin : public IModuleInterface
{
    virtual void    StartupModule()     override;
    virtual void    ShutdownModule()    override;
public:
    virtual bool    LoadObject(FString &Path);
    virtual bool    UnLoadObject(FString &Path);
};

UCLASS()
class UE4ORCHESTRATOR_API URCHTTP : public UObject, public FTickableGameObject
{
    GENERATED_UCLASS_BODY()
    virtual ~URCHTTP();

  public:

    void Init();
    static URCHTTP& Get();

    /*
     *  FTickableObject interface.
     */
    virtual void    Tick(float dt)                  override;
    virtual TStatId GetStatId()             const   override;
    virtual bool    IsTickable()            const { return true; }
    virtual bool    IsTickableWhenPaused()  const { return true; }
    virtual bool    IsTickableInEditor()    const { return true; }

    /*
     *  UObject interface.
     */
    virtual void Serialize(FArchive& ar) override;
    virtual void PostLoad() override;
    virtual void PostInitProperties() override;
#if WITH_EDITOR
    virtual void PostEditChangeProperty(FPropertyChangedEvent& evt) override;
#endif

    void SetPollInterval(int v);

  private:

    struct mg_mgr         mgr;
    struct mg_connection* conn;

    /*
     *  The poll_interval dictates how often we wish to poll.  If
     *  this is set to 0 (default), it will invoke a poll of
     *  `poll_ms` (default=1ms) each time this plugin gets a tick.
     *  However, if this is set to N (N > 0), this will only call
     *  poll once every N ticks.
     */
    int poll_interval;
    int poll_ms;

    /*
     *  Pak file.
     */
    FPakPlatformFile PakFileMgr_o;

  public:

    UFUNCTION()
    int MountPakFile(const FString& PakPath, bool bLoadContent);

    UFUNCTION()
    int LoadObject(const FString& ObjectPath);

    UFUNCTION()
    int UnloadObject(const FString& ObjectPath);
};
