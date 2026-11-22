// SPDX-License-Identifier: MIT
// Headless interaction driver: builds the widget tree from the demo WAML, then
// drives synthesized input through the same InputRouter/HitTest path the Win32
// window uses, asserting on ViewModel state. This is what makes the "input
// works" claim verifiable in CI without a desktop.
#include "Controls.h"
#include "Core.h"
#include "Diagnostics.h"
#include "GalleryVm.h"  // sample: the Gallery page's ViewModel
#include "Mvvm.h"
#include "Resource.h"  // WAML_RESOURCE_TYPE / IDR_WAML_GALLERY
#include "Resources.h" // LoadWaml honours the debug/release switch
#include "SoftwareRender.h"
#include "Types.h"
#include "Waml.h"
#include "Widget.h"

#include <cstdio>
#include <fstream>
#include <functional>
#include <sstream>
#include <string>

using namespace wui;

namespace {

int g_failures = 0;

void Expect(bool cond, const char* what) {
    std::printf("  %s %s\n", cond ? "[ ok ]" : "[FAIL]", what);
    if (!cond) ++g_failures;
}

void Assert(bool cond, const char* what) { Expect(cond, what); }

// Sample ViewModel, shared with the other tools.
using Vm = sample::GalleryVm;

std::string ReadFile(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return {};
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

Widget* FindFirst(Widget* w, const char* typeName) {
    if (!w) return nullptr;
    if (std::string(w->TypeName()) == typeName) return w;
    for (auto& c : w->Children()) {
        if (Widget* found = FindFirst(c.get(), typeName)) return found;
    }
    return nullptr;
}

// Click at the centre of a widget, using its absolute bounds.
void ClickAt(Widget* root, Widget* target) {
    const Rect& b = target->Bounds();
    root->HitTest(Point{b.x + b.w * 0.5f, b.y + b.h * 0.5f})
        ? (void)0
        : (void)0;
    Widget* hit = root->HitTest(Point{b.x + b.w * 0.5f, b.y + b.h * 0.5f});
    if (!hit) return;
    hit->OnPointerDown(Point{b.x + b.w * 0.5f, b.y + b.h * 0.5f});
    hit->OnPointerUp(Point{b.x + b.w * 0.5f, b.y + b.h * 0.5f});
}

Widget* ButtonWithText(Widget* root, const std::wstring& content) {
    for (auto& c : root->Children()) {
        if (std::string(c->TypeName()) == "wui/Button") {
            if (c->Get<std::wstring>(c->SlotOf("Content")) == content) return c.get();
        }
        if (Widget* found = ButtonWithText(c.get(), content)) return found;
    }
    return nullptr;
}

} // namespace

int main(int argc, char** argv) {
    setvbuf(stdout, nullptr, _IONBF, 0);
    // Same delivery switch as the demo: an explicit path wins, otherwise the
    // build configuration decides (debug = file, release = embedded resource).
    WamlLoadResult loaded;
    if (argc > 1) {
        const std::string resolved = ResolveWamlPath(argv[1]);
        loaded = LoadWamlFile(resolved.empty() ? argv[1] : resolved);
    } else {
        loaded = LoadWaml(WAML_RESOURCE_TYPE, IDR_WAML_GALLERY, "samples\\gallery\\Gallery.waml");
    }
    if (!loaded.ok) {
        std::printf("cannot load markup (%s): %s\n",
                    PrefersLooseWamlFiles() ? "loose files" : "embedded resource", loaded.error.c_str());
        return 1;
    }
    std::printf("markup: %s\n", loaded.origin.c_str());

    DiagnosticBag bag;
    ParseOptions opt;
    opt.fileName = loaded.FileName();
    auto doc = CompileWaml(loaded.text, opt, bag);
    if (!doc) {
        std::printf("compile failed:\n%s", bag.ToText().c_str());
        return 1;
    }
    std::printf("WAML: %zu nodes, %zu bindings\n", doc->nodeCount, doc->bindingCount);

    FakeTextService text;
    TextSystem::Set(&text);
    auto vm = std::make_shared<Vm>();
    vm->Title.Set(L"demo"); // this tool asserts against a known title
    vm->SeedItems(3);
    WidgetPtr root = doc->Instantiate(&text);
    root->SetDataContext(std::static_pointer_cast<RefObject>(vm));
    root->ForceLayout(Size{760, 900});

    std::printf("\n[1] initial bindings\n");
    Widget* title = FindFirst(root.get(), "wui/TextBlock");
    Expect(title && title->Get<std::wstring>(title->SlotOf("Text")) == L"demo", "title bound from VM");

    std::printf("\n[2] command via click\n");
    Widget* plus = ButtonWithText(root.get(), L"+");
    Expect(plus != nullptr, "found '+' button");
    const int before = vm->Count.Get();
    if (plus) {
        root->ForceLayout(Size{760, 900}); // ensure bounds are current
        ClickAt(root.get(), plus);
    }
    Expect(vm->Count.Get() == before + 1, "click executed IncrementCommand");
    Widget* minus = ButtonWithText(root.get(), L"-");
    if (minus) ClickAt(root.get(), minus);
    Expect(vm->Count.Get() == before, "click executed DecrementCommand");

    std::printf("\n[3] two-way text input\n");
    Widget* box = FindFirst(root.get(), "wui/TextBox");
    Expect(box != nullptr, "found TextBox");
    if (box) {
        auto* tb = dynamic_cast<TextBox*>(box);
        Expect(tb != nullptr, "TextBox has the expected type");
        if (tb) {
            tb->SetCaretToEnd();
            tb->OnChar(L'o');
            tb->OnChar(L'k');
            Expect(vm->Name.Get() == L"ok", "typed characters reached the ViewModel");
            vm->Name.Set(L"from-vm");
            Expect(box->Get<std::wstring>(box->SlotOf("Text")) == L"from-vm",
                   "VM change flowed back into the TextBox");
        }
    }

    std::printf("\n[4] two-way checkbox toggle\n");
    Widget* check = FindFirst(root.get(), "wui/CheckBox");
    Expect(check != nullptr, "found CheckBox");
    if (check) {
        const bool wasChecked = vm->Enabled.Get();
        const Rect& b = check->Bounds();
        Widget* hit = root->HitTest(Point{b.x + 8, b.y + b.h * 0.5f});
        Expect(hit == check, "hit test lands on the CheckBox");
        if (hit) {
            hit->OnPointerDown(Point{b.x + 8, b.y + b.h * 0.5f});
            hit->OnPointerUp(Point{b.x + 8, b.y + b.h * 0.5f});
        }
        std::printf("    enabled: before=%d after=%d\n", static_cast<int>(wasChecked),
                    static_cast<int>(vm->Enabled.Get()));
        Expect(vm->Enabled.Get() != wasChecked, "toggle wrote back to IsChecked");
    }

    std::printf("\n[5] list rendering from DataTemplate\n");
    Widget* list = FindFirst(root.get(), "wui/ListBox");
    Expect(list != nullptr, "found ListBox");
    if (auto* lb = dynamic_cast<ListBox*>(list)) {
        Expect(lb->ItemCount() == 3, "3 rows materialised from ObservableList");
        Widget* row = lb->ItemWidgetAt(1);
        Assert(row != nullptr, "row 1 exists");
        // Dump the row subtree so the check cannot silently test the wrong node.
        std::function<void(Widget*, int)> dump = [&](Widget* w, int depth) {
            if (!w) return;
            std::printf("      %*s%s", depth * 2, "", w->TypeName());
            const int slot = w->SlotOf("Text");
            if (slot >= 0) std::printf(" text='%s'", WideToUtf8(w->Get<std::wstring>(slot)).c_str());
            std::printf("\n");
            for (auto& c2 : w->Children()) dump(c2.get(), depth + 1);
        };
        dump(row, 0);
        // Find the row's title TextBlock (the one that is not the "#'n'" index).
        Widget* rowText = nullptr;
        std::function<void(Widget*)> findTitle = [&](Widget* w) {
            if (!w || rowText) return;
            const int slot = w->SlotOf("Text");
            if (slot >= 0) {
                const auto v = w->Get<std::wstring>(slot);
                if (v.rfind(L"Item ", 0) == 0) {
                    rowText = w;
                    return;
                }
            }
            for (auto& c2 : w->Children()) findTitle(c2.get());
        };
        findTitle(row);
        if (rowText) {
            std::printf("    row1 title = '%s'\n",
                        WideToUtf8(rowText->Get<std::wstring>(rowText->SlotOf("Text"))).c_str());
        }
        Expect(rowText != nullptr, "row 1 contains a bound title");
        if (rowText) {
            Expect(rowText->Get<std::wstring>(rowText->SlotOf("Text")) == L"Item 2",
                   "row 1 shows its own item (no DataContext crossover)");
        }
    }

    std::printf("\n%s\n", g_failures == 0 ? "INTERACTION OK" : "INTERACTION FAILED");
    return g_failures == 0 ? 0 : 1;
}
