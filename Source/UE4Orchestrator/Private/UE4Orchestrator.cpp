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

typedef struct mg_str       mg_str_t;
typedef struct http_message http_message_t;
typedef FLevelEditorModule  FLvlEditor;
typedef FModuleManager      FManager;


////////////////////////////////////////////////////////////////////////////////

static void
debugFn(const char * pakFilePath)
{
    //  Override
    IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();
    FPakPlatformFile* PakPlatformFile = new FPakPlatformFile();
    PakPlatformFile->Initialize(&PlatformFile, TEXT(""));
    FPlatformFileManager::Get().SetPlatformFile(*PakPlatformFile);

    const FString PakFilename(pakFilePath);
    FPakFile PakFile(&PlatformFile, *PakFilename, false);

    FString MountPoint = FPaths::GameDir() + "/Content/Import/02843684/";
    FPaths::MakeStandardFilename(MountPoint);
    PakFile.SetMountPoint(*MountPoint);

    if (PakPlatformFile->Mount(*PakFilename, 0, *MountPoint))
    {
        UE_LOG(LogUE4Orc, Log, TEXT("Mount success!"));

        TArray<FString> FileList;
        PakFile.FindFilesAtPath(FileList, *PakFile.GetMountPoint(), true, false, true);
        for (auto item : FileList)
        {
            UE_LOG(LogUE4Orc, Log, TEXT("%s"), *item);

            FString AssetName = item;
            FString AssetShortName = FPackageName::GetShortName(AssetName);
            FString Left, Right;
            AssetShortName.Split(TEXT("."), &Left, &Right);
            AssetName = TEXT("/Game/Import/02843684/1025dd84537d737febed407fccfaf6d8/") + Left + TEXT(".") + Left;
            FStringAssetReference ref = AssetName;

            FStreamableManager StreamableManager;
            UObject* lo = StreamableManager.SynchronousLoad(ref);
            if (lo != nullptr)
            {
                UE_LOG(LogUE4Orc, Log, TEXT(" - %s load success!"), *AssetName);
            }
            else
            {
                UE_LOG(LogUE4Orc, Log, TEXT(" - %s load failed!"), *AssetName);
            }
        }


        FAssetRegistryModule& AssetRegistry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
        TArray<FAssetData> AssetData;
        AssetRegistry.Get().GetAllAssets(AssetData);

        for (auto data : AssetData)
        {
            UE_LOG(LogUE4Orc, Log, TEXT("%s"), *(data.PackageName.ToString()));
        }

    }
    else
    {
        UE_LOG(LogUE4Orc, Log, TEXT("mount failed!"));
    }

    return;
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

// Action "verbs".
const mg_str_t UE4_PLAY             = mg_mk_str("/ue4/play");
const mg_str_t UE4_STOP             = mg_mk_str("/ue4/stop");
const mg_str_t UE4_SHUTDOWN         = mg_mk_str("/ue4/shutdown");
const mg_str_t UE4_COMMAND          = mg_mk_str("/ue4/command");
const mg_str_t UE4_DEBUG            = mg_mk_str("/ue4/debug");


////////////////////////////////////////////////////////////////////////////////

static void
ev_handler(struct mg_connection* conn, int ev, void *ev_data)
{
    if (ev == MG_EV_HTTP_REQUEST)
    {
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
                    UE_LOG(LogUE4Orc, Log, TEXT("ERROR no valid viewport"));
                    return;
                }

                FVector  v;
                FRotator r;
                auto vp = Editor.GetFirstActiveViewport();
                GEditor->RequestPlaySession(true, vp, true, &v, &r);

                rspMsg = STATUS_OK;
                rspStatus = 200;
            }
            else if (mg_strcmp(msg->uri, UE4_STOP) == 0)
            {
                if (!Editor.GetFirstActiveViewport().IsValid())
                {
                    UE_LOG(LogUE4Orc, Log, TEXT("ERROR no valid viewport"));
                    return;
                }

                if (Editor.GetFirstActiveViewport()->HasPlayInEditorViewport())
                {
                    FString cmd = "quit";
                    auto ew = GEditor->GetEditorWorldContext().World();
                    GEditor->Exec(ew, *cmd, *GLog);
                }

                rspMsg = STATUS_OK;
                rspStatus = 200;
            }
            else if (mg_strcmp(msg->uri, UE4_SHUTDOWN) == 0)
            {
                FGenericPlatformMisc::RequestExit(false);

                rspMsg = STATUS_OK;
                rspStatus = 200;
            }
            else if (mg_strcmp(msg->uri, UE4_DEBUG) == 0)
            {
                debugFn("/tmp/foo.pak");

                rspMsg = STATUS_OK;
                rspStatus = 200;
            }
            else
            {
                rspMsg = STATUS_BAD_ACTION;
                rspStatus = 500;
            }
        }
        else if (mg_strcmp(msg->method, HTTP_POST) == 0)
        {
            if (mg_strcmp(msg->uri, UE4_COMMAND) == 0)
            {
                if (msg->body.len > 0)
                {
                    FString cmd = FString::Printf(TEXT("%.*s"), msg->body.len, UTF8_TO_TCHAR(msg->body.p));
                    auto ew = GEditor->GetEditorWorldContext().World();
                    GEditor->Exec(ew, *cmd, *GLog);

                    rspMsg = STATUS_OK;
                    rspStatus = 200;
                }
                else
                {
                    rspMsg = STATUS_BAD_ACTION;
                    rspStatus = 500;
                }
            }
            else
            {
                rspMsg = STATUS_BAD_ACTION;
                rspStatus = 500;
            }
        }
#else
        rspMsg = STATUS_ERROR;
        rspStatus = 501;
#endif // WITH_EDITOR

        mg_send_head(conn, rspStatus, rspMsg.len, "Content-Type: text/plain");
        mg_printf(conn, "%s", rspMsg.p);
    }
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
