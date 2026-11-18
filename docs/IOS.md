# LYNX SHARE on iOS

An iOS front end for LYNX SHARE, built from the same engine as the desktop app. This is a **port in progress**,
not the desktop workspace: see [Status](#status) before you plan around it.

## Why it is not a conversion

LYNX SHARE is a C++20/JUCE desktop program (`juce_add_gui_app`) built around VST3/CLAP/AU/LV2 hosting,
ASIO/ALSA/CoreAudio drivers, a mouse-and-keyboard multi-window workspace, and desktop file dialogs. None of that
maps to a phone by transpiling or wrapping:

- there is no web UI to put in a WebView — the `website/` folder is the marketing site only;
- there is no "decompiling" step — the source is here, and it is the source that has to change;
- an `.ipa` cannot be produced on Windows; it needs a macOS toolchain (this repo builds it on a GitHub Actions
  `macos-15` runner).

What *does* port cleanly is the part that matters: `lynx_core` (749 files) is JUCE-free and already builds on
macOS/arm64. It contains the song model, the sequencer, every built-in instrument and effect, the FL Studio,
Ableton Live and FL Studio Mobile importers, and `RackRenderer`, the same block renderer the desktop app and
`tools/renderwav` use.

So the iOS app is:

```
lynx_core ──► RackState / RackRenderer ──► CoreAudio (AudioDeviceManager)
                    ▲
             src/ios (this port): touch UI, project loading, sample lookup
```

## Status

Works in the current scaffold:

- opens `.lynx`, `.flp`, `.als` and `.flm` projects (the same importers as desktop, minus third-party plugins);
- plays the selected pattern through the built-in instruments and effects;
- a touch Channel Rack: one row per channel, mute, and a 16-step sequencer that edits the pattern;
- transport (play/stop/rewind), BPM +/- and pattern switching;
- samples are found next to the project, or in `Documents/Samples`; WAV, WavPack, Ogg Vorbis and MP3 decode
  through `tools/WavReader.h`.

Not there yet (the desktop app has all of it):

- third-party plugins: VST3/CLAP/AU hosting is off; plugin channels fall back to LYNX's own engines or silence;
- the full UI: Piano Roll, Playlist, Mixer, Browser, clip launching, editors, automation lanes;
- song (Playlist) playback — the app currently loops a pattern;
- recording, audio input, MIDI keyboards, Ableton Link, online sessions, export, `.flp` export, undo, settings;
- FLAC and AIFF samples (the reader handles WAV, WavPack, Ogg Vorbis and MP3);
- "Open in…" from other apps (tapping a project in Files does not launch LYNX yet).

## Build

### GitHub Actions (no Mac needed)

Push the branch, then **Actions → iOS → Run workflow**. The job configures with
`-DCMAKE_SYSTEM_NAME=iOS`, builds the `LynxIOS` target for arm64, packages `Payload/LYNX.app` into
`LYNX-unsigned.ipa` and uploads it as the `LYNX-unsigned-ipa` artifact.

### On a Mac

```bash
./scripts/build-ios.sh            # device, writes LYNX-unsigned.ipa
./scripts/build-ios.sh simulator  # Simulator build, installs nothing
```

or by hand:

```bash
cmake -S . -B build-ios -G Xcode \
  -DCMAKE_SYSTEM_NAME=iOS -DCMAKE_OSX_SYSROOT=iphoneos \
  -DCMAKE_OSX_ARCHITECTURES=arm64 -DCMAKE_OSX_DEPLOYMENT_TARGET=15.0
cmake --build build-ios --config Release --target LynxIOS -- -sdk iphoneos
```

## Install on your device

The `.ipa` is unsigned. Sign it at install time with your own Apple ID:

1. **AltStore** (Windows or macOS): install AltServer, connect the iPhone, then *My Apps → +* and pick the `.ipa`.
2. **Sideloadly** (Windows or macOS): drop the `.ipa` in, enter your Apple ID, Start.
3. **Xcode** (macOS): *Window → Devices and Simulators → +* and pick the `.app` from `build-ios`.

A free Apple ID re-signs every 7 days (and allows 3 sideloaded apps at a time); a paid developer account lasts a
year. The device must trust the developer certificate under *Settings → General → VPN & Device Management*.

## Getting projects onto the device

The app's Documents folder is visible in the Files app (*On My iPhone → LYNX*). Two routes:

- **Best for samples:** copy the whole project folder (project + its samples) into *LYNX* over Files, iCloud
  Drive or AirDrop, then press **OPEN** and pick the project there. Samples resolve next to the project.
- **Anywhere else:** pick a project from another app's folder. iOS only grants scoped access to the picked file,
  so LYNX copies it into *LYNX/Imported* and opens that copy. Samples that lived next to the original are not
  copied — drop them into *LYNX/Samples* to have them found.

## Known limits of the port

- Only patterns loop; arrangements (Playlist) do not play end to end yet.
- Plugin channels are silent or fall back to LYNX's own instruments.
- Sample rate changes rebuild the engine snapshots on the message thread (a short glitch when switching output).

## Where the code lives

| Path | What |
| --- | --- |
| `ios/CMakeLists.txt` | the `LynxIOS` target, Info.plist keys, icons |
| `src/ios/IosMain.cpp` | `JUCEApplication`, full-screen landscape window |
| `src/ios/IosAudioEngine.*` | CoreAudio callback, snapshot publishing, transport, sample lookup |
| `src/ios/IosRootComponent.*` | transport bar, pattern bar, touch Channel Rack / step grid |
| `src/engine/ProjectFile.cpp` | reused unchanged for the native `.lynx` format |
| `.github/workflows/ios.yml` | builds the unsigned `.ipa` |

The audio thread never allocates or locks: the UI builds an immutable `RackState` on the message thread and
publishes it with an atomic pointer; replaced states are kept alive for 64 audio blocks before they are freed.

## Next steps

1. Playlist (song) playback and a Playlist view, so imported arrangements play end to end.
2. Piano Roll and Mixer views with touch gestures (pinch zoom, drag, long-press menus).
3. Live keyboard/pads via `LiveEventQueue` (already supported by `RackRenderer::process`).
4. Audio input and recording (`AudioCapture` in the core; needs the microphone permission the plist already asks for).
5. FLAC/AIFF sample decoding, then `.mid` import into the Piano Roll.
6. "Open in…" support (`CFBundleDocumentTypes` + iOS URL handling) and a Files-app project browser.

## Licence

LYNX SHARE is AGPL-3.0. That is compatible with sideloading and with publishing the source; it is **not**
compatible with distributing the app through the Apple App Store, whose terms add restrictions the AGPL forbids.
This port therefore targets sideloading only.
