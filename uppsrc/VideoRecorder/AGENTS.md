# VideoRecorder Agent Notes

Scope: this file applies to `uppsrc/VideoRecorder`.

## Purpose

Record a running `VideoServer` stream into reusable regression video files and
sidecar manifests.

## Build

- Use `bin\build.exe -m MSVS22x64 .\uppsrc\VideoRecorder\VideoRecorder.upp`.
- Do not use `script/build.py`.

## Notes

- The recorder writes MP4 directly through FFmpeg/libavcodec/libavformat.
- Do not reintroduce the old `JPEG frame sequence -> ffmpeg.exe -> mp4`
  default path. Per-frame image dumps are allowed only behind explicit
  diagnostics flags.
- Keep stdout diagnostics explicit; long captures must show progress.
- Source files include `VideoRecorder.h` first.
