cmake_minimum_required(VERSION 3.20)
project(AudioForge LANGUAGES CXX)

if(NOT WIN32)
    message(FATAL_ERROR "AudioForge is a native Windows desktop application.")
endif()

enable_language(RC)
configure_file(assets/audioforge.ico "${CMAKE_CURRENT_BINARY_DIR}/audioforge.ico"
    COPYONLY)
configure_file(src/audioforge.rc.in "${CMAKE_CURRENT_BINARY_DIR}/audioforge.rc"
    @ONLY)

add_executable(AudioForge WIN32 src/main.cpp
    "${CMAKE_CURRENT_BINARY_DIR}/audioforge.rc")
target_compile_features(AudioForge PRIVATE cxx_std_17)
target_compile_definitions(AudioForge PRIVATE UNICODE _UNICODE)
target_link_libraries(AudioForge PRIVATE comctl32 comdlg32 dwmapi shell32 uxtheme winmm)

find_program(FFMPEG_EXECUTABLE NAMES ffmpeg ffmpeg.exe REQUIRED)
find_program(FFPROBE_EXECUTABLE NAMES ffprobe ffprobe.exe REQUIRED)
add_custom_command(TARGET AudioForge POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${FFMPEG_EXECUTABLE}" "$<TARGET_FILE_DIR:AudioForge>/ffmpeg.exe"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${FFPROBE_EXECUTABLE}" "$<TARGET_FILE_DIR:AudioForge>/ffprobe.exe"
    COMMENT "Copying FFmpeg tools beside AudioForge")

add_custom_command(TARGET AudioForge POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_CURRENT_SOURCE_DIR}/assets" "$<TARGET_FILE_DIR:AudioForge>/assets"
    COMMENT "Copying AudioForge assets beside the application")

if(MSVC)
    target_compile_options(AudioForge PRIVATE /W4 /permissive- /utf-8)
else()
    target_compile_options(AudioForge PRIVATE -Wall -Wextra -Wpedantic -municode)
    target_link_options(AudioForge PRIVATE -municode -static -static-libgcc
        -static-libstdc++)
endif()
