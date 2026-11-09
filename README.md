# SoftVideo

Video playback, recording, capture streaming pipelines, and raster animations for the U++ Ecosystem.

Part of the [OuluBSD](https://github.com/OuluBSD) project.

---

## Overview

`SoftVideo` provides modular video engine tools and controls decoupled from monolithic dependencies:

| Component | Package | Description |
|---|---|---|
| **Video Core** | `VideoCore` | Headless pixel buffers (`VsmImageBuffer`), frame source interfaces (`VsmFrameSource`), and streaming TCP client |
| **Video Player** | `VideoPlayer` | Live fullscreen/windowed video player, HUD diagnostic overlay (FPS/latency), and playlist viewer |
| **Video Server** | `VideoServer` | Live capture/video source server (camera, screen, image directory, synthetic patterns) |
| **Video Recorder** | `VideoRecorder` | Direct-to-disk MP4 stream recording pipeline using FFmpeg / libavcodec |
| **Raster Player** | `RasterPlayer` | Lightweight animated GIF and multi-page TIFF player control |

---

## Building & Verification

`SoftVideo` builds with [UppBuilder](https://github.com/OuluBSD/UppBuilder) across Linux, macOS, and Windows:

```bash
# Build and run verification test suite
uppbuilder tests/VideoCheck -m CLANG --bazaarcore /path/to/BazaarCore -v
./bin/VideoCheck
```

---

## Continuous Integration

`SoftVideo` maintains an automated 4-platform CI matrix:
- **Ubuntu Linux Clang** (`CLANG`)
- **Ubuntu Linux GCC** (`GCC`)
- **macOS Apple Silicon Clang** (`CLANG_macOS`)
- **Windows MSVC** (`MSVS22x64`)

---

## License

MIT License (see [LICENSE](LICENSE)).
