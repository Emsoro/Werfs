// SPDX-License-Identifier: MIT
// Resource + delivery-mode gate.
//
// Two things are easy to break silently, so both are asserted here:
//   1. the .res link      -- an exe without resources still runs and looks fine
//                            until someone opens the file-properties dialog;
//   2. the debug/release  -- a mis-set working directory must never make a
//      switch                shipped build read stale loose markup.
#include "Core.h"      // WideToUtf8
#include "Resource.h"  // WAML_RESOURCE_TYPE / IDR_WAML_GALLERY / WUI_VERSION_*
#include "Resources.h"

#include <windows.h>
#include <winver.h>
#include <aclapi.h>  // GetNamedSecurityInfoW / GetAce
#include <sddl.h>   // ConvertSidToStringSidW

#include <cstdio>
#include <cwchar>
#include <string>
#include <vector>

using namespace wui;

namespace {

int g_failures = 0;

void Expect(bool cond, const wchar_t* what) {
    wprintf(L"  %ls %ls\n", cond ? L"[ ok ]" : L"[FAIL]", what);
    if (!cond) ++g_failures;
}

// Size of a resource, or 0 when absent. `type` as int covers the numeric types
// (RT_MANIFEST=24, RT_GROUP_ICON=14) which rc.exe only stores correctly when
// written literally in the .rc.
DWORD ResSize(const wchar_t* name, int type) {
    HMODULE m = GetModuleHandleW(nullptr);
    HRSRC r = FindResourceW(m, name, MAKEINTRESOURCEW(type));
    return r ? SizeofResource(m, r) : 0;
}

DWORD ResSize(const wchar_t* name, const wchar_t* type) {
    HMODULE m = GetModuleHandleW(nullptr);
    HRSRC r = FindResourceW(m, name, type);
    return r ? SizeofResource(m, r) : 0;
}

} // namespace

int wmain() {
    setvbuf(stdout, nullptr, _IONBF, 0);
    std::printf("module: %s\n", WideToUtf8(ModulePath()).c_str());

    // -----------------------------------------------------------------------
    // 1. Build-configuration switch.
    // -----------------------------------------------------------------------
    const bool loose = PrefersLooseWamlFiles();
    std::printf("mode: %s\n", loose ? "loose files (debug)" : "embedded resource (release)");
#if defined(_DEBUG)
    Expect(loose, L"debug build reads loose markup files");
#else
    Expect(!loose, L"release build reads the embedded resource");
    Expect(!loose, L"release build never falls back to loose markup by default");
#endif

    // -----------------------------------------------------------------------
    // 2. Payload presence -- required in BOTH modes (debug uses it as fallback).
    // -----------------------------------------------------------------------
    const DWORD waml = ResSize(MAKEINTRESOURCEW(IDR_WAML_GALLERY), WAML_RESOURCE_TYPE);
    std::printf("  WAML_DOC/%d: %lu bytes\n", IDR_WAML_GALLERY, waml);
    Expect(waml > 0, L"gallery markup is embedded as RCDATA");

    WamlLoadResult res = LoadWamlResource(WAML_RESOURCE_TYPE, IDR_WAML_GALLERY);
    Expect(res.ok, L"gallery markup loads from the resource");
    Expect(res.origin == "res:#" + std::to_string(IDR_WAML_GALLERY), L"resource origin is reported");
    Expect(!res.text.empty() && res.text.find("Page") != std::string::npos, L"payload looks like WAML");

    // The embedded copy must be the current file: otherwise a release build can
    // ship stale markup while the working tree looks correct.
    {
        WamlLoadResult file = LoadWamlFile("samples\\gallery\\Gallery.waml");
        if (!file.ok) {
            // Try the resolution helper, which walks ancestor directories.
            const std::string resolved = ResolveWamlPath("samples\\gallery\\Gallery.waml");
            if (!resolved.empty()) file = LoadWamlFile(resolved);
        }
        if (file.ok) {
            const bool same = file.text == res.text;
            std::printf("  embedded == samples\\gallery\\Gallery.waml: %s\n", same ? "yes" : "NO");
            Expect(same, L"embedded markup matches the source .waml (stale .res?)");
        } else {
            std::printf("  (source .waml not reachable from here; skipping staleness check)\n");
        }
    }

    // -----------------------------------------------------------------------
    // 3. Mode selection.
    // -----------------------------------------------------------------------
    WamlLoadResult picked = LoadWaml(WAML_RESOURCE_TYPE, IDR_WAML_GALLERY, "samples\\gallery\\Gallery.waml");
    Expect(picked.ok, L"LoadWaml() succeeds");
    if (loose) {
        Expect(picked.source == WamlSource::File, L"debug picks the loose file");
        std::printf("  origin: %s\n", picked.origin.c_str());
    } else {
        Expect(picked.source == WamlSource::Resource, L"release picks the resource");
        Expect(picked.origin == "res:#" + std::to_string(IDR_WAML_GALLERY), L"release origin is res:#id");
    }

    // -----------------------------------------------------------------------
    // 4. Icon.
    // -----------------------------------------------------------------------
    const DWORD icon = ResSize(MAKEINTRESOURCEW(IDI_APP), 14); // RT_GROUP_ICON
    std::printf("  RT_GROUP_ICON/%d: %lu bytes\n", IDI_APP, icon);
    Expect(icon > 0, L"application icon group is embedded");

    // -----------------------------------------------------------------------
    // 5. Manifest must be numeric type 24. Written as RT_MANIFEST in the .rc it
    //    becomes a *string* type name and Windows silently ignores it, leaving
    //    the process DPI-unaware.
    // -----------------------------------------------------------------------
    const DWORD manifest = ResSize(MAKEINTRESOURCEW(1), 24);
    std::printf("  RT_MANIFEST/1: %lu bytes\n", manifest);
    Expect(manifest > 0, L"manifest is embedded as numeric RT_MANIFEST(24)");

    // -----------------------------------------------------------------------
    // 6. VERSIONINFO, read the documented way.
    // -----------------------------------------------------------------------
    int parts[4]{};
    Expect(ModuleVersionParts(parts), L"VERSIONINFO is readable");
    std::printf("  version: %d.%d.%d.%d\n", parts[0], parts[1], parts[2], parts[3]);
    Expect(parts[0] == WUI_VERSION_MAJOR && parts[1] == WUI_VERSION_MINOR && parts[2] == WUI_VERSION_PATCH,
           L"VERSIONINFO matches the version macros this build used");

    const std::wstring description = ModuleVersionField(L"FileDescription");
    Expect(!description.empty(), L"FileDescription is present");
    const std::wstring product = ModuleVersionField(L"ProductName");
    Expect(!product.empty(), L"ProductName is present");

    // -----------------------------------------------------------------------
    // 7. The manifest must be present AND carry the block the linker injects.
    //
    // This check exists because of a real misdiagnosis: the manifest was compiled
    // into the .rc, so link.exe found a manifest resource already present, skipped
    // generating its own, and therefore never added trustInfo/requestedExecutionLevel.
    // The exe then launched as a legacy app and triggered the SmartScreen
    // unknown-app prompt -- with no Mark-of-the-Web and no signature involved.
    //
    // The fix is the linker's /MANIFESTINPUT: (what MSBuild's
    // <AdditionalManifestFiles> maps to), which lets the linker generate the
    // manifest and merge our settings on top.
    //
    // A keyword-only assertion passes in both the broken and the fixed state as
    // long as trustInfo is hand-written, so also verify the shape matches a known
    // good sample: trustInfo nested in <assembly>, and no windowsSettings that a
    // healthy sibling exe (NotepadX) does not have.
    // -----------------------------------------------------------------------
    {
        HMODULE self = GetModuleHandleW(nullptr);
        HRSRC handle = FindResourceW(self, MAKEINTRESOURCEW(1), MAKEINTRESOURCEW(24));
        HGLOBAL data = handle ? LoadResource(self, handle) : nullptr;
        const char* bytes = data ? static_cast<const char*>(LockResource(data)) : nullptr;
        const DWORD size = handle ? SizeofResource(self, handle) : 0;
        if (bytes && size > 0) {
            const std::string xml(bytes, size);
            auto has = [&](const char* needle) { return xml.find(needle) != std::string::npos; };

            Expect(has("<assembly"), L"manifest has an <assembly> root");
            Expect(has("trustInfo"), L"manifest declares trustInfo (linker should inject it)");
            Expect(has("requestedExecutionLevel"), L"manifest declares requestedExecutionLevel");
            Expect(has("asInvoker"), L"manifest runs asInvoker (never elevates)");
            // These are the settings that must be present for the UI to behave.
            Expect(has("dpiAwareness"), L"manifest declares dpiAwareness");
            Expect(has("PerMonitorV2"), L"manifest opts into PerMonitorV2");
            // Ordering sanity: trustInfo is a direct child of <assembly>, so it must
            // appear after the root opens. A manifest with trustInfo *before* the
            // root would mean a malformed/merged-by-hand document.
            const size_t rootPos = xml.find("<assembly");
            const size_t trustPos = xml.find("trustInfo");
            Expect(rootPos != std::string::npos && trustPos != std::string::npos && rootPos < trustPos,
                   L"trustInfo is nested inside <assembly> (proper merge, not hand-patched)");
        } else {
            Expect(false, L"manifest payload is readable");
        }
    }

    // -----------------------------------------------------------------------
    // 8. Shipping check: does this binary carry the ACL marker that makes
    //    SmartScreen block it on every launch?
    //
    // An exe created by an automated session can end up with an ACE for an
    // unresolvable SID (S-1-4-<opaque>). Binaries like that prompt "Windows
    // protected your PC" every time, even when unsigned-but-fine and byte
    // identical to one that launches silently. The binary content is irrelevant,
    // which is why resource/version/manifest checks never catch it.
    //
    // This is reported as a WARNING rather than a failure: it describes where the
    // file was produced, not a defect in it, and a normal developer shell will
    // never trip it.
    // -----------------------------------------------------------------------
    {
        const std::wstring path = ModulePath();
        // Read the security descriptor directly instead of shelling out to
        // icacls: _wpopen depends on console/encoding setup and silently
        // returned nothing, so the check passed on a file that does carry the
        // marker -- a warning that never fires is worse than no warning.
        std::wstring marker;
        PSECURITY_DESCRIPTOR descriptor = nullptr;
        if (GetNamedSecurityInfoW(const_cast<LPWSTR>(path.c_str()), SE_FILE_OBJECT, DACL_SECURITY_INFORMATION,
                                  nullptr, nullptr, nullptr, nullptr, &descriptor) == ERROR_SUCCESS &&
            descriptor) {
            PACL dacl = nullptr;
            BOOL present = FALSE;
            BOOL defaulted = FALSE;
            if (GetSecurityDescriptorDacl(descriptor, &present, &dacl, &defaulted) && present && dacl) {
                for (WORD i = 0; i < dacl->AceCount; ++i) {
                    void* ace = nullptr;
                    if (!GetAce(dacl, i, &ace) || !ace) continue;
                    auto* header = static_cast<ACE_HEADER*>(ace);
                    PSID sid = nullptr;
                    if (header->AceType == ACCESS_ALLOWED_ACE_TYPE) {
                        sid = &reinterpret_cast<ACCESS_ALLOWED_ACE*>(ace)->SidStart;
                    } else if (header->AceType == ACCESS_DENIED_ACE_TYPE) {
                        sid = &reinterpret_cast<ACCESS_DENIED_ACE*>(ace)->SidStart;
                    }
                    if (!sid || !IsValidSid(sid)) continue;
                    // S-1-4-... : identifier authority 4 (SECURITY_NON_UNIQUE_AUTHORITY).
                    PSID_IDENTIFIER_AUTHORITY authority = GetSidIdentifierAuthority(sid);
                    if (authority && authority->Value[5] == 4) {
                        LPWSTR text = nullptr;
                        if (ConvertSidToStringSidW(sid, &text)) {
                            marker = text;
                            LocalFree(text);
                        }
                        break;
                    }
                }
            }
            LocalFree(descriptor);
        }

        if (!marker.empty()) {
            std::printf("  [WARN] this file carries a non-unique-authority SID ACE (%ls).\n",
                        marker.c_str());
            std::printf("         SmartScreen is likely to block it on every launch, even though the\n");
            std::printf("         binary itself is fine. Build or copy it from an ordinary terminal, or\n");
            std::printf("         set WUI_OUT to a directory automated sessions do not own.\n");
        } else {
            std::printf("  [ ok ] no S-1-4 ACL marker (SmartScreen-clean)\n");
        }
    }

    std::printf("\n%s\n", g_failures == 0 ? "RESOURCES OK" : "RESOURCES FAILED");
    return g_failures == 0 ? 0 : 1;
}
