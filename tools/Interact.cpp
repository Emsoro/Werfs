// SPDX-License-Identifier: MIT
// Headless interaction driver: builds the widget tree from the demo WAML, then
// drives synthesized input through the same InputRouter/HitTest path the Win32
// window uses, asserting on ViewModel state. This is what makes the "input
// works" claim verifiable in CI without a desktop.
#include "Controls.h"
#include "Core.h"
#include "Diagnostics.h"
#include "BasicVm.h"    // sample: the basic controls page's ViewModel
#include "EditorVm.h"   // sample: the multi-document editor's ViewModel
#include "FileDialog.h"
#include "ListVm.h"     // sample: the list page's ViewModel
#include "Mvvm.h"
#include "Overlay.h"
#include "BasicResource.h"  // IDR_WAML_BASIC (this tool defaults to the basic page)
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

// The basic sample has a different ViewModel. Running the gallery assertions
// against it would report failures that say nothing about the page, so the two
// pages get separate checks that both drive real hit tests and real input.
bool IsBasicPage(const std::string& origin, const std::string& path) {
    return origin.find("Basic.waml") != std::string::npos ||
           path.find("Basic.waml") != std::string::npos;
}

bool IsEditorPage(const std::string& origin, const std::string& path) {
    return origin.find("Editor.waml") != std::string::npos ||
           path.find("Editor.waml") != std::string::npos;
}

// Checks that only make sense on samples/basic/Basic.waml.

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

int RunBasicChecks(Widget* root, const std::shared_ptr<sample::BasicViewVm>& vm) {
    std::printf("\n[1] TextBlock bound from ViewModel\n");
    Widget* heading = nullptr;
    std::function<void(Widget*)> findHeading = [&](Widget* w) {
        if (!w || heading) return;
        const int slot = w->SlotOf("Text");
        if (slot >= 0 && w->Get<std::wstring>(slot) == L"Basic controls") heading = w;
        for (auto& c : w->Children()) findHeading(c.get());
    };
    findHeading(root);
    Expect(heading != nullptr, "heading text came from the VM");
    Expect(vm->Heading.Get() == L"Basic controls", "VM heading matches the page");

    std::printf("\n[2] Button -> command -> VM -> label\n");
    Widget* click = ButtonWithText(root, L"Click me");
    Expect(click != nullptr, "found 'Click me' button");
    const int before = vm->ClickCount.Get();
    if (click) {
        root->ForceLayout(Size{700, 1200});
        ClickAt(root, click);
    }
    Expect(vm->ClickCount.Get() == before + 1, "click executed ClickCommand");
    Expect(vm->ClickSummary.Get().find(L"First click") != std::wstring::npos ||
               vm->ClickSummary.Get().find(L"Still counting") != std::wstring::npos ||
               vm->ClickSummary.Get().find(L"Five or more") != std::wstring::npos,
           "command also updated the derived summary");
    Widget* reset = ButtonWithText(root, L"Reset");
    Expect(reset != nullptr, "found Reset button");
    if (reset) {
        root->ForceLayout(Size{700, 1200});
        ClickAt(root, reset);
    }
    Expect(vm->ClickCount.Get() == 0, "Reset restored the count");

    std::printf("\n[3] TextBox TwoWay\n");
    Widget* boxWidget = FindFirst(root, "wui/TextBox");
    Expect(boxWidget != nullptr, "found TextBox");
    if (auto* tb = dynamic_cast<TextBox*>(boxWidget)) {
        tb->SetCaretToEnd();
        tb->OnChar(L'!');
        Expect(vm->Greeting.Get() == L"Hello, WAML!", "typing wrote back to Greeting");
        vm->Greeting.Set(L"from-vm");
        Expect(tb->Get<std::wstring>(tb->SlotOf("Text")) == L"from-vm",
               "VM change flowed back into the TextBox");
    } else {
        Expect(false, "TextBox has the expected type");
    }

    std::printf("\n[4] CheckBox TwoWay + derived caption\n");
    Widget* check = FindFirst(root, "wui/CheckBox");
    Expect(check != nullptr, "found CheckBox");
    if (check) {
        const bool wasChecked = vm->IsAgreed.Get();
        const Rect& b = check->Bounds();
        Widget* hit = root->HitTest(Point{b.x + 8, b.y + b.h * 0.5f});
        Expect(hit == check, "hit test lands on the CheckBox");
        if (hit) {
            hit->OnPointerDown(Point{b.x + 8, b.y + b.h * 0.5f});
            hit->OnPointerUp(Point{b.x + 8, b.y + b.h * 0.5f});
        }
        Expect(vm->IsAgreed.Get() != wasChecked, "click toggled IsAgreed in the VM");
        Expect(vm->AgreementText.Get().find(vm->IsAgreed.Get() ? L"agreed" : L"Not agreed") !=
                   std::wstring::npos,
               "the caption followed the box state");
    }

    std::printf("\n[5] Visibility follows the CheckBox through a converter\n");
    // The confirmed row is driven by {Binding IsAgreed, Converter=BoolToVisibility}
    // so this exercises the whole chain: VM -> converter -> Visibility -> layout.
    Widget* confirmRow = nullptr;
    std::function<void(Widget*)> findConfirm = [&](Widget* w) {
        if (!w || confirmRow) return;
        const int slot = w->SlotOf("Text");
        if (slot >= 0) {
            const auto v = w->Get<std::wstring>(slot);
            if (v.rfind(L"Confirmed.", 0) == 0) { confirmRow = w->Parent(); return; }
        }
        for (auto& c2 : w->Children()) findConfirm(c2.get());
    };
    findConfirm(root);
    Expect(confirmRow != nullptr, "found the confirmation row");
    if (confirmRow) {
        const int visSlot = confirmRow->SlotOf("Visibility");
        Expect(visSlot >= 0, "confirmation row has Visibility");
        // IsAgreed is false after the toggle above only if it started false; use
        // the authoritative VM value rather than assuming.
        const bool agreed = vm->IsAgreed.Get();
        Expect(confirmRow->Get<int>(visSlot) == (agreed ? 0 : 1),
               "Visibility matches IsAgreed (true=Visible)");

        // Flip through the VM and confirm the widget follows -- this is the
        // property that would silently break if the converter stopped running.
        vm->IsAgreed.Set(!agreed);
        Expect(confirmRow->Get<int>(visSlot) == (agreed ? 1 : 0),
               "flipping IsAgreed flips Visibility");
        vm->IsAgreed.Set(agreed); // restore for later checks
    }

    std::printf("\n[6] no ListBox on this page\n");
    std::printf("\n[5] no ListBox on this page\n");
    // The basic page deliberately has no list; asserting its absence keeps the
    // page from quietly growing into a second list sample.
    Expect(FindFirst(root, "wui/ListBox") == nullptr, "basic page has no ListBox");

    std::printf("\n%s\n", g_failures == 0 ? "INTERACTION OK" : "INTERACTION FAILED");
    return g_failures == 0 ? 0 : 1;
}

// Checks for samples/editor/Editor.waml. This is the integration page, so the
// checks walk the whole story rather than a single control:
//   a tab strip drives the ViewModel, the toolbar and menu reach the same
//   commands, editing marks the document dirty, and closing a dirty tab asks.
int RunEditorChecks(Widget* root, const std::shared_ptr<sample::EditorVm>& vm) {
    std::printf("\n[1] the tab strip is seeded from the document list\n");
    Widget* tabWidget = FindFirst(root, "wui/TabView");
    Expect(tabWidget != nullptr, "found the TabView");
    if (!tabWidget) return g_failures == 0 ? 0 : 1;
    auto* tabs = dynamic_cast<TabView*>(tabWidget);
    Expect(tabs != nullptr, "TabView has the expected type");
    if (!tabs) return g_failures == 0 ? 0 : 1;

    Expect(tabs->ItemCount() == 3, "three documents seeded");
    Expect(vm->DocumentCount() == 3, "the ViewModel agrees");
    Expect(tabs->SelectedIndex() == 0, "the first tab is active");

    std::printf("\n[2] clicking a tab switches the active document\n");
    {
        const Rect t1 = tabs->TabBounds(1);
        tabs->OnPointerDown(Point{t1.x + 8, t1.y + t1.h * 0.5f});
        tabs->OnPointerUp(Point{t1.x + 8, t1.y + t1.h * 0.5f});
        Expect(tabs->SelectedIndex() == 1, "the second tab is selected");
        Expect(vm->ActiveIndex.Get() == 1, "the selection reached the ViewModel");
        Expect(vm->ActiveTitle.Get() == L"notes.txt", "the active document followed");
        // The editor area shows the NEW document's text, not the old one's: this is
        // the TwoWay binding doing its job in the reverse direction.
        Expect(vm->ActiveContent.Get().find(L"second document") != std::wstring::npos,
               "the editor area holds the second document's text");
    }

    std::printf("\n[3] the + button creates a document\n");
    {
        const int before = static_cast<int>(tabs->ItemCount());
        const Rect add = tabs->AddButtonBounds();
        tabs->OnPointerDown(Point{add.x + 4, add.y + 4});
        tabs->OnPointerUp(Point{add.x + 4, add.y + 4});
        Expect(static_cast<int>(tabs->ItemCount()) == before + 1, "+ added a tab");
        Expect(vm->DocumentCount() == static_cast<size_t>(before) + 1,
               "the ViewModel created the document");
        Expect(tabs->SelectedIndex() == before, "the new tab became active");
    }

    std::printf("\n[4] typing marks the document dirty\n");
    // The TextBox in the editor area is bound TwoWay to ActiveContent. Driving it
    // is what a user typing does.
    Widget* editorBox = FindFirst(root, "wui/TextBox");
    Expect(editorBox != nullptr, "found the editor TextBox");
    auto doc = vm->ActiveDoc();
    Expect(doc != nullptr, "there is an active document");
    if (doc) {
        Expect(!doc->IsDirty.Get(), "a newly created document starts clean");
        if (auto* tb = dynamic_cast<TextBox*>(editorBox)) {
            tb->SetCaretToEnd();
            tb->OnChar(L'x');
        }
        Expect(doc->IsDirty.Get(), "typing marked it dirty");
        Expect(doc->TabLabel.Get().find(L"\u2022") != std::wstring::npos,
               "the tab label gained the unsaved dot");
        // Undoing the edit must make it clean again: the flag is derived, not set.
        if (auto* tb = dynamic_cast<TextBox*>(editorBox)) {
            tb->OnKeyDown(KeyBack);
        }
        Expect(!doc->IsDirty.Get(), "deleting the character made it clean again");
    }

    std::printf("\n[5] closing a CLEAN tab removes it without asking\n");
    {
        const int before = static_cast<int>(tabs->ItemCount());
        // Make sure the active document is clean before pressing ×: a document that
        // still had edits would (correctly) raise the prompt instead.
        if (auto d = vm->ActiveDoc()) d->MarkSaved();
        tabs->ClearCloseRequest();
        const Rect close = tabs->CloseButtonBounds(static_cast<size_t>(tabs->SelectedIndex()));
        tabs->OnPointerDown(Point{close.x + 2, close.y + 2});
        tabs->OnPointerUp(Point{close.x + 2, close.y + 2});
        root->ForceLayout(Size{860, 660});
        Expect(!vm->PromptOpen.Get(), "no prompt for a clean document");
        Expect(static_cast<int>(tabs->ItemCount()) == before - 1, "the tab was removed");
    }

    std::printf("\n[6] closing a DIRTY tab asks first\n");
    {
        // Dirty the active document through the model (same path the TextBox uses).
        auto active = vm->ActiveDoc();
        Expect(active != nullptr, "there is still an active document");
        if (active) {
            active->SetContent(active->Content.Get() + L"edited");
            Expect(active->IsDirty.Get(), "the document is dirty");
        }
        const int before = static_cast<int>(tabs->ItemCount());
        tabs->ClearCloseRequest();
        const Rect close = tabs->CloseButtonBounds(static_cast<size_t>(tabs->SelectedIndex()));
        tabs->OnPointerDown(Point{close.x + 2, close.y + 2});
        tabs->OnPointerUp(Point{close.x + 2, close.y + 2});
        root->ForceLayout(Size{860, 660});
        Expect(vm->PromptOpen.Get(), "the ViewModel raised the prompt");
        Expect(static_cast<int>(tabs->ItemCount()) == before,
               "and the tab is STILL there -- nothing was removed yet");

        std::printf("\n[7] cancelling the prompt keeps the tab\n");
        vm->ResolveClose(sample::CloseChoice::Cancel);
        root->ForceLayout(Size{860, 660});
        Expect(!vm->PromptOpen.Get(), "the prompt closed");
        Expect(static_cast<int>(tabs->ItemCount()) == before, "the tab survived a cancel");
        Expect(vm->PendingCloseIndex() == -1, "no close is pending any more");
    }

    std::printf("\n[8] discarding the prompt removes the tab\n");
    {
        const int before = static_cast<int>(tabs->ItemCount());
        tabs->ClearCloseRequest();
        const Rect close = tabs->CloseButtonBounds(static_cast<size_t>(tabs->SelectedIndex()));
        tabs->OnPointerDown(Point{close.x + 2, close.y + 2});
        tabs->OnPointerUp(Point{close.x + 2, close.y + 2});
        root->ForceLayout(Size{860, 660});
        Expect(vm->PromptOpen.Get(), "the prompt is up again");
        vm->ResolveClose(sample::CloseChoice::Discard);
        root->ForceLayout(Size{860, 660});
        Expect(static_cast<int>(tabs->ItemCount()) == before - 1, "discard closed the tab");
    }

    std::printf("\n[9] the menu bar opens and its commands reach the ViewModel\n");
    {
        Widget* menuWidget = FindFirst(root, "wui/MenuBar");
        Expect(menuWidget != nullptr, "found the MenuBar");
        auto* menu = dynamic_cast<MenuBar*>(menuWidget);
        Expect(menu != nullptr, "MenuBar has the expected type");
        if (menu) {
            Expect(menu->Items().size() == 3, "File / Edit / Help");
            menu->OpenMenu(0);
            OverlayLayer* layer = root->Overlay();
            Expect(layer != nullptr && layer->Active(), "the File menu is open");
            if (layer) {
                // The first item is "New": choosing it must create a document.
                const int before = static_cast<int>(vm->DocumentCount());
                const Rect box = layer->Bounds();
                layer->OnPointerDown(Point{box.x + box.w * 0.5f, box.y + 6.0f + 15.0f});
                Expect(static_cast<int>(vm->DocumentCount()) == before + 1,
                       "New from the menu created a document");
                Expect(!layer->Active(), "and the menu closed");
                Expect(root->Overlay() == nullptr, "the overlay slot was released");
            }
        }
    }

    std::printf("\n[10] the toolbar carries the same commands and overflows\n");
    {
        Widget* barWidget = FindFirst(root, "wui/CommandBar");
        Expect(barWidget != nullptr, "found the CommandBar");
        auto* bar = dynamic_cast<CommandBar*>(barWidget);
        Expect(bar != nullptr, "CommandBar has the expected type");
        if (bar) {
            root->ForceLayout(Size{860, 660});
            Expect(!bar->IsOverflowing(), "a wide window shows every button");
            root->ForceLayout(Size{240, 660});
            Expect(bar->IsOverflowing(), "a narrow window overflows");
            root->ForceLayout(Size{860, 660});
        }
    }

    std::printf("\n[11] the page still lays out with no document open\n");
    {
        // Close everything through the model, then re-lay out: an editor with no
        // documents must not dereference a null active document.
        while (vm->DocumentCount() > 0) {
            auto d = vm->ActiveDoc();
            if (d) d->SetContent(d->Content.Get() + L"!");
            vm->RequestClose(vm->ActiveIndex.Get());
            vm->ResolveClose(sample::CloseChoice::Discard);
        }
        std::printf("    (documents closed; laying out)\n");
        root->ForceLayout(Size{860, 660});
        std::printf("    (layout done)\n");
        Expect(vm->DocumentCount() == 0, "every document was closed");
        Expect(vm->ActiveIndex.Get() == -1, "the selection reset to none");
        Expect(vm->ActiveTitle.Get() == L"(no document)", "the labels fell back cleanly");
        // Paint once. This is the check that matters: an editor with no documents
        // still has a tab strip (empty), a toolbar and a status line, and painting
        // those must not dereference a null active document. The recording context
        // is enough -- it drives every OnPaint in the tree without needing D2D.
        RecordingPaintContext recorder;
        root->PaintTree(recorder, MakePalette(ThemeKind::Light));
        Expect(recorder.OpCount() > 0, "the empty editor still paints");
        std::printf("    (%zu paint ops)\n", recorder.OpCount());
    }

    std::printf("\n[12] the tab strip survives re-layout after every close\n");
    {
        // Reopen something so the strip is not empty, then lay out twice. A strip
        // whose width cache went stale while empty computes tab rects from a vector
        // it never refilled -- that crash showed up only on the SECOND layout after
        // the list had shrunk, which is why this drives it explicitly.
        vm->NewDocument();
        root->ForceLayout(Size{860, 660});
        root->ForceLayout(Size{860, 660});
        Widget* tabWidget2 = FindFirst(root, "wui/TabView");
        if (auto* tabs2 = dynamic_cast<TabView*>(tabWidget2)) {
            Expect(tabs2->ItemCount() == 1, "the reopened strip has one tab");
            // The × geometry must be sane again: a zero-width tab draws its close
            // button off the end and makes the tab unclosable.
            const Rect tab = tabs2->TabBounds(0);
            Expect(tab.w > 0.0f, "the tab has a non-zero width");
            const Rect close = tabs2->CloseButtonBounds(0);
            Expect(close.x + close.w <= tab.x + tab.w + 0.5f,
                   "the close button sits inside the tab");
        }
    }

    std::printf("\n%s\n", g_failures == 0 ? "INTERACTION OK" : "INTERACTION FAILED");
    return g_failures == 0 ? 0 : 1;
}


// Checks for samples/list/List.waml: DataTemplate rows, per-item context and
// dynamic add/remove.
int RunListChecks(Widget* root, const std::shared_ptr<sample::ListViewVm>& vm) {
    std::printf("\n[1] DataTemplate shows per-row data\n");
    Widget* list = FindFirst(root, "wui/ListBox");
    Expect(list != nullptr, "found ListBox");
    if (!list) return g_failures == 0 ? 0 : 1;

    auto* lb = dynamic_cast<ListBox*>(list);
    Expect(lb != nullptr, "ListBox has the expected type");
    if (!lb) return g_failures == 0 ? 0 : 1;

    const int seeded = static_cast<int>(vm->ItemCount.Get());
    Expect(lb->ItemCount() == seeded, "rows materialised from ObservableList");

    // Row 1 must show its OWN title, not the first item's. This is the regression
    // that once made every row render the same data.
    if (seeded > 1) {
        Widget* row = lb->ItemWidgetAt(1);
        Assert(row != nullptr, "row 1 exists");
        Widget* title = nullptr;
        std::function<void(Widget*)> findTitle = [&](Widget* w) {
            if (!w || title) return;
            const int slot = w->SlotOf("Text");
            if (slot >= 0) {
                const auto v = w->Get<std::wstring>(slot);
                if (v.rfind(L"Item ", 0) == 0) { title = w; return; }
            }
            for (auto& c2 : w->Children()) findTitle(c2.get());
        };
        findTitle(row);
        Expect(title != nullptr, "row 1 contains a bound title");
        if (title) {
            Expect(title->Get<std::wstring>(title->SlotOf("Text")) == L"Item 2",
                   "row 1 shows its own item (no DataContext crossover)");
        }
    }

    std::printf("\n[2] commands add and remove rows\n");
    const int before = lb->ItemCount();
    Widget* add = ButtonWithText(root, L"Add");
    Expect(add != nullptr, "found Add button");
    if (add) {
        root->ForceLayout(Size{700, 2000});
        ClickAt(root, add);
    }
    Expect(lb->ItemCount() == before + 1, "Add appended a row");

    Widget* remove = ButtonWithText(root, L"Remove");
    Expect(remove != nullptr, "found Remove button");
    if (remove) {
        root->ForceLayout(Size{700, 2000});
        ClickAt(root, remove);
    }
    Expect(lb->ItemCount() == before, "Remove dropped a row");

    std::printf("\n[3] tab strip selects through the ViewModel\n");
    // The page's TabView is the only one, and its selection is bound TwoWay to
    // ActiveTab -- so clicking a tab must reach the VM, not just the control.
    Widget* tabWidget = FindFirst(root, "wui/TabView");
    Expect(tabWidget != nullptr, "found TabView");
    if (auto* tabs = dynamic_cast<TabView*>(tabWidget)) {
        Expect(tabs->ItemCount() == 3, "three tabs seeded from the VM");
        const int before = vm->ActiveTab.Get();
        Expect(tabs->SelectedIndex() == before, "control selection matches the VM");

        // Click the third tab.
        const Rect t2 = tabs->TabBounds(2);
        Widget* hit = root->HitTest(Point{t2.x + 6, t2.y + t2.h * 0.5f});
        Expect(hit == tabs, "hit test reaches the tab strip");
        tabs->OnPointerDown(Point{t2.x + 6, t2.y + t2.h * 0.5f});
        tabs->OnPointerUp(Point{t2.x + 6, t2.y + t2.h * 0.5f});
        Expect(tabs->SelectedIndex() == 2, "clicking tab 3 selected it");
        Expect(vm->ActiveTab.Get() == 2, "selection wrote back to the ViewModel");
        Expect(vm->ActiveDocumentTitle.Get() == L"History",
               "the VM summary followed the selection");

        // The + button asks the ViewModel for a new tab.
        const int countBefore = static_cast<int>(tabs->ItemCount());
        const Rect add = tabs->AddButtonBounds();
        tabs->OnPointerDown(Point{add.x + 4, add.y + 4});
        tabs->OnPointerUp(Point{add.x + 4, add.y + 4});
        root->ForceLayout(Size{700, 2400});
        Expect(static_cast<int>(tabs->ItemCount()) == countBefore + 1, "+ added a tab");
        Expect(tabs->SelectedIndex() == countBefore, "the new tab became active");
    }

    std::printf("\n[4] this page carries no basic-only controls\n");
    // Keeps the two samples from drifting into one another.
    Expect(FindFirst(root, "wui/TextBox") == nullptr, "list page has no TextBox");

    std::printf("\n%s\n", g_failures == 0 ? "INTERACTION OK" : "INTERACTION FAILED");
    return g_failures == 0 ? 0 : 1;
}

int main(int argc, char** argv) {
    setvbuf(stdout, nullptr, _IONBF, 0);
    // An explicit path wins; otherwise fall back to the basic sample's resource.
    // Each sample embeds its markup under its own id, so there is no single
    // "default page" -- basic is the smallest and the one people run first.
    WamlLoadResult loaded;
    // When falling back to the embedded resource, origin is "res:#101" and says
    // nothing about which page it is. Keep the logical path alongside so page
    // detection works the same either way.
    std::string argPath = argc > 1 ? argv[1] : "samples\\basic\\Basic.waml";
    if (argc > 1) {
        const std::string resolved = ResolveWamlPath(argv[1]);
        loaded = LoadWamlFile(resolved.empty() ? argv[1] : resolved);
    } else {
        loaded = LoadWaml(WAML_RESOURCE_TYPE, IDR_WAML_BASIC, argPath);
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

    // Dispatch on the page so each sample is driven against its own ViewModel.
    if (IsBasicPage(loaded.origin, argPath)) {
        auto vm = std::make_shared<sample::BasicViewVm>();
        WidgetPtr root = doc->Instantiate(&text);
        root->SetDataContext(std::static_pointer_cast<RefObject>(vm));
        root->ForceLayout(Size{700, 1200});
        return RunBasicChecks(root.get(), vm);
    }

    if (IsEditorPage(loaded.origin, argPath)) {
        auto vm = std::make_shared<sample::EditorVm>();
        // No dialog service: the harness must never open a real dialog (R15). The
        // editor's Open/Save As report "no service installed" instead, which is
        // exactly the headless contract.
        SetFileDialogService(nullptr);
        WidgetPtr root = doc->Instantiate(&text);
        root->SetDataContext(std::static_pointer_cast<RefObject>(vm));
        root->ForceLayout(Size{860, 660});
        return RunEditorChecks(root.get(), vm);
    }

    auto listVm = std::make_shared<sample::ListViewVm>();
    WidgetPtr listRoot = doc->Instantiate(&text);
    listRoot->SetDataContext(std::static_pointer_cast<RefObject>(listVm));
    listRoot->ForceLayout(Size{700, 2000});
    return RunListChecks(listRoot.get(), listVm);
}
