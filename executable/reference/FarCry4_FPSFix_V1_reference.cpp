// FarCry4_FPSFix
//
// Applies CPU affinity, process priority, timer resolution, power-plan and
// XML tweaks for Far Cry 4, then waits for the game to exit and restores
// anything it changed.
//
// Design notes for this revision:
//  - The executable targets the Windows (GUI) subsystem instead of the
//    console subsystem. It never allocates a console unless the user is
//    running it interactively (no console flicker, no need to relaunch a
//    hidden copy of itself just to suppress a window).
//  - Power-plan changes go through the PowrProf API directly instead of
//    shelling out to powercfg.exe, removing a CreateProcess call and the
//    CREATE_NO_WINDOW flag that came with it.
//  - Once the game process is found, a single process HANDLE (opened with
//    only the access rights actually used) is kept for the rest of that
//    process's lifetime, instead of repeatedly opening and closing handles
//    for priority, affinity, trimming, and monitoring.
//  - "Wait for game to exit" is event-driven: WaitForMultipleObjects on the
//    open process handles, not a Sleep/rescan loop. Two phases still use a
//    bounded Sleep/rescan instead: "wait for the game to launch" (no handle
//    exists yet, since Ubisoft Connect starts the process, not this tool)
//    and the post-launch "wait for full initialization" delay, which must
//    stay a plain timed wait followed by a fresh process-name scan, because
//    Ubisoft Connect's DRM/loader chain can exit the initial process and
//    hand off to a new PID under the same name while still starting up;
//    watching a handle opened before that handoff would misread it as the
//    game closing early and skip applying settings entirely.
//  - The "--background"/"--ubisoft" launch modes still relaunch a detached
//    copy of this executable, because Ubisoft Connect (and any shortcut
//    that invokes this tool as a pre-launch step) expects that call to
//    return immediately rather than block for the entire play session.
//    That relaunch no longer needs CREATE_NO_WINDOW: since the executable
//    is a GUI-subsystem app, a plain CreateProcess of it never shows a
//    window or console in the first place.

#include <windows.h>
#include <tlhelp32.h>
#include <mmsystem.h>
#include <shlobj.h>
#include <powrprof.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <regex>
#include <sstream>
#include <string>
#include <vector>

#pragma comment(lib, "winmm.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "powrprof.lib")
#pragma comment(linker, "/SUBSYSTEM:WINDOWS")

namespace fs = std::filesystem;

struct Settings {
    std::wstring processName = L"farcry4";
    std::wstring priority = L"High";
    bool useSmartAffinity = true;
    int waitSecondsAfterLaunch = 30;
    bool enableMemoryCleanup = false;
    int memoryCleanupIntervalSeconds = 120;
    bool enableTimerResolution = true;
    bool changePowerPlan = false;
    bool logToFile = false;
};

// A game process we are actively managing. The handle is opened once, with
// only the rights we need (set priority/affinity/quota, plus SYNCHRONIZE so
// we can wait on it), and reused for the process's entire lifetime.
struct GameProcess {
    DWORD pid = 0;
    HANDLE handle = nullptr;
};

static Settings g_config;
static fs::path g_root;

static std::wstring ToLower(std::wstring value) {
    std::transform(value.begin(), value.end(), value.begin(), [](wchar_t c) { return static_cast<wchar_t>(towlower(c)); });
    return value;
}

static std::string ReadText(const fs::path& path) {
    std::ifstream file(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
}

static bool WriteText(const fs::path& path, const std::string& text) {
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    if (!file) return false;
    file.write(text.data(), static_cast<std::streamsize>(text.size()));
    return static_cast<bool>(file);
}

static std::wstring Utf8ToWide(const std::string& value) {
    if (value.empty()) return L"";
    int size = MultiByteToWideChar(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), nullptr, 0);
    if (size == 0) return L"";
    std::wstring result(size, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), result.data(), size);
    return result;
}

static std::optional<std::string> JsonString(const std::string& json, const std::string& key) {
    std::regex expression("\\\"" + key + "\\\"\\s*:\\s*\\\"([^\\\"]*)\\\"", std::regex::icase);
    std::smatch match;
    return std::regex_search(json, match, expression) ? std::optional<std::string>(match[1].str()) : std::nullopt;
}

static std::optional<bool> JsonBool(const std::string& json, const std::string& key) {
    std::regex expression("\\\"" + key + "\\\"\\s*:\\s*(true|false)", std::regex::icase);
    std::smatch match;
    if (!std::regex_search(json, match, expression)) return std::nullopt;
    return ToLower(Utf8ToWide(match[1].str())) == L"true";
}

static std::optional<int> JsonInt(const std::string& json, const std::string& key) {
    std::regex expression("\\\"" + key + "\\\"\\s*:\\s*(-?[0-9]+)", std::regex::icase);
    std::smatch match;
    if (!std::regex_search(json, match, expression)) return std::nullopt;
    try { return std::stoi(match[1].str()); } catch (...) { return std::nullopt; }
}

static Settings LoadSettings() {
    Settings result;
    const auto path = g_root / L"config" / L"settings.json";
    const auto json = ReadText(path);
    if (json.empty()) return result;
    if (auto value = JsonString(json, "ProcessName")) result.processName = Utf8ToWide(*value);
    if (auto value = JsonString(json, "Priority")) result.priority = Utf8ToWide(*value);
    if (auto value = JsonBool(json, "UseSmartAffinity")) result.useSmartAffinity = *value;
    if (auto value = JsonInt(json, "WaitSecondsAfterLaunch")) result.waitSecondsAfterLaunch = std::max(0, *value);
    if (auto value = JsonBool(json, "EnableMemoryCleanup")) result.enableMemoryCleanup = *value;
    if (auto value = JsonInt(json, "MemoryCleanupIntervalSeconds")) result.memoryCleanupIntervalSeconds = std::max(1, *value);
    if (auto value = JsonBool(json, "EnableTimerResolution")) result.enableTimerResolution = *value;
    if (auto value = JsonBool(json, "ChangePowerPlan")) result.changePowerPlan = *value;
    if (auto value = JsonBool(json, "LogToFile")) result.logToFile = *value;
    return result;
}

static void Log(const std::wstring& message) {
    SYSTEMTIME now{};
    GetLocalTime(&now);
    wchar_t line[2048]{};
    swprintf_s(line, L"[%02u:%02u:%02u] %s", now.wHour, now.wMinute, now.wSecond, message.c_str());
    std::wcout << line << L'\n';
    if (g_config.logToFile) {
        std::wofstream log(g_root / L"optimizer.log", std::ios::app);
        if (log) log << line << L'\n';
    }
}

// Attaches a real console only when the user is running the tool
// interactively (default invocation, --help, --restore-xml-settings). The
// silent modes (--worker, --background handoff) never call this, so they
// never allocate or show a console window at all.
static void AttachVisibleConsole() {
    if (!AllocConsole()) return;
    FILE* stream = nullptr;
    freopen_s(&stream, "CONOUT$", "w", stdout);
    freopen_s(&stream, "CONOUT$", "w", stderr);
    freopen_s(&stream, "CONIN$", "r", stdin);
    std::wcout.clear();
    std::wcin.clear();
    std::wcerr.clear();
}

static std::wstring ProcessBaseName() {
    auto name = g_config.processName;
    const auto separator = name.find_last_of(L"\\/");
    if (separator != std::wstring::npos) name = name.substr(separator + 1);
    if (name.size() > 4 && ToLower(name.substr(name.size() - 4)) == L".exe") name.resize(name.size() - 4);
    return name;
}

// The game is launched by Ubisoft Connect, not by this tool, so there is no
// handle to wait on until the process actually exists. A Toolhelp32 snapshot
// is the lightest-weight way to check for it by name; this is only used
// during the bounded "wait for launch" phase and for the one-off
// already-running checks below, never as an ongoing monitoring loop.
static std::vector<DWORD> FindGameProcesses() {
    std::vector<DWORD> pids;
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE) return pids;
    PROCESSENTRY32W entry{};
    entry.dwSize = sizeof(entry);
    const auto expected = ToLower(ProcessBaseName() + L".exe");
    for (BOOL more = Process32FirstW(snapshot, &entry); more; more = Process32NextW(snapshot, &entry)) {
        if (ToLower(entry.szExeFile) == expected) pids.push_back(entry.th32ProcessID);
    }
    CloseHandle(snapshot);
    return pids;
}

// Polls (bounded, at a modest interval) until the game process appears or
// the timeout elapses. This is the one phase that cannot be made
// event-driven, since we don't own the process and have no handle yet.
static std::vector<DWORD> WaitForGameLaunch(int timeoutSeconds, int pollIntervalSeconds) {
    for (int elapsed = 0; elapsed < timeoutSeconds; elapsed += pollIntervalSeconds) {
        auto pids = FindGameProcesses();
        if (!pids.empty()) return pids;
        Sleep(static_cast<DWORD>(pollIntervalSeconds) * 1000);
    }
    return FindGameProcesses();
}

static std::vector<GameProcess> OpenGameProcesses(const std::vector<DWORD>& pids) {
    std::vector<GameProcess> result;
    result.reserve(pids.size());
    for (DWORD pid : pids) {
        HANDLE handle = OpenProcess(PROCESS_SET_INFORMATION | PROCESS_SET_QUOTA | SYNCHRONIZE, FALSE, pid);
        if (handle) {
            result.push_back({pid, handle});
        } else {
            Log(L"Could not open game process (pid " + std::to_wstring(pid) + L").");
        }
    }
    return result;
}

static void CloseGameProcesses(std::vector<GameProcess>& processes) {
    for (auto& process : processes) {
        if (process.handle) CloseHandle(process.handle);
    }
    processes.clear();
}

static std::wstring CpuVendor() {
    wchar_t value[256]{};
    DWORD size = sizeof(value);
    if (RegGetValueW(HKEY_LOCAL_MACHINE, L"HARDWARE\\DESCRIPTION\\System\\CentralProcessor\\0", L"VendorIdentifier", RRF_RT_REG_SZ, nullptr, value, &size) == ERROR_SUCCESS && ToLower(value).find(L"amd") != std::wstring::npos) return L"AMD";
    return L"Intel";
}

static DWORD PriorityClass(const std::wstring& name) {
    const auto value = ToLower(name);
    if (value == L"idle") return IDLE_PRIORITY_CLASS;
    if (value == L"belownormal") return BELOW_NORMAL_PRIORITY_CLASS;
    if (value == L"normal") return NORMAL_PRIORITY_CLASS;
    if (value == L"abovenormal") return ABOVE_NORMAL_PRIORITY_CLASS;
    if (value == L"realtime") return REALTIME_PRIORITY_CLASS;
    return HIGH_PRIORITY_CLASS;
}

// Applies priority and CPU affinity once, using the already-open handles.
static void ApplyProcessSettings(const std::vector<GameProcess>& processes) {
    const auto priority = PriorityClass(g_config.priority);
    for (const auto& process : processes) {
        if (!SetPriorityClass(process.handle, priority)) Log(L"Could not set process priority.");
    }
    Log(L"Priority set to: " + g_config.priority);
    if (!g_config.useSmartAffinity) return;

    SYSTEM_INFO info{};
    GetSystemInfo(&info);
    const int logical = static_cast<int>(info.dwNumberOfProcessors);
    const auto vendor = CpuVendor();
    const std::vector<int> coreIndexes = vendor == L"AMD" ? std::vector<int>{2, 4, 6, 8} : std::vector<int>{0, 2, 4, 6};
    DWORD_PTR mask = 0;
    for (int index : coreIndexes) if (index < logical && index < static_cast<int>(sizeof(DWORD_PTR) * 8)) mask |= (static_cast<DWORD_PTR>(1) << index);
    if (mask == 0) for (int index = 1; index < std::min(logical, static_cast<int>(sizeof(DWORD_PTR) * 8)); ++index) mask |= (static_cast<DWORD_PTR>(1) << index);

    for (const auto& process : processes) {
        if (!SetProcessAffinityMask(process.handle, mask)) Log(L"Could not set CPU affinity.");
    }
    Log(L"CPU detected: " + vendor + L" | " + std::to_wstring(logical) + L" logical processors");
    Log(L"Affinity set. Mask=" + std::to_wstring(static_cast<unsigned long long>(mask)) + L" | Using " + vendor + L" core profile.");
}

static void TrimWorkingSets(const std::vector<GameProcess>& processes) {
    for (const auto& process : processes) {
        SetProcessWorkingSetSize(process.handle, static_cast<SIZE_T>(-1), static_cast<SIZE_T>(-1));
    }
}

// Blocks, event-driven, until every watched game process has exited. If
// memory cleanup is enabled, the wait is sliced into intervals so the
// working set can be trimmed periodically; otherwise this is a single
// indefinite wait with zero wakeups until the game closes.
static void WaitForGameExit(const std::vector<GameProcess>& processes) {
    if (processes.empty()) return;
    std::vector<HANDLE> handles;
    handles.reserve(processes.size());
    for (const auto& process : processes) handles.push_back(process.handle);
    if (handles.size() > MAXIMUM_WAIT_OBJECTS) {
        Log(L"More game processes than can be waited on at once; monitoring the first " + std::to_wstring(MAXIMUM_WAIT_OBJECTS) + L".");
        handles.resize(MAXIMUM_WAIT_OBJECTS);
    }
    const DWORD count = static_cast<DWORD>(handles.size());
    const DWORD waitSliceMs = g_config.enableMemoryCleanup
        ? static_cast<DWORD>(g_config.memoryCleanupIntervalSeconds) * 1000
        : INFINITE;
    for (;;) {
        const DWORD result = WaitForMultipleObjects(count, handles.data(), TRUE, waitSliceMs);
        if (result == WAIT_TIMEOUT) {
            TrimWorkingSets(processes);
            Log(L"Memory working-set trimmed.");
            continue;
        }
        break; // WAIT_OBJECT_0 (all processes exited) or an unexpected result: either way, stop waiting.
    }
}

// High performance power scheme GUID (standard Windows built-in scheme).
static const GUID kHighPerformanceScheme = {
    0x8c5e7fda, 0xe8bf, 0x4a96, {0x9a, 0x85, 0xa6, 0xe2, 0x3a, 0x8c, 0x63, 0x5c}};

static std::optional<GUID> ActivePowerScheme() {
    GUID* active = nullptr;
    if (PowerGetActiveScheme(nullptr, &active) != ERROR_SUCCESS || !active) return std::nullopt;
    const GUID result = *active;
    LocalFree(active);
    return result;
}

static bool SetPowerScheme(const GUID& scheme) {
    return PowerSetActiveScheme(nullptr, const_cast<GUID*>(&scheme)) == ERROR_SUCCESS;
}

static std::vector<fs::path> FindProfileFiles(const std::wstring& name) {
    PWSTR documents = nullptr;
    std::vector<fs::path> result;
    if (FAILED(SHGetKnownFolderPath(FOLDERID_Documents, 0, nullptr, &documents))) return result;
    fs::path root = fs::path(documents) / L"My Games" / L"Far Cry 4";
    CoTaskMemFree(documents);
    std::error_code error;
    if (!fs::exists(root, error)) return result;
    for (fs::recursive_directory_iterator it(root, fs::directory_options::skip_permission_denied, error), end; it != end; it.increment(error)) {
        if (!error && it->is_regular_file(error) && ToLower(it->path().filename().wstring()) == ToLower(name)) result.push_back(it->path());
        error.clear();
    }
    return result;
}

static bool BackupThenWrite(const fs::path& path, const std::string& updated) {
    const auto backup = path.wstring() + L".fc4fpsfix.backup";
    std::error_code error;
    if (!fs::exists(backup, error)) fs::copy_file(path, backup, fs::copy_options::none, error);
    if (error) { Log(L"Could not create XML backup: " + path.wstring()); return false; }
    if (!WriteText(path, updated)) { Log(L"Could not update XML file: " + path.wstring()); return false; }
    return true;
}

static void ApplyXmlPerformanceSettings() {
    if (!FindGameProcesses().empty()) { Log(L"Game already running; XML performance settings were not changed."); return; }
    int updates = 0;
    const std::regex mip(R"(DisableLoadingMip0\s*=\s*"[^"]*")", std::regex::icase);
    const std::regex buffered(R"(GPUMaxBufferedFrames\s*=\s*"[^"]*")", std::regex::icase);
    for (const auto& file : FindProfileFiles(L"GamerProfile.xml")) {
        const auto source = ReadText(file);
        auto updated = std::regex_replace(source, mip, "DisableLoadingMip0=\"1\"");
        updated = std::regex_replace(updated, buffered, "GPUMaxBufferedFrames=\"1\"");
        if (updated != source && BackupThenWrite(file, updated)) { Log(L"Applied GamerProfile performance settings: " + file.wstring()); ++updates; }
    }
    const std::regex geometry(R"(<OPTION\b(?=[^>]*\bName\s*=\s*"GEOMETRY")(?=[^>]*\bType\s*=\s*"Enum")[^>]*>)", std::regex::icase);
    const std::regex value(R"((\bValue\s*=\s*")[^"]*("))", std::regex::icase);
    for (const auto& file : FindProfileFiles(L"GFXSettings.FarCry464.xml")) {
        const auto source = ReadText(file);
        std::string updated; size_t position = 0; bool found = false; bool changed = false;
        for (std::sregex_iterator it(source.begin(), source.end(), geometry), end; it != end; ++it) {
            const auto match = *it; found = true;
            updated.append(source, position, static_cast<size_t>(match.position()) - position);
            const auto tag = match.str();
            const auto replacement = std::regex_replace(tag, value, "$1high$2");
            if (replacement != tag) changed = true;
            updated += replacement; position = static_cast<size_t>(match.position() + match.length());
        }
        if (found) updated.append(source, position, std::string::npos);
        if (changed && BackupThenWrite(file, updated)) { Log(L"Set Geometry to High: " + file.wstring()); ++updates; }
    }
    Log(updates ? L"XML performance settings checked and updated." : L"XML performance settings already applied or profile files are not available yet.");
}

static int RestoreXmlPerformanceSettings() {
    if (!FindGameProcesses().empty()) { std::wcerr << L"Close Far Cry 4 before restoring XML settings.\n"; return 1; }
    int restored = 0;
    for (const auto& name : {L"GamerProfile.xml", L"GFXSettings.FarCry464.xml"}) {
        for (const auto& file : FindProfileFiles(name)) {
            const auto backup = file.wstring() + L".fc4fpsfix.backup";
            std::error_code error;
            if (!fs::exists(backup, error)) continue;
            fs::copy_file(backup, file, fs::copy_options::overwrite_existing, error);
            if (!error) { fs::remove(backup, error); std::wcout << L"Restored: " << file.wstring() << L'\n'; ++restored; }
        }
    }
    if (!restored) std::wcerr << L"No FC4-FPSFix XML backup was found.\n";
    return restored ? 0 : 1;
}

// Relaunches this executable, detached, running the actual monitoring
// ("--worker"). Needed because callers of --background/--ubisoft (Ubisoft
// Connect pre-launch hooks, shortcuts) expect the invocation to return
// immediately rather than block for the whole play session. Because this
// executable is a GUI-subsystem application, the relaunched copy never
// shows a window or console on its own, so no window-hiding creation flags
// are required.
static bool LaunchDetachedWorker() {
    wchar_t executable[MAX_PATH]{};
    if (!GetModuleFileNameW(nullptr, executable, MAX_PATH)) return false;
    std::wstring command = L"\"" + std::wstring(executable) + L"\" --worker";
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION process{};
    std::vector<wchar_t> commandLine(command.begin(), command.end());
    commandLine.push_back(L'\0');
    const BOOL started = CreateProcessW(nullptr, commandLine.data(), nullptr, nullptr, FALSE, 0, nullptr, g_root.c_str(), &startup, &process);
    if (!started) return false;
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess); // The worker is independent; we don't track its lifetime.
    return true;
}

static int RunOptimizer() {
    g_config = LoadSettings();
    Log(L"=== FarCry4-FPSFix started ===");
    ApplyXmlPerformanceSettings();

    bool timerSet = false;
    std::optional<GUID> savedPowerScheme;
    if (g_config.enableTimerResolution) {
        timerSet = timeBeginPeriod(1) == TIMERR_NOERROR;
        Log(timerSet ? L"Timer resolution set to 1ms." : L"Could not set timer resolution.");
    }
    if (g_config.changePowerPlan) {
        savedPowerScheme = ActivePowerScheme();
        Log(SetPowerScheme(kHighPerformanceScheme) ? L"Power plan switched to High Performance." : L"Could not switch power plan.");
    }

    const auto restoreSystemSettings = [&]() {
        if (timerSet) { timeEndPeriod(1); Log(L"Timer resolution restored to Windows default."); }
        if (savedPowerScheme) { Log(SetPowerScheme(*savedPowerScheme) ? L"Power plan restored." : L"Could not restore power plan."); }
    };

    Log(L"Waiting for " + g_config.processName + L".exe to launch...");
    if (WaitForGameLaunch(300, 2).empty()) {
        Log(L"Timed out waiting for game. Exiting.");
        restoreSystemSettings();
        return 1;
    }

    // Deliberately a plain, fixed-duration wait rather than watching a
    // handle: Ubisoft Connect's DRM/loader chain can exit the initial
    // farcry4.exe process and hand off to a new one (same name, different
    // PID) while the game is still starting up. Waiting on a handle opened
    // to the initial process would misread that normal handoff as the game
    // closing early. Re-scanning by name after the delay, instead of
    // reusing whatever we found before it, is what makes this reliable.
    Log(L"Game detected. Waiting " + std::to_wstring(g_config.waitSecondsAfterLaunch) + L"s for full initialization...");
    Sleep(static_cast<DWORD>(g_config.waitSecondsAfterLaunch) * 1000);

    const auto pids = FindGameProcesses();
    if (pids.empty()) {
        Log(L"Process ended before optimization could apply.");
        restoreSystemSettings();
        return 1;
    }

    auto processes = OpenGameProcesses(pids);
    if (processes.empty()) {
        Log(L"Could not open any game process handles. Exiting.");
        restoreSystemSettings();
        return 1;
    }

    ApplyProcessSettings(processes);
    Log(L"All optimizations applied. Monitoring until game exits...");
    WaitForGameExit(processes);
    CloseGameProcesses(processes);

    Log(L"Game exited. Restoring settings...");
    restoreSystemSettings();
    Log(L"Process Ended.");
    return 0;
}

int APIENTRY wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) {
    wchar_t module[MAX_PATH]{};
    GetModuleFileNameW(nullptr, module, MAX_PATH);
    g_root = fs::path(module).parent_path();

    const std::wstring argument = __argc > 1 ? ToLower(__wargv[1]) : L"";

    if (argument == L"--help" || argument == L"-h" || argument == L"/?") {
        AttachVisibleConsole();
        std::wcout << L"FarCry4_FPSFix\n  (no argument)           Apply XML performance settings and run visibly.\n  --background             Start hidden, then exit.\n  --ubisoft                Start hidden and open Ubisoft Connect.\n  --restore-xml-settings   Restore XML files backed up by this tool.\n";
        return 0;
    }
    if (argument == L"--restore-xml-settings") {
        AttachVisibleConsole();
        return RestoreXmlPerformanceSettings();
    }
    if (argument == L"--background") {
        return LaunchDetachedWorker() ? 0 : 1;
    }
    if (argument == L"--ubisoft") {
        const bool launched = LaunchDetachedWorker();
        if (launched) ShellExecuteW(nullptr, L"open", L"uplay://launch/9/0", nullptr, nullptr, SW_SHOWNORMAL);
        return launched ? 0 : 1;
    }
    if (argument != L"--worker") {
        // Default (no argument) and any unrecognized argument: run visibly,
        // matching the original tool's fallback behavior.
        AttachVisibleConsole();
    }
    return RunOptimizer();
}
