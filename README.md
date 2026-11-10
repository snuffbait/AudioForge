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

Run `build\Release\AudioForge.exe`. The build copies `ffmpeg.exe`, `ffprobe.exe`,
and the app assets beside the application. A successful batch plays `done.mp3`;
`done-goblin.mp3` plays instead with a 1% chance. AudioForge creates an `output`
folder beside the executable and uses it as the default destination. The MinGW
build statically links its C++ runtime libraries, so the app does not require
separate MinGW runtime DLLs.
