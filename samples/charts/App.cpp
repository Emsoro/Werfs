// SPDX-License-Identifier: MIT
// Sample entry point: a live chart and the downsampling comparison.
//
// Everything here is sample-specific: which ViewModel, which markup, how a frame
// advances the data, and the window title. The Win32 / D2D / DPI / input plumbing
// lives in samples/shared/SampleHost, so adding a sample does not mean copying it.
#include "ChartsVm.h"
#include "ChartsResource.h" // IDR_WAML_CHARTS
#include "SampleHost.h"

#include <windows.h>

#include <memory>

namespace {

std::shared_ptr<sample::ChartsVm> g_vm;

// Hands the page to the host. The frame callback is wired here rather than inside
// the ViewModel because "advance the data" is application behaviour: the framework
// and the host deliberately know nothing about what a sample does per frame.
std::shared_ptr<wui::RefObject> MakeViewModel() {
    g_vm = std::make_shared<sample::ChartsVm>();
    return std::static_pointer_cast<wui::RefObject>(g_vm);
}

} // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int) {
    sample::HostOptions options;
    options.resourceType = WAML_RESOURCE_TYPE;
    options.resourceId = IDR_WAML_CHARTS;
    // Used in debug builds: markup read from disk so editing it needs no rebuild.
    options.relativePath = "samples\\charts\\Charts.waml";
    options.title = L"Werfs \u2014 Live Charts";
    options.widthDip = 840.0f;
    options.heightDip = 900.0f;
    // One frame of live data: append the samples time has earned. Returning true only
    // when something actually changed keeps a paused page from repainting 60 times a
    // second for nothing.
    //
    // The frame rate is NOT fed from here: ChartPlot measures the interval between its
    // own paints. An earlier version called a TickChartFrame() that required the sample
    // to wire up a chart pointer, the call was never made, and the overlay silently
    // never appeared -- a property that exists and reads as implemented while doing
    // nothing. The control observing itself cannot be forgotten.
    options.onFrame = [](float seconds) {
        if (!g_vm) return false;
        return g_vm->Tick(seconds);
    };
    return sample::RunSample(instance, options, &MakeViewModel);
}
