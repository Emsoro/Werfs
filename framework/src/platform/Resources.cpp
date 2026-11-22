// SPDX-License-Identifier: MIT
#include "Resources.h"

#include "Core.h" // Utf8ToWide / WideToUtf8

#include <windows.h>
#include <winver.h> // VS_FIXEDFILEINFO + VerQueryValueW

#include <algorithm>
#include <cstdio>
#include <cwchar>

namespace wui {

namespace {

// VS_FIXEDFILEINFO comes from verrsrc.h, which winver.h pulls in with the full
// definition (not just a forward declaration). Reading the version block needs
// that layout, so make the dependency explicit rather than relying on a
// transitive include.
constexpr DWORD kFixedFileInfoSignature = 0xFEEF04BD;

// ---------------------------------------------------------------------------
// Module + resource access
// ---------------------------------------------------------------------------
HMODULE SelfModule() {
    HMODULE module = nullptr;
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                           GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                       reinterpret_cast<LPCWSTR>(&SelfModule), &module);
    return module;
}

// RCDATA payload handle. Resources stay mapped for the module lifetime, so the
// pointer is valid until unload and must not be freed.
class ResourceBytes {
public:
    ResourceBytes(const wchar_t* type, int id) {
        const HMODULE module = SelfModule();
        if (!module || !type) return;
        HRSRC found = FindResourceW(module, MAKEINTRESOURCEW(static_cast<WORD>(id)), type);
        if (!found) return;
        HGLOBAL loaded = LoadResource(module, found);
        if (!loaded) return;
        data_ = static_cast<const char*>(LockResource(loaded));
        size_ = SizeofResource(module, found);
    }
    bool ok() const { return data_ != nullptr && size_ > 0; }
    const char* data() const { return data_; }
    size_t size() const { return size_; }

private:
    const char* data_ = nullptr;
    DWORD size_ = 0;
};

// UTF-8 BOM is optional; strip it so the parser sees a clean document.
std::string StripBom(const char* data, size_t size) {
    if (size >= 3 && static_cast<unsigned char>(data[0]) == 0xEF &&
        static_cast<unsigned char>(data[1]) == 0xBB && static_cast<unsigned char>(data[2]) == 0xBF) {
        return std::string(data + 3, size - 3);
    }
    return std::string(data, size);
}

bool FileExists(const std::string& path) {
    if (path.empty()) return false;
    const DWORD attr = GetFileAttributesW(Utf8ToWide(path).c_str());
    return attr != INVALID_FILE_ATTRIBUTES && !(attr & FILE_ATTRIBUTE_DIRECTORY);
}

std::string JoinPath(const std::wstring& dir, const std::string& relative) {
    if (dir.empty()) return relative;
    std::wstring full = dir;
    if (full.back() != L'\\' && full.back() != L'/') full.push_back(L'\\');
    full += Utf8ToWide(relative);
    return WideToUtf8(full);
}

std::wstring DirectoryOf(const std::wstring& path) {
    const size_t slash = path.find_last_of(L"\\/");
    return slash == std::wstring::npos ? std::wstring() : path.substr(0, slash);
}

std::wstring ParentOf(const std::wstring& dir) {
    const std::wstring trimmed =
        (!dir.empty() && (dir.back() == L'\\' || dir.back() == L'/')) ? dir.substr(0, dir.size() - 1) : dir;
    const size_t slash = trimmed.find_last_of(L"\\/");
    return slash == std::wstring::npos ? std::wstring() : trimmed.substr(0, slash);
}

std::wstring EnvDir(const wchar_t* name) {
    wchar_t buffer[MAX_PATH * 2]{};
    const DWORD n = GetEnvironmentVariableW(name, buffer, static_cast<DWORD>(std::size(buffer)));
    if (n == 0 || n >= std::size(buffer)) return std::wstring();
    return buffer;
}

// ---------------------------------------------------------------------------
// VERSIONINFO
//
// Reading the version through GetFileVersionInfo + VerQueryValue is the only
// reliable route: FindResource(RT_VERSION) succeeds for the block but the
// payload layout is not a plain VS_FIXEDFILEINFO (the header and children are
// length-prefixed and 32-bit aligned), so casting the HRSRC data misreads it.
// ---------------------------------------------------------------------------
struct VersionBlock {
    std::vector<BYTE> bytes;
    bool ok = false;
};

const VersionBlock& CachedVersionBlock() {
    static VersionBlock block = [] {
        VersionBlock out;
        const std::wstring path = ModulePath();
        if (path.empty()) return out;
        DWORD ignored = 0;
        const DWORD size = GetFileVersionInfoSizeW(path.c_str(), &ignored);
        if (size == 0) return out;
        out.bytes.resize(size);
        if (!GetFileVersionInfoW(path.c_str(), 0, size, out.bytes.data())) {
            out.bytes.clear();
            return out;
        }
        out.ok = true;
        return out;
    }();
    return block;
}

} // namespace

std::wstring ModulePath() {
    wchar_t buffer[MAX_PATH * 2]{};
    const DWORD len = GetModuleFileNameW(SelfModule(), buffer, static_cast<DWORD>(std::size(buffer)));
    return len > 0 ? std::wstring(buffer, len) : std::wstring();
}

bool PrefersLooseWamlFiles() {
#if defined(WUI_WAML_LOOSE_FILES)
    return true;
#else
    return false;
#endif
}

WamlLoadResult LoadWamlResource(const wchar_t* typeName, int id) {
    WamlLoadResult result;
    result.source = WamlSource::Resource;
    ResourceBytes payload(typeName, id);
    if (!payload.ok()) {
        char buffer[192];
        snprintf(buffer, sizeof(buffer),
                 "resource %d of type '%ls' is missing from this module "
                 "(was the .rc compiled and its .res linked in?)",
                 id, typeName ? typeName : L"");
        result.error = buffer;
        return result;
    }
    result.text = StripBom(payload.data(), payload.size());
    result.origin = "res:#" + std::to_string(id);
    result.ok = true;
    return result;
}

WamlLoadResult LoadWamlFile(const std::string& path) {
    WamlLoadResult result;
    result.source = WamlSource::File;
    result.origin = path;
    FILE* fp = nullptr;
    if (_wfopen_s(&fp, Utf8ToWide(path).c_str(), L"rb") != 0 || !fp) {
        result.error = "cannot open '" + path + "'";
        return result;
    }
    std::string data;
    char chunk[8192];
    size_t read = 0;
    while ((read = fread(chunk, 1, sizeof(chunk), fp)) > 0) data.append(chunk, read);
    fclose(fp);
    if (data.empty()) {
        result.error = "'" + path + "' is empty";
        return result;
    }
    result.text = StripBom(data.c_str(), data.size());
    result.ok = true;
    return result;
}

std::string ResolveWamlPath(const std::string& relativePath) {
    if (relativePath.empty()) return {};
    // Absolute paths are taken as-is.
    if (relativePath.size() > 1 && (relativePath[1] == ':' || relativePath[0] == '\\')) {
        return FileExists(relativePath) ? relativePath : std::string();
    }

    // Search: %WUI_WAML_DIR%, then the exe directory and each ancestor, then the
    // current working directory.
    //
    // Walking ancestors matters because build layouts nest: the debug exe lives
    // in build\debug\, so "samples\Gallery.waml" is two levels up, and a fixed
    // "exe dir + parent" list silently misses it and falls back to the packaged
    // copy -- which looks like the debug switch not working.
    std::vector<std::wstring> dirs;
    const std::wstring env = EnvDir(L"WUI_WAML_DIR");
    if (!env.empty()) dirs.push_back(env);

    std::wstring dir = DirectoryOf(ModulePath());
    for (int depth = 0; depth < 6 && !dir.empty(); ++depth) {
        dirs.push_back(dir);
        const std::wstring parent = ParentOf(dir);
        if (parent == dir) break;
        dir = parent;
    }

    for (const auto& candidateDir : dirs) {
        const std::string candidate = JoinPath(candidateDir, relativePath);
        if (FileExists(candidate)) return candidate;
    }
    // Finally the current working directory, which is what a developer running
    // from the repository root expects.
    return FileExists(relativePath) ? relativePath : std::string();
}

WamlLoadResult LoadWaml(const wchar_t* typeName, int id, const std::string& relativePath) {
    const bool looseBuild = PrefersLooseWamlFiles();
    const bool forcedDir = !EnvDir(L"WUI_WAML_DIR").empty();

    // Loose-file builds (and any build with WUI_WAML_DIR set) prefer the file.
    if ((looseBuild || forcedDir) && !relativePath.empty()) {
        const std::string path = ResolveWamlPath(relativePath);
        if (!path.empty()) {
            WamlLoadResult file = LoadWamlFile(path);
            if (file.ok) return file;
            // Keep the error but still try the packaged copy below.
        }
    }

    WamlLoadResult resource = LoadWamlResource(typeName, id);
    if (resource.ok) return resource;

    // A shipped build has no fallback, but a dev build might: report the file
    // problem as the primary message when it is the more actionable one.
    if (!relativePath.empty()) {
        const std::string path = ResolveWamlPath(relativePath);
        if (!path.empty()) {
            WamlLoadResult file = LoadWamlFile(path);
            if (file.ok) return file;
            resource.error += "; loose file '" + path + "' also failed: " + file.error;
        } else if (looseBuild || forcedDir) {
            resource.error += "; loose file '" + relativePath + "' was not found in any search directory";
        }
    }
    return resource;
}

std::vector<int> EnumerateWamlResources(const wchar_t* typeName) {
    std::vector<int> ids;
    const HMODULE module = SelfModule();
    if (!module || !typeName) return ids;
    EnumResourceNamesW(
        module, typeName,
        [](HMODULE, LPCWSTR, LPWSTR name, LONG_PTR param) -> BOOL {
            auto* out = reinterpret_cast<std::vector<int>*>(param);
            // Only numeric ids are addressable with MAKEINTRESOURCEW.
            if (HIWORD(reinterpret_cast<ULONG_PTR>(name)) == 0) {
                out->push_back(static_cast<int>(reinterpret_cast<ULONG_PTR>(name)));
            }
            return TRUE;
        },
        reinterpret_cast<LONG_PTR>(&ids));
    std::sort(ids.begin(), ids.end());
    return ids;
}

bool ModuleVersionParts(int parts[4]) {
    const VersionBlock& block = CachedVersionBlock();
    if (!block.ok) return false;
    VS_FIXEDFILEINFO* info = nullptr;
    UINT size = 0;
    if (!VerQueryValueW(block.bytes.data(), L"\\", reinterpret_cast<LPVOID*>(&info), &size) || !info ||
        size < sizeof(VS_FIXEDFILEINFO)) {
        return false;
    }
    if (info->dwSignature != kFixedFileInfoSignature) return false;
    parts[0] = HIWORD(info->dwFileVersionMS);
    parts[1] = LOWORD(info->dwFileVersionMS);
    parts[2] = HIWORD(info->dwFileVersionLS);
    parts[3] = LOWORD(info->dwFileVersionLS);
    return true;
}

std::wstring ModuleVersionString() {
    int parts[4]{};
    if (!ModuleVersionParts(parts)) return L"(no version resource)";
    wchar_t buffer[64];
    swprintf_s(buffer, L"%d.%d.%d.%d", parts[0], parts[1], parts[2], parts[3]);
    return buffer;
}

std::wstring ModuleVersionField(const wchar_t* field) {
    if (!field) return std::wstring();
    const VersionBlock& block = CachedVersionBlock();
    if (!block.ok) return std::wstring();

    struct LangCodepage {
        WORD language;
        WORD codepage;
    };
    LangCodepage* translation = nullptr;
    UINT size = 0;
    if (!VerQueryValueW(block.bytes.data(), L"\\VarFileInfo\\Translation",
                        reinterpret_cast<LPVOID*>(&translation), &size) ||
        !translation || size < sizeof(LangCodepage)) {
        return std::wstring();
    }
    wchar_t subBlock[128];
    swprintf_s(subBlock, L"\\StringFileInfo\\%04x%04x\\%ls", translation[0].language,
               translation[0].codepage, field);
    wchar_t* value = nullptr;
    if (!VerQueryValueW(block.bytes.data(), subBlock, reinterpret_cast<LPVOID*>(&value), &size) || !value)
        return std::wstring();
    return std::wstring(value);
}

} // namespace wui
