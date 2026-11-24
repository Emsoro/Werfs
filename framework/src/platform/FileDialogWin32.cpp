// SPDX-License-Identifier: MIT
// WIN32_LEAN_AND_MEAN / NOMINMAX come from the build flags; redefining them here
// only produces C4005 on every translation unit that includes this file.
#include <windows.h>

// shobjidl brings in IFileOpenDialog; the CLSID_FileOpenDialog and
// IID_IFileOpenDialog GUIDs need uuid.lib, which build.ps1 links.
#include <shobjidl.h>

#include "FileDialog.h"

#include <atomic>
#include <memory>

namespace wui {

namespace {

void SetGlobalService(IFileDialogService* service);
IFileDialogService* GetGlobalService();

// Everything a dialog needs from COM, done explicitly so a failure is a clean
// nullptr rather than an uninitialised pointer that crashes later.
//
// CoInitializeEx returns RPC_E_CHANGED_MODE when the thread already initialised as
// MTA. That is not a failure for our purposes -- the dialogs work in either
// apartment -- but CoUninitialize must only be called when we did the init.
class ComScope {
public:
    ComScope() {
        const HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
        initialized_ = (hr == S_OK);
        usable_ = SUCCEEDED(hr) || hr == RPC_E_CHANGED_MODE;
    }
    ~ComScope() {
        if (initialized_) CoUninitialize();
    }
    bool Usable() const { return usable_; }

private:
    bool initialized_ = false;
    bool usable_ = false;
};

// RAII for a COM interface pointer. Without this, every early-return path in the
// code below would leak -- and there are a lot of them, one per HRESULT check.
template <class T>
class ComPtr {
public:
    ~ComPtr() {
        if (ptr_) ptr_->Release();
    }
    T** Put() {
        Reset();
        return &ptr_;
    }
    T* Get() const { return ptr_; }
    T* operator->() const { return ptr_; }
    explicit operator bool() const { return ptr_ != nullptr; }
    void Reset() {
        if (ptr_) {
            ptr_->Release();
            ptr_ = nullptr;
        }
    }

private:
    T* ptr_ = nullptr;
};

// CoTaskMemFree for a pointer handed to us by the shell.
class CoTaskFree {
public:
    explicit CoTaskFree(PWSTR p) : p_(p) {}
    ~CoTaskFree() {
        if (p_) CoTaskMemFree(p_);
    }
    CoTaskFree(const CoTaskFree&) = delete;
    CoTaskFree& operator=(const CoTaskFree&) = delete;
    const wchar_t* get() const { return p_; }

private:
    PWSTR p_;
};

// Fills the parts of the dialog configuration shared by open and save.
HRESULT ConfigureDialog(IFileDialog* dialog, const FileDialogRequest& request) {
    if (!request.title.empty()) {
        dialog->SetTitle(request.title.c_str());
    }
    if (!request.defaultExtension.empty()) {
        dialog->SetDefaultExtension(request.defaultExtension.c_str());
    }
    if (!request.suggestedName.empty()) {
        dialog->SetFileName(request.suggestedName.c_str());
    }

    // Filters must be copied into one COMDLG_FILTERSPEC array whose strings
    // outlive the call. A local vector is fine because SetFileTypes copies.
    if (!request.filters.empty()) {
        std::vector<COMDLG_FILTERSPEC> specs;
        specs.reserve(request.filters.size());
        for (const auto& f : request.filters) {
            specs.push_back(COMDLG_FILTERSPEC{f.label.c_str(), f.pattern.c_str()});
        }
        dialog->SetFileTypes(static_cast<UINT>(specs.size()), specs.data());
        dialog->SetFileTypeIndex(1); // 1-based in the shell API.
    }

    if (!request.initialPath.empty()) {
        // A folder passed as a plain path is the common case; IShellItem is what
        // the API wants. Failure here is not fatal -- the dialog just opens at
        // the default location.
        ComPtr<IShellItem> folder;
        if (SUCCEEDED(SHCreateItemFromParsingName(request.initialPath.c_str(), nullptr,
                                                  IID_PPV_ARGS(folder.Put())))) {
            dialog->SetFolder(folder.Get());
        }
    }
    return S_OK;
}

// Turns the shell's result list into our plain struct.
FileDialogResult CollectResults(IFileOpenDialog* dialog, bool allowMultiple) {
    FileDialogResult result;
    result.accepted = true;

    if (allowMultiple) {
        ComPtr<IShellItemArray> items;
        if (SUCCEEDED(dialog->GetResults(items.Put()))) {
            DWORD count = 0;
            if (SUCCEEDED(items->GetCount(&count))) {
                for (DWORD i = 0; i < count; ++i) {
                    ComPtr<IShellItem> item;
                    if (FAILED(items->GetItemAt(i, item.Put()))) continue;
                    PWSTR raw = nullptr;
                    if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &raw))) {
                        CoTaskFree holder(raw);
                        result.paths.emplace_back(holder.get());
                    }
                }
            }
        }
        return result;
    }

    ComPtr<IShellItem> item;
    if (FAILED(dialog->GetResult(item.Put()))) {
        // Accepted but no item: treat as cancel rather than an empty success, so
        // callers do not have to check both.
        return FileDialogResult{};
    }
    PWSTR raw = nullptr;
    if (FAILED(item->GetDisplayName(SIGDN_FILESYSPATH, &raw))) return FileDialogResult{};
    CoTaskFree holder(raw);
    result.paths.emplace_back(holder.get());
    return result;
}


class Win32FileDialogService final : public IFileDialogService {
public:
    FileDialogResult ShowOpen(const FileDialogRequest& request) override {
        if (request.pickFolders) return ShowFolder(request);
        return ShowCommon(request, /*save=*/false);
    }

    FileDialogResult ShowSave(const FileDialogRequest& request) override {
        return ShowCommon(request, /*save=*/true);
    }

private:
    FileDialogResult ShowFolder(const FileDialogRequest& request) {
        ComScope com;
        if (!com.Usable()) return FileDialogResult{};

        ComPtr<IFileOpenDialog> dialog;
        if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER,
                                    IID_PPV_ARGS(dialog.Put())))) {
            return FileDialogResult{};
        }
        DWORD options = 0;
        if (SUCCEEDED(dialog->GetOptions(&options))) {
            dialog->SetOptions(options | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM);
        }
        ConfigureDialog(dialog.Get(), request);
        if (FAILED(dialog->Show(nullptr))) return FileDialogResult{};
        return CollectResults(dialog.Get(), /*allowMultiple=*/false);
    }

    FileDialogResult ShowCommon(const FileDialogRequest& request, bool save) {
        ComScope com;
        if (!com.Usable()) return FileDialogResult{};

        ComPtr<IFileOpenDialog> dialog;
        const CLSID clsid = save ? CLSID_FileSaveDialog : CLSID_FileOpenDialog;
        if (FAILED(CoCreateInstance(clsid, nullptr, CLSCTX_INPROC_SERVER,
                                    IID_PPV_ARGS(dialog.Put())))) {
            return FileDialogResult{};
        }

        DWORD options = 0;
        if (SUCCEEDED(dialog->GetOptions(&options))) {
            DWORD flags = options | FOS_FORCEFILESYSTEM;
            if (!save && request.allowMultiple) flags |= FOS_ALLOWMULTISELECT;
            dialog->SetOptions(flags);
        }
        ConfigureDialog(dialog.Get(), request);

        const HRESULT hr = dialog->Show(nullptr);
        if (FAILED(hr)) {
            if (hr == HRESULT_FROM_WIN32(ERROR_CANCELLED)) return FileDialogResult{};
            return FileDialogResult{};
        }
        return CollectResults(dialog.Get(), !save && request.allowMultiple);
    }
};

// The global registration is a bare pointer on purpose: the service is owned by
// the host for the life of the process, and the framework must not take on
// lifetime management for something it did not create.
std::atomic<IFileDialogService*>& ServiceSlot() {
    static std::atomic<IFileDialogService*> slot{nullptr};
    return slot;
}

void SetGlobalService(IFileDialogService* service) { ServiceSlot().store(service); }
IFileDialogService* GetGlobalService() { return ServiceSlot().load(); }

} // namespace

void SetFileDialogService(IFileDialogService* service) { SetGlobalService(service); }

IFileDialogService* FileDialogService() { return GetGlobalService(); }

IFileDialogService* CreateWin32FileDialogService() { return new Win32FileDialogService(); }

} // namespace wui
