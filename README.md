# AudioForge

A native C++ audio converter for Windows.

<img width="571" height="390" alt="test" src="https://github.com/user-attachments/assets/8f7702d6-25fb-47be-82ec-a8a33b5edca7" />

## Requirements

- Windows 10 or later
- Visual Studio 2022 with **Desktop development with C++**
- CMake 3.20 or later
- FFmpeg available on `PATH` when configuring and building

## Build

Install Visual Studio 2022 with **Desktop development with C++**, CMake 3.20+,
and FFmpeg. Add FFmpeg's `bin` folder to `PATH`.

Run these commands in this folder:

```powershell
cmake -S . -B build -A x64
cmake --build build --config Release
```

Run `build\Release\AudioForge.exe`. The build copies `ffmpeg.exe` and
`ffprobe.exe` beside it automatically.
