// SPDX-License-Identifier: MIT
// Sample entry point: four indeterminate waiting animations, one clock.
//
// The Win32 / D2D / DPI / input plumbing lives in samples/shared/SampleHost, so
// this file only says which markup, which ViewModel, and how a frame advances
// the phase. The spinners themselves are a framework control; this sample is
// the page that gives them a clock.
#include "AnimationResource.h" // IDR_WAML_ANIMATION
#include "AnimationVm.h"
#include "SampleHost.h"

#include <windows.h>

#include <memory>

namespace {

std::shared_ptr<sample::AnimationVm> g_vm;

std::shared_ptr<wui::RefObject> MakeViewModel() {
    g_vm = std::make_shared<sample::AnimationVm>();
    return std::static_pointer_cast<wui::RefObject>(g_vm);
}

} // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int) {
    sample::HostOptions options;
    options.resourceType = WAML_RESOURCE_TYPE;
    options.resourceId = IDR_WAML_ANIMATION;
    // Used in debug builds: markup read from disk so editing it needs no rebuild.
    options.relativePath = "samples\\animation\\Animation.waml";
    options.title = L"Werfs \u2014 Waiting Animations";
    options.widthDip = 820.0f;
    options.heightDip = 820.0f;
    // One frame of clock work. Tick advances Phase and returns true only when
    // something moved; returning false while paused keeps the window idle.
    options.onFrame = [](float seconds) {
        return g_vm != nullptr && g_vm->Tick(seconds);
    };
    return sample::RunSample(instance, options, &MakeViewModel);
}
