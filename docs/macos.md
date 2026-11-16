# macOS

Apple Silicon, macOS 13+. Tested on an M1 Max running macOS 27.0.1. Intel Macs and earlier macOS versions have not been tested.

## Install

Download the DMG from [Releases](https://github.com/achuille/yapfast-macos/releases/latest), open it and drag Yapfast into Applications. A ZIP is also available.

The app uses an ad hoc signature and is not notarized. A new build may ask for Keychain access again. “Always Allow” saves access for subsequent launches of that build. Distributing notarized updates requires a Developer ID certificate.

## Build

Install the Xcode command line tools (`xcode-select --install`), [Rust](https://rustup.rs) and the native build tools:

```sh
brew install cmake ninja pkgconf nasm python
scripts/build-macos
```

The Rust toolchain is pinned. Allow at least 25 GB of free space for a fresh build and tests. Builds use up to four jobs and store compiler output in `~/.local/share/yapfast-build/target`, outside system caches and synced folders. Override this with `CARGO_TARGET_DIR`.

The app, ZIP and DMG go in `target/macos`. The DMG builder installs its pinned Python dependencies in `~/.local/share/yapfast-packaging`. It creates a read-only image containing the app and an Applications shortcut.

To sign with an existing Apple identity:

```sh
YAPFAST_SIGNING_IDENTITY="Developer ID Application: Name (TEAMID)" scripts/build-macos
```

A missing identity fails the build; it does not fall back to ad hoc signing. Keep signing keys out of the repository. A self-signed certificate did not prevent Keychain prompts between builds on the test machine.

## Platform support

- Native title bar and clipboard, including attachment paste with Cmd+V. Cmd+, uses the keyboard layout’s comma, including AZERTY.
- Session credentials and the cache key use Keychain.
- New installations default to Metal. Saved renderer preferences take precedence; GPU startup failure falls back to software rendering.
- CoreAudio uses three reusable output buffers and bounds microphone capture to one second. Audio threads wait for callbacks.
- VideoToolbox decodes H.264 8-bit 4:2:0 files and Annex B streams. File playback reorders B-frames; real-time streams do not wait for reordering.
- AudioToolbox decodes AAC and removes priming samples. VP8, VP9 and AV1 use the existing decoders.
- System fonts cover color emoji, Japanese punctuation such as `・` and styled letters such as `𝘾𝙍𝐏`.
- Metal’s recyclable resource cache has a 32 MiB budget. Active textures may exceed it; this is not a cap on total app memory.
- Paused and cached off-screen emoji do not keep a render timer running. On macOS 14+, the display link pauses while the window is occluded and resumes pending drawing when exposed.
- WebRTC’s Objective-C categories are linked into the executable so audio settings can open.

## Checks

```sh
scripts/check --release --matrix
scripts/test-fake-discord    # requires Bun
scripts/check-macos-native
```

The main check covers formatting, documentation links, Clippy, workspace tests, builds and startup. The fake Discord suite checks protocol behavior locally. Native tests use AddressSanitizer and UndefinedBehaviorSanitizer for CoreAudio output, queue limits, drain, flush, restart, invalid devices, H.264 decoding and malformed input. They play silence and do not record audio.

For a packaging-only release, verify the previous release checksum before reusing its executable. Record the original release and executable hash in the new release notes. Validate the new bundle signature, DMG checksum, mounted contents and startup. This does not constitute a fresh test of the Rust source.

To measure a running app:

```sh
python3 scripts/measure-macos.py PID --seconds 20
```

Compare the same conversation, window size and scrolling history after images load, without compiling during measurement. CPU is reported as a percentage of one core; memory is the physical footprint. These measurements are not comparable to the upstream Linux figures.

## Limits

Camera, application audio capture, global voice shortcuts, menu bar support, notifications and launch at login are not ported. Real calls, microphone quality and screen sharing still need validation. H.264 10-bit, 4:2:2 and 4:4:4 are not supported by this VideoToolbox path. Idle measurements do not establish performance during calls.

Never put session tokens in source, command arguments or screenshots. The development probe accepts `store-token --stdin`.

## Releases

macOS packaging revisions use `v<app-version>-macos.<revision>`. The executable keeps its upstream version; the DMG filename carries the full release version. Existing tags and release assets are not replaced. Each release includes a DMG, ZIP, source archive and SHA-256 checksums.

Commit a changelog entry, create an annotated tag and push it. In GitHub Actions, run **macOS release** with that tag. The workflow checks, builds and publishes the DMG, ZIP, source and checksums. It fails if a release already exists. Local builds use `YAPFAST_RELEASE_TAG` for the same filenames.

See the [changelog](../CHANGELOG.md) for release history and [build documentation](building.md) for dependency patches.
