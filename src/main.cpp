#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <windowsx.h>
#include <commctrl.h>
#include <commdlg.h>
#include <dwmapi.h>
#include <mmsystem.h>
#include <shellapi.h>
#include <shlobj.h>
#include <uxtheme.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cwctype>
#include <cstdlib>
#include <deque>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iterator>
#include <limits>
#include <map>
#include <memory>
#include <mutex>
#include <random>
#include <set>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#ifdef _MSC_VER
#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "comdlg32.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "winmm.lib")
#endif

namespace fs = std::filesystem;

namespace {

constexpr wchar_t kClassName[] = L"AudioForgeWindow";
constexpr UINT kTaskUpdateMessage = WM_APP + 1;
constexpr UINT kConversionCompleteMessage = WM_APP + 2;
constexpr UINT kArtworkReadyMessage = WM_APP + 4;
constexpr UINT kMetadataReadyMessage = WM_APP + 5;
constexpr int kFooterHeight = 44;
constexpr WORD kApplicationIconResource = 101;

enum ControlId : int {
    IdAddFiles = 100,
    IdAddFolder,
    IdRemove,
    IdClear,
    IdBrowseOutput,
    IdFormat,
    IdQuality,
    IdWorkers,
    IdOutput,
    IdConvert,
    IdStop,
};

struct AudioFormat {
    const wchar_t* name;
    const wchar_t* extension;
};

struct EncoderProfile {
    const wchar_t* name;
    const wchar_t* codec;
    const wchar_t* arguments;
};

constexpr AudioFormat kFormats[] = {
    {L"MP3", L"mp3"},
    {L"AAC / M4A", L"m4a"},
    {L"FLAC", L"flac"},
    {L"WAV", L"wav"},
    {L"OGG Vorbis", L"ogg"},
    {L"Opus", L"opus"},
};

constexpr EncoderProfile kMp3Profiles[] = {
    {L"320 kbps CBR", L"libmp3lame", L"-b:a 320k"},
    {L"256 kbps CBR", L"libmp3lame", L"-b:a 256k"},
    {L"224 kbps CBR", L"libmp3lame", L"-b:a 224k"},
    {L"192 kbps CBR", L"libmp3lame", L"-b:a 192k"},
    {L"160 kbps CBR", L"libmp3lame", L"-b:a 160k"},
    {L"128 kbps CBR", L"libmp3lame", L"-b:a 128k"},
    {L"96 kbps CBR", L"libmp3lame", L"-b:a 96k"},
    {L"64 kbps CBR", L"libmp3lame", L"-b:a 64k"},
    {L"VBR V0 (best)", L"libmp3lame", L"-q:a 0"},
    {L"VBR V1", L"libmp3lame", L"-q:a 1"},
    {L"VBR V2", L"libmp3lame", L"-q:a 2"},
    {L"VBR V3", L"libmp3lame", L"-q:a 3"},
    {L"VBR V4", L"libmp3lame", L"-q:a 4"},
    {L"VBR V5 (smallest)", L"libmp3lame", L"-q:a 5"},
};

constexpr EncoderProfile kAacProfiles[] = {
    {L"256 kbps", L"aac", L"-b:a 256k"},
    {L"224 kbps", L"aac", L"-b:a 224k"},
    {L"192 kbps", L"aac", L"-b:a 192k"},
    {L"160 kbps", L"aac", L"-b:a 160k"},
    {L"128 kbps", L"aac", L"-b:a 128k"},
    {L"96 kbps", L"aac", L"-b:a 96k"},
    {L"64 kbps", L"aac", L"-b:a 64k"},
};

constexpr EncoderProfile kFlacProfiles[] = {
    {L"Compression 0 (fastest)", L"flac", L"-compression_level 0"},
    {L"Compression 1", L"flac", L"-compression_level 1"},
    {L"Compression 2", L"flac", L"-compression_level 2"},
    {L"Compression 3", L"flac", L"-compression_level 3"},
    {L"Compression 4", L"flac", L"-compression_level 4"},
    {L"Compression 5", L"flac", L"-compression_level 5"},
    {L"Compression 6", L"flac", L"-compression_level 6"},
    {L"Compression 7", L"flac", L"-compression_level 7"},
    {L"Compression 8 (smallest)", L"flac", L"-compression_level 8"},
};

constexpr EncoderProfile kWavProfiles[] = {
    {L"8-bit PCM", L"pcm_u8", L""},
    {L"16-bit PCM", L"pcm_s16le", L""},
    {L"24-bit PCM", L"pcm_s24le", L""},
    {L"32-bit PCM", L"pcm_s32le", L""},
    {L"32-bit float", L"pcm_f32le", L""},
};

constexpr EncoderProfile kVorbisProfiles[] = {
    {L"Quality 10 (best)", L"libvorbis", L"-q:a 10"},
    {L"Quality 8", L"libvorbis", L"-q:a 8"},
    {L"Quality 6", L"libvorbis", L"-q:a 6"},
    {L"Quality 4", L"libvorbis", L"-q:a 4"},
    {L"Quality 2", L"libvorbis", L"-q:a 2"},
    {L"Quality 0 (smallest)", L"libvorbis", L"-q:a 0"},
};

constexpr EncoderProfile kOpusProfiles[] = {
    {L"256 kbps", L"libopus", L"-b:a 256k"},
    {L"224 kbps", L"libopus", L"-b:a 224k"},
    {L"192 kbps", L"libopus", L"-b:a 192k"},
    {L"160 kbps", L"libopus", L"-b:a 160k"},
    {L"128 kbps", L"libopus", L"-b:a 128k"},
    {L"96 kbps", L"libopus", L"-b:a 96k"},
    {L"64 kbps", L"libopus", L"-b:a 64k"},
};

const EncoderProfile* encoderProfiles(int format, size_t& count) {
    switch (format) {
    case 0: count = std::size(kMp3Profiles); return kMp3Profiles;
    case 1: count = std::size(kAacProfiles); return kAacProfiles;
    case 2: count = std::size(kFlacProfiles); return kFlacProfiles;
    case 3: count = std::size(kWavProfiles); return kWavProfiles;
    case 4: count = std::size(kVorbisProfiles); return kVorbisProfiles;
    case 5: count = std::size(kOpusProfiles); return kOpusProfiles;
    default: count = 0; return nullptr;
    }
}

int defaultEncoderProfile(int format) {
    switch (format) {
    case 2: return 5;
    case 3: return 1;
    case 4: return 1;
    case 5: return 2;
    default: return 0;
    }
}

struct Track {
    fs::path path;
    fs::path sourceFolder;
    std::wstring status = L"Ready";
    std::wstring displayArtist;
    std::wstring displayTitle;
    std::wstring displayDuration;
    int imageIndex = 0;
};

struct ArtworkRequest {
    fs::path source;
};

struct ArtworkUpdate {
    fs::path source;
    fs::path thumbnail;
    fs::path temporaryDirectory;
};

struct MetadataRequest {
    fs::path source;
};

struct MetadataUpdate {
    fs::path source;
    std::wstring artist;
    std::wstring title;
    std::wstring duration;
    std::wstring error;
};

struct WorkerTask {
    size_t index;
    fs::path input;
    fs::path output;
    int format;
    int profile;
};

struct TaskUpdate {
    size_t index;
    std::wstring status;
};

HWND g_window = nullptr;
HWND g_list = nullptr;
HIMAGELIST g_rowImages = nullptr;
HWND g_format = nullptr;
HWND g_quality = nullptr;
HWND g_workers = nullptr;
HWND g_output = nullptr;
fs::path g_defaultOutputFolder;
HWND g_emptyLabel = nullptr;
HWND g_statusLabel = nullptr;
HWND g_progress = nullptr;
HWND g_buttons[7]{};
HFONT g_font = nullptr;
std::vector<Track> g_tracks;
std::vector<std::thread> g_workerThreads;
std::thread g_artworkThread;
std::thread g_metadataThread;
std::mutex g_artworkMutex;
std::condition_variable g_artworkCondition;
std::deque<ArtworkRequest> g_artworkQueue;
bool g_artworkStop = false;
std::mutex g_metadataMutex;
std::condition_variable g_metadataCondition;
std::deque<MetadataRequest> g_metadataQueue;
bool g_metadataStop = false;
std::atomic_size_t g_nextTask{0};
std::atomic_size_t g_finishedWorkers{0};
std::atomic_bool g_cancel{false};
std::mutex g_activeProcessMutex;
std::vector<HANDLE> g_activeProcesses;
bool g_isConverting = false;
bool g_completionSoundOpen = false;
int g_completedCount = 0;
int g_failedCount = 0;
int g_cancelledCount = 0;
constexpr bool g_darkTheme = true;
std::atomic_uint64_t g_temporarySequence{0};

COLORREF kBackground = RGB(20, 20, 20);
COLORREF kPanel = RGB(39, 39, 39);
COLORREF kBorder = RGB(61, 61, 61);
COLORREF kText = RGB(238, 238, 238);
COLORREF kMuted = RGB(157, 157, 157);
COLORREF kAccent = RGB(39, 130, 218);
COLORREF kSelection = RGB(45, 93, 141);
HBRUSH g_themePanelBrush = nullptr;
HBRUSH g_themeBackgroundBrush = nullptr;

LRESULT CALLBACK darkComboProc(HWND combo, UINT message, WPARAM wParam, LPARAM lParam,
                               UINT_PTR subclassId, DWORD_PTR referenceData);
LRESULT CALLBACK darkHeaderProc(HWND header, UINT message, WPARAM wParam, LPARAM lParam,
                                UINT_PTR subclassId, DWORD_PTR referenceData);
void drawComboItem(const DRAWITEMSTRUCT* item);
void drawButton(const DRAWITEMSTRUCT* item);

void setDarkTitleBar(HWND window) {
    constexpr DWORD kUseImmersiveDarkMode = 20;
    constexpr DWORD kCaptionColor = 35;
    constexpr DWORD kTextColor = 36;
    const BOOL enabled = g_darkTheme;
    const COLORREF caption = g_darkTheme ? RGB(20, 20, 20) : RGB(245, 245, 245);
    const COLORREF text = g_darkTheme ? RGB(238, 238, 238) : RGB(35, 35, 35);
    DwmSetWindowAttribute(window, kUseImmersiveDarkMode, &enabled, sizeof(enabled));
    DwmSetWindowAttribute(window, kCaptionColor, &caption, sizeof(caption));
    DwmSetWindowAttribute(window, kTextColor, &text, sizeof(text));
}

std::wstring lower(std::wstring text) {
    std::transform(text.begin(), text.end(), text.begin(),
        [](wchar_t ch) { return static_cast<wchar_t>(std::towlower(ch)); });
    return text;
}

bool supportsAttachedPictureStream(int format) {
    return format == 0 || format == 1 || format == 2;
}

bool isAudioPath(const fs::path& path) {
    static const std::vector<std::wstring> audioExtensions = {
        L".mp3", L".wav", L".flac", L".m4a", L".aac", L".ogg", L".opus",
        L".wma", L".aiff", L".aif", L".ape", L".alac", L".ac3", L".dsf",
        L".dff", L".mp4", L".webm", L".mka", L".mp2", L".mpc", L".m4b",
        L".mkv", L".mov", L".avi", L".m4v", L".mpeg", L".mpg"
    };
    const std::wstring extension = lower(path.extension().wstring());
    return std::find(audioExtensions.begin(), audioExtensions.end(), extension) !=
        audioExtensions.end();
}

void updateQualityChoices() {
    const int format = static_cast<int>(SendMessageW(g_format, CB_GETCURSEL, 0, 0));
    SendMessageW(g_quality, CB_RESETCONTENT, 0, 0);
    size_t count = 0;
    const EncoderProfile* profiles = encoderProfiles(format, count);
    for (size_t i = 0; i < count; ++i) {
        SendMessageW(g_quality, CB_ADDSTRING, 0,
                     reinterpret_cast<LPARAM>(profiles[i].name));
    }
    const int selected = count == 0 ? CB_ERR : defaultEncoderProfile(format);
    SendMessageW(g_quality, CB_SETCURSEL, selected, 0);
    EnableWindow(g_quality, count != 0);
    InvalidateRect(g_quality, nullptr, TRUE);
}

std::wstring formatSize(const fs::path& path) {
    std::error_code error;
    const auto bytes = fs::file_size(path, error);
    if (error) return L"--";
    const double megabytes = static_cast<double>(bytes) / (1024.0 * 1024.0);
    std::wostringstream text;
    if (megabytes >= 1024.0) {
        text << std::fixed << std::setprecision(1) << megabytes / 1024.0 << L" GB";
    } else if (megabytes >= 1.0) {
        text << std::fixed << std::setprecision(1) << megabytes << L" MB";
    } else {
        text << std::fixed << std::setprecision(0)
             << static_cast<double>(bytes) / 1024.0 << L" KB";
    }
    return text.str();
}

std::wstring quoteArgument(const std::wstring& argument) {
    std::wstring quoted = L"\"";
    size_t slashes = 0;
    for (wchar_t ch : argument) {
        if (ch == L'\\') {
            ++slashes;
        } else if (ch == L'"') {
            quoted.append(slashes * 2 + 1, L'\\');
            quoted += ch;
            slashes = 0;
        } else {
            quoted.append(slashes, L'\\');
            slashes = 0;
            quoted += ch;
        }
    }
    quoted.append(slashes * 2, L'\\');
    quoted += L'"';
    return quoted;
}

std::wstring findFfmpeg() {
    std::wstring modulePath(32768, L'\0');
    const DWORD length = GetModuleFileNameW(nullptr, modulePath.data(),
        static_cast<DWORD>(modulePath.size()));
    if (length > 0 && length < modulePath.size()) {
        modulePath.resize(length);
        const fs::path besideApp = fs::path(modulePath).parent_path() / L"ffmpeg.exe";
        std::error_code error;
        if (fs::is_regular_file(besideApp, error)) return besideApp.wstring();
    }
    std::wstring found(32768, L'\0');
    const DWORD foundLength = SearchPathW(nullptr, L"ffmpeg.exe", nullptr,
        static_cast<DWORD>(found.size()), found.data(), nullptr);
    if (foundLength > 0 && foundLength < found.size()) {
        found.resize(foundLength);
        return found;
    }
    return L"";
}

std::wstring findFfprobe() {
    std::wstring modulePath(32768, L'\0');
    const DWORD length = GetModuleFileNameW(nullptr, modulePath.data(),
        static_cast<DWORD>(modulePath.size()));
    if (length > 0 && length < modulePath.size()) {
        modulePath.resize(length);
        const fs::path besideApp = fs::path(modulePath).parent_path() / L"ffprobe.exe";
        std::error_code error;
        if (fs::is_regular_file(besideApp, error)) return besideApp.wstring();
    }
    const std::wstring ffmpeg = findFfmpeg();
    if (!ffmpeg.empty()) {
        const fs::path besideFfmpeg = fs::path(ffmpeg).parent_path() / L"ffprobe.exe";
        std::error_code error;
        if (fs::is_regular_file(besideFfmpeg, error)) return besideFfmpeg.wstring();
    }
    std::wstring found(32768, L'\0');
    const DWORD foundLength = SearchPathW(nullptr, L"ffprobe.exe", nullptr,
        static_cast<DWORD>(found.size()), found.data(), nullptr);
    if (foundLength > 0 && foundLength < found.size()) {
        found.resize(foundLength);
        return found;
    }
    return L"";
}

std::wstring makeOutputName(const fs::path& input, const fs::path& outputFolder,
                            int format, std::set<std::wstring>& reservedNames) {
    const fs::path& folder = outputFolder;
    const std::wstring stem = input.stem().wstring();
    const std::wstring extension = L"." + std::wstring(kFormats[format].extension);
    fs::path candidate = folder / (stem + extension);
    if (lower(candidate.wstring()) == lower(input.wstring()) || fs::exists(candidate) ||
        reservedNames.count(lower(candidate.wstring())) != 0) {
        candidate = folder / (stem + L" (2)" + extension);
    }
    unsigned suffix = 3;
    while (lower(candidate.wstring()) == lower(input.wstring()) || fs::exists(candidate) ||
           reservedNames.count(lower(candidate.wstring())) != 0) {
        candidate = folder / (stem + L" (" + std::to_wstring(suffix++) + L")" + extension);
    }
    reservedNames.insert(lower(candidate.wstring()));
    return candidate.wstring();
}

void postTaskUpdate(size_t index, std::wstring status) {
    auto* update = new TaskUpdate{index, std::move(status)};
    if (!PostMessageW(g_window, kTaskUpdateMessage, 0,
                      reinterpret_cast<LPARAM>(update))) {
        delete update;
    }
}

struct AuxiliaryProcessResult {
    bool started = false;
    bool cancelled = false;
    DWORD exitCode = 1;
    std::string output;
};

AuxiliaryProcessResult runAuxiliaryProcess(const std::wstring& executable,
                                           std::wstring command,
                                           bool captureOutput,
                                           bool honorConversionCancel = true) {
    AuxiliaryProcessResult result;
    SECURITY_ATTRIBUTES security{sizeof(SECURITY_ATTRIBUTES), nullptr, TRUE};
    HANDLE readPipe = nullptr;
    HANDLE writePipe = nullptr;
    if (!CreatePipe(&readPipe, &writePipe, &security, 0)) return result;
    SetHandleInformation(readPipe, HANDLE_FLAG_INHERIT, 0);

    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESTDHANDLES;
    startup.hStdOutput = writePipe;
    startup.hStdError = writePipe;
    startup.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
    PROCESS_INFORMATION process{};
    const BOOL created = CreateProcessW(executable.c_str(), command.data(), nullptr,
        nullptr, TRUE, CREATE_NO_WINDOW, nullptr, nullptr, &startup, &process);
    CloseHandle(writePipe);
    if (!created) {
        CloseHandle(readPipe);
        return result;
    }

    result.started = true;
    char buffer[4096];
    auto drainOutput = [&]() {
        DWORD available = 0;
        while (PeekNamedPipe(readPipe, nullptr, 0, nullptr, &available, nullptr) &&
               available > 0) {
            DWORD bytesRead = 0;
            const DWORD request = (std::min)(available, static_cast<DWORD>(sizeof(buffer)));
            if (!ReadFile(readPipe, buffer, request, &bytesRead, nullptr) || bytesRead == 0) {
                break;
            }
            if (captureOutput && result.output.size() < 1024 * 1024) {
                const size_t remaining = 1024 * 1024 - result.output.size();
                result.output.append(buffer,
                    (std::min)(static_cast<size_t>(bytesRead), remaining));
            }
        }
    };
    for (;;) {
        if (honorConversionCancel && g_cancel.load()) {
            result.cancelled = true;
            TerminateProcess(process.hProcess, 1);
        }
        drainOutput();
        if (WaitForSingleObject(process.hProcess, 100) == WAIT_OBJECT_0) {
            drainOutput();
            break;
        }
    }

    GetExitCodeProcess(process.hProcess, &result.exitCode);
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    CloseHandle(readPipe);
    return result;
}

std::wstring decodeUtf8(const std::string& value) {
    if (value.empty()) return {};
    const int length = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
        value.data(), static_cast<int>(value.size()), nullptr, 0);
    if (length <= 0) {
        const int fallbackLength = MultiByteToWideChar(CP_ACP, 0, value.data(),
            static_cast<int>(value.size()), nullptr, 0);
        std::wstring result(static_cast<size_t>(fallbackLength), L'\0');
        if (fallbackLength > 0) {
            MultiByteToWideChar(CP_ACP, 0, value.data(), static_cast<int>(value.size()),
                result.data(), fallbackLength);
        }
        return result;
    }
    std::wstring result(static_cast<size_t>(length), L'\0');
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(),
        static_cast<int>(value.size()), result.data(), length);
    return result;
}

std::wstring formatDuration(const std::string& value) {
    std::string text = value;
    if (text.compare(0, 3, "\xEF\xBB\xBF") == 0) text.erase(0, 3);
    const size_t equals = text.find('=');
    if (equals != std::string::npos) {
        text.erase(0, equals + 1);
    }
    const size_t begin = text.find_first_not_of(" \t\r\n\"");
    if (begin == std::string::npos) return {};
    const size_t end = text.find_last_not_of(" \t\r\n\"}");
    if (end == std::string::npos || end < begin) return {};
    const std::string number = text.substr(begin, end - begin + 1);
    char* parsedEnd = nullptr;
    const double seconds = std::strtod(number.c_str(), &parsedEnd);
    if (parsedEnd == number.c_str() || !std::isfinite(seconds) || seconds < 0.0 ||
        seconds >= static_cast<double>((std::numeric_limits<unsigned long long>::max)())) {
        return {};
    }
    while (*parsedEnd == ' ' || *parsedEnd == '\t' || *parsedEnd == '\r' ||
           *parsedEnd == '\n') {
        ++parsedEnd;
    }
    if (*parsedEnd != '\0') return {};
    const auto total = static_cast<unsigned long long>(seconds + 0.5);
    const auto hours = total / 3600;
    const auto minutes = (total / 60) % 60;
    const auto remainingSeconds = total % 60;
    std::wostringstream formatted;
    if (hours > 0) {
        formatted << hours << L':' << std::setfill(L'0') << std::setw(2) << minutes;
    } else {
        formatted << total / 60;
    }
    formatted << L':' << std::setfill(L'0') << std::setw(2) << remainingSeconds;
    return formatted.str();
}

std::wstring probeDuration(const std::wstring& ffprobe, const fs::path& source,
                           std::wstring& error) {
    if (ffprobe.empty()) {
        error = L"FFprobe not found";
        return {};
    }
    const std::wstring input = quoteArgument(source.wstring());
    const std::wstring queries[] = {
        quoteArgument(ffprobe) +
            L" -v error -show_entries format=duration "
            L"-of default=noprint_wrappers=1:nokey=1 " + input,
        quoteArgument(ffprobe) +
            L" -v error -select_streams a:0 -show_entries stream=duration "
            L"-of default=noprint_wrappers=1:nokey=1 " + input
    };
    bool started = false;
    bool nonzeroExit = false;
    for (const auto& command : queries) {
        const AuxiliaryProcessResult result =
            runAuxiliaryProcess(ffprobe, command, true, false);
        if (!result.started) continue;
        started = true;
        if (result.exitCode != 0) {
            nonzeroExit = true;
            continue;
        }
        std::istringstream lines(result.output);
        std::string line;
        while (std::getline(lines, line)) {
            const std::wstring duration = formatDuration(line);
            if (!duration.empty()) return duration;
        }
    }
    error = !started ? L"FFprobe start failed"
        : nonzeroExit ? L"FFprobe query failed"
                      : L"No duration reported";
    return {};
}

std::wstring inferArtistFromPath(const fs::path& path) {
    auto artistFromFolder = [](std::wstring folder) {
        const std::wstring converted = L" (converted)";
        if (folder.size() >= converted.size() &&
            lower(folder.substr(folder.size() - converted.size())) == converted) {
            folder.erase(folder.size() - converted.size());
        }
        const size_t separator = folder.find(L" - ");
        if (separator == std::wstring::npos || separator == 0) return std::wstring{};
        return folder.substr(0, separator);
    };

    std::wstring artist = artistFromFolder(path.parent_path().filename().wstring());
    if (artist.empty()) artist = artistFromFolder(path.stem().wstring());
    if (!artist.empty() && std::all_of(artist.begin(), artist.end(),
        [](wchar_t ch) { return std::iswdigit(ch) || std::iswspace(ch); })) {
        return {};
    }
    return artist;
}

void parseMetadataOutput(const std::string& output, std::wstring& artist,
                         std::wstring& title) {
    std::wstring albumArtist;
    std::istringstream lines(output);
    std::string line;
    while (std::getline(lines, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        const size_t separator = line.find('=');
        if (separator == std::string::npos) continue;
        std::wstring key = lower(decodeUtf8(line.substr(0, separator)));
        if (key.rfind(L"tag:", 0) == 0) key.erase(0, 4);
        std::replace(key.begin(), key.end(), L' ', L'_');
        const std::wstring value = decodeUtf8(line.substr(separator + 1));
        if ((key == L"artist" || key == L"author" || key == L"performer") &&
            !value.empty() && artist.empty()) {
            artist = value;
        } else if ((key == L"album_artist" || key == L"albumartist" ||
                    key == L"wm/album_artist" || key == L"wm/albumartist") &&
                   !value.empty() && albumArtist.empty()) {
            albumArtist = value;
        } else if (key == L"title" && !value.empty()) {
            title = value;
        }
    }
    if (artist.empty()) artist = std::move(albumArtist);
}

void appendBigEndian(std::vector<unsigned char>& output, uint32_t value) {
    output.push_back(static_cast<unsigned char>((value >> 24) & 0xFF));
    output.push_back(static_cast<unsigned char>((value >> 16) & 0xFF));
    output.push_back(static_cast<unsigned char>((value >> 8) & 0xFF));
    output.push_back(static_cast<unsigned char>(value & 0xFF));
}

uint32_t readBigEndian(const std::vector<unsigned char>& input, size_t offset) {
    return (static_cast<uint32_t>(input[offset]) << 24) |
           (static_cast<uint32_t>(input[offset + 1]) << 16) |
           (static_cast<uint32_t>(input[offset + 2]) << 8) |
           static_cast<uint32_t>(input[offset + 3]);
}

std::string base64Encode(const std::vector<unsigned char>& input) {
    static constexpr char alphabet[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string output;
    output.reserve(((input.size() + 2) / 3) * 4);
    for (size_t i = 0; i < input.size(); i += 3) {
        const uint32_t first = input[i];
        const uint32_t second = i + 1 < input.size() ? input[i + 1] : 0;
        const uint32_t third = i + 2 < input.size() ? input[i + 2] : 0;
        const uint32_t value = (first << 16) | (second << 8) | third;
        output.push_back(alphabet[(value >> 18) & 63]);
        output.push_back(alphabet[(value >> 12) & 63]);
        output.push_back(i + 1 < input.size() ? alphabet[(value >> 6) & 63] : '=');
        output.push_back(i + 2 < input.size() ? alphabet[value & 63] : '=');
    }
    return output;
}

bool createOpusPictureMetadata(const fs::path& pngPath, const fs::path& metadataPath) {
    std::ifstream image(pngPath, std::ios::binary);
    if (!image) return false;
    std::vector<unsigned char> png((std::istreambuf_iterator<char>(image)),
                                   std::istreambuf_iterator<char>());
    constexpr unsigned char pngSignature[] = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
    if (png.size() < 29 ||
        !std::equal(std::begin(pngSignature), std::end(pngSignature), png.begin()) ||
        std::string(png.begin() + 12, png.begin() + 16) != "IHDR") {
        return false;
    }
    if (png.size() > UINT32_MAX) return false;

    const uint32_t width = readBigEndian(png, 16);
    const uint32_t height = readBigEndian(png, 20);
    const unsigned char bitDepth = png[24];
    const unsigned char colorType = png[25];
    uint32_t channels = 0;
    switch (colorType) {
    case 0: channels = 1; break;
    case 2: channels = 3; break;
    case 3: channels = 1; break;
    case 4: channels = 2; break;
    case 6: channels = 4; break;
    default: return false;
    }

    std::vector<unsigned char> picture;
    constexpr char mime[] = "image/png";
    appendBigEndian(picture, 3);
    appendBigEndian(picture, sizeof(mime) - 1);
    picture.insert(picture.end(), mime, mime + sizeof(mime) - 1);
    appendBigEndian(picture, 0);
    appendBigEndian(picture, width);
    appendBigEndian(picture, height);
    appendBigEndian(picture, static_cast<uint32_t>(bitDepth) * channels);
    appendBigEndian(picture, 0);
    appendBigEndian(picture, static_cast<uint32_t>(png.size()));
    picture.insert(picture.end(), png.begin(), png.end());

    std::ofstream metadata(metadataPath, std::ios::binary | std::ios::trunc);
    if (!metadata) return false;
    metadata << ";FFMETADATA1\nMETADATA_BLOCK_PICTURE="
             << base64Encode(picture) << '\n';
    metadata.flush();
    return metadata.good();
}

enum class ArtworkResult {
    None,
    Ready,
    Failed,
    Cancelled
};

struct TemporaryDirectory {
    fs::path path;

    ~TemporaryDirectory() {
        if (!path.empty()) {
            std::error_code error;
            fs::remove_all(path, error);
        }
    }
};

bool createTemporaryDirectory(size_t taskIndex, fs::path& directory) {
    std::wstring base(32768, L'\0');
    const DWORD length = GetTempPathW(static_cast<DWORD>(base.size()), base.data());
    if (length == 0 || length >= base.size()) return false;
    base.resize(length);

    for (unsigned attempt = 0; attempt < 32; ++attempt) {
        directory = fs::path(base) /
            (L"AudioForge-" + std::to_wstring(GetCurrentProcessId()) + L"-" +
             std::to_wstring(taskIndex) + L"-" +
             std::to_wstring(GetTickCount64()) + L"-" +
             std::to_wstring(g_temporarySequence.fetch_add(1)));
        if (CreateDirectoryW(directory.c_str(), nullptr)) return true;
        if (GetLastError() != ERROR_ALREADY_EXISTS) return false;
    }
    return false;
}

ArtworkResult prepareOpusArtwork(const std::wstring& ffmpegPath,
                                const fs::path& inputPath,
                                const fs::path& coverPath,
                                const fs::path& metadataPath) {
    std::wstring command = quoteArgument(ffmpegPath) +
        L" -hide_banner -loglevel error -nostats -y -i " +
        quoteArgument(inputPath.wstring()) +
        L" -map 0:v:disp:attached_pic -frames:v 1 -c:v png " +
        quoteArgument(coverPath.wstring());
    const AuxiliaryProcessResult extraction =
        runAuxiliaryProcess(ffmpegPath, std::move(command), true);
    if (!extraction.started || extraction.cancelled) {
        return extraction.cancelled ? ArtworkResult::Cancelled : ArtworkResult::Failed;
    }
    if (extraction.exitCode != 0) {
        if (extraction.output.find("matches no streams") != std::string::npos) {
            return ArtworkResult::None;
        }
        return ArtworkResult::Failed;
    }
    return createOpusPictureMetadata(coverPath, metadataPath)
        ? ArtworkResult::Ready
        : ArtworkResult::Failed;
}

void runConversion(const WorkerTask& task, const std::wstring& ffmpegPath) {
    const fs::path output(task.output);
    const std::wstring temporarySuffix =
        L".AudioForge-" + std::to_wstring(GetCurrentProcessId()) + L"-" +
        std::to_wstring(GetTickCount64()) + L"-" +
        std::to_wstring(g_temporarySequence.fetch_add(1)) + L".partial";
    const fs::path temporary = output.parent_path() /
        (output.stem().wstring() + temporarySuffix + output.extension().wstring());
    std::error_code fileError;
    fs::remove(temporary, fileError);

    const bool opusWithArtwork = task.format == 5;
    bool hasArtworkMetadata = false;
    TemporaryDirectory artworkDirectory;
    if (opusWithArtwork) {
        postTaskUpdate(task.index, L"Checking embedded cover...");
        if (!createTemporaryDirectory(task.index, artworkDirectory.path)) {
            postTaskUpdate(task.index, L"Failed: could not create temporary artwork folder");
            return;
        }
        const fs::path coverPath = artworkDirectory.path / L"cover.png";
        const fs::path artworkMetadataPath = artworkDirectory.path / L"artwork.ffmeta";
        const ArtworkResult artwork =
            prepareOpusArtwork(ffmpegPath, task.input, coverPath, artworkMetadataPath);
        if (artwork == ArtworkResult::Cancelled) {
            postTaskUpdate(task.index, L"Cancelled");
            return;
        }
        if (artwork == ArtworkResult::Failed) {
            postTaskUpdate(task.index, L"Failed: could not preserve embedded cover art");
            return;
        }
        hasArtworkMetadata = artwork == ArtworkResult::Ready;
    }

    const fs::path artworkMetadataPath =
        hasArtworkMetadata ? artworkDirectory.path / L"artwork.ffmeta" : fs::path();

    SECURITY_ATTRIBUTES security{sizeof(SECURITY_ATTRIBUTES), nullptr, TRUE};
    HANDLE readPipe = nullptr;
    HANDLE writePipe = nullptr;
    if (!CreatePipe(&readPipe, &writePipe, &security, 0)) {
        postTaskUpdate(task.index, L"Failed: could not create FFmpeg output pipe");
        return;
    }
    SetHandleInformation(readPipe, HANDLE_FLAG_INHERIT, 0);

    std::wstring command = quoteArgument(ffmpegPath) +
        L" -hide_banner -loglevel error -nostats -progress pipe:1 -y -i " +
        quoteArgument(task.input.wstring());
    if (hasArtworkMetadata) {
        command += L" -f ffmetadata -i " + quoteArgument(artworkMetadataPath.wstring());
    }
    command += L" -map 0:a";
    if (supportsAttachedPictureStream(task.format)) {
        command += L" -map 0:v:disp:attached_pic? -c:v copy -disposition:v attached_pic";
    }
    command += L" -map_metadata 0 -map_metadata:s:a 0:s:a -map_chapters 0";
    if (hasArtworkMetadata) {
        command += L" -map_metadata 1";
    }
    size_t profileCount = 0;
    const EncoderProfile* profiles = encoderProfiles(task.format, profileCount);
    if (!profiles || task.profile < 0 ||
        static_cast<size_t>(task.profile) >= profileCount) {
        fs::remove(temporary, fileError);
        postTaskUpdate(task.index, L"Failed: invalid encoder profile");
        return;
    }
    const EncoderProfile& profile = profiles[task.profile];
    command += L" -c:a ";
    command += profile.codec;
    if (profile.arguments[0] != L'\0') {
        command += L" ";
        command += profile.arguments;
    }
    if (task.format == 0) command += L" -id3v2_version 3";
    command += L" -threads 1 ";
    command += quoteArgument(temporary.wstring());

    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESTDHANDLES;
    startup.hStdOutput = writePipe;
    startup.hStdError = writePipe;
    startup.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
    PROCESS_INFORMATION process{};
    const BOOL created = CreateProcessW(ffmpegPath.c_str(), command.data(), nullptr,
        nullptr, TRUE, CREATE_NO_WINDOW, nullptr, nullptr, &startup, &process);
    CloseHandle(writePipe);

    if (!created) {
        CloseHandle(readPipe);
        postTaskUpdate(task.index, L"Failed: FFmpeg could not be started");
        return;
    }

    {
        std::lock_guard<std::mutex> lock(g_activeProcessMutex);
        g_activeProcesses.push_back(process.hProcess);
    }

    postTaskUpdate(task.index, L"Converting...");
    std::string pending;
    auto lastUpdate = std::chrono::steady_clock::now();
    bool cancelled = false;
    DWORD available = 0;
    char buffer[4096];
    bool temporaryFileHidden = false;

    while (true) {
        if (g_cancel.load()) {
            cancelled = true;
            TerminateProcess(process.hProcess, 1);
        }
        if (!temporaryFileHidden) {
            const DWORD attributes = GetFileAttributesW(temporary.c_str());
            if (attributes != INVALID_FILE_ATTRIBUTES) {
                temporaryFileHidden = SetFileAttributesW(
                    temporary.c_str(), attributes | FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_TEMPORARY) != FALSE;
            }
        }

        while (PeekNamedPipe(readPipe, nullptr, 0, nullptr, &available, nullptr) &&
               available > 0) {
            DWORD bytesRead = 0;
            const DWORD request = (std::min)(available, static_cast<DWORD>(sizeof(buffer)));
            if (!ReadFile(readPipe, buffer, request, &bytesRead, nullptr) || bytesRead == 0) break;
            pending.append(buffer, bytesRead);
            size_t newline = std::string::npos;
            while ((newline = pending.find('\n')) != std::string::npos) {
                std::string line = pending.substr(0, newline);
                pending.erase(0, newline + 1);
                constexpr char key[] = "out_time_ms=";
                const size_t keyPosition = line.find(key);
                if (keyPosition != std::string::npos) {
                    try {
                        const auto microseconds = std::stoll(line.substr(keyPosition + sizeof(key) - 1));
                        const auto seconds = microseconds / 1000000;
                        const auto now = std::chrono::steady_clock::now();
                        if (now - lastUpdate >= std::chrono::seconds(1)) {
                            std::wostringstream status;
                            status << L"Converting... " << seconds / 60 << L":"
                                   << std::setw(2) << std::setfill(L'0') << seconds % 60;
                            postTaskUpdate(task.index, status.str());
                            lastUpdate = now;
                        }
                    } catch (...) {
                    }
                }
            }
        }
        if (WaitForSingleObject(process.hProcess, 100) == WAIT_OBJECT_0) break;
    }

    DWORD exitCode = 1;
    GetExitCodeProcess(process.hProcess, &exitCode);
    {
        std::lock_guard<std::mutex> lock(g_activeProcessMutex);
        const auto it = std::find(g_activeProcesses.begin(), g_activeProcesses.end(), process.hProcess);
        if (it != g_activeProcesses.end()) g_activeProcesses.erase(it);
    }
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    CloseHandle(readPipe);

    if (cancelled || g_cancel.load()) {
        fs::remove(temporary, fileError);
        postTaskUpdate(task.index, L"Cancelled");
    } else if (exitCode == 0 && fs::is_regular_file(temporary, fileError)) {
        SetFileAttributesW(temporary.c_str(), FILE_ATTRIBUTE_NORMAL);
        fs::rename(temporary, output, fileError);
        if (fileError) {
            fs::remove(temporary, fileError);
            postTaskUpdate(task.index, L"Failed: could not save output file");
        } else {
            postTaskUpdate(task.index, L"Complete");
        }
    } else {
        fs::remove(temporary, fileError);
        postTaskUpdate(task.index, L"Failed: FFmpeg could not convert this file");
    }
}

void updateTrackRow(size_t index) {
    if (index >= g_tracks.size()) return;
    const auto& track = g_tracks[index];
    const std::wstring filename = track.path.filename().wstring();
    const std::wstring extension = track.path.extension().wstring();
    LVITEMW item{};
    item.mask = LVIF_TEXT;
    item.iItem = static_cast<int>(index);
    item.iSubItem = 0;
    item.pszText = const_cast<wchar_t*>(filename.c_str());
    ListView_SetItem(g_list, &item);

    const wchar_t* artist = track.displayArtist.empty() ? L"—" : track.displayArtist.c_str();
    item.iSubItem = 1;
    item.pszText = const_cast<wchar_t*>(artist);
    ListView_SetItem(g_list, &item);
    item.iSubItem = 2;
    item.pszText = const_cast<wchar_t*>(
        track.displayDuration.empty() ? L"—" : track.displayDuration.c_str());
    ListView_SetItem(g_list, &item);
    std::wstring details = extension.empty() ? L"Audio" : extension.substr(1);
    details += L"  ·  ";
    details += formatSize(track.path);
    item.iSubItem = 3;
    item.pszText = const_cast<wchar_t*>(details.c_str());
    ListView_SetItem(g_list, &item);
    item.iSubItem = 4;
    item.pszText = const_cast<wchar_t*>(track.status.c_str());
    ListView_SetItem(g_list, &item);
}

void updateListColumns() {
    RECT rect{};
    GetClientRect(g_list, &rect);
    const int width = (std::max)(720, static_cast<int>(rect.right) - 4);
    const int fixedColumns = 145 + 80 + 170 + 145;
    const int nameWidth = (std::max)(220, width - fixedColumns);
    ListView_SetColumnWidth(g_list, 0, nameWidth);
    ListView_SetColumnWidth(g_list, 1, 145);
    ListView_SetColumnWidth(g_list, 2, 80);
    ListView_SetColumnWidth(g_list, 3, 170);
    ListView_SetColumnWidth(g_list, 4, 145);
}

void updateEmptyState() {
    ShowWindow(g_emptyLabel, g_tracks.empty() ? SW_SHOW : SW_HIDE);
    if (!g_isConverting && g_statusLabel) {
        std::wstring status = std::to_wstring(g_tracks.size());
        status += g_tracks.size() == 1 ? L" file ready for conversion" : L" files ready for conversion";
        SetWindowTextW(g_statusLabel, status.c_str());
    }
}

bool createArtworkThumbnail(const std::wstring& ffmpegPath, const fs::path& input,
                            const fs::path& output, bool attachedPicture) {
    std::wstring command = quoteArgument(ffmpegPath) +
        L" -hide_banner -loglevel error -nostats -y -i " +
        quoteArgument(input.wstring());
    command += attachedPicture
        ? L" -map 0:v:disp:attached_pic"
        : L" -map 0:v:0";
    command += L" -frames:v 1 -vf "
        L"\"scale=42:42:force_original_aspect_ratio=decrease,"
        L"pad=42:42:(ow-iw)/2:(oh-ih)/2:color=0x202020\""
        L" -an -sn -dn -threads 1 -c:v bmp " + quoteArgument(output.wstring());
    const AuxiliaryProcessResult result =
        runAuxiliaryProcess(ffmpegPath, std::move(command), true, false);
    if (!result.started || result.cancelled || result.exitCode != 0) return false;
    std::error_code error;
    return fs::is_regular_file(output, error) && fs::file_size(output, error) > 54 && !error;
}

fs::path findCompanionArtwork(const fs::path& audioPath) {
    const fs::path folder = audioPath.parent_path();
    constexpr const wchar_t* names[] = {
        L"cover", L"folder", L"front", L"albumart", L"album", L"artwork"
    };
    constexpr const wchar_t* extensions[] = {
        L".jpg", L".jpeg", L".png", L".webp", L".bmp"
    };
    std::error_code error;
    for (const auto* name : names) {
        for (const auto* extension : extensions) {
            const fs::path candidate = folder / (std::wstring(name) + extension);
            if (fs::is_regular_file(candidate, error)) return candidate;
            error.clear();
        }
    }
    return {};
}

void artworkWorkerLoop() {
    const std::wstring ffmpeg = findFfmpeg();
    for (;;) {
        ArtworkRequest request;
        {
            std::unique_lock<std::mutex> lock(g_artworkMutex);
            g_artworkCondition.wait(lock, [] {
                return g_artworkStop || !g_artworkQueue.empty();
            });
            if (g_artworkStop) return;
            request = std::move(g_artworkQueue.front());
            g_artworkQueue.pop_front();
        }

        if (ffmpeg.empty()) continue;
        fs::path temporaryDirectory;
        const uint64_t taskNumber = g_temporarySequence.fetch_add(1);
        if (!createTemporaryDirectory(static_cast<size_t>(taskNumber), temporaryDirectory)) {
            continue;
        }
        const fs::path thumbnail = temporaryDirectory / L"thumbnail.bmp";
        bool extracted = createArtworkThumbnail(ffmpeg, request.source, thumbnail, true);
        if (!extracted) {
            const fs::path companion = findCompanionArtwork(request.source);
            if (!companion.empty()) {
                extracted = createArtworkThumbnail(ffmpeg, companion, thumbnail, false);
            }
        }
        if (!extracted) {
            std::error_code error;
            fs::remove_all(temporaryDirectory, error);
            continue;
        }

        auto* update = new ArtworkUpdate{
            std::move(request.source), thumbnail, temporaryDirectory
        };
        if (!g_window || !PostMessageW(g_window, kArtworkReadyMessage, 0,
                                       reinterpret_cast<LPARAM>(update))) {
            delete update;
            std::error_code error;
            fs::remove_all(temporaryDirectory, error);
        }
    }
}

void startArtworkWorker() {
    std::lock_guard<std::mutex> lock(g_artworkMutex);
    g_artworkStop = false;
    g_artworkThread = std::thread(artworkWorkerLoop);
}

void requestArtwork(const fs::path& source) {
    {
        std::lock_guard<std::mutex> lock(g_artworkMutex);
        if (g_artworkStop || !g_artworkThread.joinable()) return;
        g_artworkQueue.push_back({source});
    }
    g_artworkCondition.notify_one();
}

void deleteArtworkUpdate(ArtworkUpdate* update) {
    if (!update) return;
    std::error_code error;
    fs::remove_all(update->temporaryDirectory, error);
    delete update;
}

void handleArtworkReady(LPARAM parameter) {
    std::unique_ptr<ArtworkUpdate, decltype(&deleteArtworkUpdate)> update(
        reinterpret_cast<ArtworkUpdate*>(parameter), deleteArtworkUpdate);
    if (!update || !g_rowImages || !g_list) return;
    const auto track = std::find_if(g_tracks.begin(), g_tracks.end(),
        [&](const Track& item) {
            return lower(item.path.lexically_normal().wstring()) ==
                    lower(update->source.lexically_normal().wstring());
        });
    if (track == g_tracks.end()) return;

    HBITMAP bitmap = static_cast<HBITMAP>(LoadImageW(nullptr,
        update->thumbnail.c_str(), IMAGE_BITMAP, 0, 0,
        LR_LOADFROMFILE | LR_CREATEDIBSECTION));
    if (!bitmap) return;
    const int imageIndex = ImageList_Add(g_rowImages, bitmap, nullptr);
    DeleteObject(bitmap);
    if (imageIndex < 0) return;
    track->imageIndex = imageIndex;
    const int row = static_cast<int>(std::distance(g_tracks.begin(), track));
    LVITEMW item{};
    item.mask = LVIF_IMAGE;
    item.iItem = row;
    item.iImage = imageIndex;
    ListView_SetItem(g_list, &item);
}

void stopArtworkWorker() {
    {
        std::lock_guard<std::mutex> lock(g_artworkMutex);
        g_artworkStop = true;
        g_artworkQueue.clear();
    }
    g_artworkCondition.notify_all();
    if (g_artworkThread.joinable()) g_artworkThread.join();

    MSG message{};
    while (PeekMessageW(&message, g_window, kArtworkReadyMessage,
                        kArtworkReadyMessage, PM_REMOVE)) {
        deleteArtworkUpdate(reinterpret_cast<ArtworkUpdate*>(message.lParam));
    }
}

void metadataWorkerLoop() {
    const std::wstring ffprobe = findFfprobe();
    for (;;) {
        MetadataRequest request;
        {
            std::unique_lock<std::mutex> lock(g_metadataMutex);
            g_metadataCondition.wait(lock, [] {
                return g_metadataStop || !g_metadataQueue.empty();
            });
            if (g_metadataStop) return;
            request = std::move(g_metadataQueue.front());
            g_metadataQueue.pop_front();
        }
        auto* update = new MetadataUpdate{};
        update->source = std::move(request.source);
        if (ffprobe.empty()) {
            update->error = L"FFprobe not found";
        } else {
            update->duration = probeDuration(ffprobe, update->source, update->error);
            std::wstring command = quoteArgument(ffprobe) +
                L" -v error -show_entries format_tags:stream_tags "
                L"-of default=noprint_wrappers=1 " +
                quoteArgument(update->source.wstring());
            const AuxiliaryProcessResult result =
                runAuxiliaryProcess(ffprobe, std::move(command), true, false);
            if (!result.started && update->error.empty()) {
                update->error = L"FFprobe start failed";
            } else if (result.started && result.exitCode != 0 &&
                       update->error.empty()) {
                update->error = L"FFprobe read failed";
            } else {
                if (result.started && result.exitCode == 0) {
                    parseMetadataOutput(result.output, update->artist, update->title);
                }
                if (update->artist.empty()) {
                    update->artist = inferArtistFromPath(update->source);
                }
            }
        }
        if (update->artist.empty()) {
            update->artist = inferArtistFromPath(update->source);
        }
        if (update->duration.empty() && update->error.empty()) {
            update->error = L"Duration not reported by FFprobe";
        }
        if (!g_window || !PostMessageW(g_window, kMetadataReadyMessage, 0,
                                       reinterpret_cast<LPARAM>(update))) {
            delete update;
        }
    }
}

void startMetadataWorker() {
    std::lock_guard<std::mutex> lock(g_metadataMutex);
    g_metadataStop = false;
    g_metadataThread = std::thread(metadataWorkerLoop);
}

void requestMetadata(const fs::path& source) {
    {
        std::lock_guard<std::mutex> lock(g_metadataMutex);
        if (g_metadataStop || !g_metadataThread.joinable()) return;
        g_metadataQueue.push_back({source});
    }
    g_metadataCondition.notify_one();
}

void handleMetadataReady(LPARAM parameter) {
    std::unique_ptr<MetadataUpdate> update(
        reinterpret_cast<MetadataUpdate*>(parameter));
    if (!update || !g_list) return;
    const auto track = std::find_if(g_tracks.begin(), g_tracks.end(),
        [&](const Track& item) {
            return lower(item.path.lexically_normal().wstring()) ==
                lower(update->source.lexically_normal().wstring());
        });
    if (track == g_tracks.end()) return;
    track->displayArtist = std::move(update->artist);
    track->displayTitle = std::move(update->title);
    track->displayDuration = std::move(update->duration);
    if (track->status == L"Reading metadata...") {
        track->status = !update->error.empty() ? std::move(update->error)
            : track->displayDuration.empty() ? L"Duration unavailable" : L"Ready";
    }
    updateTrackRow(static_cast<size_t>(std::distance(g_tracks.begin(), track)));
}

void stopMetadataWorker() {
    {
        std::lock_guard<std::mutex> lock(g_metadataMutex);
        g_metadataStop = true;
        g_metadataQueue.clear();
    }
    g_metadataCondition.notify_all();
    if (g_metadataThread.joinable()) g_metadataThread.join();

    MSG message{};
    while (PeekMessageW(&message, g_window, kMetadataReadyMessage,
                        kMetadataReadyMessage, PM_REMOVE)) {
        delete reinterpret_cast<MetadataUpdate*>(message.lParam);
    }
}

void addAudioFile(const fs::path& path, const fs::path& sourceFolder = {}) {
    std::error_code error;
    if (!fs::is_regular_file(path, error)) return;
    if (!isAudioPath(path)) return;
    const std::wstring key = lower(path.lexically_normal().wstring());
    for (auto& track : g_tracks) {
        if (lower(track.path.lexically_normal().wstring()) == key) {
            if (track.sourceFolder.empty() && !sourceFolder.empty()) {
                track.sourceFolder = sourceFolder;
            }
            return;
        }
    }

    g_tracks.push_back({path, sourceFolder, L"Reading metadata...", {}, {}, {}, 0});
    const int row = ListView_GetItemCount(g_list);
    LVITEMW item{};
    item.mask = LVIF_TEXT;
    item.iItem = row;
    item.pszText = const_cast<wchar_t*>(g_tracks.back().path.filename().c_str());
    item.mask |= LVIF_IMAGE;
    item.iImage = g_tracks.back().imageIndex;
    ListView_InsertItem(g_list, &item);
    updateTrackRow(static_cast<size_t>(row));
    updateEmptyState();
    updateListColumns();
    requestArtwork(path);
    requestMetadata(path);
}

void addAudioFolder(const fs::path& folder) {
    std::error_code error;
    if (!fs::is_directory(folder, error)) return;
    fs::recursive_directory_iterator it(folder, fs::directory_options::skip_permission_denied, error);
    const fs::recursive_directory_iterator end;
    while (it != end) {
        if (g_cancel.load()) return;
        std::error_code entryError;
        if (it->is_regular_file(entryError)) addAudioFile(it->path(), folder);
        it.increment(error);
        if (error) error.clear();
    }
}

void chooseFiles() {
    std::vector<wchar_t> buffer(32768, L'\0');
    OPENFILENAMEW dialog{};
    dialog.lStructSize = sizeof(dialog);
    dialog.hwndOwner = g_window;
    dialog.lpstrFilter = L"Audio and media files\0*.mp3;*.wav;*.flac;*.m4a;*.aac;*.ogg;*.opus;*.wma;*.aiff;*.aif;*.ape;*.alac;*.ac3;*.dsf;*.dff;*.mp4;*.webm;*.mka;*.mp2;*.mpc;*.m4b;*.mkv;*.mov;*.avi;*.m4v;*.mpeg;*.mpg\0All files\0*.*\0";
    dialog.lpstrFile = buffer.data();
    dialog.nMaxFile = static_cast<DWORD>(buffer.size());
    dialog.Flags = OFN_FILEMUSTEXIST | OFN_ALLOWMULTISELECT | OFN_EXPLORER |
                   OFN_HIDEREADONLY;
    dialog.lpstrTitle = L"Add audio files";
    if (!GetOpenFileNameW(&dialog)) {
        const DWORD error = CommDlgExtendedError();
        if (error != 0) {
            MessageBoxW(g_window,
                (L"The Windows file picker could not be opened (error " +
                 std::to_wstring(error) + L").").c_str(),
                L"Could not add files", MB_OK | MB_ICONERROR);
        }
        return;
    }
    const std::wstring first(buffer.data());
    const wchar_t* next = buffer.data() + first.size() + 1;
    if (*next == L'\0') {
        addAudioFile(first);
    } else {
        const fs::path folder(first);
        while (*next != L'\0') {
            addAudioFile(folder / next);
            next += wcslen(next) + 1;
        }
    }
}

void chooseFolder() {
    BROWSEINFOW dialog{};
    dialog.hwndOwner = g_window;
    dialog.lpszTitle = L"Choose a folder containing audio files";
    dialog.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;
    PIDLIST_ABSOLUTE selection = SHBrowseForFolderW(&dialog);
    if (!selection) return;
    wchar_t folder[MAX_PATH]{};
    const BOOL resolved = SHGetPathFromIDListW(selection, folder);
    CoTaskMemFree(selection);
    if (resolved) addAudioFolder(folder);
}

void chooseOutputFolder() {
    BROWSEINFOW dialog{};
    dialog.hwndOwner = g_window;
    dialog.lpszTitle = L"Choose the conversion destination";
    dialog.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;
    PIDLIST_ABSOLUTE selection = SHBrowseForFolderW(&dialog);
    if (!selection) return;
    wchar_t folder[MAX_PATH]{};
    const BOOL resolved = SHGetPathFromIDListW(selection, folder);
    CoTaskMemFree(selection);
    if (resolved) SetWindowTextW(g_output, folder);
}

void setControlsEnabled(bool enabled) {
    for (HWND button : g_buttons) EnableWindow(button, enabled);
    EnableWindow(g_format, enabled);
    EnableWindow(g_quality, enabled);
    EnableWindow(g_workers, enabled);
    EnableWindow(g_output, enabled);
    EnableWindow(g_list, enabled);
    ShowWindow(g_buttons[5], enabled ? SW_SHOW : SW_HIDE);
    ShowWindow(g_buttons[6], enabled ? SW_HIDE : SW_SHOW);
    EnableWindow(g_buttons[6], !enabled);
}

void beginConversion() {
    if (g_isConverting) return;
    if (g_tracks.empty()) {
        MessageBoxW(g_window, L"Add one or more audio files to the queue first.",
                    L"Nothing to convert", MB_OK | MB_ICONINFORMATION);
        return;
    }
    wchar_t folderBuffer[32768]{};
    GetWindowTextW(g_output, folderBuffer, static_cast<int>(std::size(folderBuffer)));
    const fs::path destination(folderBuffer);
    if (!destination.empty()) {
        std::error_code error;
        if (!fs::is_directory(destination, error)) {
            MessageBoxW(g_window, L"Choose an existing destination folder, or clear the field to save beside each source file.",
                        L"Invalid destination", MB_OK | MB_ICONWARNING);
            return;
        }
    }

    const std::wstring ffmpeg = findFfmpeg();
    if (ffmpeg.empty()) {
        MessageBoxW(g_window,
            L"FFmpeg was not found. Put ffmpeg.exe beside AudioForge.exe or add FFmpeg to PATH.",
            L"FFmpeg is required", MB_OK | MB_ICONERROR);
        return;
    }

    const int format = static_cast<int>(SendMessageW(g_format, CB_GETCURSEL, 0, 0));
    const int profile = static_cast<int>(SendMessageW(g_quality, CB_GETCURSEL, 0, 0));
    size_t profileCount = 0;
    encoderProfiles(format, profileCount);
    if (profile < 0 || static_cast<size_t>(profile) >= profileCount) {
        MessageBoxW(g_window, L"Choose a valid encoding profile for the selected output format.",
                    L"Invalid encoding profile", MB_OK | MB_ICONWARNING);
        return;
    }
    const int selectedWorkers = static_cast<int>(SendMessageW(g_workers, CB_GETCURSEL, 0, 0));
    unsigned int hardwareThreads = std::thread::hardware_concurrency();
    if (hardwareThreads == 0) hardwareThreads = 1;
    size_t workerCount = (std::min)(static_cast<size_t>(hardwareThreads), size_t{4});
    if (selectedWorkers > 0) {
        const int requested[] = {0, 1, 2, 4, 8};
        workerCount = (std::min)(static_cast<size_t>(requested[selectedWorkers]),
                                 static_cast<size_t>(hardwareThreads));
    }
    workerCount = (std::max)(size_t{1}, (std::min)(workerCount, g_tracks.size()));

    std::vector<WorkerTask> tasks;
    tasks.reserve(g_tracks.size());
    std::set<std::wstring> reservedNames;
    std::set<std::wstring> reservedGroupRoots;
    std::map<std::wstring, fs::path> groupOutputRoots;
    std::error_code directoryError;
    for (size_t i = 0; i < g_tracks.size(); ++i) {
        fs::path outputFolder;
        if (!g_tracks[i].sourceFolder.empty()) {
            const fs::path sourceFolder = g_tracks[i].sourceFolder.lexically_normal();
            const std::wstring groupKey = lower(sourceFolder.wstring());
            auto group = groupOutputRoots.find(groupKey);
            if (group == groupOutputRoots.end()) {
                const fs::path parent = destination.empty()
                    ? sourceFolder.parent_path()
                    : destination;
                const std::wstring baseName = sourceFolder.filename().wstring();
                fs::path candidate = parent / baseName;
                unsigned suffix = 2;
                while (fs::exists(candidate, directoryError) ||
                       reservedGroupRoots.count(lower(candidate.wstring())) != 0) {
                    candidate = parent / (sourceFolder.filename().wstring() +
                        L" (" + std::to_wstring(suffix++) + L")");
                }
                reservedGroupRoots.insert(lower(candidate.wstring()));
                group = groupOutputRoots.emplace(groupKey, candidate).first;
            }
            const fs::path relativeParent =
                g_tracks[i].path.parent_path().lexically_relative(sourceFolder);
            outputFolder = group->second / relativeParent;
        } else {
            outputFolder = destination.empty() ? g_tracks[i].path.parent_path() : destination;
        }

        fs::create_directories(outputFolder, directoryError);
        if (directoryError) {
            MessageBoxW(g_window,
                (L"Could not create the output folder:\n" + outputFolder.wstring()).c_str(),
                L"Could not prepare conversion folders", MB_OK | MB_ICONERROR);
            return;
        }
        tasks.push_back({i, g_tracks[i].path,
            makeOutputName(g_tracks[i].path, outputFolder, format, reservedNames),
            format, profile});
    }
    for (size_t i = 0; i < g_tracks.size(); ++i) {
        g_tracks[i].status = L"Queued";
        updateTrackRow(i);
    }

    g_isConverting = true;
    g_cancel = false;
    g_nextTask = 0;
    g_finishedWorkers = 0;
    g_completedCount = 0;
    g_failedCount = 0;
    g_cancelledCount = 0;
    SendMessageW(g_progress, PBM_SETRANGE32, 0, static_cast<LPARAM>(tasks.size()));
    SendMessageW(g_progress, PBM_SETPOS, 0, 0);
    SetWindowTextW(g_statusLabel, L"Preparing conversion...");
    setControlsEnabled(false);
    SetFocus(g_window);
    InvalidateRect(g_window, nullptr, FALSE);

    const auto sharedTasks = std::make_shared<const std::vector<WorkerTask>>(std::move(tasks));
    for (size_t i = 0; i < workerCount; ++i) {
        g_workerThreads.emplace_back([sharedTasks, ffmpeg, workerCount]() {
            while (!g_cancel.load()) {
                const size_t index = g_nextTask.fetch_add(1);
                if (index >= sharedTasks->size()) break;
                runConversion((*sharedTasks)[index], ffmpeg);
            }
            if (g_finishedWorkers.fetch_add(1) + 1 == workerCount) {
                PostMessageW(g_window, kConversionCompleteMessage, 0, 0);
            }
        });
    }
}

void stopConversion() {
    if (!g_isConverting) return;
    g_cancel = true;
    {
        std::lock_guard<std::mutex> lock(g_activeProcessMutex);
        for (HANDLE process : g_activeProcesses) TerminateProcess(process, 1);
    }
    SetWindowTextW(g_statusLabel, L"Stopping after active conversions are cancelled...");
    EnableWindow(g_buttons[6], FALSE);
}

void removeSelectedRows() {
    if (g_isConverting) return;
    std::vector<int> selected;
    int row = -1;
    while ((row = ListView_GetNextItem(g_list, row, LVNI_SELECTED)) != -1) {
        selected.push_back(row);
    }
    for (auto it = selected.rbegin(); it != selected.rend(); ++it) {
        ListView_DeleteItem(g_list, *it);
        g_tracks.erase(g_tracks.begin() + *it);
    }
    for (size_t i = 0; i < g_tracks.size(); ++i) updateTrackRow(i);
    updateEmptyState();
}

void clearQueue() {
    if (g_isConverting) return;
    g_tracks.clear();
    ListView_DeleteAllItems(g_list);
    updateEmptyState();
}

void initializeTheme() {
    if (g_themePanelBrush) DeleteObject(g_themePanelBrush);
    if (g_themeBackgroundBrush) DeleteObject(g_themeBackgroundBrush);
    g_themePanelBrush = CreateSolidBrush(kPanel);
    g_themeBackgroundBrush = CreateSolidBrush(kBackground);
    if (g_list) {
        ListView_SetBkColor(g_list, kBackground);
        ListView_SetTextBkColor(g_list, kBackground);
        ListView_SetTextColor(g_list, kText);
    }
    if (g_progress) {
        SendMessageW(g_progress, PBM_SETBKCOLOR, 0, static_cast<LPARAM>(kBackground));
        SendMessageW(g_progress, PBM_SETBARCOLOR, 0, static_cast<LPARAM>(kAccent));
    }
    if (g_window) {
        setDarkTitleBar(g_window);
        InvalidateRect(g_window, nullptr, TRUE);
        EnumChildWindows(g_window, [](HWND child, LPARAM) -> BOOL {
            InvalidateRect(child, nullptr, TRUE);
            return TRUE;
        }, 0);
    }
}

LRESULT CALLBACK darkComboProc(HWND combo, UINT message, WPARAM wParam, LPARAM lParam,
                               UINT_PTR subclassId, DWORD_PTR referenceData);
void drawComboItem(const DRAWITEMSTRUCT* item);

void createControls(HWND window) {
    g_font = CreateFontW(-15, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
        DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");

    auto createLabel = [&](const wchar_t* text, int x, int y, int width, int height) {
        HWND label = CreateWindowExW(0, L"STATIC", text, WS_CHILD | WS_VISIBLE,
            x, y, width, height, window, nullptr, nullptr, nullptr);
        SendMessageW(label, WM_SETFONT, reinterpret_cast<WPARAM>(g_font), TRUE);
        return label;
    };
    auto createButton = [&](const wchar_t* text, int id) {
        HWND button = CreateWindowExW(0, L"BUTTON", text,
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW | BS_NOTIFY,
            0, 0, 100, 34, window, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
            GetModuleHandleW(nullptr), nullptr);
        SendMessageW(button, WM_SETFONT, reinterpret_cast<WPARAM>(g_font), TRUE);
        return button;
    };

    g_buttons[0] = createButton(L"+ Add files", IdAddFiles);
    g_buttons[1] = createButton(L"Add folder", IdAddFolder);
    g_buttons[2] = createButton(L"Remove", IdRemove);
    g_buttons[3] = createButton(L"Clear", IdClear);

    const DWORD comboStyle = WS_CHILD | WS_VISIBLE | WS_TABSTOP | CBS_DROPDOWNLIST |
        CBS_OWNERDRAWFIXED | CBS_HASSTRINGS | WS_VSCROLL;
    g_format = CreateWindowExW(0, WC_COMBOBOXW, L"",
        comboStyle,
        0, 0, 230, 230, window, reinterpret_cast<HMENU>(IdFormat), nullptr, nullptr);
    SetWindowSubclass(g_format, darkComboProc, 1, 0);
    for (const auto& format : kFormats) SendMessageW(g_format, CB_ADDSTRING, 0,
                                                       reinterpret_cast<LPARAM>(format.name));
    SendMessageW(g_format, CB_SETCURSEL, 0, 0);

    g_quality = CreateWindowExW(0, WC_COMBOBOXW, L"",
        comboStyle,
        0, 0, 145, 120, window, reinterpret_cast<HMENU>(IdQuality), nullptr, nullptr);
    SetWindowSubclass(g_quality, darkComboProc, 1, 0);
    updateQualityChoices();

    g_workers = CreateWindowExW(0, WC_COMBOBOXW, L"",
        comboStyle,
        0, 0, 105, 140, window, reinterpret_cast<HMENU>(IdWorkers), nullptr, nullptr);
    SetWindowSubclass(g_workers, darkComboProc, 1, 0);
    const wchar_t* workerChoices[] = {L"Auto", L"1 worker", L"2 workers", L"4 workers", L"8 workers"};
    for (const auto* choice : workerChoices) SendMessageW(g_workers, CB_ADDSTRING, 0,
                                                          reinterpret_cast<LPARAM>(choice));
    SendMessageW(g_workers, CB_SETCURSEL, 0, 0);

    g_output = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT",
        g_defaultOutputFolder.c_str(),
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
        0, 0, 200, 30, window, reinterpret_cast<HMENU>(IdOutput), nullptr, nullptr);
    SendMessageW(g_output, EM_SETCUEBANNER, TRUE,
                 reinterpret_cast<LPARAM>(L"Output folder (optional)"));
    g_buttons[4] = createButton(L"Browse", IdBrowseOutput);

    g_list = CreateWindowExW(0, WC_LISTVIEWW, L"",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | LVS_REPORT | LVS_SHOWSELALWAYS,
        0, 0, 100, 100, window, nullptr, GetModuleHandleW(nullptr), nullptr);
    SetWindowTheme(g_list, L"DarkMode_Explorer", nullptr);
    HWND header = ListView_GetHeader(g_list);
    ListView_SetExtendedListViewStyle(g_list, LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER |
                                              LVS_EX_LABELTIP | LVS_EX_HEADERDRAGDROP);
    ListView_SetBkColor(g_list, kBackground);
    ListView_SetTextBkColor(g_list, kBackground);
    ListView_SetTextColor(g_list, kText);
    const wchar_t* columns[] = {L"TRACK", L"ARTIST", L"DURATION",
                                L"FORMAT / SIZE", L"STATUS"};
    for (int i = 0; i < 5; ++i) {
        LVCOLUMNW column{};
        column.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_SUBITEM;
        column.pszText = const_cast<wchar_t*>(columns[i]);
        column.cx = i == 0 ? 360 : 140;
        column.iSubItem = i;
        ListView_InsertColumn(g_list, i, &column);
    }
    if (header) SetWindowSubclass(header, darkHeaderProc, 1, 0);

    g_emptyLabel = createLabel(L"Your queue is empty - drop files here or choose Add files to get started.",
                               250, 450, 600, 55);
    SetWindowLongPtrW(g_emptyLabel, GWL_STYLE,
        GetWindowLongPtrW(g_emptyLabel, GWL_STYLE) | SS_CENTER);
    g_statusLabel = createLabel(L"0 files ready for conversion", 14, 0, 360, 20);
    SetWindowLongPtrW(g_statusLabel, GWL_STYLE,
        GetWindowLongPtrW(g_statusLabel, GWL_STYLE) | SS_PATHELLIPSIS | SS_NOPREFIX);
    g_progress = CreateWindowExW(0, PROGRESS_CLASSW, L"",
        WS_CHILD | WS_VISIBLE | PBS_SMOOTH, 0, 0, 200, 12, window, nullptr,
        GetModuleHandleW(nullptr), nullptr);
    g_buttons[5] = createButton(L"Convert", IdConvert);
    g_buttons[6] = createButton(L"Stop", IdStop);
    EnableWindow(g_buttons[6], FALSE);
    ShowWindow(g_buttons[6], SW_HIDE);

    g_rowImages = ImageList_Create(42, 42, ILC_COLOR32, 16, 8);
    if (g_rowImages) {
        HDC dc = GetDC(g_list);
        HBITMAP placeholder = CreateCompatibleBitmap(dc, 42, 42);
        HDC memoryDc = CreateCompatibleDC(dc);
        HGDIOBJ oldBitmap = SelectObject(memoryDc, placeholder);
        RECT tile{0, 0, 42, 42};
        HBRUSH tileBrush = CreateSolidBrush(g_darkTheme ? RGB(50, 54, 60) : RGB(224, 228, 233));
        FillRect(memoryDc, &tile, tileBrush);
        DeleteObject(tileBrush);
        SetBkMode(memoryDc, TRANSPARENT);
        SetTextColor(memoryDc, g_darkTheme ? RGB(189, 196, 205) : RGB(98, 108, 120));
        HFONT iconFont = CreateFontW(-24, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
            DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI Symbol");
        HGDIOBJ oldFont = SelectObject(memoryDc, iconFont);
        DrawTextW(memoryDc, L"\u266A", -1, &tile, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        SelectObject(memoryDc, oldFont);
        SelectObject(memoryDc, oldBitmap);
        DeleteObject(iconFont);
        DeleteDC(memoryDc);
        ImageList_Add(g_rowImages, placeholder, nullptr);
        DeleteObject(placeholder);
        ReleaseDC(g_list, dc);
        ListView_SetImageList(g_list, g_rowImages, LVSIL_SMALL);
    }

    for (HWND child : {g_format, g_quality, g_workers, g_output, g_list, g_progress}) {
        SendMessageW(child, WM_SETFONT, reinterpret_cast<WPARAM>(g_font), TRUE);
    }
    updateListColumns();
}

void layoutControls(HWND window) {
    RECT rect{};
    GetClientRect(window, &rect);
    const int width = rect.right;
    const int height = rect.bottom;
    const int margin = 14;
    const bool compactToolbar = width < 1120;
    const int toolbarY = compactToolbar ? 8 : 12;
    const int toolbarHeight = 34;
    int x = margin;
    MoveWindow(g_buttons[0], x, toolbarY, 102, toolbarHeight, TRUE);
    x += 110;
    MoveWindow(g_buttons[1], x, toolbarY, 102, toolbarHeight, TRUE);
    x += 110;
    MoveWindow(g_buttons[2], x, toolbarY, 76, toolbarHeight, TRUE);
    x += 82;
    MoveWindow(g_buttons[3], x, toolbarY, 64, toolbarHeight, TRUE);
    x += 78;

    const int rightMargin = width - margin;
    const int workersWidth = 112;
    const int qualityWidth = 170;
    const int selectorsY = compactToolbar ? 49 : toolbarY;
    if (compactToolbar) x = margin;
    const int formatWidth = (std::max)(190, rightMargin - x - workersWidth -
        qualityWidth - (compactToolbar ? 16 : 24));
    MoveWindow(g_format, x, selectorsY, formatWidth, 250, TRUE);
    x += formatWidth + 8;
    MoveWindow(g_quality, x, selectorsY, qualityWidth, 250, TRUE);
    x += qualityWidth + 8;
    MoveWindow(g_workers, x, selectorsY, workersWidth, 150, TRUE);

    const int footerHeight = kFooterHeight;
    const int listTop = compactToolbar ? 98 : 58;
    const int listHeight = (std::max)(120, height - listTop - footerHeight);
    MoveWindow(g_list, 0, listTop, width, listHeight, TRUE);
    MoveWindow(g_emptyLabel, 30, listTop + (listHeight - 55) / 2,
               width - 60, 55, TRUE);

    const int footerTop = height - footerHeight;
    const int convertWidth = 96;
    const int stopWidth = 72;
    const int browseWidth = 72;
    const int outputWidth = 200;
    const int convertX = width - margin - convertWidth;
    const int browseX = convertX - 8 - browseWidth;
    const int outputX = browseX - 8 - outputWidth;
    MoveWindow(g_output, outputX, footerTop + 7, outputWidth, 30, TRUE);
    MoveWindow(g_buttons[4], browseX, footerTop + 5, browseWidth, 34, TRUE);
    MoveWindow(g_buttons[5], convertX, footerTop + 5, convertWidth, 34, TRUE);
    MoveWindow(g_buttons[6], convertX, footerTop + 5, stopWidth, 34, TRUE);
    MoveWindow(g_statusLabel, margin, footerTop + 3, 245, 19, TRUE);
    MoveWindow(g_progress, margin, footerTop + 26,
               (std::max)(120, outputX - margin - 20), 8, TRUE);
    updateListColumns();
}

LRESULT CALLBACK darkComboProc(HWND combo, UINT message, WPARAM wParam, LPARAM lParam,
                               UINT_PTR, DWORD_PTR) {
    const LRESULT result = DefSubclassProc(combo, message, wParam, lParam);
    if (message == WM_PAINT) {
        COMBOBOXINFO info{};
        info.cbSize = sizeof(info);
        if (GetComboBoxInfo(combo, &info)) {
            RECT button = info.rcButton;
            MapWindowPoints(HWND_DESKTOP, combo, reinterpret_cast<POINT*>(&button), 2);

            HDC dc = GetDC(combo);
            if (dc) {
                HBRUSH background = CreateSolidBrush(kPanel);
                FillRect(dc, &button, background);
                DeleteObject(background);

                HPEN borderPen = CreatePen(PS_SOLID, 1, kBorder);
                HGDIOBJ oldPen = SelectObject(dc, borderPen);
                MoveToEx(dc, button.left, button.top + 1, nullptr);
                LineTo(dc, button.left, button.bottom - 1);

                HPEN arrowPen = CreatePen(PS_SOLID, 1, kMuted);
                SelectObject(dc, arrowPen);
                const int centerX = (button.left + button.right) / 2;
                const int centerY = (button.top + button.bottom) / 2;
                MoveToEx(dc, centerX - 4, centerY - 1, nullptr);
                LineTo(dc, centerX, centerY + 3);
                LineTo(dc, centerX + 4, centerY - 1);

                SelectObject(dc, oldPen);
                RECT client{};
                GetClientRect(combo, &client);
                HPEN framePen = CreatePen(PS_SOLID, 1, kBorder);
                HGDIOBJ frameOldPen = SelectObject(dc, framePen);
                HGDIOBJ frameOldBrush = SelectObject(dc, GetStockObject(HOLLOW_BRUSH));
                Rectangle(dc, client.left, client.top, client.right, client.bottom);
                SelectObject(dc, frameOldBrush);
                SelectObject(dc, frameOldPen);
                DeleteObject(framePen);
                DeleteObject(arrowPen);
                DeleteObject(borderPen);
                ReleaseDC(combo, dc);
            }
        }
    } else if (message == WM_NCDESTROY) {
        RemoveWindowSubclass(combo, darkComboProc, 1);
    }
    return result;
}

LRESULT CALLBACK darkHeaderProc(HWND header, UINT message, WPARAM wParam, LPARAM lParam,
                                UINT_PTR, DWORD_PTR) {
    if (message == WM_ERASEBKGND) return 1;
    if (message == WM_PAINT) {
        PAINTSTRUCT paint{};
        HDC dc = BeginPaint(header, &paint);
        RECT client{};
        GetClientRect(header, &client);
        HBRUSH background = CreateSolidBrush(kPanel);
        FillRect(dc, &client, background);
        DeleteObject(background);
        const int count = Header_GetItemCount(header);
        for (int i = 0; i < count; ++i) {
            RECT cell{};
            if (!Header_GetItemRect(header, i, &cell)) continue;
            wchar_t title[128]{};
            HDITEMW item{};
            item.mask = HDI_TEXT;
            item.pszText = title;
            item.cchTextMax = static_cast<int>(std::size(title));
            Header_GetItem(header, i, &item);
            SetBkMode(dc, TRANSPARENT);
            SetTextColor(dc, kMuted);
            HGDIOBJ oldFont = SelectObject(dc, g_font);
            RECT text = cell;
            text.left += 10;
            DrawTextW(dc, title, -1, &text,
                DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
            SelectObject(dc, oldFont);
            HPEN separator = CreatePen(PS_SOLID, 1, kBorder);
            HGDIOBJ oldPen = SelectObject(dc, separator);
            MoveToEx(dc, cell.right - 1, cell.top + 5, nullptr);
            LineTo(dc, cell.right - 1, cell.bottom - 5);
            SelectObject(dc, oldPen);
            DeleteObject(separator);
        }
        EndPaint(header, &paint);
        return 0;
    }
    if (message == WM_NCDESTROY) {
        RemoveWindowSubclass(header, darkHeaderProc, 1);
    }
    return DefSubclassProc(header, message, wParam, lParam);
}

void drawComboItem(const DRAWITEMSTRUCT* item) {
    const HWND combo = item->hwndItem;
    HDC dc = item->hDC;
    RECT rect = item->rcItem;
    const bool disabled = (item->itemState & ODS_DISABLED) != 0 ||
        !IsWindowEnabled(combo);
    const bool highlighted = (item->itemState & ODS_SELECTED) != 0 &&
        (item->itemState & ODS_COMBOBOXEDIT) == 0;
    HBRUSH background = CreateSolidBrush(highlighted ? RGB(50, 67, 84) : kPanel);
    FillRect(dc, &rect, background);
    DeleteObject(background);

    wchar_t text[256]{};
    const LRESULT selected = item->itemID == static_cast<UINT>(-1)
        ? SendMessageW(combo, CB_GETCURSEL, 0, 0)
        : static_cast<LRESULT>(item->itemID);
    if (selected != CB_ERR) {
        SendMessageW(combo, CB_GETLBTEXT, static_cast<WPARAM>(selected),
                     reinterpret_cast<LPARAM>(text));
    }

    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, disabled ? kMuted : kText);
    HFONT font = reinterpret_cast<HFONT>(SendMessageW(combo, WM_GETFONT, 0, 0));
    HGDIOBJ oldFont = SelectObject(dc, font);
    rect.left += 8;
    rect.right -= 26;
    DrawTextW(dc, text, -1, &rect, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    SelectObject(dc, oldFont);

    if (item->itemState & ODS_FOCUS) {
        InflateRect(&rect, -2, -2);
        HPEN focusPen = CreatePen(PS_SOLID, 1, kAccent);
        HGDIOBJ oldPen = SelectObject(dc, focusPen);
        HGDIOBJ oldBrush = SelectObject(dc, GetStockObject(HOLLOW_BRUSH));
        Rectangle(dc, rect.left, rect.top, rect.right, rect.bottom);
        SelectObject(dc, oldBrush);
        SelectObject(dc, oldPen);
        DeleteObject(focusPen);
    }
}

void drawButton(const DRAWITEMSTRUCT* item) {
    HDC dc = item->hDC;
    RECT rect = item->rcItem;
    HBRUSH buttonBackground = CreateSolidBrush(kPanel);
    FillRect(dc, &rect, buttonBackground);
    DeleteObject(buttonBackground);

    const int id = GetDlgCtrlID(item->hwndItem);
    const bool primary = id == IdAddFiles || id == IdConvert;
    const bool stop = id == IdStop;
    const bool enabled = IsWindowEnabled(item->hwndItem) != FALSE;
    const bool pressed = (item->itemState & ODS_SELECTED) != 0;
    const bool hot = (item->itemState & ODS_HOTLIGHT) != 0;
    COLORREF fill = primary ? kAccent : (stop
        ? (g_darkTheme ? RGB(80, 46, 52) : RGB(250, 231, 231)) : kPanel);
    if (pressed) fill = primary ? RGB(28, 104, 183)
        : (g_darkTheme ? RGB(50, 50, 50) : RGB(230, 233, 236));
    else if (hot) fill = primary ? RGB(52, 144, 226)
        : (g_darkTheme ? RGB(53, 53, 53) : RGB(235, 239, 243));
    if (!enabled) fill = g_darkTheme ? RGB(34, 40, 49) : RGB(230, 232, 235);
    const COLORREF text = enabled ? kText : kMuted;
    HBRUSH brush = CreateSolidBrush(fill);
    HPEN pen = CreatePen(PS_SOLID, 1, primary ? fill : kBorder);
    HGDIOBJ oldBrush = SelectObject(dc, brush);
    HGDIOBJ oldPen = SelectObject(dc, pen);
    RoundRect(dc, rect.left, rect.top, rect.right, rect.bottom, 8, 8);
    SelectObject(dc, oldBrush);
    SelectObject(dc, oldPen);
    DeleteObject(brush);
    DeleteObject(pen);

    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, text);
    HFONT font = reinterpret_cast<HFONT>(SendMessageW(item->hwndItem, WM_GETFONT, 0, 0));
    HGDIOBJ oldFont = SelectObject(dc, font);
    wchar_t label[256]{};
    GetWindowTextW(item->hwndItem, label, static_cast<int>(std::size(label)));
    DrawTextW(dc, label, -1, &rect,
              DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    SelectObject(dc, oldFont);
    if (item->itemState & ODS_FOCUS) {
        InflateRect(&rect, -4, -4);
        HPEN focusPen = CreatePen(PS_SOLID, 1, RGB(190, 210, 232));
        HGDIOBJ previousPen = SelectObject(dc, focusPen);
        HGDIOBJ previousBrush = SelectObject(dc, GetStockObject(HOLLOW_BRUSH));
        RoundRect(dc, rect.left, rect.top, rect.right, rect.bottom, 8, 8);
        SelectObject(dc, previousBrush);
        SelectObject(dc, previousPen);
        DeleteObject(focusPen);
    }
}

void drawBackground(HWND window) {
    PAINTSTRUCT paint{};
    HDC dc = BeginPaint(window, &paint);
    RECT client{};
    GetClientRect(window, &client);
    HBRUSH background = CreateSolidBrush(kBackground);
    FillRect(dc, &client, background);
    DeleteObject(background);

    RECT toolbar{0, 0, client.right, client.right < 1120 ? 98 : 58};
    RECT footer{0, client.bottom - kFooterHeight, client.right, client.bottom};
    HBRUSH panel = CreateSolidBrush(kPanel);
    FillRect(dc, &toolbar, panel);
    FillRect(dc, &footer, panel);
    DeleteObject(panel);

    HPEN separator = CreatePen(PS_SOLID, 1, kBorder);
    HGDIOBJ oldPen = SelectObject(dc, separator);
    MoveToEx(dc, 0, toolbar.bottom - 1, nullptr);
    LineTo(dc, client.right, toolbar.bottom - 1);
    MoveToEx(dc, 0, footer.top, nullptr);
    LineTo(dc, client.right, footer.top);
    SelectObject(dc, oldPen);
    DeleteObject(separator);
    EndPaint(window, &paint);
}

void handleTaskUpdate(LPARAM parameter) {
    std::unique_ptr<TaskUpdate> update(reinterpret_cast<TaskUpdate*>(parameter));
    if (update->index >= g_tracks.size()) return;
    g_tracks[update->index].status = std::move(update->status);
    if (g_tracks[update->index].status == L"Complete") ++g_completedCount;
    else if (g_tracks[update->index].status.rfind(L"Failed:", 0) == 0) ++g_failedCount;
    else if (g_tracks[update->index].status == L"Cancelled") ++g_cancelledCount;
    updateTrackRow(update->index);
    const int completed = g_completedCount + g_failedCount + g_cancelledCount;
    SendMessageW(g_progress, PBM_SETPOS, completed, 0);
    std::wstring summary = std::to_wstring(completed) + L" of " +
        std::to_wstring(g_tracks.size()) + L" files finished";
    if (g_failedCount > 0) summary += L"  |  " + std::to_wstring(g_failedCount) + L" failed";
    if (g_cancelledCount > 0) summary += L"  |  " + std::to_wstring(g_cancelledCount) + L" cancelled";
    SetWindowTextW(g_statusLabel, summary.c_str());
}

void playCompletionSound() {
    if (g_completedCount == 0) return;
    if (g_completionSoundOpen) {
        mciSendStringW(L"close AudioForgeCompletionSound", nullptr, 0, nullptr);
        g_completionSoundOpen = false;
    }

    static std::mt19937 generator(std::random_device{}());
    static std::bernoulli_distribution playGoblin(0.01);
    const wchar_t* filename = playGoblin(generator)
        ? L"done-goblin.mp3" : L"done.mp3";
    const fs::path sound = g_defaultOutputFolder.parent_path() / L"assets" / filename;
    const std::wstring openCommand = L"open " + quoteArgument(sound.wstring()) +
        L" type mpegvideo alias AudioForgeCompletionSound";
    MCIERROR error = mciSendStringW(openCommand.c_str(), nullptr, 0, nullptr);
    if (error == 0) {
        g_completionSoundOpen = true;
        error = mciSendStringW(L"play AudioForgeCompletionSound notify",
                               nullptr, 0, g_window);
        if (error != 0) {
            mciSendStringW(L"close AudioForgeCompletionSound", nullptr, 0, nullptr);
            g_completionSoundOpen = false;
        }
    }
    if (error != 0) {
        wchar_t message[256]{};
        const wchar_t* detail = mciGetErrorStringW(error, message,
            static_cast<UINT>(std::size(message)))
            ? message : L"Could not play the completion sound.";
        MessageBoxW(g_window, detail, L"AudioForge sound error",
                    MB_OK | MB_ICONWARNING);
    }
}

void handleConversionComplete() {
    for (auto& worker : g_workerThreads) {
        if (worker.joinable()) worker.join();
    }
    g_workerThreads.clear();
    g_isConverting = false;
    for (size_t i = 0; i < g_tracks.size(); ++i) {
        if (g_tracks[i].status == L"Queued" || g_tracks[i].status == L"Ready") {
            g_tracks[i].status = L"Cancelled";
            ++g_cancelledCount;
            updateTrackRow(i);
        }
    }
    const int finished = g_completedCount + g_failedCount + g_cancelledCount;
    SendMessageW(g_progress, PBM_SETPOS, finished, 0);
    setControlsEnabled(true);
    updateEmptyState();
    InvalidateRect(g_window, nullptr, FALSE);

    std::wstring summary = L"Finished: " + std::to_wstring(g_completedCount) +
        L" converted, " + std::to_wstring(g_failedCount) + L" failed, " +
        std::to_wstring(g_cancelledCount) + L" cancelled";
    SetWindowTextW(g_statusLabel, summary.c_str());
    playCompletionSound();
    if (g_failedCount > 0) {
        MessageBoxW(g_window,
            L"Some audio files could not be converted. Check their queue status and make sure FFmpeg supports the selected codec.",
            L"Conversion finished with errors", MB_OK | MB_ICONWARNING);
    }
}

LRESULT CALLBACK windowProcedure(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
    case WM_CREATE: {
        INITCOMMONCONTROLSEX controls{sizeof(controls),
            ICC_LISTVIEW_CLASSES | ICC_PROGRESS_CLASS};
        InitCommonControlsEx(&controls);
        createControls(window);
        initializeTheme();
        DragAcceptFiles(window, TRUE);
        return 0;
    }
    case WM_SIZE:
        if (g_list) {
            layoutControls(window);
            InvalidateRect(window, nullptr, TRUE);
        }
        return 0;
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT:
        drawBackground(window);
        return 0;
    case WM_CTLCOLORSTATIC: {
        HDC dc = reinterpret_cast<HDC>(wParam);
        const HWND control = reinterpret_cast<HWND>(lParam);
        SetTextColor(dc, control == g_statusLabel ? kText : kMuted);
        const COLORREF background = control == g_emptyLabel ? kBackground : kPanel;
        SetBkMode(dc, OPAQUE);
        SetBkColor(dc, background);
        if (background == kBackground) {
            return reinterpret_cast<LRESULT>(g_themeBackgroundBrush);
        }
        return reinterpret_cast<LRESULT>(g_themePanelBrush);
    }
    case WM_CTLCOLOREDIT: {
        HDC dc = reinterpret_cast<HDC>(wParam);
        SetTextColor(dc, kText);
        SetBkColor(dc, kPanel);
        return reinterpret_cast<LRESULT>(g_themePanelBrush);
    }
    case WM_MEASUREITEM: {
        auto* item = reinterpret_cast<MEASUREITEMSTRUCT*>(lParam);
        if (item->CtlType == ODT_COMBOBOX) {
            item->itemHeight = 28;
            return TRUE;
        }
        return FALSE;
    }
    case WM_CTLCOLORLISTBOX: {
        HDC dc = reinterpret_cast<HDC>(wParam);
        SetTextColor(dc, kText);
        SetBkColor(dc, kPanel);
        return reinterpret_cast<LRESULT>(g_themePanelBrush);
    }
    case WM_NOTIFY: {
        const auto* header = reinterpret_cast<const NMHDR*>(lParam);
        if (header->hwndFrom == g_list && header->code == NM_CUSTOMDRAW) {
            auto* draw = reinterpret_cast<NMLVCUSTOMDRAW*>(lParam);
            if (draw->nmcd.dwDrawStage == CDDS_PREPAINT) return CDRF_NOTIFYITEMDRAW;
            if (draw->nmcd.dwDrawStage == CDDS_ITEMPREPAINT) {
                const int row = static_cast<int>(draw->nmcd.dwItemSpec);
                const bool selected =
                    (ListView_GetItemState(g_list, row, LVIS_SELECTED) & LVIS_SELECTED) != 0;
                draw->clrText = kText;
                draw->clrTextBk = selected ? kSelection : kBackground;
                return CDRF_NEWFONT | CDRF_NOTIFYSUBITEMDRAW;
            }
            if (draw->nmcd.dwDrawStage == (CDDS_ITEMPREPAINT | CDDS_SUBITEM)) {
                const int row = static_cast<int>(draw->nmcd.dwItemSpec);
                const bool selected =
                    (ListView_GetItemState(g_list, row, LVIS_SELECTED) & LVIS_SELECTED) != 0;
                draw->clrText = kText;
                draw->clrTextBk = selected ? kSelection : kBackground;
                return CDRF_NEWFONT;
            }
        }
        break;
    }
    case WM_DRAWITEM: {
        const auto* item = reinterpret_cast<const DRAWITEMSTRUCT*>(lParam);
        if (item->CtlType == ODT_COMBOBOX) drawComboItem(item);
        else drawButton(item);
        return TRUE;
    }
    case WM_DROPFILES: {
        HDROP drop = reinterpret_cast<HDROP>(wParam);
        const UINT count = DragQueryFileW(drop, 0xFFFFFFFF, nullptr, 0);
        for (UINT i = 0; i < count; ++i) {
            std::vector<wchar_t> path(32768, L'\0');
            DragQueryFileW(drop, i, path.data(), static_cast<UINT>(path.size()));
            std::error_code error;
            if (fs::is_directory(path.data(), error)) addAudioFolder(path.data());
            else addAudioFile(path.data());
        }
        DragFinish(drop);
        return 0;
    }
    case WM_COMMAND:
        switch (LOWORD(wParam)) {
        case IdAddFiles:
            if (HIWORD(wParam) == BN_CLICKED) chooseFiles();
            return 0;
        case IdAddFolder:
            if (HIWORD(wParam) == BN_CLICKED) chooseFolder();
            return 0;
        case IdBrowseOutput:
            if (HIWORD(wParam) == BN_CLICKED) chooseOutputFolder();
            return 0;
        case IdRemove:
            if (HIWORD(wParam) == BN_CLICKED) removeSelectedRows();
            return 0;
        case IdClear:
            if (HIWORD(wParam) == BN_CLICKED) clearQueue();
            return 0;
        case IdConvert:
            if (HIWORD(wParam) == BN_CLICKED) beginConversion();
            return 0;
        case IdStop:
            if (HIWORD(wParam) == BN_CLICKED) stopConversion();
            return 0;
        case IdQuality:
            if (HIWORD(wParam) == CBN_SELCHANGE) InvalidateRect(window, nullptr, FALSE);
            return 0;
        case IdFormat:
            if (HIWORD(wParam) == CBN_SELCHANGE) {
                updateQualityChoices();
                InvalidateRect(window, nullptr, FALSE);
            }
            return 0;
        default: break;
        }
        return 0;
    case kTaskUpdateMessage:
        handleTaskUpdate(lParam);
        return 0;
    case kArtworkReadyMessage:
        handleArtworkReady(lParam);
        return 0;
    case kMetadataReadyMessage:
        handleMetadataReady(lParam);
        return 0;
    case kConversionCompleteMessage:
        handleConversionComplete();
        return 0;
    case MM_MCINOTIFY:
        if (g_completionSoundOpen) {
            const MCIERROR error =
                mciSendStringW(L"close AudioForgeCompletionSound", nullptr, 0, nullptr);
            g_completionSoundOpen = false;
            if (error != 0) {
                wchar_t message[256]{};
                const wchar_t* detail = mciGetErrorStringW(error, message,
                    static_cast<UINT>(std::size(message)))
                    ? message : L"Could not close the completion sound.";
                MessageBoxW(window, detail, L"AudioForge sound error",
                            MB_OK | MB_ICONWARNING);
            }
        }
        return 0;
    case WM_GETMINMAXINFO: {
        auto* info = reinterpret_cast<MINMAXINFO*>(lParam);
        info->ptMinTrackSize.x = 760;
        info->ptMinTrackSize.y = 540;
        return 0;
    }
    case WM_CLOSE:
        if (g_isConverting) {
            stopConversion();
            for (auto& worker : g_workerThreads) {
                if (worker.joinable()) worker.join();
            }
            g_workerThreads.clear();
            g_isConverting = false;
        }
        DestroyWindow(window);
        return 0;
    case WM_DESTROY:
        if (g_completionSoundOpen) {
            mciSendStringW(L"close AudioForgeCompletionSound", nullptr, 0, nullptr);
            g_completionSoundOpen = false;
        }
        stopArtworkWorker();
        stopMetadataWorker();
        if (g_themePanelBrush) DeleteObject(g_themePanelBrush);
        if (g_themeBackgroundBrush) DeleteObject(g_themeBackgroundBrush);
        PostQuitMessage(0);
        return 0;
    default:
        return DefWindowProcW(window, message, wParam, lParam);
    }
    return DefWindowProcW(window, message, wParam, lParam);
}

} // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int showCommand) {
    SetProcessDPIAware();
    std::wstring modulePath(32768, L'\0');
    const DWORD moduleLength = GetModuleFileNameW(nullptr, modulePath.data(),
        static_cast<DWORD>(modulePath.size()));
    if (moduleLength == 0 || moduleLength >= modulePath.size()) {
        MessageBoxW(nullptr, L"Could not determine the application folder.",
                    L"AudioForge", MB_OK | MB_ICONERROR);
        return 1;
    }
    modulePath.resize(moduleLength);
    g_defaultOutputFolder = fs::path(modulePath).parent_path() / L"output";
    std::error_code outputError;
    fs::create_directories(g_defaultOutputFolder, outputError);
    if (outputError) {
        MessageBoxW(nullptr,
            (L"Could not create the default output folder:\n" +
             g_defaultOutputFolder.wstring()).c_str(),
            L"AudioForge", MB_OK | MB_ICONERROR);
        return 1;
    }
    INITCOMMONCONTROLSEX controls{sizeof(controls), ICC_LISTVIEW_CLASSES | ICC_PROGRESS_CLASS};
    InitCommonControlsEx(&controls);

    WNDCLASSEXW windowClass{};
    windowClass.cbSize = sizeof(windowClass);
    windowClass.style = CS_HREDRAW | CS_VREDRAW;
    windowClass.lpfnWndProc = windowProcedure;
    windowClass.hInstance = instance;
    windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    windowClass.hIcon = LoadIconW(instance,
        MAKEINTRESOURCEW(kApplicationIconResource));
    windowClass.hIconSm = windowClass.hIcon;
    windowClass.hbrBackground = nullptr;
    windowClass.lpszClassName = kClassName;
    if (!RegisterClassExW(&windowClass)) {
        MessageBoxW(nullptr, L"Could not register the application window.",
                    L"AudioForge", MB_OK | MB_ICONERROR);
        return 1;
    }
    RECT workArea{};
    if (!SystemParametersInfoW(SPI_GETWORKAREA, 0, &workArea, 0)) {
        workArea = {0, 0, GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN)};
    }
    const int workWidth = workArea.right - workArea.left;
    const int workHeight = workArea.bottom - workArea.top;
    const int initialWidth = (std::max)(760, (std::min)(1180, workWidth - 32));
    const int initialHeight = (std::max)(540, (std::min)(800, workHeight - 32));
    const int initialX = workArea.left + (workWidth - initialWidth) / 2;
    const int initialY = workArea.top + (workHeight - initialHeight) / 2;
    HWND window = CreateWindowExW(WS_EX_ACCEPTFILES, kClassName, L"AudioForge",
        WS_OVERLAPPEDWINDOW, initialX, initialY, initialWidth, initialHeight,
        nullptr, nullptr, instance, nullptr);
    if (!window) {
        MessageBoxW(nullptr, L"Could not create the application window.",
                    L"AudioForge", MB_OK | MB_ICONERROR);
        return 1;
    }
    g_window = window;
    startArtworkWorker();
    startMetadataWorker();
    setDarkTitleBar(window);
    ShowWindow(window, showCommand);
    UpdateWindow(window);

    MSG message{};
    while (GetMessageW(&message, nullptr, 0, 0) > 0) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }

    if (g_font) DeleteObject(g_font);
    if (g_rowImages) ImageList_Destroy(g_rowImages);
    return static_cast<int>(message.wParam);
}
