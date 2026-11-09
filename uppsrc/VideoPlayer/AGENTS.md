# VideoPlayer — live VideoServer viewer window

## Package Map
- [VideoPlayer.h](file:///C:/Users/sblo/Dev/ai-upp/uppsrc/VideoPlayer/VideoPlayer.h) — package main header.
- [main.cpp](file:///C:/Users/sblo/Dev/ai-upp/uppsrc/VideoPlayer/main.cpp) — implementation of `VideoPlayerWindow` and entry point.

## Build and Run
- Build: `bin\build.exe -m MSVS22x64 .\uppsrc\VideoPlayer\VideoPlayer.upp`
- Run: `bin\VideoPlayer.exe --host 127.0.0.1 --port 8082`

## Implementation Notes
- Connecting and stream polling are done using `VsmVideoServerFrameSource` from `VisualStateModel`.
- A background thread polls frames continuously using `ReadFrame()` (with a bounded timeout).
- Decoded frames are converted from `VsmImageBuffer` to U++ `Image` in the background thread.
- Thread-safe updates to the GUI thread are posted using `PostCallback`.
- Aspect-ratio preserving scaling is done natively in `Paint` using `Draw::DrawImage`.
- A minimalist HUD overlays the bottom of the window, offering reconnection options and diagnostic info (FPS, Latency).
- Keyboard shortcuts: `Escape` or `q`/`Q` closes the window.
- Mouse interaction: Left click and drag to move the frameless window. Right click to show a context menu.
