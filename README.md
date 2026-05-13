# maisobs

OBS Studio plugins built for Mais Esports' multi-platform broadcast workflow.

Two independent plugins in this monorepo:

## [`obs-scene-multistream/`](obs-scene-multistream/) — Plugin A

Send **a different scene to each RTMP destination** simultaneously. Each destination has its own `obs_view` + encoder + RTMP output, so you can stream a 1920×1080 horizontal layout to Twitch and a 1080×1920 vertical layout to TikTok at the same time, from the same OBS.

Unlike `obs-multi-rtmp` and Aitum Multistream (which duplicate the same program output), this plugin uses `obs_view_create` + `obs_view_add2` to fork the video pipeline per destination.

**Status:** v0.1 — alpha, code written, build/test pending.

## `obs-stream-sync/` — Plugin B *(not started)*

OAuth-based metadata sync across **Twitch**, **YouTube** and **Kick**. One click to:
- Change the stream title on all 3 platforms
- Create a YouTube broadcast (returns RTMP URL/key for Plugin A)
- Update category/game
- Upload thumbnail (YouTube only)

Loopback-redirect OAuth (RFC 8252) + DPAPI-encrypted refresh tokens on Windows.

**Status:** planned, not started.

---

## Why two plugins instead of one

Concerns are unrelated: Plugin A touches the GPU/encoder pipeline; Plugin B touches HTTP and OAuth. If one crashes, the other keeps working. They share nothing except the user's OBS install.

## Build

Each plugin is a self-contained OBS plugin based on [`obs-plugintemplate`](https://github.com/obsproject/obs-plugintemplate). See each plugin's README for build instructions. CI workflows (`.github/workflows/`) build Windows / macOS / Linux artifacts on every push.

## License

GPL-2.0 (same as OBS Studio).
