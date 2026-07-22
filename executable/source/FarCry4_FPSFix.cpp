#include <windows.h>
#include <tlhelp32.h>
#include <mmsystem.h>
#include <shlobj.h>

#include <algorithm>
#include <chrono>
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

static std::wstring ProcessBaseName() {
    auto name = g_config.processName;
    const auto separator = name.find_last_of(L"\\/");
    if (separator != std::wstring::npos) name = name.substr(separator + 1);
    if (name.size() > 4 && ToLower(name.substr(name.size() - 4)) == L".exe") name.resize(name.size() - 4);
    return name;
}

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

static void ApplyProcessSettings(const std::vector<DWORD>& pids) {
    const auto priority = PriorityClass(g_config.priority);
    for (DWORD pid : pids) {
        HANDLE process = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_SET_INFORMATION | PROCESS_SET_QUOTA, FALSE, pid);
        if (!process) { Log(L"Could not open game process for priority adjustment."); continue; }
        if (!SetPriorityClass(process, priority)) Log(L"Could not set process priority.");
        CloseHandle(process);
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

    for (DWORD pid : pids) {
        HANDLE process = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_SET_INFORMATION, FALSE, pid);
        if (!process || !SetProcessAffinityMask(process, mask)) Log(L"Could not set CPU affinity.");
        if (process) CloseHandle(process);
    }
    Log(L"CPU detected: " + vendor + L" | " + std::to_wstring(logical) + L" logical processors");
    Log(L"Affinity set. Mask=" + std::to_wstring(static_cast<unsigned long long>(mask)) + L" | Using " + vendor + L" core profile.");
}

static void TrimWorkingSets(const std::vector<DWORD>& pids) {
    for (DWORD pid : pids) {
        HANDLE process = OpenProcess(PROCESS_SET_QUOTA, FALSE, pid);
        if (process) { SetProcessWorkingSetSize(process, static_cast<SIZE_T>(-1), static_cast<SIZE_T>(-1)); CloseHandle(process); }
    }
}

static bool RunHidden(const std::wstring& command, std::wstring* output = nullptr) {
    SECURITY_ATTRIBUTES security{sizeof(security), nullptr, TRUE};
    HANDLE readPipe = nullptr, writePipe = nullptr;
    if (output && (!CreatePipe(&readPipe, &writePipe, &security, 0) || !SetHandleInformation(readPipe, HANDLE_FLAG_INHERIT, 0))) return false;
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESHOWWINDOW | (output ? STARTF_USESTDHANDLES : 0);
    startup.wShowWindow = SW_HIDE;
    if (output) { startup.hStdOutput = writePipe; startup.hStdError = writePipe; startup.hStdInput = GetStdHandle(STD_INPUT_HANDLE); }
    PROCESS_INFORMATION process{};
    std::vector<wchar_t> commandLine(command.begin(), command.end()); commandLine.push_back(L'\0');
    const BOOL started = CreateProcessW(nullptr, commandLine.data(), nullptr, nullptr, output != nullptr, CREATE_NO_WINDOW, nullptr, nullptr, &startup, &process);
    if (writePipe) CloseHandle(writePipe);
    if (!started) { if (readPipe) CloseHandle(readPipe); return false; }
    if (output) {
        wchar_t buffer[256]; DWORD read = 0;
        while (ReadFile(readPipe, buffer, sizeof(buffer) - sizeof(wchar_t), &read, nullptr) && read) output->append(buffer, read / sizeof(wchar_t));
        CloseHandle(readPipe);
    }
    WaitForSingleObject(process.hProcess, INFINITE);
    CloseHandle(process.hThread); CloseHandle(process.hProcess);
    return true;
}

static std::wstring ActivePowerPlan() {
    std::wstring output;
    if (!RunHidden(L"powercfg /getactivescheme", &output)) return L"";
    std::wregex guid(LR"(GUID:\s*([0-9a-fA-F-]+))");
    std::wsmatch match;
    return std::regex_search(output, match, guid) ? match[1].str() : L"";
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

static int StartBackgroundWorker() {
    wchar_t executable[MAX_PATH]{};
    if (!GetModuleFileNameW(nullptr, executable, MAX_PATH)) return 1;
    std::wstring command = L"\"" + std::wstring(executable) + L"\" --worker";
    STARTUPINFOW startup{}; startup.cb = sizeof(startup); startup.dwFlags = STARTF_USESHOWWINDOW; startup.wShowWindow = SW_HIDE;
    PROCESS_INFORMATION process{};
    std::vector<wchar_t> commandLine(command.begin(), command.end()); commandLine.push_back(L'\0');
    if (!CreateProcessW(nullptr, commandLine.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, g_root.c_str(), &startup, &process)) return 1;
    CloseHandle(process.hThread); CloseHandle(process.hProcess); return 0;
}

static int RunOptimizer() {
    g_config = LoadSettings();
    Log(L"=== FarCry4-FPSFix started ===");
    ApplyXmlPerformanceSettings();
    bool timerSet = false;
    std::wstring savedPowerPlan;
    if (g_config.enableTimerResolution) { timerSet = timeBeginPeriod(1) == TIMERR_NOERROR; Log(timerSet ? L"Timer resolution set to 1ms." : L"Could not set timer resolution."); }
    if (g_config.changePowerPlan) { savedPowerPlan = ActivePowerPlan(); RunHidden(L"powercfg /setactive 8c5e7fda-e8bf-4a96-9a85-a6e23a8c635c"); Log(L"Power plan switched to High Performance."); }

    Log(L"Waiting for " + g_config.processName + L".exe to launch...");
    std::vector<DWORD> processes;
    for (int elapsed = 0; elapsed < 300 && processes.empty(); elapsed += 2) {
        processes = FindGameProcesses();
        if (processes.empty()) Sleep(2000);
    }
    if (processes.empty()) { Log(L"Timed out waiting for game. Exiting."); if (timerSet) timeEndPeriod(1); if (!savedPowerPlan.empty()) RunHidden(L"powercfg /setactive " + savedPowerPlan); return 1; }
    Log(L"Game detected. Waiting " + std::to_wstring(g_config.waitSecondsAfterLaunch) + L"s for full initialization...");
    Sleep(static_cast<DWORD>(g_config.waitSecondsAfterLaunch) * 1000);
    processes = FindGameProcesses();
    if (processes.empty()) { Log(L"Process ended before optimization could apply."); if (timerSet) timeEndPeriod(1); if (!savedPowerPlan.empty()) RunHidden(L"powercfg /setactive " + savedPowerPlan); return 1; }
    ApplyProcessSettings(processes);
    Log(L"All optimizations applied. Monitoring until game exits...");
    int cleanupElapsed = 0;
    while (!(processes = FindGameProcesses()).empty()) {
        Sleep(5000); cleanupElapsed += 5;
        if (g_config.enableMemoryCleanup && cleanupElapsed >= g_config.memoryCleanupIntervalSeconds) { cleanupElapsed = 0; TrimWorkingSets(processes); Log(L"Memory working-set trimmed."); }
    }
    Log(L"Game exited. Restoring settings...");
    if (timerSet) { timeEndPeriod(1); Log(L"Timer resolution restored to Windows default."); }
    if (!savedPowerPlan.empty()) { RunHidden(L"powercfg /setactive " + savedPowerPlan); Log(L"Power plan restored."); }
    Log(L"Process Ended.");
    return 0;
}

int wmain(int argc, wchar_t* argv[]) {
    wchar_t module[MAX_PATH]{}; GetModuleFileNameW(nullptr, module, MAX_PATH); g_root = fs::path(module).parent_path();
    const std::wstring argument = argc > 1 ? ToLower(argv[1]) : L"";
    if (argument == L"--help" || argument == L"-h" || argument == L"/?") {
        std::wcout << L"FarCry4_FPSFix\n  (no argument)           Apply XML performance settings and run visibly.\n  --background             Start hidden, then exit.\n  --ubisoft                Start hidden and open Ubisoft Connect.\n  --restore-xml-settings   Restore XML files backed up by this tool.\n";
        return 0;
    }
    if (argument == L"--background") return StartBackgroundWorker();
    if (argument == L"--ubisoft") { const int result = StartBackgroundWorker(); if (!result) ShellExecuteW(nullptr, L"open", L"uplay://launch/9/0", nullptr, nullptr, SW_SHOWNORMAL); return result; }
    if (argument == L"--restore-xml-settings") return RestoreXmlPerformanceSettings();
    return RunOptimizer();
}
