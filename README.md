# AudioForge

A Windows audio converter written in C++.

<img width="575" height="391" alt="fdfgdgdgdgf" src="https://github.com/user-attachments/assets/1a52832a-bfc3-4b2c-8587-d8deae48bdad" />

## Requirements

- Visual Studio 2022 with the **Desktop development with C++** workload
- CMake 3.20 or newer
- FFmpeg on `PATH`

## Build

Run these commands from the project folder:

```powershell
cmake -S . -B build -A x64
cmake --build build --config Release
```

Run `build\Release\AudioForge.exe`. The build copies `ffmpeg.exe` and
`ffprobe.exe` beside the application.
