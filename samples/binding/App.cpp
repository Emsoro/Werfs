// SPDX-License-Identifier: MIT
// Sample entry point: one ViewModel property bound four different ways.
//
// The Win32 / D2D / DPI / input plumbing lives in samples/shared/SampleHost, so
// this file only says which markup, which ViewModel and which window.
#include "BindingResource.h" // IDR_WAML_BINDING
#include "BindingVm.h"
#include "SampleHost.h"

#include <windows.h>

#include <memory>

namespace {

std::shared_ptr<wui::RefObject> MakeViewModel() {
    auto vm = std::make_shared<sample::BindingVm>();
    return std::static_pointer_cast<wui::RefObject>(vm);
}

} // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int) {
    sample::HostOptions options;
    options.resourceType = WAML_RESOURCE_TYPE;
    options.resourceId = IDR_WAML_BINDING;
    // Used in debug builds: markup read from disk so editing it needs no rebuild.
    options.relativePath = "samples\\binding\\Binding.waml";
    options.title = L"Werfs — Binding Modes";
    // Wide enough for four columns side by side, which is the point of the page.
    options.widthDip = 840.0f;
    options.heightDip = 900.0f;
    return sample::RunSample(instance, options, &MakeViewModel);
}
