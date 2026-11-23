// SPDX-License-Identifier: MIT
// Sample entry point: semantic tokens and a runtime Light/Dark switch.
//
// The host listens to the ViewModel's IsDarkTheme property, so this file only
// names it; all Win32 / D2D plumbing stays in samples/shared/SampleHost.
#include "SampleHost.h"
#include "ThemingResource.h" // IDR_WAML_THEMING
#include "ThemingVm.h"

#include <windows.h>

#include <memory>

namespace {

std::shared_ptr<wui::RefObject> MakeViewModel() {
    auto vm = std::make_shared<sample::ThemingVm>();
    return std::static_pointer_cast<wui::RefObject>(vm);
}

} // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int) {
    sample::HostOptions options;
    options.resourceType = WAML_RESOURCE_TYPE;
    options.resourceId = IDR_WAML_THEMING;
    // Used in debug builds: markup read from disk so editing it needs no rebuild.
    options.relativePath = "samples\\theming\\Theming.waml";
    options.title = L"Werfs — Theming";
    // A live theme switch is a host feature: true = dark.
    options.themeProperty = L"IsDarkTheme";
    options.widthDip = 720.0f;
    options.heightDip = 900.0f;
    return sample::RunSample(instance, options, &MakeViewModel);
}
