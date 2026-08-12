# Dobby

Compact Minecraft Bedrock developer client for macOS
[mcpelauncher](https://github.com/minecraft-linux/mcpelauncher-manifest).

It captures packet violations and raw decode evidence, displays entity/player hitboxes, outlines client-known chests and ores, and shows passive network, chunk, and packet-traffic metrics. It also includes an isolated, opt-in cape entitlement test.

## In-game diagnostic

`Mods > Dobby` contains the developer overlay. It shows native `PING`, client-observed `TPS~`, loaded chunks and outstanding requests, plus client `FPS` and resident memory use.

The bottom-right packet overlay shows compact incoming/outgoing packet and byte rates plus cumulative traffic formatted in `B`, `KB`, `MB`, `GB`, or `TB`. It appears only while a client world is rendering. `Packet traffic` toggles it independently from the top-right network metrics.

![Dobby developer metrics overlay](media/debugger.png)

`Chest ESP` outlines chests found in Bedrock's decoded client chunk storage. It does not request or modify world data, so concealed chests appear only if the server actually sent them.

`Ore ESP` incrementally scans client-decoded subchunk palettes for vanilla ores, ancient debris, and mineral storage blocks. Loaded chunks refresh nearest-first when enabled, while nearby chunks are rechecked for live block changes. It never requests or modifies chunk data.

Menu toggles are saved locally and restored on the next launch.

## Local cape entitlement test

Put local `* (persona).zip` cape archives in the ignored `capes/` directory. The verified install workflow validates every archive and PNG, decodes each bounded 64×32 RGBA texture, and writes only Dobby's private `dobby-capes` index and pixel files. It does not edit Bedrock's persona cache, catalog, resource-pack directories, or account data. Previous Dobby cape data is backed up before replacement. Cape assets remain local and are never staged by the publish workflow.

Enable `Mods > Dobby > Cape entitlement test` before opening the cape picker. For the exact supported build, Dobby validates the native persona manager and `PersonaRepository` layouts, adds local UUIDs to the manager's in-memory `persona_capes` vector, and resolves those UUIDs through validated piece-lookup hooks. Disabling the toggle removes the local IDs from that vector. Selecting a local UUID reuses the client's validated Pan Cape piece as a resource template. On equip, Dobby accepts only an exact local cape ID and an exact 64×32 RGBA `SerializedSkinImpl` layout, copies that cape's local pixels into the shared skin, and marks the outgoing `PlayerSkinPacket` premium, non-persona, and cape-on-classic. The toggle defaults to off; any target, ABI, ID, image, or vector mismatch leaves mutation disabled.

![Packet rejection diagnostic window](media/image.png)

![Entity and player hitbox overlay](media/esp.png)

## Target

- Minecraft Android `1.26.40.5`
- `arm64-v8a`
- `libminecraftpe.so` build ID `5893edc8d56c93cbdb50e0f9436320236b78c89d`

The mod validates the target signature and refuses to patch incompatible builds.

## Build

```sh
./build.sh
```

The default workflow runs release and sanitizer tests, builds ARM64, audits public
content, installs the verified artifact, commits and pushes changes, then starts
Minecraft and confirms Dobby is ready. Use `./build.sh --local` for a build-only
iteration or `./build.sh --help` for individual opt-outs.

Logs default to `~/Library/Application Support/mcpelauncher/`. Set
`DOBBY_OUTPUT_DIR` to override the output directory. Optional configuration:

- `DOBBY_AUTO_POPUP=0` disables automatic violation popups.
- `DOBBY_VERBOSE=1` enables verbose developer events.
- `DOBBY_HISTORY_LIMIT=100` sets the bounded in-memory history size.
- `DOBBY_RAW_CAPTURE_LIMIT=2048` sets the maximum captured packet-body bytes.

Raw captures and logs may contain server-provided data and remain excluded from Git.
