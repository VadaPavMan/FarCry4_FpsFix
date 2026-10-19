#define NOMINMAX
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0A00
#endif
#ifndef WINVER
#define WINVER _WIN32_WINNT
#endif
#include <windows.h>
#include <tlhelp32.h>
#include <mmsystem.h>
#include <shlobj.h>
#include <shellapi.h>
#include <psapi.h>
#include <winreg.h>

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cwctype>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <optional>
#include <regex>
#include <string>
#include <vector>

#pragma comment(lib, "winmm.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "psapi.lib")
#pragma comment(lib, "advapi32.lib")

namespace fs = std::filesystem;

struct Settings {
    std::wstring processName = L"farcry4;fc4";
    std::wstring priority = L"High";
    std::wstring graphicsProfile = L"Balanced"; // Balanced | Off
    // Combined V1/V2 default: restore V1-style CPU affinity, but derive the mask
    // from Windows CPU topology rather than hard-coded logical CPU indexes.
    std::wstring affinityMode = L"VendorProfile"; // All | PhysicalCores | VendorProfile
    int waitSecondsAfterLaunch = 30;
    bool enableMemoryCleanup = false;
    int memoryCleanupIntervalSeconds = 120;
    // Off by default: timer resolution is not a guaranteed game-FPS optimization.
    bool enableTimerResolution = false;
    bool changePowerPlan = false;
    bool logToFile = true;
    bool legacyAffinitySettingMapped = false;
};

static Settings g_config;
static fs::path g_root;

static std::wstring ToLower(std::wstring value) {
    std::transform(value.begin(), value.end(), value.begin(),
        [](wchar_t c) { return static_cast<wchar_t>(towlower(c)); });
    return value;
}

enum class TextEncoding {
    Utf8,
    Utf8Bom,
    Utf16LE,
    Utf16LEBom,
    Utf16BE,
    Utf16BEBom
};

struct TextDocument {
    std::string utf8;
    TextEncoding encoding = TextEncoding::Utf8;
};

static std::wstring TextEncodingName(TextEncoding encoding) {
    switch (encoding) {
    case TextEncoding::Utf8: return L"UTF-8";
    case TextEncoding::Utf8Bom: return L"UTF-8 with BOM";
    case TextEncoding::Utf16LE: return L"UTF-16LE without BOM";
    case TextEncoding::Utf16LEBom: return L"UTF-16LE with BOM";
    case TextEncoding::Utf16BE: return L"UTF-16BE without BOM";
    case TextEncoding::Utf16BEBom: return L"UTF-16BE with BOM";
    }
    return L"unknown";
}

static std::optional<std::wstring> Utf8ToWideChecked(const std::string& value) {
    if (value.empty()) return std::wstring{};
    const int size = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(),
        static_cast<int>(value.size()), nullptr, 0);
    if (size <= 0) return std::nullopt;
    std::wstring result(static_cast<size_t>(size), L'\0');
    if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(),
        static_cast<int>(value.size()), result.data(), size) != size) return std::nullopt;
    return result;
}

static std::wstring Utf8ToWide(const std::string& value) {
    const auto converted = Utf8ToWideChecked(value);
    return converted ? *converted : L"";
}

static std::optional<std::string> WideToUtf8(const std::wstring& value) {
    if (value.empty()) return std::string{};
    const int size = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value.data(),
        static_cast<int>(value.size()), nullptr, 0, nullptr, nullptr);
    if (size <= 0) return std::nullopt;
    std::string result(static_cast<size_t>(size), '\0');
    if (WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value.data(),
        static_cast<int>(value.size()), result.data(), size, nullptr, nullptr) != size) return std::nullopt;
    return result;
}

static std::optional<TextDocument> ReadTextDocument(const fs::path& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) return std::nullopt;
    const std::string raw{std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
    if (raw.empty()) return TextDocument{};

    size_t offset = 0;
    TextEncoding encoding = TextEncoding::Utf8;
    bool isUtf16LE = false;
    bool isUtf16BE = false;

    if (raw.size() >= 3 && static_cast<unsigned char>(raw[0]) == 0xEF &&
        static_cast<unsigned char>(raw[1]) == 0xBB && static_cast<unsigned char>(raw[2]) == 0xBF) {
        encoding = TextEncoding::Utf8Bom;
        offset = 3;
    } else if (raw.size() >= 2 && static_cast<unsigned char>(raw[0]) == 0xFF &&
        static_cast<unsigned char>(raw[1]) == 0xFE) {
        encoding = TextEncoding::Utf16LEBom;
        offset = 2;
        isUtf16LE = true;
    } else if (raw.size() >= 2 && static_cast<unsigned char>(raw[0]) == 0xFE &&
        static_cast<unsigned char>(raw[1]) == 0xFF) {
        encoding = TextEncoding::Utf16BEBom;
        offset = 2;
        isUtf16BE = true;
    } else {
        // Some Far Cry 4 GFXSettings files declare UTF-16 but omit the BOM.
        // Detect the characteristic alternating NUL bytes so XML tags remain searchable.
        const size_t sample = (std::min)(raw.size(), static_cast<size_t>(512));
        size_t evenNuls = 0, oddNuls = 0;
        for (size_t i = 0; i < sample; ++i) {
            if (raw[i] == '\0') (i % 2 == 0 ? evenNuls : oddNuls)++;
        }
        if (sample >= 4 && oddNuls > sample / 8 && oddNuls > evenNuls * 4) {
            encoding = TextEncoding::Utf16LE;
            isUtf16LE = true;
        } else if (sample >= 4 && evenNuls > sample / 8 && evenNuls > oddNuls * 4) {
            encoding = TextEncoding::Utf16BE;
            isUtf16BE = true;
        }
    }

    if (isUtf16LE || isUtf16BE) {
        if ((raw.size() - offset) % 2 != 0) return std::nullopt;
        std::wstring wide;
        wide.reserve((raw.size() - offset) / 2);
        for (size_t i = offset; i + 1 < raw.size(); i += 2) {
            const auto first = static_cast<unsigned char>(raw[i]);
            const auto second = static_cast<unsigned char>(raw[i + 1]);
            const uint16_t unit = isUtf16LE
                ? static_cast<uint16_t>(first | (static_cast<uint16_t>(second) << 8))
                : static_cast<uint16_t>((static_cast<uint16_t>(first) << 8) | second);
            wide.push_back(static_cast<wchar_t>(unit));
        }
        const auto converted = WideToUtf8(wide);
        if (!converted) return std::nullopt;
        return TextDocument{*converted, encoding};
    }

    const std::string utf8 = raw.substr(offset);
    if (!Utf8ToWideChecked(utf8)) return std::nullopt;
    return TextDocument{utf8, encoding};
}

static std::string ReadText(const fs::path& path) {
    const auto document = ReadTextDocument(path);
    return document ? document->utf8 : std::string{};
}

static bool WriteText(const fs::path& path, const std::string& utf8, TextEncoding encoding = TextEncoding::Utf8) {
    std::string bytes;
    if (encoding == TextEncoding::Utf8 || encoding == TextEncoding::Utf8Bom) {
        if (encoding == TextEncoding::Utf8Bom) bytes.append("\xEF\xBB\xBF", 3);
        bytes += utf8;
    } else {
        const auto wide = Utf8ToWideChecked(utf8);
        if (!wide) return false;
        const bool littleEndian = encoding == TextEncoding::Utf16LE || encoding == TextEncoding::Utf16LEBom;
        const bool hasBom = encoding == TextEncoding::Utf16LEBom || encoding == TextEncoding::Utf16BEBom;
        if (hasBom) {
            if (littleEndian) bytes.append("\xFF\xFE", 2);
            else bytes.append("\xFE\xFF", 2);
        }
        bytes.reserve(bytes.size() + wide->size() * 2);
        for (wchar_t ch : *wide) {
            const uint16_t unit = static_cast<uint16_t>(ch);
            const char low = static_cast<char>(unit & 0xFF);
            const char high = static_cast<char>((unit >> 8) & 0xFF);
            if (littleEndian) { bytes.push_back(low); bytes.push_back(high); }
            else { bytes.push_back(high); bytes.push_back(low); }
        }
    }
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    if (!file) return false;
    file.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    return static_cast<bool>(file);
}

static std::optional<std::string> JsonString(const std::string& json, const std::string& key) {
    // Deliberately supports the simple settings.json values used by this project,
    // not arbitrary escaped JSON strings.
    const std::regex expression("\\\"" + key + "\\\"\\s*:\\s*\\\"([^\\\"]*)\\\"", std::regex::icase);
    std::smatch match;
    if (std::regex_search(json, match, expression)) return match[1].str();
    return std::nullopt;
}

static std::optional<bool> JsonBool(const std::string& json, const std::string& key) {
    const std::regex expression("\\\"" + key + "\\\"\\s*:\\s*(true|false)", std::regex::icase);
    std::smatch match;
    if (!std::regex_search(json, match, expression)) return std::nullopt;
    return ToLower(Utf8ToWide(match[1].str())) == L"true";
}

static std::optional<int> JsonInt(const std::string& json, const std::string& key) {
    const std::regex expression("\\\"" + key + "\\\"\\s*:\\s*(-?[0-9]+)", std::regex::icase);
    std::smatch match;
    if (!std::regex_search(json, match, expression)) return std::nullopt;
    try { return std::stoi(match[1].str()); } catch (...) { return std::nullopt; }
}

static Settings LoadSettings() {
    Settings result;
    const fs::path path = g_root / L"config" / L"settings.json";
    const std::string json = ReadText(path);
    if (json.empty()) return result;

    if (auto value = JsonString(json, "ProcessName")) result.processName = Utf8ToWide(*value);
    if (auto value = JsonString(json, "Priority")) result.priority = Utf8ToWide(*value);
    if (auto value = JsonString(json, "GraphicsProfile")) result.graphicsProfile = Utf8ToWide(*value);
    if (auto value = JsonString(json, "AffinityMode")) {
        result.affinityMode = Utf8ToWide(*value);
    } else if (auto legacy = JsonBool(json, "UseSmartAffinity")) {
        // Backward compatibility with V1 settings.json. True now maps to a
        // topology-derived vendor profile; false preserves Windows defaults.
        result.affinityMode = *legacy ? L"VendorProfile" : L"All";
        result.legacyAffinitySettingMapped = true;
    }
    if (auto value = JsonInt(json, "WaitSecondsAfterLaunch")) result.waitSecondsAfterLaunch = std::clamp(*value, 0, 300);
    if (auto value = JsonBool(json, "EnableMemoryCleanup")) result.enableMemoryCleanup = *value;
    if (auto value = JsonInt(json, "MemoryCleanupIntervalSeconds")) result.memoryCleanupIntervalSeconds = std::clamp(*value, 30, 3600);
    if (auto value = JsonBool(json, "EnableTimerResolution")) result.enableTimerResolution = *value;
    if (auto value = JsonBool(json, "ChangePowerPlan")) result.changePowerPlan = *value;
    if (auto value = JsonBool(json, "LogToFile")) result.logToFile = *value;

    const std::wstring affinity = ToLower(result.affinityMode);
    if (affinity == L"physicalcores") result.affinityMode = L"PhysicalCores";
    else if (affinity == L"vendorprofile" || affinity == L"intelprofile" || affinity == L"amdprofile")
        result.affinityMode = L"VendorProfile";
    else result.affinityMode = L"All";
    result.graphicsProfile = ToLower(result.graphicsProfile) == L"off" ? L"Off" : L"Balanced";
    const std::wstring priority = ToLower(result.priority);
    if (priority != L"idle" && priority != L"belownormal" && priority != L"normal" &&
        priority != L"abovenormal" && priority != L"high") {
        result.priority = L"High";
    }
    return result;
}

static void Log(const std::wstring& message) {
    SYSTEMTIME now{};
    GetLocalTime(&now);
    wchar_t line[4096]{};
    swprintf_s(line, L"[%04u-%02u-%02u %02u:%02u:%02u] %s",
        now.wYear, now.wMonth, now.wDay, now.wHour, now.wMinute, now.wSecond, message.c_str());
    std::wcout << line << L'\n';
    if (g_config.logToFile) {
        std::wofstream log(g_root / L"optimizer.log", std::ios::app);
        if (log) log << line << L'\n';
    }
}

static std::wstring WinErrorText(DWORD error = GetLastError()) {
    return L" (Windows error " + std::to_wstring(error) + L")";
}

static std::vector<std::wstring> ConfiguredProcessNames() {
    std::vector<std::wstring> names;
    std::wstring item;
    for (wchar_t ch : g_config.processName) {
        if (ch == L';' || ch == L',') {
            if (!item.empty()) names.push_back(item);
            item.clear();
        } else {
            item.push_back(ch);
        }
    }
    if (!item.empty()) names.push_back(item);

    for (auto& name : names) {
        const auto separator = name.find_last_of(L"\\/");
        if (separator != std::wstring::npos) name = name.substr(separator + 1);
        if (name.size() > 4 && ToLower(name.substr(name.size() - 4)) == L".exe")
            name.resize(name.size() - 4);
        name = ToLower(name);
    }
    names.erase(std::remove_if(names.begin(), names.end(), [](const std::wstring& n) { return n.empty(); }), names.end());
    std::sort(names.begin(), names.end());
    names.erase(std::unique(names.begin(), names.end()), names.end());
    return names;
}

static std::wstring ConfiguredProcessNamesDisplay() {
    const auto names = ConfiguredProcessNames();
    std::wstring display;
    for (const auto& name : names) {
        if (!display.empty()) display += L", ";
        display += name + L".exe";
    }
    return display.empty() ? L"(none configured)" : display;
}

static bool IsConfiguredGameProcess(const std::wstring& imageName, const std::vector<std::wstring>& names) {
    const auto lowerName = ToLower(imageName);
    for (const auto& name : names) {
        if (lowerName == name || lowerName == name + L".exe") return true;
    }
    return false;
}

static std::wstring ProcessImagePath(DWORD pid) {
    HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!process) return L"(path unavailable; Windows error " + std::to_wstring(GetLastError()) + L")";
    std::wstring path(32768, L'\0');
    DWORD length = static_cast<DWORD>(path.size());
    if (!QueryFullProcessImageNameW(process, 0, path.data(), &length)) {
        const DWORD error = GetLastError();
        CloseHandle(process);
        return L"(path unavailable; Windows error " + std::to_wstring(error) + L")";
    }
    CloseHandle(process);
    path.resize(length);
    return path;
}

static void LogMatchedProcesses(const std::wstring& label, const std::vector<DWORD>& pids) {
    for (DWORD pid : pids) {
        Log(label + L": PID=" + std::to_wstring(pid) + L", image=" + ProcessImagePath(pid));
    }
}

static void LogRelevantProcesses() {
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE) {
        Log(L"Could not make diagnostic process snapshot" + WinErrorText());
        return;
    }
    Log(L"Diagnostic process list (Far Cry / FC4 / Ubisoft game launcher matches):");
    PROCESSENTRY32W entry{};
    entry.dwSize = sizeof(entry);
    for (BOOL more = Process32FirstW(snapshot, &entry); more; more = Process32NextW(snapshot, &entry)) {
        const std::wstring name = ToLower(entry.szExeFile);
        if (name.find(L"farcry") != std::wstring::npos || name == L"fc4.exe" || name.find(L"ige_wpf") != std::wstring::npos) {
            Log(L"Candidate process: PID=" + std::to_wstring(entry.th32ProcessID) + L", name=" + entry.szExeFile +
                L", image=" + ProcessImagePath(entry.th32ProcessID));
        }
    }
    CloseHandle(snapshot);
}

static std::vector<DWORD> FindGameProcesses() {
    std::vector<DWORD> pids;
    const auto names = ConfiguredProcessNames();
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE) {
        Log(L"Could not enumerate processes" + WinErrorText());
        return pids;
    }
    PROCESSENTRY32W entry{};
    entry.dwSize = sizeof(entry);
    for (BOOL more = Process32FirstW(snapshot, &entry); more; more = Process32NextW(snapshot, &entry)) {
        if (IsConfiguredGameProcess(entry.szExeFile, names)) pids.push_back(entry.th32ProcessID);
    }
    CloseHandle(snapshot);
    std::sort(pids.begin(), pids.end());
    return pids;
}

static DWORD PriorityClass(const std::wstring& name) {
    const auto value = ToLower(name);
    if (value == L"idle") return IDLE_PRIORITY_CLASS;
    if (value == L"belownormal") return BELOW_NORMAL_PRIORITY_CLASS;
    if (value == L"normal") return NORMAL_PRIORITY_CLASS;
    if (value == L"abovenormal") return ABOVE_NORMAL_PRIORITY_CLASS;
    return HIGH_PRIORITY_CLASS;
}

static std::wstring CpuVendor() {
    wchar_t value[256]{};
    DWORD size = sizeof(value);
    if (RegGetValueW(HKEY_LOCAL_MACHINE,
        L"HARDWARE\\DESCRIPTION\\System\\CentralProcessor\\0",
        L"VendorIdentifier", RRF_RT_REG_SZ, nullptr, value, &size) == ERROR_SUCCESS) {
        const std::wstring vendor = ToLower(value);
        if (vendor.find(L"amd") != std::wstring::npos) return L"AMD";
        if (vendor.find(L"intel") != std::wstring::npos) return L"Intel";
    }
    return L"Unknown";
}

static std::optional<DWORD_PTR> BuildPhysicalCoreMask() {
    // Process affinity masks address one processor group and are pointer-width limited.
    // Avoid applying a partial mask on multi-group systems; Windows should schedule normally.
    if (GetActiveProcessorGroupCount() != 1 || GetActiveProcessorCount(0) > sizeof(DWORD_PTR) * 8) {
        Log(L"Physical-core affinity skipped: this system uses multiple processor groups or exceeds the supported mask width.");
        return std::nullopt;
    }

    DWORD bytes = 0;
    GetLogicalProcessorInformationEx(RelationProcessorCore, nullptr, &bytes);
    if (GetLastError() != ERROR_INSUFFICIENT_BUFFER || bytes == 0) {
        Log(L"Could not query physical CPU topology" + WinErrorText());
        return std::nullopt;
    }
    std::vector<unsigned char> buffer(bytes);
    if (!GetLogicalProcessorInformationEx(RelationProcessorCore,
            reinterpret_cast<PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX>(buffer.data()), &bytes)) {
        Log(L"Could not read physical CPU topology" + WinErrorText());
        return std::nullopt;
    }

    DWORD_PTR mask = 0;
    size_t offset = 0;
    while (offset + sizeof(SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX) <= bytes) {
        auto* item = reinterpret_cast<PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX>(buffer.data() + offset);
        if (item->Size == 0 || offset + item->Size > bytes) break;
        if (item->Relationship == RelationProcessorCore) {
            for (WORD i = 0; i < item->Processor.GroupCount; ++i) {
                const GROUP_AFFINITY& group = item->Processor.GroupMask[i];
                if (group.Group != 0 || group.Mask == 0) continue;
                // Select one logical processor from each physical core. This is experimental,
                // and remains opt-in because SMT scheduling varies by CPU and game workload.
                const DWORD_PTR firstBit = group.Mask & (~group.Mask + 1);
                mask |= firstBit;
            }
        }
        offset += item->Size;
    }
    if (mask == 0) return std::nullopt;
    return mask;
}

static void ApplyProcessSettings(const std::vector<DWORD>& pids) {
    const DWORD priority = PriorityClass(g_config.priority);
    int priorityApplied = 0;
    for (DWORD pid : pids) {
        HANDLE process = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_SET_INFORMATION, FALSE, pid);
        if (!process) {
            Log(L"Could not open process " + std::to_wstring(pid) + L" for priority adjustment" + WinErrorText());
            continue;
        }
        if (SetPriorityClass(process, priority)) {
            ++priorityApplied;
            Log(L"Priority applied to PID " + std::to_wstring(pid) + L": " + g_config.priority);
        } else {
            Log(L"Could not set priority for PID " + std::to_wstring(pid) + WinErrorText());
        }
        CloseHandle(process);
    }
    Log(L"Priority result: " + std::to_wstring(priorityApplied) + L" of " + std::to_wstring(pids.size()) + L" process(es) updated.");

    if (g_config.affinityMode == L"All") {
        Log(L"CPU affinity unchanged (AffinityMode=All); Windows may schedule the game across available processors.");
        return;
    }

    const std::wstring vendor = CpuVendor();
    if (g_config.affinityMode == L"VendorProfile") {
        Log(L"Detected CPU vendor: " + vendor + L". Applying topology-derived " + vendor +
            L" profile (one logical processor per physical core); no hard-coded CPU indexes are used.");
    } else {
        Log(L"Applying experimental PhysicalCores affinity profile. CPU vendor: " + vendor + L".");
    }
    const auto possibleMask = BuildPhysicalCoreMask();
    if (!possibleMask) {
        Log(L"Physical-core affinity was not applied; leaving the system's default process affinity intact.");
        return;
    }
    int affinityApplied = 0;
    for (DWORD pid : pids) {
        HANDLE process = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_SET_INFORMATION, FALSE, pid);
        if (!process) {
            Log(L"Could not open process " + std::to_wstring(pid) + L" for affinity adjustment" + WinErrorText());
            continue;
        }
        if (SetProcessAffinityMask(process, *possibleMask)) {
            ++affinityApplied;
            Log(L"Experimental physical-core affinity applied to PID " + std::to_wstring(pid) + L". Mask=" +
                std::to_wstring(static_cast<unsigned long long>(*possibleMask)));
        } else {
            Log(L"Could not set affinity for PID " + std::to_wstring(pid) + WinErrorText());
        }
        CloseHandle(process);
    }
    Log(L"Affinity result: " + std::to_wstring(affinityApplied) + L" of " + std::to_wstring(pids.size()) + L" process(es) updated.");
}

static void TrimWorkingSets(const std::vector<DWORD>& pids) {
    for (DWORD pid : pids) {
        HANDLE process = OpenProcess(PROCESS_SET_QUOTA, FALSE, pid);
        if (process) {
            if (!SetProcessWorkingSetSize(process, static_cast<SIZE_T>(-1), static_cast<SIZE_T>(-1)))
                Log(L"Working-set trim failed for PID " + std::to_wstring(pid) + WinErrorText());
            CloseHandle(process);
        }
    }
    Log(L"Working-set trim requested. This option is experimental and may cause additional paging/stutter.");
}

static bool RunHidden(const std::wstring& command, std::wstring* output = nullptr) {
    SECURITY_ATTRIBUTES security{ sizeof(security), nullptr, TRUE };
    HANDLE readPipe = nullptr, writePipe = nullptr;
    if (output) {
        if (!CreatePipe(&readPipe, &writePipe, &security, 0)) return false;
        if (!SetHandleInformation(readPipe, HANDLE_FLAG_INHERIT, 0)) {
            CloseHandle(readPipe); CloseHandle(writePipe); return false;
        }
    }
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESHOWWINDOW | (output ? STARTF_USESTDHANDLES : 0);
    startup.wShowWindow = SW_HIDE;
    if (output) {
        startup.hStdOutput = writePipe;
        startup.hStdError = writePipe;
        startup.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
    }
    PROCESS_INFORMATION process{};
    std::vector<wchar_t> commandLine(command.begin(), command.end());
    commandLine.push_back(L'\0');
    const BOOL started = CreateProcessW(nullptr, commandLine.data(), nullptr, nullptr, output != nullptr,
        CREATE_NO_WINDOW, nullptr, g_root.c_str(), &startup, &process);
    if (writePipe) CloseHandle(writePipe);
    if (!started) {
        if (readPipe) CloseHandle(readPipe);
        return false;
    }
    if (output) {
        // powercfg emits the GUID in ASCII/OEM-compatible text. Read bytes, not WCHARs,
        // because an anonymous pipe is a byte stream.
        char buffer[512];
        DWORD read = 0;
        while (ReadFile(readPipe, buffer, sizeof(buffer), &read, nullptr) && read) {
            for (DWORD i = 0; i < read; ++i) output->push_back(static_cast<unsigned char>(buffer[i]));
        }
        CloseHandle(readPipe);
    }
    WaitForSingleObject(process.hProcess, INFINITE);
    DWORD exitCode = 1;
    GetExitCodeProcess(process.hProcess, &exitCode);
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    return exitCode == 0;
}

static std::wstring ActivePowerPlan() {
    std::wstring output;
    if (!RunHidden(L"powercfg /getactivescheme", &output)) return L"";
    std::wregex guid(LR"(GUID:\s*([0-9a-fA-F-]+))", std::regex::icase);
    std::wsmatch match;
    return std::regex_search(output, match, guid) ? match[1].str() : L"";
}

static std::vector<fs::path> FindProfileFiles(const std::wstring& name) {
    PWSTR documents = nullptr;
    std::vector<fs::path> result;
    if (FAILED(SHGetKnownFolderPath(FOLDERID_Documents, 0, nullptr, &documents))) return result;
    const fs::path root = fs::path(documents) / L"My Games" / L"Far Cry 4";
    CoTaskMemFree(documents);
    std::error_code error;
    if (!fs::exists(root, error)) return result;
    for (fs::recursive_directory_iterator it(root, fs::directory_options::skip_permission_denied, error), end;
         it != end; it.increment(error)) {
        if (!error && it->is_regular_file(error) && ToLower(it->path().filename().wstring()) == ToLower(name))
            result.push_back(it->path());
        error.clear();
    }
    return result;
}

static bool BackupThenWrite(const fs::path& path, const std::string& updated, TextEncoding encoding) {
    const fs::path backup = path.wstring() + L".fc4fpsfix.backup";
    std::error_code error;
    if (!fs::exists(backup, error)) fs::copy_file(path, backup, fs::copy_options::none, error);
    if (error) {
        Log(L"Could not create XML backup: " + path.wstring() + L" (error " + std::to_wstring(error.value()) + L")");
        return false;
    }
    if (!WriteText(path, updated, encoding)) {
        Log(L"Could not update XML file while preserving its original encoding: " + path.wstring());
        return false;
    }
    return true;
}

static bool SetAttributeInTag(std::string& tag, const std::string& attributeName,
    const std::string& value, bool* attributeFound = nullptr) {
    const std::regex attributeRegex("(\\b" + attributeName + "\\s*=\\s*\")[^\"]*(\")", std::regex::icase);
    std::smatch match;
    if (!std::regex_search(tag, match, attributeRegex)) {
        if (attributeFound) *attributeFound = false;
        return false;
    }
    if (attributeFound) *attributeFound = true;
    const std::regex exactValueRegex("\\b" + attributeName + "\\s*=\\s*\"" + value + "\"", std::regex::icase);
    if (std::regex_search(tag, exactValueRegex)) return false;
    const std::string replacement = match.str(1) + value + match.str(2);
    tag.replace(static_cast<size_t>(match.position(0)), static_cast<size_t>(match.length(0)), replacement);
    return true;
}

static bool SetAttributeInElement(std::string& xml, const std::string& elementName,
    const std::string& attributeName, const std::string& value, bool* elementFound = nullptr,
    bool* attributeFound = nullptr) {
    const std::regex elementRegex("<" + elementName + "\\b[^>]*>", std::regex::icase);
    std::smatch match;
    if (!std::regex_search(xml, match, elementRegex)) {
        if (elementFound) *elementFound = false;
        if (attributeFound) *attributeFound = false;
        return false;
    }
    if (elementFound) *elementFound = true;
    std::string tag = match.str();
    const bool changed = SetAttributeInTag(tag, attributeName, value, attributeFound);
    if (changed) xml.replace(static_cast<size_t>(match.position()), static_cast<size_t>(match.length()), tag);
    return changed;
}

static bool SetGfxOptionValue(std::string& xml, const std::string& optionName,
    const std::string& value, bool* optionFound = nullptr) {
    const std::regex optionRegex("<OPTION\\b[^>]*>", std::regex::icase);
    const std::regex nameRegex("\\bName\\s*=\\s*\"([^\"]*)\"", std::regex::icase);
    for (std::sregex_iterator it(xml.begin(), xml.end(), optionRegex), end; it != end; ++it) {
        const auto match = *it;
        const std::string matchedTag = match.str();
        std::smatch nameMatch;
        if (!std::regex_search(matchedTag, nameMatch, nameRegex)) continue;
        if (ToLower(Utf8ToWide(nameMatch[1].str())) != ToLower(Utf8ToWide(optionName))) continue;
        if (optionFound) *optionFound = true;
        std::string updatedTag = matchedTag;
        const bool changed = SetAttributeInTag(updatedTag, "Value", value);
        if (changed) xml.replace(static_cast<size_t>(match.position()), static_cast<size_t>(match.length()), updatedTag);
        return changed;
    }
    if (optionFound) *optionFound = false;
    return false;
}

static bool ApplyXmlPerformanceSettings() {
    if (!FindGameProcesses().empty()) {
        Log(L"Game is already running; XML settings were not changed.");
        return false;
    }

    int filesChanged = 0;
    int profilesFound = 0;
    int profilesReadable = 0;
    const auto gamerProfiles = FindProfileFiles(L"GamerProfile.xml");
    profilesFound += static_cast<int>(gamerProfiles.size());
    if (gamerProfiles.empty()) Log(L"GamerProfile.xml was not found under Documents\\My Games\\Far Cry 4.");
    for (const auto& file : gamerProfiles) {
        const auto document = ReadTextDocument(file);
        if (!document) {
            Log(L"Could not read GamerProfile XML (unsupported encoding or read error): " + file.wstring());
            continue;
        }
        ++profilesReadable;
        Log(L"Read GamerProfile using " + TextEncodingName(document->encoding) + L": " + file.wstring());
        const std::string source = document->utf8;
        std::string updated = source;
        bool changed = false;
        changed |= SetAttributeInElement(updated, "RenderProfile", "DisableLoadingMip0", "1");
        changed |= SetAttributeInElement(updated, "RenderProfile", "GPUMaxBufferedFrames", "1", nullptr, nullptr);

        if (g_config.graphicsProfile == L"Balanced") {
            // Reduce several documented GPU-cost settings while retaining Ultra textures and PCSS shadows.
            changed |= SetAttributeInElement(updated, "RenderProfile", "AlphaToCoverage", "0");
            changed |= SetAttributeInElement(updated, "RenderProfile", "SSAOLevel", "2");
            changed |= SetAttributeInElement(updated, "RenderProfile", "GodRaysLevel", "1");
            changed |= SetAttributeInElement(updated, "RenderProfile", "FurLevel", "1");
            changed |= SetAttributeInElement(updated, "RenderProfile", "TreeTessellationLevel", "0");
            changed |= SetAttributeInElement(updated, "quality", "GeometryQuality", "high");
            changed |= SetAttributeInElement(updated, "quality", "TerrainQuality", "high");
            changed |= SetAttributeInElement(updated, "quality", "VegetationQuality", "high");
            changed |= SetAttributeInElement(updated, "quality", "PostFxQuality", "high");
        }

        if (changed) {
            if (BackupThenWrite(file, updated, document->encoding)) {
                Log(L"Applied GamerProfile settings with encoding preserved: " + file.wstring());
                ++filesChanged;
            }
        } else {
            Log(L"GamerProfile already matches the selected profile, or expected attributes were missing: " + file.wstring());
        }
    }

    const auto gfxProfiles = FindProfileFiles(L"GFXSettings.FarCry464.xml");
    profilesFound += static_cast<int>(gfxProfiles.size());
    if (gfxProfiles.empty()) Log(L"GFXSettings.FarCry464.xml was not found under Documents\\My Games\\Far Cry 4.");
    for (const auto& file : gfxProfiles) {
        const auto document = ReadTextDocument(file);
        if (!document) {
            Log(L"Could not read GFXSettings XML (unsupported encoding or read error): " + file.wstring());
            continue;
        }
        ++profilesReadable;
        Log(L"Read GFXSettings using " + TextEncodingName(document->encoding) + L": " + file.wstring());
        const std::string source = document->utf8;
        std::string updated = source;
        bool changed = false;
        bool geometryFound = false;

        if (g_config.graphicsProfile == L"Balanced") {
            changed |= SetGfxOptionValue(updated, "GEOMETRY", "high", &geometryFound);
            bool found = false;
            changed |= SetGfxOptionValue(updated, "VEGETATION", "high", &found);
            changed |= SetGfxOptionValue(updated, "TERRAIN", "high", &found);
            changed |= SetGfxOptionValue(updated, "POSTFX", "high", &found);
            changed |= SetGfxOptionValue(updated, "SSAO", "2", &found);
            changed |= SetGfxOptionValue(updated, "FUR", "1", &found);
            changed |= SetGfxOptionValue(updated, "GODRAYS", "1", &found);
            changed |= SetGfxOptionValue(updated, "TREESRELIEF", "false", &found);
        }

        if (changed) {
            if (BackupThenWrite(file, updated, document->encoding)) {
                Log(L"Applied Balanced GFX options with original text encoding preserved: " + file.wstring());
                ++filesChanged;
            }
        } else if (g_config.graphicsProfile == L"Off") {
            Log(L"GraphicsProfile=Off; no optional GFX tuning applied: " + file.wstring());
        } else if (!geometryFound) {
            Log(L"GFX XML decoded successfully, but no GEOMETRY option was found: " + file.wstring());
        } else {
            Log(L"GFX settings already match the selected profile, or individual options were missing: " + file.wstring());
        }
    }
    Log(L"XML setup pass complete; files changed: " + std::to_wstring(filesChanged) +
        L", profiles found: " + std::to_wstring(profilesFound) +
        L", readable: " + std::to_wstring(profilesReadable) +
        L". Original files are backed up with .fc4fpsfix.backup.");
    if (profilesFound == 0 || profilesReadable == 0) {
        Log(L"XML PREPARATION FAILED: no readable Far Cry 4 profile was found. Confirm the game has been run at least once and check Documents\\My Games\\Far Cry 4.");
        return false;
    }
    return true;
}

static int RestoreXmlPerformanceSettings() {
    if (!FindGameProcesses().empty()) {
        std::wcerr << L"Close Far Cry 4 before restoring XML settings.\n";
        return 1;
    }
    int restored = 0;
    for (const auto& name : { L"GamerProfile.xml", L"GFXSettings.FarCry464.xml" }) {
        for (const auto& file : FindProfileFiles(name)) {
            const fs::path backup = file.wstring() + L".fc4fpsfix.backup";
            std::error_code error;
            if (!fs::exists(backup, error)) continue;
            fs::copy_file(backup, file, fs::copy_options::overwrite_existing, error);
            if (!error) {
                fs::remove(backup, error);
                std::wcout << L"Restored: " << file.wstring() << L'\n';
                ++restored;
            } else {
                std::wcerr << L"Could not restore: " << file.wstring() << L" (error " << error.value() << L")\n";
            }
        }
    }
    if (!restored) std::wcerr << L"No FC4-FPSFix XML backup was found.\n";
    return restored ? 0 : 1;
}

static void LogProcessDiagnostics(const std::vector<DWORD>& pids) {
    for (DWORD pid : pids) {
        HANDLE process = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, pid);
        if (!process) {
            Log(L"Diagnostics unavailable for PID " + std::to_wstring(pid) + WinErrorText());
            continue;
        }
        PROCESS_MEMORY_COUNTERS_EX counters{};
        counters.cb = sizeof(counters);
        if (GetProcessMemoryInfo(process, reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&counters), sizeof(counters))) {
            Log(L"Diagnostics PID " + std::to_wstring(pid) + L": working set=" +
                std::to_wstring(counters.WorkingSetSize / (1024 * 1024)) + L" MiB, private bytes=" +
                std::to_wstring(counters.PrivateUsage / (1024 * 1024)) + L" MiB");
        } else {
            Log(L"Memory diagnostics failed for PID " + std::to_wstring(pid) + WinErrorText());
        }
        CloseHandle(process);
    }
}

static int StartBackgroundWorker() {
    wchar_t executable[MAX_PATH]{};
    if (!GetModuleFileNameW(nullptr, executable, MAX_PATH)) return 1;
    const std::wstring command = L"\"" + std::wstring(executable) + L"\" --worker";
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESHOWWINDOW;
    startup.wShowWindow = SW_HIDE;
    PROCESS_INFORMATION process{};
    std::vector<wchar_t> commandLine(command.begin(), command.end());
    commandLine.push_back(L'\0');
    if (!CreateProcessW(nullptr, commandLine.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW,
            nullptr, g_root.c_str(), &startup, &process)) {
        std::wcerr << L"Could not start background worker" << WinErrorText() << L'\n';
        return 1;
    }
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    return 0;
}

static int PrepareForLaunch() {
    g_config = LoadSettings();
    Log(L"=== FarCry4-FPSFix V2 pre-launch preparation ===");
    Log(L"Config: " + (g_root / L"config" / L"settings.json").wstring());
    if (g_config.legacyAffinitySettingMapped)
        Log(L"Legacy V1 UseSmartAffinity setting mapped to AffinityMode=" + g_config.affinityMode + L".");
    if (!ApplyXmlPerformanceSettings()) {
        if (!FindGameProcesses().empty()) {
            Log(L"PRE-LAUNCH PREPARATION FAILED: Far Cry 4 is already running. Close the game completely, run SteamLauncher.bat first, then launch the game.");
        } else {
            Log(L"PRE-LAUNCH PREPARATION FAILED: profile files were missing or unreadable. Run Far Cry 4 once, then verify Documents\\My Games\\Far Cry 4 and review optimizer.log.");
        }
        return 2;
    }
    Log(L"Pre-launch preparation finished successfully. You may now launch Far Cry 4.");
    return 0;
}

class SingleInstanceGuard {
public:
    SingleInstanceGuard() {
        handle_ = CreateMutexW(nullptr, TRUE, L"Local\\FarCry4_FPSFix_V23_Optimizer");
        if (!handle_) {
            error_ = GetLastError();
            return;
        }
        if (GetLastError() == ERROR_ALREADY_EXISTS) {
            CloseHandle(handle_);
            handle_ = nullptr;
            alreadyRunning_ = true;
            return;
        }
        owns_ = true;
    }
    ~SingleInstanceGuard() {
        if (handle_) {
            if (owns_) ReleaseMutex(handle_);
            CloseHandle(handle_);
        }
    }
    bool acquired() const { return handle_ != nullptr && owns_; }
    bool alreadyRunning() const { return alreadyRunning_; }
    DWORD error() const { return error_; }
private:
    HANDLE handle_ = nullptr;
    bool owns_ = false;
    bool alreadyRunning_ = false;
    DWORD error_ = ERROR_SUCCESS;
};

static int RunOptimizer() {
    g_config = LoadSettings();
    SingleInstanceGuard singleInstance;
    if (!singleInstance.acquired()) {
        if (singleInstance.alreadyRunning()) {
            Log(L"Another optimizer instance is already active. Refusing to start a duplicate worker.");
            return 3;
        }
        Log(L"Could not create optimizer single-instance mutex" + WinErrorText(singleInstance.error()));
        return 4;
    }
    Log(L"=== FarCry4-FPSFix V2.3 combined started ===");
    Log(L"Config: " + (g_root / L"config" / L"settings.json").wstring());
    if (g_config.legacyAffinitySettingMapped)
        Log(L"Legacy V1 UseSmartAffinity setting mapped to AffinityMode=" + g_config.affinityMode + L".");
    Log(L"Configured target process names: " + ConfiguredProcessNamesDisplay());
    Log(L"Priority=" + g_config.priority + L", AffinityMode=" + g_config.affinityMode +
        L", GraphicsProfile=" + g_config.graphicsProfile +
        L", TimerResolution=" + std::wstring(g_config.enableTimerResolution ? L"enabled" : L"disabled") +
        L", MemoryCleanup=" + std::wstring(g_config.enableMemoryCleanup ? L"enabled" : L"disabled"));

    if (!ApplyXmlPerformanceSettings()) {
        Log(L"WARNING: graphics-profile changes were skipped because the game was already running. This session cannot test the GraphicsProfile setting; close the game and run SteamLauncher.bat before relaunching.");
    }

    bool timerSet = false;
    std::wstring savedPowerPlan;
    if (g_config.enableTimerResolution) {
        const MMRESULT result = timeBeginPeriod(1);
        timerSet = result == TIMERR_NOERROR;
        if (timerSet) {
            Log(L"Requested 1 ms timer resolution for the optimizer process (no guaranteed FPS gain).");
        } else {
            Log(L"Could not request 1 ms timer resolution (code " + std::to_wstring(result) + L").");
        }
    }
    if (g_config.changePowerPlan) {
        savedPowerPlan = ActivePowerPlan();
        if (!savedPowerPlan.empty() && RunHidden(L"powercfg /setactive 8c5e7fda-e8bf-4a96-9a85-a6e23a8c635c"))
            Log(L"Power plan switched to High Performance. Previous GUID=" + savedPowerPlan);
        else if (savedPowerPlan.empty())
            Log(L"Could not change power plan; the previous power plan GUID could not be read.");
        else
            Log(L"Could not change power plan. Previous GUID=" + savedPowerPlan);
    }

    Log(L"Watching for up to 300 seconds for game process(es): " + ConfiguredProcessNamesDisplay());
    Log(L"The initialization delay now requires a matching process to remain stable; short-lived launcher/bootstrap processes will not immediately abort detection.");
    std::vector<DWORD> processes;
    std::vector<DWORD> previousCandidate;
    ULONGLONG stableSince = 0;
    int elapsedSeconds = 0;
    const ULONGLONG requiredStableMs = static_cast<ULONGLONG>(g_config.waitSecondsAfterLaunch) * 1000ULL;

    while (elapsedSeconds < 300) {
        const std::vector<DWORD> current = FindGameProcesses();
        if (current.empty()) {
            if (!previousCandidate.empty()) {
                Log(L"Detected game-process candidate exited during initialization; continuing to watch for the actual game process.");
                previousCandidate.clear();
                stableSince = 0;
            }
        } else if (current != previousCandidate) {
            previousCandidate = current;
            stableSince = GetTickCount64();
            LogMatchedProcesses(L"Game-process candidate detected", current);
            if (requiredStableMs == 0) {
                processes = current;
                break;
            }
        } else if (GetTickCount64() - stableSince >= requiredStableMs) {
            processes = current;
            break;
        }

        Sleep(1000);
        ++elapsedSeconds;
    }

    if (processes.empty()) {
        Log(L"Timed out before a configured Far Cry process remained stable long enough. No runtime process settings were applied.");
        LogRelevantProcesses();
        if (timerSet) timeEndPeriod(1);
        if (!savedPowerPlan.empty()) RunHidden(L"powercfg /setactive " + savedPowerPlan);
        return 1;
    }

    Log(L"Stable game process detected after " + std::to_wstring(elapsedSeconds) + L" second(s); applying configured process settings now.");
    LogMatchedProcesses(L"Target before optimization", processes);

    SYSTEM_INFO info{};
    GetSystemInfo(&info);
    Log(L"Windows reports " + std::to_wstring(info.dwNumberOfProcessors) + L" logical processor(s) in the current group.");
    ApplyProcessSettings(processes);
    LogProcessDiagnostics(processes);
    Log(L"Monitoring game process. Memory diagnostics are logged every 60 seconds.");

    int cleanupElapsed = 0;
    int diagnosticsElapsed = 0;
    while (!(processes = FindGameProcesses()).empty()) {
        Sleep(5000);
        cleanupElapsed += 5;
        diagnosticsElapsed += 5;
        if (g_config.enableMemoryCleanup && cleanupElapsed >= g_config.memoryCleanupIntervalSeconds) {
            cleanupElapsed = 0;
            processes = FindGameProcesses();
            if (!processes.empty()) TrimWorkingSets(processes);
        }
        if (diagnosticsElapsed >= 60) {
            diagnosticsElapsed = 0;
            processes = FindGameProcesses();
            if (!processes.empty()) LogProcessDiagnostics(processes);
        }
    }

    Log(L"Game exited. Restoring temporary system settings...");
    if (timerSet) {
        timeEndPeriod(1);
        Log(L"Timer-resolution request released.");
    }
    if (!savedPowerPlan.empty()) {
        if (RunHidden(L"powercfg /setactive " + savedPowerPlan)) Log(L"Previous power plan restored.");
        else Log(L"Warning: could not restore previous power plan GUID=" + savedPowerPlan);
    }
    Log(L"Optimizer session ended.");
    return 0;
}

int wmain(int argc, wchar_t* argv[]) {
    wchar_t module[MAX_PATH]{};
    if (!GetModuleFileNameW(nullptr, module, MAX_PATH)) {
        std::wcerr << L"Could not determine executable path.\n";
        return 1;
    }
    g_root = fs::path(module).parent_path();
    const std::wstring argument = argc > 1 ? ToLower(argv[1]) : L"";

    if (argument == L"--help" || argument == L"-h" || argument == L"/?") {
        std::wcout << L"FarCry4_FPSFix V2.3 combined\n"
                   << L"  (no argument)           Run optimizer visibly and wait for the game\n"
                   << L"  --prepare               Apply reversible pre-launch XML settings and exit\n"
                   << L"  --worker                Run optimizer in background-worker mode\n"
                   << L"  --background            Start a hidden worker and exit\n"
                   << L"  --ubisoft               Start a hidden worker and open Ubisoft Connect\n"
                   << L"  --restore-xml-settings  Restore XML files backed up by this tool\n";
        return 0;
    }
    if (argument == L"--restore-xml-settings") return RestoreXmlPerformanceSettings();
    if (argument == L"--prepare") return PrepareForLaunch();
    if (argument == L"--background") return StartBackgroundWorker();
    if (argument == L"--ubisoft") {
        const int result = StartBackgroundWorker();
        if (result != 0) return result;
        const auto shellResult = reinterpret_cast<INT_PTR>(ShellExecuteW(nullptr, L"open", L"uplay://launch/9/0", nullptr, nullptr, SW_SHOWNORMAL));
        if (shellResult <= 32) {
            std::wcerr << L"Background worker started, but Ubisoft Connect launch request failed (ShellExecute result " << shellResult << L").\n";
            return 1;
        }
        return 0;
    }
    // No argument and --worker both run the same, authoritative optimizer implementation.
    return RunOptimizer();
}
