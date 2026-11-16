# Changelog

## 0.4.1-macos.1 - 2026-11-16

### Changed

- Updated to [Yapfast 0.4.1](https://github.com/yapfast/yapfast/releases/tag/v0.4.1).
- Adapted the new emoji text shaping to CoreText, including joined emoji.
- Update checks now use this port's releases. Installation remains manual.
- Bundled font, emoji and dependency licenses with the app.

### Fixed

- Key combinations recognize either Command key on macOS.

## 0.3.0-macos.3 - 2026-11-16

### Added
- Drag-to-Applications DMG, source archive and SHA-256 checksums.

### Changed
- English README, macOS documentation and permission descriptions.
- macOS releases have a dedicated publishing workflow.

### Fixed
- Fresh builds wait for WebRTC headers before compiling native code.

This release reuses the executable from 0.3.0-macos.2. App behavior is unchanged.

## 0.3.0-macos.2 - 2026-11-07

### Fixed
- Missing Japanese punctuation and styled letters.
- Cmd+, now opens settings on macOS, including AZERTY keyboards.
- Paused emoji and occluded windows no longer keep unnecessary rendering active.
- Metal retains fewer cached resources.

## 0.3.0-macos.1 - 2026-11-06

### Added
- Initial Apple Silicon port of [Yapfast](https://github.com/yapfast/yapfast).
- Metal rendering, CoreAudio, VideoToolbox, AudioToolbox and Keychain integration.
