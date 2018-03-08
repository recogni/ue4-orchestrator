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
  #include "Editor.h"
  #include "Editor/LevelEditor/Public/ILevelViewport.h"
  #include "Editor/UnrealEd/Public/LevelEditorViewport.h"
#endif


// HTTP server
#include "mongoose.h"

////////////////////////////////////////////////////////////////////////////////

// Random helper defines / misc

typedef struct mg_str       mg_str_t;
typedef struct http_message http_message_t;
typedef FLevelEditorModule  FLvlEditor;
typedef FModuleManager      FManager;

#define T TEXT
#define LOG(...) UE_LOG(LogUE4Orc, Log, __VA_ARGS__)

////////////////////////////////////////////////////////////////////////////////

static void
debugFn()
{
    return;
}

////////////////////////////////////////////////////////////////////////////////

static int
mountPakFile(FString& pakPath, FString& mountPath)
{
    LOG(T("Attempting to mount %s -> %s"), *pakPath, *mountPath);

    //  Override
    IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();
    FPakPlatformFile* PakPlatformFile = new FPakPlatformFile();
    PakPlatformFile->Initialize(&PlatformFile, T(""));
    FPlatformFileManager::Get().SetPlatformFile(*PakPlatformFile);

    const FString PakFilename(pakPath);
    FPakFile PakFile(&PlatformFile, *PakFilename, false);

    // "/Content/Import/02843684/"
    FString MountPoint = FPaths::GameDir() + mountPath;
    FPaths::MakeStandardFilename(MountPoint);
    PakFile.SetMountPoint(*MountPoint);

    if (PakPlatformFile->Mount(*PakFilename, 0, *MountPoint))
    {
        LOG(T("Mount %s success"), *MountPoint);

        TArray<FString> FileList;
        PakFile.FindFilesAtPath(FileList, *PakFile.GetMountPoint(), true, false, true);
        for (auto item : FileList)
        {
            LOG(T("%s"), *item);

            FString AssetName = item;
            FString AssetShortName = FPackageName::GetShortName(AssetName);
            FString Left, Right;
            AssetShortName.Split(T("."), &Left, &Right);
            AssetName = T("/Game/Import/02843684/1025dd84537d737febed407fccfaf6d8/") + Left + T(".") + Left;
            FStringAssetReference ref = AssetName;

            FStreamableManager StreamableManager;
            UObject* lo = StreamableManager.SynchronousLoad(ref);
            if (lo != nullptr)
            {
                LOG(T(" - %s load success!"), *AssetName);
            }
            else
            {
                LOG(T(" - %s load failed!"), *AssetName);
                return -1;
            }
        }
    }
    else
    {
        LOG(T("mount failed!"));
        return -1;
    }

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

// Action "verbs".
const mg_str_t UE4_PLAY             = mg_mk_str("/ue4/play");
const mg_str_t UE4_STOP             = mg_mk_str("/ue4/stop");
const mg_str_t UE4_SHUTDOWN         = mg_mk_str("/ue4/shutdown");
const mg_str_t UE4_COMMAND          = mg_mk_str("/ue4/command");
const mg_str_t UE4_LIST_ASSETS      = mg_mk_str("/ue4/listassets");
const mg_str_t UE4_DEBUG            = mg_mk_str("/ue4/debug");
const mg_str_t UE4_LOAD_PAKFILE     = mg_mk_str("/ue4/loadpak");


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
            if (!Editor.GetFirstActiveViewport().IsValid())
            {
                LOG(T("ERROR no valid viewport"));
                goto ERROR;
            }

            FVector  v;
            FRotator r;
            auto vp = Editor.GetFirstActiveViewport();
            GEditor->RequestPlaySession(true, vp, true, &v, &r);
            goto OK;
        }
        else if (mg_strcmp(msg->uri, UE4_STOP) == 0)
        {
            if (!Editor.GetFirstActiveViewport().IsValid())
            {
                LOG(T("ERROR no valid viewport"));
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
        else if (mg_strcmp(msg->uri, UE4_LIST_ASSETS) == 0)
        {
            FAssetRegistryModule& AssetRegistry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
            TArray<FAssetData> AssetData;
            AssetRegistry.Get().GetAllAssets(AssetData);
            for (auto data : AssetData)
            {
                LOG(T("%s"), *(data.PackageName.ToString()));
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
