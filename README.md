# UE4-Orchestrator

An unreal engine plugin with an embedded HTTP server to do your bidding.

## Install

Install this plugin like any other UE4 plugin.  Once this is done, the plugin will host a HTTP server at port `18820`.

## HTTP GET Endpoints

All these endpoints will trigger the subsequent functionality in the engine.  There is never a request body expected, and only the `200` status code indicates a success.

### `GET /ue4/play`

Play the current scene in the editor viewport and activate the `server pawn`.  Note that this uses `PlayMap` vs `GEditor->RequestPlaySession`.

### `GET /ue4/stop`

TODO: WIP - does not work.

Stop the current scene in the editor viewport.

### `GET /ue4/shutdown`

Shutdown the unreal engine (and stop any running game).

### `GET /ue4/listassets`

List all assets to the UE4 console that are registered with the UE4 `AssetRegistry`.

### `GET /ue4/debug`

Stub to invoke empty debug function for random experiments.

## HTTP POST Endpoints

### `POST /ue4/command`

Post body is expected to be a `string`.  The body is passed directly to the UE4 editor console where it is `Exec`'d.

Example: Run a python script in the scripts dir called `foo.py` with arguments `a`, `b` and `42`:
```
echo "py.exec_args foo.py a b 42" | http POST localhost:18820/ue4/command
```

Example: Run a random UE4 engine console command:
```
echo "showfps" | http POST localhost:18820/ue4/command
```

### `POST /ue4/loadpak`

Post body is expected to be a comma-separated-`string`.  The first element is the path in the local file-system to the `.pak` file we wish to mount, and the second argument is the mount path.

Eample: Mount `/tmp/foo.pak` into `/Content/Import/02843684`:
```
echo /tmp/foo.pak,/Content/Import/02843684 | http POST localhost:18820/ue4/loadpak
```

## Detailed usage example

### Import Shapenet class `00000001` from `/tmp/shapenet/` into `/Game/Import` and generate `/tmp/output.pak`:

```
echo "py.exec_args import_fbx.py import_shapenet /tmp/shapenet/ 00000001" | http POST localhost:18820/ue4/command
echo "py.exec_args import_fbx.py make_pak /Game/Import/ /tmp/output.pak" | http POST localhost:18820/ue4/command
echo "/tmp/output.pak,/Content/Import/02843684" | http POST localhost:18820/ue4/loadpak
```

