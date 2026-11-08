# TLAP

**Tenokun's Light Audio Player: a very light music player for Windows.** It plays the songs in the folder you are looking at. No library, no playlists, no shuffle. Open a song, and the songs of its folder line up and play from that one, gaplessly.

## Download

Get either one from GitHub Releases:

- **Installer** (`TLAPSetup-<version>.exe`): per-user, no administrator rights needed.
- **Portable** (`TLAP-<version>-portable.zip`): unzip anywhere and run `TLAP\TLAP.exe`. Settings are saved in `TLAP.ini` next to the exe. Keep the `visualizers` folder next to it.

TLAP is not code-signed, so Windows SmartScreen may warn you. Click "More info" > "Run anyway".

## Keys and mouse

| Input | Action |
|---|---|
| Space | Play / pause |
| Left / Right | Previous / next song (Left within 3 s of the start goes back one song; later it restarts the song) |
| Shift+Left / Right | Seek -5 s / +5 s |
| Up / Down | Volume +5% / -5% |
| M | Mute |
| R | Switch repeat: Repeat one / Repeat folder / Stop at folder end / Stop after track |
| E | Equalizer panel |
| V | Visualizer on / off |
| I | Info panel |
| C | Cover on / off |
| Ctrl+M | Mini window <-> normal window |
| Ctrl+T | Always on top |
| Ctrl+O / Ctrl+Shift+O | Open a song / open a folder |
| Ctrl+, | Settings |
| Enter | Play the selected song |
| Media keys | Play/pause, previous, next, stop (work even when TLAP is behind other windows) |
| Wheel | Seek bar: +-5 s / volume slider: +-5% / list: scroll |

You can also drop a song or a folder on the window, or start `TLAP.exe <file or folder>`. A ZIP full of songs can be dropped and played as it is. TLAP runs only one window: opening another song hands it to the running TLAP.

## Features

- **Song list** with selectable columns (Title, Artist, Album, Genre, Length, Format, Bitrate, Size, ...). Click a header to sort; the play order follows the sort.
- **Left column**: the cover on top and a small folder explorer below.
- **Info panel**, **mini window** (with an optional visualizer under it), Always on Top.
- **10-band equalizer** with presets and an automatic preamp.
- **Visualizer** drawn by an HLSL pixel shader on a pixel-art canvas (integer scaling, no smoothing). Put your own `.hlsl` in `visualizers\`; saving the file reloads it within a second. See `visualizers\README.txt`.
- **Settings (Ctrl+,)**: language (English / 日本語 / 简体中文 / 繁體中文), mini-window visualizer, open the last folder on startup, a fixed startup folder.

Volume, mute, repeat, EQ, visualizer, columns and window positions are saved automatically.

## Formats / Requirements

MP3, M4A (AAC / ALAC), AAC, FLAC, WAV, WMA, OGG (Vorbis / Opus), OGA, OPUS. Windows 10 / 11 (x64).

## Build

- Run `build.bat` with VS 2022 Build Tools installed (`build.bat clean` for a clean build). Output: `build\TLAP.exe`.
- Run `release.bat` for the distributables in `dist\` (needs Inno Setup 6).
- If you redraw the icons in `images\`, run `build.bat icons` (needs Python 3) to regenerate `res\`.

Audio is decoded by Media Foundation (MP3, M4A, AAC, FLAC, WAV, WMA), stb_vorbis (Ogg Vorbis) and opusfile (Opus), and played through WASAPI. No external DLLs.

## License

MIT. Third-party libraries (stb_vorbis, libogg, libopus, opusfile) are listed in `THIRD_PARTY_NOTICES.txt`. The icon artwork in `images\` is under the same license as TLAP.
