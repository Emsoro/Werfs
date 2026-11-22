// SPDX-License-Identifier: MIT
// Sample entry point: collections and the layout panels around them.
//
// Everything here is sample-specific: which ViewModel, which markup, and the
// window title. The Win32 / D2D / DPI / input plumbing lives in
// samples/shared/SampleHost, so adding a sample does not mean copying it.
#include "ListVm.h"
#include "ListResource.h"    // IDR_WAML_LIST
#include "SampleHost.h"

#include <windows.h>

#include <memory>

namespace {

std::shared_ptr<wui::RefObject> MakeViewModel() {
    auto vm = std::make_shared<sample::ListViewVm>();
    return std::static_pointer_cast<wui::RefObject>(vm);
}

} // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int) {
    sample::HostOptions options;
    options.resourceType = WAML_RESOURCE_TYPE;
    options.resourceId = IDR_WAML_LIST;
    // Used in debug builds: markup read from disk so editing it needs no rebuild.
    options.relativePath = "samples\\list\\List.waml";
    options.title = L"Werfs — Collections & Layout";
    // Wider than the default sample: the page demonstrates panels side by side.
    options.widthDip = 720.0f;
    options.heightDip = 760.0f;
    return sample::RunSample(instance, options, &MakeViewModel);
}
