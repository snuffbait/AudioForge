# AudioForge

A native C++ audio converter for Windows.

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
