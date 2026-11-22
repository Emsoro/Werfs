// SPDX-License-Identifier: MIT
// WAML payload loading with a debug/release delivery switch.
//
//   Debug   : markup is read from loose files next to the executable, so editing
//             a .waml takes effect on the next run -- no rebuild, and a syntax
//             error points at a real path you can open.
//   Release : markup is read from the RT_RCDATA payloads compiled into the exe,
//             so shipping is a single file with nothing to lose or tamper with.
//
// The switch is compile-time (WUI_WAML_LOOSE_FILES) so a mis-set working
// directory can never make a shipped build silently read stale markup. A runtime
// escape hatch exists for support scenarios: set WUI_WAML_DIR to a folder and
// even a release build will prefer files found there.
#pragma once

#include <string>
#include <vector>

namespace wui {

enum class WamlSource {
    Resource, // RT_RCDATA payload inside this module.
    File,     // Loose file on disk.
};

struct WamlLoadResult {
    bool ok = false;
    WamlSource source = WamlSource::Resource;
    std::string text;    // UTF-8 document body, BOM stripped.
    std::string origin;  // "res:#101" or the resolved path; used in diagnostics.
    std::string error;   // Populated when ok == false.

    // Diagnostics need a stable file name; this is it.
    const std::string& FileName() const { return origin; }
};

// True when this build prefers loose files (Debug). Exposed so tools can print
// which mode they are in instead of guessing.
bool PrefersLooseWamlFiles();

// Loads an RCDATA payload of `typeName` with numeric `id` from this module.
WamlLoadResult LoadWamlResource(const wchar_t* typeName, int id);

// Loads from an explicit path.
WamlLoadResult LoadWamlFile(const std::string& path);

// Resolves `relativePath` against, in order: %WUI_WAML_DIR%, the directory of the
// executable, its parent, and the current working directory. Returns an empty
// string when nothing exists. This is what makes a double-clicked exe work.
std::string ResolveWamlPath(const std::string& relativePath);

// The entry point applications should call.
//
//   relativePath  e.g. "samples\\Gallery.waml" -- only used in file mode
//   id            resource id used in resource mode
//
// Resolution order:
//   loose-file build : file -> resource (resource keeps tools working even when
//                      the dev tree is incomplete)
//   resource build   : %WUI_WAML_DIR% file (if set) -> resource
WamlLoadResult LoadWaml(const wchar_t* typeName, int id, const std::string& relativePath);

// Lists every payload of `typeName` packaged in this module.
std::vector<int> EnumerateWamlResources(const wchar_t* typeName);

// Version of this module, taken from its VERSIONINFO resource, so the window
// title/About text and the file-properties dialog can never disagree.
std::wstring ModuleVersionString();
bool ModuleVersionParts(int parts[4]);
// A VERSIONINFO string field, e.g. L"FileDescription". Empty when absent.
std::wstring ModuleVersionField(const wchar_t* field);

// Path of the current module, for diagnostics ("loaded from <exe>").
std::wstring ModulePath();

} // namespace wui
