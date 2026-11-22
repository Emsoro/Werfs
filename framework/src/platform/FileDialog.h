// SPDX-License-Identifier: MIT
// Host-provided file dialogs.
//
// Why an interface instead of calling Win32 directly from a ViewModel: a
// ViewModel that creates an HWND dialog depends on the UI thread and a window
// handle, which makes it untestable headlessly and impossible to reuse. Here the
// ViewModel states its intent and the host supplies the implementation, so the
// test harness can inject a fake and never open a real dialog.
//
// The Win32 implementation lives in platform/FileDialogWin32.cpp. It uses
// IFileOpenDialog / IFileSaveDialog rather than the legacy GetOpenFileName
// because one API covers open, save and folder picking (FOS_PICKFOLDERS), and the
// legacy call cannot pick a folder at all.
#pragma once

#include <string>
#include <vector>

namespace wui {

struct FileFilter {
    std::wstring label;    // L"WAML files"
    std::wstring pattern;  // L"*.waml;*.txt" -- semicolon-separated, Win32 style
};

struct FileDialogRequest {
    std::wstring title;
    std::wstring defaultExtension;  // without the dot
    std::wstring suggestedName;     // save dialogs: prefilled file name
    std::wstring initialPath;       // optional folder to start in
    std::vector<FileFilter> filters;
    bool pickFolders = false;       // true = choose a directory
    bool allowMultiple = false;     // open dialogs only
};

struct FileDialogResult {
    bool accepted = false;
    // Empty when cancelled or on error. One entry normally; more only when
    // allowMultiple was set.
    std::vector<std::wstring> paths;
};

class IFileDialogService {
public:
    virtual ~IFileDialogService() = default;
    virtual FileDialogResult ShowOpen(const FileDialogRequest& request) = 0;
    virtual FileDialogResult ShowSave(const FileDialogRequest& request) = 0;
};

// Registers the process-wide service. Passing nullptr unregisters it, which is
// what the headless harness does -- every call then returns accepted=false
// instead of crashing.
void SetFileDialogService(IFileDialogService* service);
IFileDialogService* FileDialogService();

// Creates the Win32 implementation, or nullptr when COM is unavailable. The
// caller owns the result. Exposed as a factory so the platform file can be
// excluded from headless builds without touching this header.
IFileDialogService* CreateWin32FileDialogService();

} // namespace wui
