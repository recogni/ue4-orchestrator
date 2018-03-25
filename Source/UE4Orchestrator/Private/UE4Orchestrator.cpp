/*
 *  UE4Orchestrator.h acts as the PCH for this project and must be the
 *  very first file imported.
 */
#include "UE4Orchestrator.h"
#include <vector>
#include <string>

// UE4
#include "LevelEditor.h"
#include "IPlatformFilePak.h"
#include "FileManagerGeneric.h"
#include "StreamingNetworkPlatformFile.h"
#include "Runtime/AssetRegistry/Public/AssetRegistryModule.h"

#if WITH_EDITOR
#  include "Editor.h"
#  include "Editor/LevelEditor/Public/ILevelViewport.h"
#  include "Editor/LevelEditor/Public/LevelEditorActions.h"
#  include "Editor/UnrealEd/Public/LevelEditorViewport.h"
#endif

// HTTP server
#include "mongoose.h"

////////////////////////////////////////////////////////////////////////////////

typedef struct mg_str       mg_str_t;
typedef struct http_message http_message_t;
typedef FLevelEditorModule  FLvlEditor;
typedef FModuleManager      FManager;

#define T                   TEXT
#define LOG(fmt, ...)       UE_LOG(LogUE4Orc, Log, TEXT(fmt), __VA_ARGS__)

////////////////////////////////////////////////////////////////////////////////

static void
debugFn()
{
    return;
}

////////////////////////////////////////////////////////////////////////////////

/*
 *  mkdir
 *
 *  Creates a directory and returns true if it was successful (or if it already
 *  exists).
 */
static FORCEINLINE bool
mkdir(FString path)
{
    IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();
    if (PlatformFile.DirectoryExists(*path))
        return true;

    FPaths::NormalizeDirectoryName(path);
    path += "/";

    FString base;
    FString head;
    FString tail;

    path.Split(TEXT("/"), &base, &tail);
    base += "/";

    int32 ct = 0;
    while(tail != "" && ct++ < 32)
    {
        tail.Split(TEXT("/"), &head, &tail);
        base += head + FString("/");

        if (PlatformFile.DirectoryExists(*base))
            continue;

        PlatformFile.CreateDirectory(*base);
    }

    return PlatformFile.DirectoryExists(*path);
}

////////////////////////////////////////////////////////////////////////////////

static int
mountPakFile(FString& pakPath, FString& mountPath)
{
    int ret = 0;

    IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();
    FPakPlatformFile* PakPlatformFile = new FPakPlatformFile();
    PakPlatformFile->Initialize(&PlatformFile, T(""));
    FPlatformFileManager::Get().SetPlatformFile(*PakPlatformFile);

    const FString PakFilename(pakPath);
    FPakFile PakFile(&PlatformFile, *PakFilename, false);

    FString MountPoint = FPaths::ProjectDir() + mountPath;
    FPaths::MakeStandardFilename(MountPoint);
    PakFile.SetMountPoint(*MountPoint);

    if (!mkdir(*MountPoint))
    {
        LOG("Could not create mount dir %s", *MountPoint);
        ret = -1; goto exit;
    }

    if (PakPlatformFile->Mount(*PakFilename, 0, *MountPoint))
    {
        LOG("Mount %s success", *MountPoint);
        FStreamableManager StreamableManager;

        TArray<FString> FileList;
        PakFile.FindFilesAtPath(
            FileList,
            *PakFile.GetMountPoint(),
            true,
            false,
            true);

        for (auto assetPath : FileList)
        {
            FString sn, x, noop, subpath, base, ap, bp;

            FPackageName::GetShortName(*assetPath).Split(T("."), &sn, &noop);
            assetPath.Split(*mountPath, &base, &subpath);

            // Calculate the asset game directory.
            ap = mountPath;
            ap /= subpath;
            ap.Split(T("/"), &x, &noop,
                ESearchCase::CaseSensitive,
                ESearchDir::FromEnd);
            ap = x.Replace(UTF8_TO_TCHAR("Content"), UTF8_TO_TCHAR("Game"));
            ap /= FString::Printf(T("%s.%s"), *sn, *sn);

            // Create the directory before importing the asset.
            bp = FPaths::ProjectDir() + x;
            FPaths::MakeStandardFilename(bp);
            if (!mkdir(*bp))
            {
                LOG("Could not create dir %s", *bp);
                ret = -1; goto exit;
            }

            FStringAssetReference ref = ap;
            UObject* lo = StreamableManager.LoadSynchronous(ref, true);
            if (lo == nullptr)
            {
                LOG("%s load failed!", *ap);
                ret = -1; goto exit;
            }

            LOG("%s load success!", *ap);
        }
    }
    else
    {
        LOG("%s", "mount failed!");
        ret = -1; goto exit;
    }

  exit:
    FPlatformFileManager::Get().SetPlatformFile(PlatformFile);
    return ret;
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


static void
ev_handler(struct mg_connection* conn, int ev, void *ev_data)
{
    if (ev != MG_EV_HTTP_REQUEST)
        return;

    http_message_t* msg       = (http_message_t *)ev_data;
    mg_str_t        rspMsg    = STATUS_ERROR;
    int             rspStatus = 404;

#if WITH_EDITOR
    auto ar = "AssetRegistry";
    FAssetRegistryModule& AssetRegistry =
        FModuleManager::LoadModuleChecked<FAssetRegistryModule>(ar);

    FLvlEditor &Editor =
        FManager::LoadModuleChecked<FLvlEditor>("LevelEditor");

    /*
     *  HTTP GET commands
     */
    if (mg_vcmp(&msg->method, "GET") == 0)
    {
        /*
         *  HTTP GET /play
         *
         *  Trigger a play in the current level.
         */
        if (matches_any(&msg->uri, "/play", "/ue4/play"))
        {
            GEditor->PlayMap(NULL, NULL, -1, -1, false);
            goto OK;
        }

        /*
         *  HTTP GET /stop
         *
         *  Trigger a stop in the current level.
         */
        else if (matches_any(&msg->uri, "/stop", "/ue4/stop"))
        {
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
                LOG("%s", *(data.PackageName.ToString()));
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
            debugFn();
            goto OK;
        }

        goto BAD_ACTION;
    }

    /*
     *  HTTP POST commands
     */
    else if (mg_vcmp(&msg->method, "POST") == 0)
    {
        /*
         *  HTTP POST /command
         *
         *  POST body should contain the exact console command that is to be
         *  run in the UE4 console.
         */
        if (matches_any(&msg->uri, "/command", "/ue4/command"))
        {
            if (msg->body.len > 0)
            {
                auto ew = GEditor->GetEditorWorldContext().World();
                FString cmd = FString::Printf(T("%.*s"), msg->body.len,
                                              UTF8_TO_TCHAR(msg->body.p));
                GEditor->Exec(ew, *cmd, *GLog);
                goto OK;
            }
            else goto BAD_ENTITY;
        }

        /*
         *  HTTP POST /ue4/loadpak
         *
         *  POST body should contain a comma separated list of the following two
         *  arguments:
         *  1. Local .pak file path to mount into the engine.
         *  2. The mount point to load it at.
         */
        else if (matches_any(&msg->uri, "/loadpak", "/ue4/loadpak"))
        {
            if (msg->body.len > 0)
            {
                FString payload, pakPath, mountPath;
                payload = FString::Printf(T("%.*s"), msg->body.len,
                                          UTF8_TO_TCHAR(msg->body.p));
                payload.TrimEndInline();
                payload.Split(T(","), &pakPath, &mountPath);

                if (mountPakFile(pakPath, mountPath) < 0)
                    goto ERROR;
                goto OK;
            }
            else goto BAD_ENTITY;
        }

        goto BAD_ACTION;
    }
#endif // WITH_EDITOR

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

  done:
    mg_send_head(conn, rspStatus, rspMsg.len, "Content-Type: text/plain");
    mg_printf(conn, "%s", rspMsg.p);
}

////////////////////////////////////////////////////////////////////////////////

URCHTTP&
URCHTTP::Get()
{
    static URCHTTP Singleton;
    return Singleton;
}

////////////////////////////////////////////////////////////////////////////////

URCHTTP::URCHTTP()
{
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
URCHTTP::Tick(float dt)
{
    mg_mgr_poll(&mgr, 10);
}

TStatId
URCHTTP::GetStatId() const
{
    RETURN_QUICK_DECLARE_CYCLE_STAT(URCHTTP, STATGROUP_Tickables);
}

////////////////////////////////////////////////////////////////////////////////
