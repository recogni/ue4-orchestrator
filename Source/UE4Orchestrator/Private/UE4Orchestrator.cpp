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
#include "FileManagerGeneric.h"
#include "StreamingNetworkPlatformFile.h"
#include "Runtime/AssetRegistry/Public/AssetRegistryModule.h"
#include "Runtime/Core/Public/Misc/WildcardString.h"
#include "Runtime/Engine/Classes/Engine/StreamableManager.h"
#include "Runtime/Engine/Classes/Engine/AssetManager.h"
#include "Runtime/Engine/Public/ShaderCompiler.h"

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

    if (PakFileMgr == nullptr)
    {
        PakFileMgr =  new FPakPlatformFile;
        // Initialize the lower level file from the previous top layer
        PakFileMgr->Initialize(&FPlatformFileManager::Get().GetPlatformFile(),T(""));
        PakFileMgr->InitializeNewAsyncIO();
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
        LOG("Mount %s success", *MountPoint);
        if (UAssetManager* Manager = UAssetManager::GetIfValid())
        {
            Manager->GetAssetRegistry().SearchAllAssets(true);

            if (bLoadContent)
            {
                TArray<FString> FileList;
                PakFile.FindFilesAtPath(FileList, *PakFile.GetMountPoint(),
                            true, false, true);

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

    FinishAllShaderCompilation();

    return ret;
}

int
URCHTTP::LoadObject(const FString& assetPath)
{
    int ret = -1;
    IPlatformFile *originalPlatform = &FPlatformFileManager::Get().GetPlatformFile();

    if (PakFileMgr == nullptr)
    {
        LOG("Failed to create platform file %s", T("PakFile"));
        return -1;
    }

    // The pak reader is now the current platform file
    FPlatformFileManager::Get().SetPlatformFile(*PakFileMgr);

    if (UAssetManager* Manager = UAssetManager::GetIfValid())
    {
        if (!FindObject< UStaticMesh>(ANY_PACKAGE,*assetPath))
        {
            // Depend on UE's GC
            UObject *object = Manager->GetStreamableManager().LoadSynchronous(assetPath, false, nullptr);
            if (object == nullptr)
                ret = -1;
            else
            {
                LoadedAssetMap.Add(assetPath, object);
                ret = 0;
            }
        }
        else
            ret = 0;
    }

    FPlatformFileManager::Get().SetPlatformFile(*originalPlatform);

    FinishAllShaderCompilation();

    return ret;
}

//#define DEBUG_UNLOAD_OBJECT
//#define FORCE_UNLOAD_GC
int
URCHTTP::UnloadObject(const FString& assetPath)
{
    int ret = -1;

    if(LoadedAssetMap.Find(assetPath)) {
      LoadedAssetMap.Remove(assetPath);
      ret = 0;
    }

    CollectGarbage(RF_NoFlags, true);

    return ret;
}

void
URCHTTP::FinishAllShaderCompilation()
{
    if (GShaderCompilingManager != nullptr)
    {
        GShaderCompilingManager->FinishAllCompilation();
    }
}

void
URCHTTP::PakTest()
{
  FString obj_paths[] = {
    "/Game/Import/ShapeNet/04256520/7051b028c8c1facfced9bf2a92246703/model_normalized",
    "/Game/Import/ShapeNet/04256520/11f31367f34bfea04b3c42e318f3affc/model_normalized",
    "/Game/Import/ShapeNet/03467517/176ceacdbc44bc3569da8e7f9a803d12/model_normalized",
    "/Game/Import/ShapeNet/03636649/a11dd450220af960ca70272754aeb3c9/model_normalized",
    "/Game/Import/ShapeNet/04379243/75f2bc98aecf198974984b9cd0997a52/model_normalized",
    "/Game/Import/ShapeNet/04256520/5cc378efd61f0333afd8078191062c7f/model_normalized",
    "/Game/Import/ShapeNet/02691156/ecb0d8d1c592bcc11bee3078a673c2ae/model_normalized",
    "/Game/Import/ShapeNet/03001627/d29c14f180ce319f71271c0017c27c86/model_normalized",
    "/Game/Import/ShapeNet/02808440/ca14dca61e50a05b3048063251585afb/model_normalized",
    "/Game/Import/ShapeNet/03046257/fdb5537f0899ced0744fc85fe0d3e26e/model_normalized",
    "/Game/Import/ShapeNet/04256520/13d0d8dcb20c0071effcc073d8ec38f6/model_normalized",
    "/Game/Import/ShapeNet/03001627/431ca0621cab1313b0204d9cc6bc8619/model_normalized",
    "/Game/Import/ShapeNet/02691156/19e2864af4f6de438050e8e370967931/model_normalized",
    "/Game/Import/ShapeNet/04256520/3c56ceef171fa142126c0b0ea3ea0a2c/model_normalized",
    "/Game/Import/ShapeNet/03636649/def342a8d095d8501ab5f696a41d80c/model_normalized",
    "/Game/Import/ShapeNet/03691459/403649d8cf6b019d5c01f9a624be205a/model_normalized",
    "/Game/Import/ShapeNet/03991062/16e6fbacfbbc4bcf9aec59741c69cf7/model_normalized",
    "/Game/Import/ShapeNet/04256520/c87497b3c00b3116be8af56c801ecf41/model_normalized",
    "/Game/Import/ShapeNet/03642806/2dbabd43fdc9e21ffae724b4e4a1ff51/model_normalized",
    "/Game/Import/ShapeNet/04401088/a2b921dea6df33765282621e4b0cea7/model_normalized",
    "/Game/Import/ShapeNet/04379243/4572e2658d6e6cfe531eb43ec132817f/model_normalized",
    "/Game/Import/ShapeNet/03467517/c5f9c372078561644368de794b251ad1/model_normalized",
    "/Game/Import/ShapeNet/04256520/76fb7ca32181075e9a547820eb170949/model_normalized",
    "/Game/Import/ShapeNet/03001627/813f2777195efc4e19fb4103277a6b93/model_normalized",
    "/Game/Import/ShapeNet/02958343/b10794a03c59e42d32a4e3dd3a89488f/model_normalized",
    "/Game/Import/ShapeNet/03211117/2ca1353d647e5c51df8d3317f6046bb8/model_normalized",
    "/Game/Import/ShapeNet/03211117/9994c527e9c39bc5b50d0c6a0c254040/model_normalized",
    "/Game/Import/ShapeNet/03928116/818575447bfc6dbfa38757550100c186/model_normalized",
    "/Game/Import/ShapeNet/04256520/8d5acb33654685d965715e89ab65beed/model_normalized",
    "/Game/Import/ShapeNet/04379243/164ec64e7a28c08b221ea40148177a97/model_normalized",
    "/Game/Import/ShapeNet/03337140/2e2704de75c84d3d6f1e07a56c129dfc/model_normalized",
    "/Game/Import/ShapeNet/04460130/76377cf0dc70d027e7abbab3021b6409/model_normalized",
    "/Game/Import/ShapeNet/04090263/460ad09b269895f73f82da5dbc1a5004/model_normalized",
    "/Game/Import/ShapeNet/04090263/487ef6821e82c9548839ade0cf1fb995/model_normalized",
    "/Game/Import/ShapeNet/03642806/f72dc1ffeae0168aadcfd37206a0d18b/model_normalized",
    "/Game/Import/ShapeNet/03046257/73956ea1cfbaa5ef14038d588fd1342f/model_normalized",
    "/Game/Import/ShapeNet/03325088/d3aa2eeb7641d99fb9101063463c970d/model_normalized",
    "/Game/Import/ShapeNet/03001627/2ed17abd0ff67d4f71a782a4379556c7/model_normalized",
    "/Game/Import/ShapeNet/03046257/f18450425e69b37b76e9713f57a5fcb6/model_normalized",
    "/Game/Import/ShapeNet/02924116/ed107e8a27dbb9e1e7aa75a25fcc93d2/model_normalized",
    "/Game/Import/ShapeNet/03211117/8dda338160076595234c2f2e8f2fe6da/model_normalized",
    "/Game/Import/ShapeNet/03636649/364ea921dbd5da869a58625fdbc8d761/model_normalized",
    "/Game/Import/ShapeNet/03001627/bb3516732bcd45f2490ad276cd2af3a4/model_normalized",
    "/Game/Import/ShapeNet/02946921/a70947df1f1490c2a81ec39fd9664e9b/model_normalized",
    "/Game/Import/ShapeNet/03467517/82f1a0f72d800cbd93f0194265a9746c/model_normalized",
    "/Game/Import/ShapeNet/03001627/541746ddb47aa2af4e186c8346f12e6/model_normalized",
    "/Game/Import/ShapeNet/03001627/6072ff799065609b6bc601efa799c927/model_normalized",
    "/Game/Import/ShapeNet/03636649/d97a86cea650ae0baf5b49ad7809302/model_normalized",
    "/Game/Import/ShapeNet/03991062/63313f52b9b69592ff67c12005f72d2/model_normalized",
    "/Game/Import/ShapeNet/02808440/fe59e0e02ca91956b362845c6edb57fc/model_normalized",
    "/Game/Import/ShapeNet/02691156/7b3bd63ff099f5b062b600da24e0965/model_normalized",
    "/Game/Import/ShapeNet/04379243/4df369ee72ea8b2c3da27ece6ae88fff/model_normalized",
    "/Game/Import/ShapeNet/02808440/5bcca919768845bdd0043cfa915003ff/model_normalized",
    "/Game/Import/ShapeNet/03636649/30fd90087f12d6ddb3a010e5a9dcf3a8/model_normalized",
    "/Game/Import/ShapeNet/02933112/931fcaa08876700e788f926f4d51e733/model_normalized",
    "/Game/Import/ShapeNet/04090263/9e6d1d06ae82ac106f21883e4b04581e/model_normalized",
    "/Game/Import/ShapeNet/02808440/6ec3dbef4a3e1cb279d57ee14f8ee702/model_normalized",
    "/Game/Import/ShapeNet/03001627/dfeb8d914d8b28ab5bb58f1e92d30bf7/model_normalized",
    "/Game/Import/ShapeNet/03001627/9ab18a33335373b2659dda512294c744/model_normalized",
    "/Game/Import/ShapeNet/04530566/ea491bbee7524859cfea3d4fc15719ea/model_normalized",
    "/Game/Import/ShapeNet/03001627/9233077bbe6926c239465fa20b0ba7fb/model_normalized",
    "/Game/Import/ShapeNet/03325088/5978d6e26b9db10db362845c6edb57fc/model_normalized",
    "/Game/Import/ShapeNet/03001627/9a8dfc7a6831749f504721639e19f609/model_normalized",
    "/Game/Import/ShapeNet/03211117/f464a98b2d4f598be8581b38259c3721/model_normalized",
    "/Game/Import/ShapeNet/02747177/637a9226a0509545cb2a965e75be701c/model_normalized",
    "/Game/Import/ShapeNet/04468005/efcf0fbc2d749875f18e4d26315469b1/model_normalized",
    "/Game/Import/ShapeNet/03001627/c5d880efc887f6f4f9111ef49c078dbe/model_normalized",
    "/Game/Import/ShapeNet/04090263/49e4708854f762ae9c27f9a5387b5fc/model_normalized",
    "/Game/Import/ShapeNet/03046257/cea1956637d8e28e11097ee614f39736/model_normalized",
    "/Game/Import/ShapeNet/02747177/77905cdb377b3c47cb2a965e75be701c/model_normalized",
    "/Game/Import/ShapeNet/02828884/4cb196a794bb7876f4d63bd79294e117/model_normalized",
    "/Game/Import/ShapeNet/03991062/c1923b90d48f2016d86b59aca5792b15/model_normalized",
    "/Game/Import/ShapeNet/04379243/d97e2a50640387adf90c06a14471bc6/model_normalized",
    "/Game/Import/ShapeNet/04530566/d74ea3567f8861dc182929c56117755a/model_normalized",
    "/Game/Import/ShapeNet/03046257/e13f5f28839b623bcff103a918fa8005/model_normalized",
    "/Game/Import/ShapeNet/02691156/9a3cb94af2f2a5b826360e1e29a956c7/model_normalized",
    "/Game/Import/ShapeNet/03046257/e75539f66fe4071e4858278d1f98c5a/model_normalized",
    "/Game/Import/ShapeNet/04554684/fa4162988208d07a1cc00550ccb8f129/model_normalized",
    "/Game/Import/ShapeNet/03046257/fb0dbe220131e28f6402b8f491cd92c7/model_normalized",
    "/Game/Import/ShapeNet/04379243/392963c87d26a617d5d95a669ff2219/model_normalized",
    "/Game/Import/ShapeNet/04379243/fc9f18e509b363fcac7bed72580dc30f/model_normalized",
    "/Game/Import/ShapeNet/04530566/bc8cfba04c86ab537117d5d39e7f9235/model_normalized",
    "/Game/Import/ShapeNet/02691156/45bd6c0723e555d4ba2821676102936/model_normalized",
    "/Game/Import/ShapeNet/02691156/7d928af41b7dd26e1d0f8853f6d023e3/model_normalized",
    "/Game/Import/ShapeNet/03046257/3d027f0431baa68a9dcc4633bad27c61/model_normalized",
    "/Game/Import/ShapeNet/02992529/df816deae044cf448e53975c71cdb91/model_normalized",
    "/Game/Import/ShapeNet/02828884/9d68ef4ac84c552819fb4103277a6b93/model_normalized",
    "/Game/Import/ShapeNet/04379243/572abcadc95a8ed14b3c42e318f3affc/model_normalized",
    "/Game/Import/ShapeNet/03513137/190364ca5d1009a324a420043b69c13e/model_normalized",
    "/Game/Import/ShapeNet/04379243/3f9b11fdf6d5aa3d9c75eb4326997fae/model_normalized",
    "/Game/Import/ShapeNet/03467517/2be11b43e7a24b4a13dd57bbc3e006f9/model_normalized",
    "/Game/Import/ShapeNet/02691156/3998242c3442e04265e04abc9923b374/model_normalized",
    "/Game/Import/ShapeNet/04379243/c0a143c5fd0048bbcd01aef15a146d7a/model_normalized",
    "/Game/Import/ShapeNet/03001627/6714df9bb34178e4f51f77a6d7299806/model_normalized",
    "/Game/Import/ShapeNet/04090263/fcc1826e28e6f512b9e600da283b7f26/model_normalized",
    "/Game/Import/ShapeNet/04379243/d14bcc93169f80d9b2d5d82056287083/model_normalized",
    "/Game/Import/ShapeNet/04090263/190ce4466c6f4c04fa9286f039319ff7/model_normalized",
    "/Game/Import/ShapeNet/04530566/ecbfa1faba336185bc33bb3e21836dd7/model_normalized",
    "/Game/Import/ShapeNet/04379243/97cb53b5e54a2abedf6cfab91d65bb91/model_normalized"
  };

  auto ar = "AssetRegistry";
  FAssetRegistryModule& AssetRegistry =
    FModuleManager::LoadModuleChecked<FAssetRegistryModule>(ar);

  for (auto obj : obj_paths ) {
    LOG("Trying to load %s", *obj);
    LoadObject(*(obj + ".model_normalized") );

    TArray<FAssetData> AssetData;

#if 0
    AssetRegistry.Get().GetAllAssets(AssetData);
    for (auto data : AssetData)
      {
        FString path = *(data.PackageName.ToString());
        FPaths::MakePlatformFilename(path);
        if( path.StartsWith(T("/Game/Import/ShapeNet"), ESearchCase::IgnoreCase) ) {
          LOG("%s %s %d", *(data.PackageName.ToString()), *path, AssetData.Num());
        }
      }
    LOG("---------------------------- %s Done --------------------------", T("Unload"));
#endif
    LOG("Trying to unload %s", *obj);
    UnloadObject(*(obj + ".model_normalized") );

#if 0
    AssetRegistry.Get().GetAllAssets(AssetData);
    for (auto data : AssetData)
      {
        FString path = *(data.PackageName.ToString());
        FPaths::MakePlatformFilename(path);
        if( path.StartsWith(T("/Game/Import/ShapeNet"), ESearchCase::IgnoreCase) ) {
          LOG("%s %s %d", *(data.PackageName.ToString()), *path, AssetData.Num());
        }
      }
    LOG("---------------------------- %s Done --------------------------", T("Unload"));
#endif

  }


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

        else if (matches_any(&msg->uri, "/paktest", "/pak_test")) {
          URCHTTP::Get()->PakTest();
        }

        else if (matches_any(&msg->uri, "/gc")) {
            CollectGarbage(RF_NoFlags, true);
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
                    if (URCHTTP::Get()->LoadObject(objects[0]) < 0)
                    {
                        goto ERROR;
                    }
                    else
                    {
                        goto OK;
                    }
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

                if (num_params==1)
                {
                    if (URCHTTP::Get()->UnloadObject(objects[0]) < 0)
                    {
                        goto ERROR;
                    }
                    else
                    {
                        goto OK;
                    }
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
    PakFileMgr = nullptr;
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
