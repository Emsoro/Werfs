// SPDX-License-Identifier: MIT
// Sample host: everything that is NOT about a particular page.
//
// A sample provides three things and gets a running window:
//   1. the WAML payload (an RCDATA id, or a relative path for debug builds)
//   2. a ViewModel factory
//   3. a title
//
// Keeping this in one place matters because otherwise every sample carries its
// own copy of the Win32 setup, D2D target management, DPI handling and input
// plumbing -- six copies that drift apart. The rules encoded here (DIP layout
// vs pixel input, COM per-process, #undef DrawText) were each learned the hard
// way; see CODE_PLAN.md §7.4-7.6.
#pragma once

// HostOptions and the entry points speak Win32 types (HINSTANCE), so the
// windows header has to be visible here. NOMINMAX / WIN32_LEAN_AND_MEAN are set
// by the build, and DrawText is #undef'd in the .cpp because winuser.h maps it
// onto the PaintCtx method name.
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

// winuser.h maps DrawText onto DrawTextW under UNICODE. PaintCtx declares a
// method with that name, so the macro must be removed before any framework
// header is parsed -- otherwise every `override` of PaintCtx::DrawText turns
// into DrawTextW and stops overriding anything. This has to happen in the
// header, not just the .cpp, because this header is what pulls <windows.h> in
// ahead of Core.h.
#undef DrawText

#include <memory>
#include <string>

#include "Core.h"

namespace wui {
class Widget;
} // namespace wui

namespace sample {

enum class MarkupSource {
    Resource, // release: RCDATA payload compiled into the exe
    File,     // debug: read from disk, honouring WUI_WAML_DIR / exe-relative search
};

struct HostOptions {
    // Where the markup comes from. PrefersLooseWamlFiles() already reports the
    // build configuration; the host uses that unless an explicit path wins.
    const wchar_t* resourceType = L"WAML_DOC";
    int resourceId = 0;
    // Relative path used in loose-file mode, e.g. "samples\\list\\List.waml".
    std::string relativePath;
    // Shown in the title bar next to the version.
    std::wstring title = L"Gridform sample";
    // Initial window size in DIPs. Sizing in DIPs keeps the layout the same
    // apparent size on 100% and 200% displays.
    float widthDip = 420.0f;
    float heightDip = 560.0f;
};

// Runs the sample until the window closes. `makeVm` returns the root
// DataContext; it may be null for a purely static page.
//
// Returns a process exit code: 0 on success, non-zero when the markup failed to
// load or compile, or when --selftest was requested and failed.
int RunSample(HINSTANCE instance,
              const HostOptions& options,
              std::shared_ptr<wui::RefObject> (*makeVm)());

// Convenience: the same, but with a VM the caller already built.
int RunSample(HINSTANCE instance,
              const HostOptions& options,
              std::shared_ptr<wui::RefObject> vm);

} // namespace sample
