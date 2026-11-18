# AudioForge

A Windows audio converter written in C++.

<img width="573" height="391" alt="gdfgfgdfgd" src="https://github.com/user-attachments/assets/fe8bcc50-9d08-4f37-8461-adcfec7096e1" />

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

WARNING BY COMPILING THIS SOFTWARE YOU ARE AGREEING THAT A GOBLIN MIGHT BE FORCED INTO CONVERTING ALL YOUR AUDIO FILES BE WARNED USE AT YOUR OWN RISK 
