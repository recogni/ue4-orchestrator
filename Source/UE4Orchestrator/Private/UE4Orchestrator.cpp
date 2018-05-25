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
#include "FileManagerGeneric.h"
#include "StreamingNetworkPlatformFile.h"
#include "Runtime/AssetRegistry/Public/AssetRegistryModule.h"
#include "Runtime/Json/Public/Dom/JsonObject.h"
#include "Runtime/Core/Public/Misc/WildcardString.h"
#include "Runtime/Engine/Classes/Engine/StreamableManager.h"
#include "Runtime/Engine/Classes/Engine/AssetManager.h"

#if WITH_EDITOR
#  include "LevelEditor.h"
#  include "Editor.h"
#  include "Editor/LevelEditor/Public/ILevelViewport.h"
#  include "Editor/LevelEditor/Public/LevelEditorActions.h"
#  include "Editor/UnrealEd/Public/LevelEditorViewport.h"
#endif

// HTTP server
#include "mongoose.h"

////////////////////////////////////////////////////////////////////////////////

static void
debugFn(FString payload)
{
    return;
}

////////////////////////////////////////////////////////////////////////////////

int URCHTTP::mountPakFile(const FString& pakPath) 
{
    int ret = 0;

    IPlatformFile *originalPlatform = &FPlatformFileManager::Get().GetPlatformFile();	    

    // Check to see if the file exists first
    if(!originalPlatform->FileExists(*pakPath))
    {
	LOG("PakFile %s does not exist", *pakPath);
	return -1;
    }
    
    // Allocate a new platform PAK object
    // FIXME - this leaks but it may not matter
    FPakPlatformFile *PakFileMgr = new FPakPlatformFile();
    if(PakFileMgr == nullptr)
    {
	LOG("Failed to create platform file %s", T("PakFile"));	    
	return -1;
    }

    // Initialize the lower level file from the previous top layer
    PakFileMgr->Initialize(&FPlatformFileManager::Get().GetPlatformFile(),T(""));
    PakFileMgr->InitializeNewAsyncIO();
    
    // The pak reader is now the current platform file
    FPlatformFileManager::Get().SetPlatformFile(*PakFileMgr);	

    // Get the mount point from the Pak meta-data
    static FPakFile PakFile(PakFileMgr, *pakPath, false);
    FString MountPoint = PakFile.GetMountPoint();

    // Determine where the on-disk path is for the mountpoint and register it
    FString PathOnDisk = FPaths::ProjectDir() / MountPoint;
    FPackageName::RegisterMountPoint(MountPoint, PathOnDisk);

    FString MountPointFull = PathOnDisk;
    FPaths::MakeStandardFilename(MountPointFull);

    LOG("Mounting at %s and registering mount point %s at %s", *MountPointFull, *MountPoint, *PathOnDisk);

    if(PakFileMgr->Mount(*pakPath, 0, *MountPointFull))
    {
	LOG("Mount %s success", *MountPoint);	

	UAssetManager *Manager;
	
	// Add to the list of assets to load
	if ((Manager = UAssetManager::GetIfValid()) != nullptr)
	{
	    Manager->GetAssetRegistry().SearchAllAssets(true);

	    TArray<FString> FileList;
	    PakFile.FindFilesAtPath(FileList, *PakFile.GetMountPoint(),
				    true, false, true);
	    
	    // Iterate over the collected files from the pak
	    for(auto asset : FileList)
	    {
		FString Package, BaseName, Extension;
		FPaths::Split(asset, Package, BaseName, Extension);
		FString ModifiedAssetName = Package / BaseName + "." + BaseName;
		// FIXME - this should test for the type rather than the name
		if( (BaseName.Find(T("material"))) ||
		    (BaseName.Find(T("model"))) )
		{
		    LOG("Trying to load %s as %s ", *asset, *ModifiedAssetName);
		    Manager->GetStreamableManager().LoadSynchronous(ModifiedAssetName, true, nullptr);
		}
	    }
	}

	// Restore the platform file
	FPlatformFileManager::Get().SetPlatformFile(*originalPlatform);
    }
    else
    {
	LOG("%s", "mount failed!");
	ret = -1; return -1;
    }

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
    URCHTTP&        server    = URCHTTP::Get();

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
            server.SetPollInterval(x);
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
         *  2. The mount point to load it at.
         *  3. (optional) wildcard pattern of assets to load (*,? form)
         */
        else if (matches_any(&msg->uri, "/loadpak", "/ue4/loadpak"))
        {
            if (body.Len() > 0)
            {
                TSharedPtr<FJsonObject> parsed;
                auto reader = TJsonReaderFactory<TCHAR>::Create(*body);

                FString pakPath, mountPoint, pattern_string;
                FWildcardString pattern(T("*")); // Default filter

                if (FJsonSerializer::Deserialize(reader, parsed))
                {
                    if (parsed->HasField("pak_path"))
                        pakPath = parsed->GetStringField("pak_path");
                    if (parsed->HasField("mount_point"))
                        mountPoint = parsed->GetStringField("mount_point");
                    if (parsed->HasField("pattern"))
                        pattern_string = parsed->GetStringField("pattern");
                        pattern = FWildcardString(pattern_string);
                }
                else
                {
                    // JSON did not work, try the old method of using the
                    // comma separated values.
                    body.TrimEndInline();

                    TArray<FString> pak_options;
                    int32 num_params = body.ParseIntoArray(pak_options, T(","), true);
                    if(num_params < 2) return;
                    pakPath = pak_options[0];
                    mountPoint = pak_options[1];
                    if(num_params == 2) {
                        pattern = FWildcardString(T("*"));
                    } else {
                        pattern = FWildcardString(pak_options[2]);
                    }
                }

                if (pakPath.Len() == 0 || mountPoint.Len() == 0)
                    return;

                LOG("Mounting pak file: %s into %s wildcard %s", *pakPath, *mountPoint, *pattern);
                if (URCHTTP::mountPakFile(pakPath) < 0)
                    goto ERROR;

                goto OK;
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

URCHTTP&
URCHTTP::Get()
{
    static URCHTTP Singleton;
    return Singleton;
}


////////////////////////////////////////////////////////////////////////////////

URCHTTP::URCHTTP()
    : poll_interval(0), poll_ms(1)
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
URCHTTP::SetPollInterval(int v)
{
    poll_interval = v;
}

void
URCHTTP::Tick(float dt)
{
    static int tick_counter = 0;

    if (poll_interval == 0 || (tick_counter++ % poll_interval) == 0)
        mg_mgr_poll(&mgr, poll_ms);
}

TStatId
URCHTTP::GetStatId() const
{
    RETURN_QUICK_DECLARE_CYCLE_STAT(URCHTTP, STATGROUP_Tickables);
}

////////////////////////////////////////////////////////////////////////////////
