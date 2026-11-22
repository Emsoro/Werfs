// SPDX-License-Identifier: MIT
// Sample entry point: the five basic controls on one ViewModel.
//
// Everything here is sample-specific: which ViewModel, which markup, the window
// title and size. The Win32 / D2D / DPI / input plumbing lives in
// samples/shared/SampleHost, so adding a sample does not mean copying it.
#include "BasicVm.h"
#include "BasicResource.h"    // IDR_WAML_BASIC
#include "SampleHost.h"

#include <windows.h>

#include <memory>

namespace {

std::shared_ptr<wui::RefObject> MakeViewModel() {
    auto vm = std::make_shared<sample::BasicViewVm>();
    return std::static_pointer_cast<wui::RefObject>(vm);
}

} // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int) {
    sample::HostOptions options;
    options.resourceType = WAML_RESOURCE_TYPE;
    options.resourceId = IDR_WAML_BASIC;
    // Used in debug builds: markup read from disk so editing it needs no rebuild.
    options.relativePath = "samples\\basic\\Basic.waml";
    options.title = L"Gridform — Basic Controls";
    // Narrower than the list sample: this page is a single column of cards.
    options.widthDip = 640.0f;
    options.heightDip = 760.0f;
    return sample::RunSample(instance, options, &MakeViewModel);
}
