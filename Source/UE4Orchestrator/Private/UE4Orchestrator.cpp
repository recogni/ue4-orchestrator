/*
 *  UE4Orchestrator.h acts as the PCH for this project and must be the
 *  very first file imported.
 */
#include "UE4Orchestrator.h"

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

// Random helper defines / misc

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
	FPlatformFileManager::Get().SetPlatformFile(PlatformFile);
        return -1;
    }

    if (PakPlatformFile->Mount(*PakFilename, 0, *MountPoint))
    {
        LOG("Mount %s success", *MountPoint);
        FStreamableManager StreamableManager;

        TArray<FString> FileList;
        PakFile.FindFilesAtPath(FileList, *PakFile.GetMountPoint(), true, false, true);
        for (auto assetPath : FileList)
        {
            FString sn, x, noop, subpath, base, ap, bp;

            FPackageName::GetShortName(*assetPath).Split(T("."), &sn, &noop);
            assetPath.Split(*mountPath, &base, &subpath);

            // Calculate the asset game directory.
            ap = mountPath;
            ap /= subpath;
            ap.Split(T("/"), &x, &noop, ESearchCase::CaseSensitive, ESearchDir::FromEnd);
            ap = x.Replace(UTF8_TO_TCHAR("Content"), UTF8_TO_TCHAR("Game"));
            ap /= FString::Printf(T("%s.%s"), *sn, *sn);

            // Create the directory before importing the asset.
            bp = FPaths::ProjectDir() + x;
            FPaths::MakeStandardFilename(bp);
            if (!mkdir(*bp))
            {
                LOG("Could not create dir %s", *bp);
		FPlatformFileManager::Get().SetPlatformFile(PlatformFile);		
                return -1;
            }

            FStringAssetReference ref = ap;
            UObject* lo = StreamableManager.LoadSynchronous(ref, true);
            if (lo == nullptr)
            {
                LOG("%s load failed!", *ap);
		FPlatformFileManager::Get().SetPlatformFile(PlatformFile);		
                return -1;
            }
            LOG("%s load success!", *ap);
        }
    }
    else
    {
        LOG("%s", "mount failed!");
	FPlatformFileManager::Get().SetPlatformFile(PlatformFile);	
        return -1;
    }


    FPlatformFileManager::Get().SetPlatformFile(PlatformFile);
    return 0;
}


////////////////////////////////////////////////////////////////////////////////

// HTTP method types.
const mg_str_t HTTP_GET             = mg_mk_str("GET");
const mg_str_t HTTP_POST            = mg_mk_str("POST");

// HTTP responses.
const mg_str_t STATUS_OK            = mg_mk_str("OK\r\n");
const mg_str_t STATUS_ERROR         = mg_mk_str("ERROR\r\n");
const mg_str_t STATUS_NOT_SUPPORTED = mg_mk_str("NOT SUPPORTED\r\n");
const mg_str_t STATUS_BAD_ACTION    = mg_mk_str("BAD ACTION\r\n");
const mg_str_t STATUS_BAD_ENTITY    = mg_mk_str("BAD ENTITY\r\n");

// HTTP query responses.
const mg_str_t STATUS_TRUE          = mg_mk_str("TRUE\r\n");
const mg_str_t STATUS_FALSE         = mg_mk_str("FALSE\r\n");

// GET APIs
const mg_str_t UE4_PLAY             = mg_mk_str("/ue4/play");
const mg_str_t UE4_STOP             = mg_mk_str("/ue4/stop");
const mg_str_t UE4_BUILD            = mg_mk_str("/ue4/build");
const mg_str_t UE4_IS_BUILDING      = mg_mk_str("/ue4/is_building");
const mg_str_t UE4_SHUTDOWN         = mg_mk_str("/ue4/shutdown");
const mg_str_t UE4_LIST_ASSETS      = mg_mk_str("/ue4/listassets");

// POST APIs
const mg_str_t UE4_COMMAND          = mg_mk_str("/ue4/command");
const mg_str_t UE4_LOAD_PAKFILE     = mg_mk_str("/ue4/loadpak");

// DEBUG APIs
const mg_str_t UE4_DEBUG            = mg_mk_str("/ue4/debug");

////////////////////////////////////////////////////////////////////////////////

static void
ev_handler(struct mg_connection* conn, int ev, void *ev_data)
{
    if (ev != MG_EV_HTTP_REQUEST)
        return;

    http_message_t* msg       = (http_message_t *)ev_data;
    mg_str_t        rspMsg    = STATUS_ERROR;
    int             rspStatus = 404;

#if WITH_EDITOR
    FLvlEditor &Editor =
        FManager::LoadModuleChecked<FLvlEditor>("LevelEditor");

    if (mg_strcmp(msg->method, HTTP_GET) == 0)
    {
        if (mg_strcmp(msg->uri, UE4_PLAY) == 0)
        {
            GEditor->PlayMap(NULL, NULL, -1, -1, false);
            goto OK;
        }
        else if (mg_strcmp(msg->uri, UE4_STOP) == 0)
        {
            if (!Editor.GetFirstActiveViewport().IsValid())
            {
                LOG("%s", "ERROR no valid viewport");
                goto ERROR;
            }

            if (Editor.GetFirstActiveViewport()->HasPlayInEditorViewport())
            {
                FString cmd = "quit";
                auto ew = GEditor->GetEditorWorldContext().World();
                GEditor->Exec(ew, *cmd, *GLog);
            }
            goto OK;
        }
        else if (mg_strcmp(msg->uri, UE4_SHUTDOWN) == 0)
        {
            FGenericPlatformMisc::RequestExit(false);
            goto OK;
        }
        else if (mg_strcmp(msg->uri, UE4_BUILD) == 0)
        {
            FLevelEditorActionCallbacks::Build_Execute();
            goto OK;
        }
        else if (mg_strcmp(msg->uri, UE4_IS_BUILDING) == 0)
        {
            bool ok = FLevelEditorActionCallbacks::Build_CanExecute();
            LOG("BUILD CAN EXEC = %d", ok);
        }
        else if (mg_strcmp(msg->uri, UE4_LIST_ASSETS) == 0)
        {
            FAssetRegistryModule& AssetRegistry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
            TArray<FAssetData> AssetData;
            AssetRegistry.Get().GetAllAssets(AssetData);
            for (auto data : AssetData)
            {
                LOG("%s", *(data.PackageName.ToString()));
            }
            goto OK;
        }
        else if (mg_strcmp(msg->uri, UE4_DEBUG) == 0)
        {
            debugFn();
            goto OK;
        }

        goto BAD_ACTION;

    }
    else if (mg_strcmp(msg->method, HTTP_POST) == 0)
    {
        if (mg_strcmp(msg->uri, UE4_COMMAND) == 0)
        {
            if (msg->body.len > 0)
            {
                FString cmd = FString::Printf(T("%.*s"), msg->body.len, UTF8_TO_TCHAR(msg->body.p));
                auto ew = GEditor->GetEditorWorldContext().World();
                GEditor->Exec(ew, *cmd, *GLog);
                goto OK;
            }
            else goto BAD_ENTITY;
        }

        /*
         *  HTTP POST /ue4/loadpak
         *
         *  POST body should contain a comma separated list of the following two arguments:
         *  1. Local .pak file path to mount into the engine.
         *  2. The mount point to load it at.
         */
        else if (mg_strcmp(msg->uri, UE4_LOAD_PAKFILE) == 0)
        {
            if (msg->body.len > 0)
            {
                FString payload, pakPath, mountPath;
                payload = FString::Printf(T("%.*s"), msg->body.len, UTF8_TO_TCHAR(msg->body.p));
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
