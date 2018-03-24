# UE4-Orchestrator

An unreal engine plugin with an embedded HTTP server to do your bidding.

## Install

Install this plugin like any other UE4 plugin.  Once this is done, the plugin will host a HTTP server at port `18820`.

## HTTP GET Endpoints

All these endpoints will trigger the subsequent functionality in the engine.  There is never a request body expected, and only the `200` status code indicates a success.

| Endpoint     | Description                                                           |
|--------------|-----------------------------------------------------------------------|
| /play        | Trigger a play in the current level                                   |
| /stop        | Stop playback                                                         |
| /shutdown    | Shutdown the UE4 editor                                               |
| /build       | Trigger a build in the editor                                         |
| /is_building | Returns TRUE if the editor is currently building                      |
| /list_assets | List all assets registered with the asset registry module             |
| /assets_idle | Returns OK if the asset importer is idle, returns TRY_AGAIN otherwise |
| /debug       | Calls the `debugFn()` used for experimentation                        |

## HTTP POST Endpoints

| Endpoint     | Description                                                           |
|--------------|-----------------------------------------------------------------------|
| /command     | Execute a console command in the editor                               |
| /loadpak     | Load a pakfile                                                        |

### `POST /command`

Post body is expected to be a `string`.  The body is passed directly to the UE4 editor console where it is `Exec`'d.

Example: Run a python script in the scripts dir called `foo.py` with arguments `a`, `b` and `42`:
```
echo "py.exec_args foo.py a b 42" | http POST localhost:18820/command
```

Example: Run a random UE4 engine console command:
```
echo "showfps" | http POST localhost:18820/command
```

### `POST /loadpak`

Post body is expected to be a comma-separated-`string`.  The first element is the path in the local file-system to the `.pak` file we wish to mount, and the second argument is the mount path.

Eample: Mount `/tmp/foo.pak` into `/Content/Import/02843684`:
```
echo /tmp/foo.pak,/Content/Import/02843684 | http POST localhost:18820/loadpak
```

## Detailed usage example

### Import Shapenet class `00000001` from `/tmp/shapenet/` into `/Game/Import` and generate `/tmp/output.pak`:

```
echo "py.exec_args import_fbx.py import_shapenet /tmp/shapenet/ 00000001" | http POST localhost:18820/command
echo "py.exec_args import_fbx.py make_pak /Game/Import/ /tmp/output.pak" | http POST localhost:18820/command
echo "/tmp/output.pak,/Content/Import/02843684" | http POST localhost:18820/loadpak
```

