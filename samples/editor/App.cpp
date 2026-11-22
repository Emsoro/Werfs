// SPDX-License-Identifier: MIT
// Sample entry point: the multi-document editor skeleton.
//
// Everything sample-specific lives here: which ViewModel, which markup, the
// window title and size, and the shortcut table. The Win32 / D2D / DPI / input
// plumbing (including overlay dispatch for menus and dialogs) is in
// samples/shared/SampleHost.
#include "EditorResource.h"  // IDR_WAML_EDITOR
#include "EditorVm.h"
#include "FileDialog.h"
#include "SampleHost.h"

#include <windows.h>

#include <memory>

namespace {

std::shared_ptr<sample::EditorVm> g_vm;
// The Win32 file dialog needs a window and an STA COM apartment; both are set up
// by the host before RunSample gets here. Created once and kept alive for the
// process, because the service is registered process-wide.
std::unique_ptr<wui::IFileDialogService> g_dialogs;

std::shared_ptr<wui::RefObject> MakeViewModel() {
    g_vm = std::make_shared<sample::EditorVm>();

    // Real file dialogs for the windowed app. The headless harness substitutes a
    // fake instead, which is why the ViewModel takes the service rather than
    // calling Win32 itself (CODE_PLAN.md §10b.1 / R15).
    g_dialogs.reset(wui::CreateWin32FileDialogService());
    wui::SetFileDialogService(g_dialogs.get());
    g_vm->SetDialogService(g_dialogs.get());

    return std::static_pointer_cast<wui::RefObject>(g_vm);
}

} // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int) {
    // Build the ViewModel up front rather than inside the factory: the shortcut
    // table needs its commands, and passing a ready instance is also what lets a
    // test construct the same page without going through wWinMain.
    std::shared_ptr<wui::RefObject> vm = MakeViewModel();
    auto editor = std::static_pointer_cast<sample::EditorVm>(g_vm);

    sample::HostOptions options;
    options.resourceType = WAML_RESOURCE_TYPE;
    options.resourceId = IDR_WAML_EDITOR;
    // Used in debug builds: markup read from disk, so editing it needs no rebuild.
    options.relativePath = "samples\\editor\\Editor.waml";
    options.title = L"Werfs — Editor";
    // Wide and tall: a menu bar, a toolbar, a tab strip and a text area need more
    // room than a control demo, and the toolbar's overflow only appears when the
    // window is genuinely narrow.
    options.widthDip = 860.0f;
    options.heightDip = 660.0f;

    // Shortcuts live in a table, not in the markup: they are application-wide, and
    // two controls declaring the same chord would otherwise both fire. The host
    // gives the focused control first refusal, so Ctrl+A inside the text box is
    // still text selection rather than a menu accelerator (CODE_PLAN.md R23).
    options.accelerators = {
        {L"Ctrl+N", editor->NewCommand.Get()},
        {L"Ctrl+O", editor->OpenCommand.Get()},
        {L"Ctrl+S", editor->SaveCommand.Get()},
        {L"Ctrl+W", editor->CloseTabCommand.Get()},
    };

    return sample::RunSample(instance, options, std::move(vm));
}
