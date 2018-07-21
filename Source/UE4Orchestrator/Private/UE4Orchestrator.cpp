/* -*- mode: c; tab-width: 4; indent-tabs-mode: nil; -*- */

/*
 *  UE4Orchestrator.h acts as the PCH for this project and must be the
 *  very first file imported.
 */
#include "UE4Orchestrator.h"

#include <vector>
#include <string>

// UE4
#include "CoreMinimal.h"
#include "IPlatformFilePak.h"
#include "Runtime/Core/Public/HAL/FileManagerGeneric.h"
#include "StreamingNetworkPlatformFile.h"
#include "Runtime/AssetRegistry/Public/AssetRegistryModule.h"
#include "Runtime/Core/Public/Misc/WildcardString.h"
#include "Runtime/Engine/Classes/Engine/StreamableManager.h"
#include "Runtime/Engine/Classes/Engine/AssetManager.h"
#include "Runtime/Engine/Public/ShaderCompiler.h"
#include "Runtime/Engine/Public/UnrealEngine.h"

#if WITH_EDITOR
#  include "LevelEditor.h"
#  include "Editor.h"
#  include "Editor/LevelEditor/Public/ILevelViewport.h"
#  include "Editor/LevelEditor/Public/LevelEditorActions.h"
#  include "Editor/UnrealEd/Public/LevelEditorViewport.h"
#endif

#include "UE4OrchestratorPrivate.h"

// HTTP server
#include "mongoose.h"

////////////////////////////////////////////////////////////////////////////////

DEFINE_LOG_CATEGORY(LogUE4Orc);

////////////////////////////////////////////////////////////////////////////////

static void
debugFn(FString payload)
{
    return;
}

////////////////////////////////////////////////////////////////////////////////

int
URCHTTP::MountPakFile(const FString& pakPath, bool bLoadContent)
{
    int ret = 0;
    IPlatformFile *originalPlatform = &FPlatformFileManager::Get().GetPlatformFile();

    // Check to see if the file exists first
    if (!originalPlatform->FileExists(*pakPath))
    {
        LOG("PakFile %s does not exist", *pakPath);
        return -1;
    }

    // The pak reader is now the current platform file
    FPlatformFileManager::Get().SetPlatformFile(*PakFileMgr);

    // Get the mount point from the Pak meta-data
    FPakFile PakFile(PakFileMgr, *pakPath, false);
    FString MountPoint = PakFile.GetMountPoint();

    // Determine where the on-disk path is for the mountpoint and register it
    FString PathOnDisk = FPaths::ProjectDir() / MountPoint;
    FPackageName::RegisterMountPoint(MountPoint, PathOnDisk);

    FString MountPointFull = PathOnDisk;
    FPaths::MakeStandardFilename(MountPointFull);

    LOG("Mounting at %s and registering mount point %s at %s", *MountPointFull, *MountPoint, *PathOnDisk);
    if (PakFileMgr->Mount(*pakPath, 0, *MountPointFull))
    {
        if (UAssetManager* Manager = UAssetManager::GetIfValid())
        {
            Manager->GetAssetRegistry().SearchAllAssets(true);
            if (bLoadContent)
            {
                TArray<FString> FileList;
                PakFile.FindFilesAtPath(FileList, *PakFile.GetMountPoint(), true, false, true);

                // Iterate over the collected files from the pak
                for (auto asset : FileList)
                {
                    FString Package, BaseName, Extension;
                    FPaths::Split(asset, Package, BaseName, Extension);
                    FString ModifiedAssetName = Package / BaseName + "." + BaseName;

                    LOG("Trying to load %s as %s ", *asset, *ModifiedAssetName);
                    Manager->GetStreamableManager().LoadSynchronous(ModifiedAssetName, true, nullptr);
                }
            }
        }
        else
        {
            LOG("Asset manager not valid!", NULL);
            ret = -1; goto exit;
        }
    }
    else
    {
        LOG("mount failed!", NULL);
        ret = -1; goto exit;
    }

  exit:
    // Restore the platform file
    FPlatformFileManager::Get().SetPlatformFile(*originalPlatform);

    return ret;
}

UObject*
URCHTTP::LoadObject(const FString& assetPath)
{
    UObject* ret = nullptr;
    IPlatformFile *originalPlatform = &FPlatformFileManager::Get().GetPlatformFile();

    if (PakFileMgr == nullptr)
    {
        LOG("Failed to create platform file %s", T("PakFile"));
        return ret;
    }

    // The pak reader is now the current platform file.
    FPlatformFileManager::Get().SetPlatformFile(*PakFileMgr);
    UAssetManager* Manager = UAssetManager::GetIfValid();

    ret = FindObject<UStaticMesh>(ANY_PACKAGE, *assetPath);
    if (Manager && ret == nullptr)
        ret = Manager->GetStreamableManager().LoadSynchronous(assetPath, false, nullptr);

    // Reset the platform file.
    FPlatformFileManager::Get().SetPlatformFile(*originalPlatform);

    return ret;
}

/*
 *  TODO: Deprecate this function.
 */
int
URCHTTP::UnloadObject(const FString& assetPath)
{
    GarbageCollect();
    return 0;
}

void
URCHTTP::GarbageCollect()
{
    CollectGarbage(RF_NoFlags, true);
}

void
URCHTTP::FinishAllShaderCompilation()
{
    if (GShaderCompilingManager)
        GShaderCompilingManager->FinishAllCompilation();
}

void
URCHTTP::GameRenderSync()
{
    static FFrameEndSync FrameEndSync;
    /* Do a full sync without allowing a frame of lag */
    FrameEndSync.Sync(false);
}

////////////////////////////////////////////////////////////////////////////////

// HTTP responses.
const mg_str_t STATUS_OK              = mg_mk_str("OK\r\n");
const mg_str_t STATUS_ERROR           = mg_mk_str("ERROR\r\n");
const mg_str_t STATUS_TRY_AGAIN       = mg_mk_str("TRY AGAIN\r\n");
const mg_str_t STATUS_NOT_SUPPORTED   = mg_mk_str("NOT SUPPORTED\r\n");
const mg_str_t STATUS_NOT_IMPLEMENTED = mg_mk_str("NOT IMPLEMENTED\r\n");
const mg_str_t STATUS_BAD_ACTION      = mg_mk_str("BAD ACTION\r\n");
const mg_str_t STATUS_BAD_ENTITY      = mg_mk_str("BAD ENTITY\r\n");

// HTTP query responses.
const mg_str_t STATUS_TRUE            = mg_mk_str("TRUE\r\n");
const mg_str_t STATUS_FALSE           = mg_mk_str("FALSE\r\n");

// Helper to match a list of URIs.
template<typename... Strings> bool
matches_any(mg_str_t* s, Strings... args)
{
    std::vector<std::string> items = {args...};
    for (int i = 0; i < items.size(); i++)
        if (mg_vcmp(s, items[i].c_str()) == 0)
            return true;
    return false;
}

////////////////////////////////////////////////////////////////////////////////

static void
ev_handler(struct mg_connection* conn, int ev, void *ev_data)
{
    if (ev != MG_EV_HTTP_REQUEST)
        return;

    http_message_t* msg       = (http_message_t *)ev_data;
    mg_str_t        rspMsg    = STATUS_ERROR;
    int             rspStatus = 404;
    URCHTTP*        server    = URCHTTP::Get();

#if WITH_EDITOR
    auto ar = "AssetRegistry";
    FAssetRegistryModule& AssetRegistry =
        FModuleManager::LoadModuleChecked<FAssetRegistryModule>(ar);

    /*
     *  HTTP GET commands
     */
    if (mg_vcmp(&msg->method, "GET") == 0)
    {
        /*
         *  HTTP GET /
         *
         *  Return "OK"
         */
        if (matches_any(&msg->uri, "/"))
        {
            goto OK;
        }

        /*
         *  HTTP GET /play
         *
         *  Trigger a play in the current level.
         */
        else if (matches_any(&msg->uri, "/play", "/ue4/play"))
        {
            GEditor->PlayMap(NULL, NULL, -1, -1, false);
            goto OK;
        }

        /*
         *  HTTP GET /play
         *
         *  Trigger a play in the current level.
         */
        else if (matches_any(&msg->uri, "/stop", "/ue4/stop"))
        {
            FLvlEditor &Editor =
                FManager::LoadModuleChecked<FLvlEditor>("LevelEditor");

            if (!Editor.GetFirstActiveViewport().IsValid())
            {
                LOG("%s", "ERROR no valid viewport");
                goto ERROR;
            }

            if (Editor.GetFirstActiveViewport()->HasPlayInEditorViewport())
            {
                FString cmd = "Exit";
                auto ew = GEditor->GetEditorWorldContext().World();
                GEditor->Exec(ew, *cmd, *GLog);
            }
            goto OK;
        }

        /*
         *  HTTP GET /shutdown-now
         *
         *  Trigger an editor immediate (possibly unclean) shutdown.
         */
        else if (matches_any(&msg->uri, "/shutdown-now", "/ue4/shutdown-now"))
        {
            FGenericPlatformMisc::RequestExit(true);
            goto OK;
        }

        /*
         *  HTTP GET /shutdown
         *
         *  Trigger an editor shutdown.
         */
        else if (matches_any(&msg->uri, "/shutdown", "/ue4/shutdown"))
        {
            FGenericPlatformMisc::RequestExit(false);
            goto OK;
        }

        /*
         *  HTTP GET /build
         *
         *  Trigger a build all for the current level.
         */
        else if (matches_any(&msg->uri, "/build", "/ue4/build"))
        {
            FLevelEditorActionCallbacks::Build_Execute();
            goto OK;
        }

        /*
         *  HTTP GET /is_building
         *
         *  Returns TRUE if the editor is currently building, FALSE otherwise.
         */
        else if (matches_any(&msg->uri, "/is_building", "/ue4/is_building"))
        {
            bool ok = FLevelEditorActionCallbacks::Build_CanExecute();
            goto NOT_IMPLEMENTED;
        }

        /*
         *  HTTP GET /list_assets
         *
         *  Logs all the assets that are registered with the asset manager.
         */
        else if (matches_any(&msg->uri, "/list_assets", "/ue4/list_assets"))
        {
            TArray<FAssetData> AssetData;
            AssetRegistry.Get().GetAllAssets(AssetData);
            for (auto data : AssetData)
            {
                FString path = *(data.PackageName.ToString());
                FPaths::MakePlatformFilename(path);
                LOG("%s %s", *(data.PackageName.ToString()), *path);
            }
            goto OK;
        }

        /*
         *  HTTP GET /assets_idle
         *
         *  Returns OK if the importer is idle.  Returns a status code to try
         *  again otherwise.
         */
        else if (matches_any(&msg->uri, "/assets_idle", "/ue4/assets_idle"))
        {
            if (AssetRegistry.Get().IsLoadingAssets())
                goto ONE_MO_TIME;
            goto OK;
        }

        /*
         *  HTTP GET /debug
         *
         *  Catch-all debug endpoint.
         */
        else if (matches_any(&msg->uri, "/debug", "/ue4/debug"))
        {
            debugFn("");
            goto OK;
        }

        else if (matches_any(&msg->uri, "/gc"))
        {
            URCHTTP::Get()->GarbageCollect();
            goto OK;
        }

        goto BAD_ACTION;
    }

    /*
     *  HTTP POST commands
     */
    else if (mg_vcmp(&msg->method, "POST") == 0)
    {
        FString body;
        if (msg->body.len > 0)
        {
            body = FString::Printf(T("%.*s"), msg->body.len,
                                   UTF8_TO_TCHAR(msg->body.p));
        }

        /*
         *  HTTP POST /poll_interval
         *
         *  POST body should contain an int which specifies the polling
         *  interval.  This value should be positive to delay the polling,
         *  and can be set to 0 (or negative) to assume default behavior of
         *  one poll() per tick().
         */
        if (matches_any(&msg->uri, "/poll_interval"))
        {
            int x = FCString::Atoi(*body);
            if (x < 0)
                x = 0;
            server->SetPollInterval(x);
            goto OK;
        }

        /*
         *  HTTP POST /command
         *
         *  POST body should contain the exact console command that is to be
         *  run in the UE4 console.
         */
        else if (matches_any(&msg->uri, "/command", "/ue4/command"))
        {
            if (body.Len() > 0)
            {
                auto ew = GEditor->GetEditorWorldContext().World();
                GEditor->Exec(ew, *body, *GLog);
                goto OK;
            }
            goto BAD_ENTITY;
        }

        /*
         *  HTTP POST /ue4/loadpak
         *
         *  POST body should contain a comma separated list of the following two
         *  arguments:
         *  1. Local .pak file path to mount into the engine.
         *  2. "all" or "none" to indicate if the pak's content should be loaded
         *
         */
        else if (matches_any(&msg->uri, "/loadpak", "/ue4/loadpak"))
        {
            if (body.Len() > 0)
            {
                int32 num_params;
                TArray<FString> pak_options;

                FString pakPath;

                body.TrimEndInline();

                num_params = body.ParseIntoArray(pak_options, T(","), true);
                pakPath = pak_options[0];

                if (pakPath.Len() == 0)
                    goto ERROR;

                if (num_params != 2)
                    goto ERROR;

                LOG("Mounting pak file: %s", *pakPath);

                if (URCHTTP::Get()->MountPakFile(pakPath, pak_options[1] == T("all")) < 0)
                    goto ERROR;

                goto OK;
            }
            goto BAD_ENTITY;
        }

        else if (matches_any(&msg->uri, "/loadobj", "/ue4/loadobj"))
        {
            if (body.Len() > 0)
            {
                body.TrimEndInline();

                TArray<FString> objects;
                int32 num_params = body.ParseIntoArray(objects, T(","), true);

                if (num_params==1)
                {
                    if (URCHTTP::Get()->LoadObject(objects[0]) != nullptr)
                        goto OK;
                    goto ERROR;
                }
            }
            goto BAD_ENTITY;
        }

        else if (matches_any(&msg->uri, "/unloadobj", "/ue4/unloadobj"))
        {
            if (body.Len() > 0)
            {
                body.TrimEndInline();

                TArray<FString> objects;
                int32 num_params = body.ParseIntoArray(objects, T(","), true);

                if (num_params == 1)
                {
                    if (URCHTTP::Get()->UnloadObject(objects[0]) < 0)
                        goto ERROR;
                    goto OK;
                }
            }
            goto BAD_ENTITY;
        }

        /*
         *  HTTP POST /debug
         *
         *  Catch-all debug endpoint.
         */
        else if (matches_any(&msg->uri, "/debug", "/ue4/debug"))
        {
            debugFn(body);
            goto OK;
        }
        goto BAD_ACTION;
    }
#endif // WITH_EDITOR

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-label"
  ERROR:
    rspMsg    = STATUS_ERROR;
    rspStatus = 501;
    goto done;

  BAD_ACTION:
    rspMsg    = STATUS_BAD_ACTION;
    rspStatus = 500;
    goto done;

  BAD_ENTITY:
    rspMsg    = STATUS_BAD_ENTITY;
    rspStatus = 422;
    goto done;

  ONE_MO_TIME:
    rspMsg    = STATUS_TRY_AGAIN;
    rspStatus = 416;
    goto done;

  NOT_IMPLEMENTED:
    rspMsg    = STATUS_NOT_IMPLEMENTED;
    rspStatus = 500;
    goto done;

  OK:
    rspMsg    = STATUS_OK;
    rspStatus = 200;
#pragma GCC diagnostic pop

  done:
    mg_send_head(conn, rspStatus, rspMsg.len, "Content-Type: text/plain");
    mg_printf(conn, "%s", rspMsg.p);
}

////////////////////////////////////////////////////////////////////////////////

URCHTTP*
URCHTTP::Get()
{
    UObject *URCHTTP_cdo = URCHTTP::StaticClass()->GetDefaultObject(true);
    return Cast<URCHTTP>(URCHTTP_cdo);
}


////////////////////////////////////////////////////////////////////////////////

URCHTTP::URCHTTP(const FObjectInitializer& oi)
    : Super(oi), poll_interval(0), poll_ms(1)
{
    // Initialize .pak file reader
    if (PakFileMgr == nullptr)
    {
        PakFileMgr = new FPakPlatformFile;
        PakFileMgr->Initialize(&FPlatformFileManager::Get().GetPlatformFile(), T(""));
        PakFileMgr->InitializeNewAsyncIO();
    }
}

URCHTTP::~URCHTTP()
{
    mg_mgr_free(&mgr);
}

////////////////////////////////////////////////////////////////////////////////

void
URCHTTP::Init()
{
    // Initialize HTTPD server
    mg_mgr_init(&mgr, NULL);
    conn = mg_bind(&mgr, "18820", ev_handler);
    mg_set_protocol_http_websocket(conn);
}

void
URCHTTP::SetPollInterval(int v)
{
    poll_interval = v;
}

void
URCHTTP::Tick(float dt)
{
    static int tick_counter = 0;
    if (tick_counter == 0)
        Init();

    if (poll_interval == 0 || (tick_counter++ % poll_interval) == 0)
        mg_mgr_poll(&mgr, poll_ms);

    if (tick_counter == 0)
        tick_counter++;
}

TStatId
URCHTTP::GetStatId() const
{
    RETURN_QUICK_DECLARE_CYCLE_STAT(URCHTTP, STATGROUP_Tickables);
}

void
URCHTTP::Serialize(FArchive& ar)
{
    Super::Serialize(ar);
}

void
URCHTTP::PostLoad()
{
    Super::PostLoad();
}

void
URCHTTP::PostInitProperties()
{
    Super::PostInitProperties();
}

#if WITH_EDITOR
void
URCHTTP::PostEditChangeProperty(FPropertyChangedEvent& evt)
{
    Super::PostEditChangeProperty(evt);
}
#endif

////////////////////////////////////////////////////////////////////////////////
