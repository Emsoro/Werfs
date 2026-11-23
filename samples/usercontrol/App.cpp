// SPDX-License-Identifier: MIT
// Sample entry point: a page whose cards bind their own data sources.
//
// The Win32 / D2D / DPI / input plumbing lives in samples/shared/SampleHost, so
// this file only says which markup, which ViewModel and which window.
#include "UserControlResource.h" // IDR_WAML_USERCONTROL
#include "SampleHost.h"
#include "UserControlVm.h"

#include <windows.h>

#include <memory>

namespace {

std::shared_ptr<wui::RefObject> MakeViewModel() {
    auto vm = std::make_shared<sample::UserControlVm>();
    return std::static_pointer_cast<wui::RefObject>(vm);
}

} // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int) {
    sample::HostOptions options;
    options.resourceType = WAML_RESOURCE_TYPE;
    options.resourceId = IDR_WAML_USERCONTROL;
    // Used in debug builds: markup read from disk so editing it needs no rebuild.
    options.relativePath = "samples\\usercontrol\\UserControl.waml";
    options.title = L"Werfs — Component Scope";
    // Two cards side by side, with a page-level block above them.
    options.widthDip = 760.0f;
    options.heightDip = 620.0f;
    return sample::RunSample(instance, options, &MakeViewModel);
}
