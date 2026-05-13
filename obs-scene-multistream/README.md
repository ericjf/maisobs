# obs-scene-multistream

OBS Studio plugin that streams **different scenes to different RTMP destinations** simultaneously. Each destination gets its own video pipeline (`obs_view` + encoder + output), so you can send a horizontal layout to Twitch, a vertical layout to TikTok, and a third cut to Kick — all at once, from the same OBS instance.

This is **not** the same as `obs-multi-rtmp` or Aitum Multistream, which duplicate the *same* program output to multiple targets. Those plugins call `obs_encoder_set_video(venc, obs_get_video())`. This plugin creates an independent `video_t` per destination via `obs_view_add2`.

> Status: **v0.1 — alpha**. Pipeline + UI work; no DPAPI for stream keys yet, no audio per destination. See the [project plan](../.claude/plans) for roadmap.

## Features (v0.1)

- N independent RTMP destinations, each bound to a separate scene
- Per-destination resolution, FPS, encoder, bitrate
- Add / edit / remove via Qt dock
- Start individual / Start all / Stop all
- Persistence in `<obs-config>/plugin_config/obs-scene-multistream/destinations.json`
- Plain RTMP/RTMPS via `rtmp_custom` service (works with YouTube, Twitch, Kick, custom nginx-rtmp, etc.)

## Architecture

```
DestinationConfig (struct)
        │
        ▼
MultistreamManager
        │
        ├── obs_view_create() ─── obs_view_set_source(scene)
        ├── obs_view_add2(ovi) ─── video_t* (independent canvas)
        ├── obs_video_encoder_create() + obs_encoder_set_video(view's video_t)
        ├── obs_audio_encoder_create() + obs_encoder_set_audio(global audio)
        ├── obs_service_create("rtmp_custom")
        └── obs_output_create("rtmp_output") → start
```

The audio path is shared (`obs_get_audio()`); only video is forked per destination.

## Build (Windows x64)

Prereqs:
- Visual Studio 2022 with Desktop C++ workload
- CMake ≥ 3.28
- Git

```powershell
cd C:\Users\Ericat\OBS\obs-scene-multistream
cmake --preset windows-x64
cmake --build --preset windows-x64 --config RelWithDebInfo
```

Dependencies (OBS sources, prebuilt deps, Qt6) are pulled automatically by `buildspec.json` + `cmake/common/buildspec_*.cmake` on first configure.

The compiled DLL ends up in `build_x64/RelWithDebInfo/obs-scene-multistream.dll`. Copy to:
```
%ProgramFiles%\obs-studio\obs-plugins\64bit\obs-scene-multistream.dll
```
and the `data/locale/` folder to:
```
%ProgramFiles%\obs-studio\data\obs-plugins\obs-scene-multistream\
```

Or use the install target if you set `OBS_STUDIO_DIR`.

## CI (no local build needed)

Push to GitHub; the included workflows (`.github/workflows/push.yaml`) build Windows / macOS / Linux artifacts. Download from the Actions run page.

## Smoke test

1. Start a local nginx-rtmp with two apps:
   ```nginx
   rtmp {
       server {
           listen 1935;
           application twitch { live on; }
           application tiktok { live on; }
       }
   }
   ```
2. In OBS, create two scenes: `Horizontal_Twitch` (1920x1080) and `Vertical_TikTok` (1080x1920) with visually distinct content.
3. Open dock **Scene Multistream**.
4. Add destination:
   - Name: `twitch_test`, Scene: `Horizontal_Twitch`, URL: `rtmp://127.0.0.1/twitch`, Key: `test1`, Res 1920x1080
5. Add destination:
   - Name: `tiktok_test`, Scene: `Vertical_TikTok`, URL: `rtmp://127.0.0.1/tiktok`, Key: `test2`, Res 1080x1920
6. **Start All**.
7. Verify with ffprobe:
   ```sh
   ffprobe rtmp://127.0.0.1/twitch/test1
   ffprobe rtmp://127.0.0.1/tiktok/test2
   ```
   Both should be live, with different resolutions and (visually) different content when played back via ffplay.

## Known limits (v0.1)

- Stream keys stored in plain JSON (DPAPI encryption planned)
- 3+ NVENC sessions require a patched NVIDIA driver on consumer GPUs (NVIDIA limits non-quadro to 3 concurrent encodes). Mix NVENC + x264 if you hit this
- Audio is the global OBS audio bus, not per-destination
- No reconnect logic; if the RTMP server drops you must restart manually
- No support for `whip` / `srt` yet — only `rtmp_custom`

## Roadmap

- v0.2: persistence polish, DPAPI for keys, reconnect, drag-reorder
- Integration with companion plugin `obs-stream-sync` (OAuth title/category/thumbnail across Twitch/YT/Kick)

## License

GPL-2.0
