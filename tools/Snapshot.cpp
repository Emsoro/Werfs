// SPDX-License-Identifier: MIT
// Real D2D/DWrite snapshot tool: compiles a WAML file, instantiates it against a
// sample ViewModel, renders through Direct2D into a WIC bitmap, and verifies
// that the PNG round-trips. Also checks cross-run determinism.
#include "Controls.h"
#include "Core.h"
#include "D2DRender.h"
#include "Diagnostics.h"
#include "GalleryVm.h"  // sample: the Gallery page's ViewModel
#include "ListVm.h"     // sample: the list page's ViewModel
#include "BasicVm.h"    // sample: the basic controls page's ViewModel
#include "Mvvm.h"
#include "Resource.h"  // WAML_RESOURCE_TYPE / IDR_WAML_GALLERY
#include "Resources.h" // LoadWaml honours the debug/release switch
#include "Types.h"
#include "Waml.h"
#include "Widget.h"

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <functional>
#include <sstream>
#include <string>
#include <vector>

#ifndef SPIKE_SAMPLE_WAML
#define SPIKE_SAMPLE_WAML "sample.waml"
#endif

using namespace wui;

namespace {

// Sample ViewModel, shared with the other tools.
using SnapshotVm = sample::GalleryVm;

std::string ReadFile(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return {};
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

} // namespace

int main(int argc, char** argv) {
    setvbuf(stdout, nullptr, _IONBF, 0); // Unbuffered: a crash must not hide progress.
    std::string wamlPath = argc > 1 ? argv[1] : std::string();
    const std::string outLight = argc > 2 ? argv[2] : "snapshot-light.png";
    const std::string outDark = argc > 3 ? argv[3] : "snapshot-dark.png";

    // Explicit path wins; otherwise the build configuration decides whether the
    // snapshot renders the loose file or the packaged payload. Both must render
    // identically, which is a useful cross-check of the packaging step.
    WamlLoadResult loaded;
    if (!wamlPath.empty()) {
        const std::string resolved = ResolveWamlPath(wamlPath);
        loaded = LoadWamlFile(resolved.empty() ? wamlPath : resolved);
    } else {
        loaded = LoadWaml(WAML_RESOURCE_TYPE, IDR_WAML_GALLERY, "samples\\gallery\\Gallery.waml");
    }
    if (!loaded.ok) {
        std::printf("FAIL: cannot load markup: %s\n", loaded.error.c_str());
        return 1;
    }
    std::printf("markup: %s\n", loaded.origin.c_str());

    ParseOptions opt;
    opt.fileName = loaded.FileName();
    DiagnosticBag bag;
    auto doc = CompileWaml(loaded.text, opt, bag);
    if (!doc) {
        std::printf("COMPILE FAILED\n%s", bag.ToText().c_str());
        return 1;
    }
    std::printf("compiled: %zu nodes, %zu bindings, %zu props\n", doc->nodeCount, doc->bindingCount,
                doc->propertyCount);

    DWriteTextService text;
    std::printf("dwrite: %s\n", text.Valid() ? "ok" : "unavailable (fallback metrics)");
    // Keep the controls and the D2D renderer measuring text the same way.
    TextSystem::Set(&text);

    // Pick the ViewModel that matches the markup being rendered. The harness is
    // shared by all samples, so binding the wrong VM would produce a page full
    // of empty values that still renders "successfully" -- a silent wrong
    // picture. The file name is a reliable enough signal here.
    const std::string source = loaded.origin + " " + wamlPath;
    const bool isListPage = source.find("List.waml") != std::string::npos;
    const bool isBasicPage = source.find("Basic.waml") != std::string::npos;

    std::shared_ptr<RefObject> vmRef;
    if (isBasicPage) {
        auto basicVm = std::make_shared<sample::BasicViewVm>();
        std::printf("vm: BasicViewVm\n");
        vmRef = std::static_pointer_cast<RefObject>(basicVm);
    } else if (isListPage) {
        auto listVm = std::make_shared<sample::ListViewVm>();
        std::printf("vm: ListViewVm (%zu rows)\n", static_cast<size_t>(listVm->ItemCount.Get()));
        vmRef = std::static_pointer_cast<RefObject>(listVm);
    } else {
        auto galleryVm = std::make_shared<SnapshotVm>();
        galleryVm->SeedItems(5);
        vmRef = std::static_pointer_cast<RefObject>(galleryVm);
    }

    // Height is a parameter because pages differ in length: the gallery fits in
    // 900, the list page needs around 2000 to show every section. Rendering a
    // too-short canvas silently clips the lower half and still reports success.
    const int width = 720;
    const int height = argc > 4 ? std::atoi(argv[4]) : 900;
    if (height <= 0) {
        std::printf("FAIL: height must be positive\n");
        return 1;
    }
    // Optional scroll offset, so the tool can prove that scrolling actually moves
    // pixels instead of only changing a number. argv[5] in DIPs.
    const float scroll = argc > 5 ? static_cast<float>(std::atof(argv[5])) : 0.0f;

    auto build = [&]() {
        WidgetPtr root = doc->Instantiate(&text);
        root->SetDataContext(vmRef);
        root->ForceLayout(Size{static_cast<float>(width), static_cast<float>(height)});
        if (scroll > 0.0f) {
            // Apply to every scroller on the page; a sample has one.
            std::function<void(Widget*)> apply = [&](Widget* w) {
                if (!w) return;
                if (auto* sv = dynamic_cast<ScrollViewer*>(w)) sv->ScrollTo(scroll);
                for (auto& c : w->Children()) apply(c.get());
            };
            apply(root.get());
            root->ForceLayout(Size{static_cast<float>(width), static_cast<float>(height)});
        }
        return root;
    };

    int failures = 0;
    for (int pass = 0; pass < 2; ++pass) {
        const ThemeKind theme = pass == 0 ? ThemeKind::Light : ThemeKind::Dark;
        const std::string& outPath = pass == 0 ? outLight : outDark;
        WidgetPtr root = build();
        D2DSnapshotRenderer renderer(width, height, theme);
        if (!renderer.Valid()) {
            std::printf("FAIL: renderer init: %s\n", renderer.ErrorMessage());
            return 1;
        }
        std::vector<uint32_t> pixelsA;
        if (!renderer.Render(*root, pixelsA)) {
            std::printf("FAIL: render pass %d\n", pass);
            return 1;
        }
        const uint64_t hashA = renderer.HashPixels(pixelsA);

        D2DSnapshotRenderer renderer2(width, height, theme);
        WidgetPtr root2 = build();
        std::vector<uint32_t> pixelsB;
        if (!renderer2.Render(*root2, pixelsB)) {
            std::printf("FAIL: render pass %d (second renderer)\n", pass);
            return 1;
        }
        const uint64_t hashB = renderer2.HashPixels(pixelsB);
        const bool deterministic = hashA == hashB && !pixelsA.empty();

        // Count non-background pixels so an all-black frame cannot pass.
        size_t distinct = 0;
        const uint32_t first = pixelsA.empty() ? 0 : pixelsA[0];
        for (uint32_t px : pixelsA)
            if (px != first) {
                ++distinct;
                if (distinct > 1000) break;
            }

        const std::wstring wideOut = Utf8ToWide(outPath);
        const bool saved = renderer.SavePng(wideOut);
        int pngW = 0;
        int pngH = 0;
        const bool verified = saved && D2DSnapshotRenderer::VerifyPng(wideOut, pngW, pngH);

        std::printf("[%s] hash=0x%016llx deterministic=%s png=%s %dx%d distinct>=%s\n",
                    pass == 0 ? "light" : "dark", static_cast<unsigned long long>(hashA),
                    deterministic ? "yes" : "NO", verified ? "ok" : "FAIL", pngW, pngH,
                    distinct > 1000 ? "1000" : "few");
        if (!deterministic || !verified || distinct < 1000) ++failures;
        if (pass == 0) {
            // Re-render the same tree to make sure rendering is repeatable.
            std::vector<uint32_t> again;
            if (renderer.Render(*root, again) && renderer.HashPixels(again) != hashA) {
                std::printf("FAIL: same-tree re-render is not deterministic\n");
                ++failures;
            }
        }
    }

    if (failures > 0) {
        std::printf("SNAPSHOT FAILED (%d)\n", failures);
        return 1;
    }
    std::printf("SNAPSHOT OK\n");
    return 0;
}
