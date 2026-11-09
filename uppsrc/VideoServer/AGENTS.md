# VideoServer Agent Notes

Scope: this file applies to `uppsrc/VideoServer`.

## Purpose

`VideoServer` is a local capture/video source server used by VisualStateModel
work. It can serve camera, screen, image, image-directory, and synthetic red
frame sources.

## Build

- Use `bin\build.exe -m MSVS22x64 .\uppsrc\VideoServer\VideoServer.upp`.
- Do not use `script/build.py`.
- Do not edit `bin/build.exe`, `bin/dbg.exe`, `stdsrc/build`, or `uppsrc/dbg`
  while working on this package.

## Verification

- Prefer non-hardware smoke tests first:
  - `bin\VideoServer.exe --help`
  - `bin\VideoServer.exe --source red --self-test`
- Hardware/capture-card tests may fail when the device is unavailable; report
  those failures explicitly instead of masking them.

## Notes

- The initial package import preserves the previous single-file implementation.
- Split source files only when there is a concrete refactor need and keep the
  package include policy intact.

