// SPDX-License-Identifier: MIT
// Headless interaction driver: builds the widget tree from the demo WAML, then
// drives synthesized input through the same InputRouter/HitTest path the Win32
// window uses, asserting on ViewModel state. This is what makes the "input
// works" claim verifiable in CI without a desktop.
#include "Controls.h"
#include "Core.h"
#include "Diagnostics.h"
#include "BasicVm.h"    // sample: the basic controls page's ViewModel
#include "BindingVm.h"  // sample: the four binding modes page's ViewModel
#include "ThemingVm.h"  // sample: the token/theme page's ViewModel
#include "UserControlVm.h" // sample: the nested-data-source page's ViewModels
#include "EditorVm.h"   // sample: the multi-document editor's ViewModel
#include "FileDialog.h"
#include "ListVm.h"     // sample: the list page's ViewModel
#include "Mvvm.h"
#include "Animation.h"
#include "VisualStates.h"
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

bool IsBindingPage(const std::string& origin, const std::string& path) {
    return origin.find("Binding.waml") != std::string::npos ||
           path.find("Binding.waml") != std::string::npos;
}

bool IsThemingPage(const std::string& origin, const std::string& path) {
    return origin.find("Theming.waml") != std::string::npos ||
           path.find("Theming.waml") != std::string::npos;
}

bool IsUserControlPage(const std::string& origin, const std::string& path) {
    return origin.find("UserControl.waml") != std::string::npos ||
           path.find("UserControl.waml") != std::string::npos;
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

// A page can carry several TextBoxes bound to the same property; the placeholder
// is what tells them apart in these checks (and is visible to the reader too).
Widget* TextBoxWithPlaceholder(Widget* root, const std::wstring& placeholder) {
    for (auto& c : root->Children()) {
        if (std::string(c->TypeName()) == "wui/TextBox") {
            const int slot = c->SlotOf("PlaceholderText");
            if (slot >= 0 && c->Get<std::wstring>(slot) == placeholder) return c.get();
        }
        if (Widget* found = TextBoxWithPlaceholder(c.get(), placeholder)) return found;
    }
    return nullptr;
}

std::wstring TextOf(Widget* w) {
    if (!w) return L"";
    const int slot = w->SlotOf("Text");
    return slot >= 0 ? w->Get<std::wstring>(slot) : std::wstring();
}

// Finds a TextBlock whose text starts with `prefix`.
Widget* TextBlockStartingWith(Widget* root, const std::wstring& prefix) {
    for (auto& c : root->Children()) {
        if (std::string(c->TypeName()) == "wui/TextBlock") {
            if (TextOf(c.get()).rfind(prefix, 0) == 0) return c.get();
        }
        if (Widget* found = TextBlockStartingWith(c.get(), prefix)) return found;
    }
    return nullptr;
}

} // namespace

namespace {

// Advances every animation by `seconds` in fixed 60 Hz steps. The host feeds real elapsed
// time from its timer; the harness feeds exact steps so the assertions can name values
// rather than ranges.
void AdvanceFrames(float seconds) {
    const float step = 1.0f / 60.0f;
    for (float t = 0.0f; t < seconds - 1e-6f; t += step) AnimationHost::AdvanceAll(step);
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

    std::printf("\n[13] ComboBox opens a real overlay and selecting a row writes the model\n");
    {
        root->SetOverlay(nullptr);
        Widget* comboWidget = FindFirst(root, "wui/ComboBox");
        auto* combo = dynamic_cast<ComboBox*>(comboWidget);
        Expect(combo != nullptr, "the page has a ComboBox");
        if (combo != nullptr) {
            Expect(!combo->Get<bool>(combo->SlotOf("IsOpen")), "it starts closed");
            const Rect box = combo->Bounds();
            // Drive the real path: down then up inside the box.
            combo->OnPointerDown(Point{box.x + 8.0f, box.y + 8.0f});
            combo->OnPointerUp(Point{box.x + 8.0f, box.y + 8.0f});
            Expect(combo->Get<bool>(combo->SlotOf("IsOpen")), "clicking it opens the list");
            OverlayLayer* layer = root->Overlay();
            Expect(layer != nullptr, "the list lives in an overlay, not in the card");
            if (layer != nullptr) {
                // Row 3 is "Python" (C++, C#, Rust, Python, Go).
                const Rect lb = layer->Bounds();
                const Point row3{lb.x + 20.0f, lb.y + 8.0f + 28.0f * 3.0f + 14.0f};
                layer->OnPointerDown(row3);
                layer->OnPointerUp(row3);
                Expect(combo->Get<int>(combo->SlotOf("SelectedIndex")) == 3,
                       "picking a row sets the index");
                // The ViewModel must have heard it, through a TwoWay binding.
                Expect(vm->LanguageIndex.Get() == 3, "the model received the new index");
                Expect(!combo->Get<bool>(combo->SlotOf("IsOpen")), "choosing closes the list");
            }
        }
    }

    std::printf("\n[14] ComboBox outside-click dismisses without changing the value\n");
    {
        root->SetOverlay(nullptr);
        Widget* comboWidget = FindFirst(root, "wui/ComboBox");
        auto* combo = dynamic_cast<ComboBox*>(comboWidget);
        if (combo != nullptr) {
            const int before = combo->Get<int>(combo->SlotOf("SelectedIndex"));
            const Rect box = combo->Bounds();
            combo->OnPointerUp(Point{box.x + 8.0f, box.y + 8.0f});
            OverlayLayer* layer = root->Overlay();
            Expect(layer != nullptr, "the list is open again");
            if (layer != nullptr) {
                // A press outside must dismiss AND be consumed, so the click does not also
                // activate whatever it landed on.
                Expect(layer->OnPointerDown(Point{2.0f, 2.0f}), "the outside press is consumed");
                Expect(!layer->Active(), "and the list closes");
            }
            Expect(combo->Get<int>(combo->SlotOf("SelectedIndex")) == before,
                   "dismissing does not change the selection");
            Expect(!combo->Get<bool>(combo->SlotOf("IsOpen")), "the control agrees it is closed");
        }
    }

    std::printf("\n[15] Flyout hosts real controls and light-dismisses\n");
    {
        root->SetOverlay(nullptr);
        Widget* flyoutWidget = FindFirst(root, "wui/Flyout");
        auto* flyout = dynamic_cast<Flyout*>(flyoutWidget);
        Expect(flyout != nullptr, "the page has a Flyout");
        if (flyout != nullptr) {
            const Rect box = flyout->Bounds();
            flyout->OnPointerUp(Point{box.x + 8.0f, box.y + 8.0f});
            OverlayLayer* layer = root->Overlay();
            Expect(layer != nullptr, "the flyout opened an overlay");
            Expect(flyout->Children().empty(),
                   "its content left the tree, so a closed flyout reserves no space");
            if (layer != nullptr) {
                const Rect lb = layer->Bounds();
                Expect(lb.w > 0.0f && lb.h > 0.0f, "the card has a real size");
                Expect(layer->OnPointerDown(Point{lb.x + 20.0f, lb.y + 20.0f}),
                       "a press inside the card is consumed");
                Expect(layer->Active(), "and does not dismiss it");
                // Escape closes it.
                Expect(layer->OnKeyDown(KeyEscape, 0), "Escape is handled");
                Expect(!flyout->Get<bool>(flyout->SlotOf("IsOpen")), "Escape closes the flyout");
            }
        }
    }

    std::printf("\n[16] ToolTip labels on hover and never intercepts clicks\n");
    {
        Widget* tipWidget = FindFirst(root, "wui/ToolTip");
        auto* tip = dynamic_cast<ToolTip*>(tipWidget);
        Expect(tip != nullptr, "the page has a ToolTip");
        if (tip != nullptr) {
            const Rect b = tip->Bounds();
            // Hits must pass through, or the label would break the control under it.
            Expect(tip->HitTest(Point{b.x + 2.0f, b.y + 2.0f}) == nullptr,
                   "a tooltip does not claim hits");
        }
    }

    std::printf("\n[17] Visual states: hover animates and leaving restores\n");
    {
        AnimationHost::ResetAll();
        Widget* chilled = nullptr;
        std::function<void(Widget*)> findLively = [&](Widget* w) {
            if (!w || chilled) return;
            if (w->VisualStates() != nullptr &&
                w->Get<std::wstring>(w->SlotOf("Content")) == L"Hover me") {
                chilled = w;
                return;
            }
            for (auto& c : w->Children()) findLively(c.get());
        };
        findLively(root);
        Expect(chilled != nullptr, "the page has a styled button");
        if (chilled != nullptr) {
            const int slot = chilled->SlotOf("FontSize");
            const float rest = chilled->Get<float>(slot);
            chilled->OnPointerEnter();
            // A transition, not a snap: the very first frame is still near the old size.
            AdvanceFrames(0.05f);
            const float early = chilled->Get<float>(slot);
            Expect(early > rest, "hovering started growing it");
            Expect(early < rest + 2.0f, "and did not jump straight to the target");
            AdvanceFrames(0.4f);
            const float full = chilled->Get<float>(slot);
            Expect(full > rest, "after the transition it is larger");

            // Release outside: leaving must return to the rest size, which is the part
            // that breaks when a state forgets to restore.
            chilled->OnPointerLeave();
            AdvanceFrames(0.4f);
            Expect(std::abs(chilled->Get<float>(slot) - rest) < 0.05f,
                   "leaving restored the style's value");
            Expect(chilled->VisualStates()->Current() == "Normal", "and the state is Normal");
        }
    }

    std::printf("\n[18] Visual states: Disabled outranks hover\n");
    {
        AnimationHost::ResetAll();
        Widget* disabled = nullptr;
        std::function<void(Widget*)> findDisabled = [&](Widget* w) {
            if (!w || disabled) return;
            if (w->VisualStates() != nullptr &&
                w->Get<std::wstring>(w->SlotOf("Content")) == L"Disabled wins") {
                disabled = w;
                return;
            }
            for (auto& c : w->Children()) findDisabled(c.get());
        };
        findDisabled(root);
        Expect(disabled != nullptr, "the page has a disabled styled button");
        if (disabled != nullptr) {
            Expect(disabled->VisualStates()->Current() == "Disabled", "it starts Disabled");
            const float disabledSize = disabled->Get<float>(disabled->SlotOf("FontSize"));
            // Even with the pointer over it, a disabled control must not advertise
            // interactivity.
            disabled->SetHovered(true);
            AnimationHost::AdvanceAll(0.3f);
            Expect(std::abs(disabled->Get<float>(disabled->SlotOf("FontSize")) - disabledSize) <
                       0.05f,
                   "hovering does not change a disabled control");
            disabled->SetHovered(false);
        }
    }

    std::printf("\n%s\n", g_failures == 0 ? "INTERACTION OK" : "INTERACTION FAILED");
    return g_failures == 0 ? 0 : 1;
}
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

// Checks for samples/binding/Binding.waml. One property, four modes: the point
// is that the columns disagree, so every assertion compares them.
int RunBindingChecks(Widget* root, const std::shared_ptr<sample::BindingVm>& vm) {
    root->ForceLayout(Size{840, 1200});

    Widget* oneTime = TextBoxWithPlaceholder(root, L"OneTime");
    Widget* oneWay = TextBoxWithPlaceholder(root, L"OneWay");
    Widget* twoWay = TextBoxWithPlaceholder(root, L"TwoWay");
    Widget* toSource = TextBoxWithPlaceholder(root, L"OneWayToSource");
    Widget* lostFocus = TextBoxWithPlaceholder(root, L"Type, then click outside");

    std::printf("\n[1] all four columns start from the same value\n");
    Expect(oneTime && oneWay && twoWay && toSource && lostFocus, "found all five TextBoxes");
    if (!oneTime || !oneWay || !twoWay || !toSource || !lostFocus) {
        std::printf("\n%s\n", "INTERACTION FAILED");
        return 1;
    }
    Expect(TextOf(oneTime) == L"Ada Lovelace", "OneTime seeded");
    Expect(TextOf(oneWay) == L"Ada Lovelace", "OneWay seeded");
    Expect(TextOf(twoWay) == L"Ada Lovelace", "TwoWay seeded");
    // OneWayToSource must NOT be seeded: only the target may write.
    Expect(TextOf(toSource).empty(), "OneWayToSource starts empty");

    std::printf("\n[2] a ViewModel change reaches every mode except OneTime and OneWayToSource\n");
    Widget* rename = ButtonWithText(root, L"Rename the source");
    Expect(rename != nullptr, "found the rename button");
    if (rename) {
        root->ForceLayout(Size{840, 1200});
        ClickAt(root, rename);
    }
    Expect(vm->Name.Get() == L"Grace Hopper", "the command changed the VM");
    Expect(vm->Count.Get() == 1, "and bumped the rename count");
    Expect(TextOf(oneTime) == L"Ada Lovelace", "OneTime ignored the change");
    Expect(TextOf(oneWay) == L"Grace Hopper", "OneWay followed the VM");
    Expect(TextOf(twoWay) == L"Grace Hopper", "TwoWay followed the VM");
    Expect(TextOf(toSource).empty(), "OneWayToSource still does not receive");

    std::printf("\n[3] StringFormat and FallbackValue\n");
    Widget* countLine = TextBlockStartingWith(root, L"Rename count:");
    Expect(countLine != nullptr, "found the formatted count");
    if (countLine) Expect(TextOf(countLine) == L"Rename count: 1", "StringFormat applied");
    Widget* fallbackLine = TextBlockStartingWith(root, L"(no such property");
    Expect(fallbackLine != nullptr, "the missing-path line shows its fallback");
    if (fallbackLine) {
        Expect(TextOf(fallbackLine).find(L"fallback is what you see") != std::wstring::npos,
               "the fallback text is the one from the markup");
    }

    std::printf("\n[4] typing writes back only in TwoWay and OneWayToSource\n");
    auto typeInto = [](Widget* box, wchar_t ch) {
        if (auto* tb = dynamic_cast<TextBox*>(box)) {
            tb->SetCaretToEnd();
            tb->OnChar(ch);
        }
    };
    typeInto(twoWay, L'?');
    Expect(vm->Name.Get() == L"Grace Hopper?", "TwoWay wrote back");
    Expect(TextOf(oneWay) == L"Grace Hopper?", "the OneWay column followed the new value");
    Expect(TextOf(oneTime) == L"Ada Lovelace", "OneTime still did not move");

    typeInto(oneWay, L'!');
    Expect(vm->Name.Get() == L"Grace Hopper?", "OneWay typing did NOT write back");
    Expect(TextOf(oneWay) == L"Grace Hopper?!", "the OneWay box kept its local edit");

    typeInto(toSource, L'X');
    Expect(vm->Name.Get() == L"X", "OneWayToSource wrote back");
    Expect(TextOf(twoWay) == L"X", "TwoWay received the new value");
    Expect(TextOf(oneTime) == L"Ada Lovelace", "OneTime is still frozen at load");

    std::printf("\n[5] Reset is a ViewModel change, so OneWayToSource ignores it\n");
    Widget* reset = ButtonWithText(root, L"Reset");
    Expect(reset != nullptr, "found the reset button");
    if (reset) {
        root->ForceLayout(Size{840, 1200});
        ClickAt(root, reset);
    }
    Expect(vm->Name.Get() == L"Ada Lovelace", "reset restored the VM");
    Expect(TextOf(oneWay) == L"Ada Lovelace", "OneWay followed the reset");
    Expect(TextOf(toSource) == L"X", "OneWayToSource kept its own value");

    std::printf("\n[6] LostFocus writes only when focus leaves\n");
    if (auto* tb = dynamic_cast<TextBox*>(lostFocus)) {
        lostFocus->SetFocused(true);
        tb->SetCaretToEnd();
        tb->OnChar(L'!');
        Expect(vm->Name.Get() == L"Ada Lovelace", "typing did not commit yet");
        lostFocus->SetFocused(false);
        Expect(vm->Name.Get() == L"Ada Lovelace!", "losing focus committed the value");
        Widget* echo = TextBlockStartingWith(root, L"Committed value:");
        Expect(echo != nullptr, "found the committed-value echo");
        if (echo) Expect(TextOf(echo) == L"Committed value: Ada Lovelace!", "the echo followed");
    }

    std::printf("\n%s\n", g_failures == 0 ? "INTERACTION OK" : "INTERACTION FAILED");
    return g_failures == 0 ? 0 : 1;
}

// Checks for samples/theming/Theming.waml. The palette itself is verified by
// the snapshot tool; here the contract under test is that the switch is an
// ordinary ViewModel bool and the page follows it.
int RunThemingChecks(Widget* root, const std::shared_ptr<sample::ThemingVm>& vm) {
    root->ForceLayout(Size{720, 1200});

    std::printf("\n[1] the page starts on the light theme\n");
    Expect(!vm->IsDarkTheme.Get(), "IsDarkTheme starts false");
    Expect(vm->ThemeLabel.Get().find(L"Light") != std::wstring::npos, "the label says Light");

    std::printf("\n[2] the button flips the ViewModel\n");
    Widget* toggle = ButtonWithText(root, L"Switch to dark");
    Expect(toggle != nullptr, "found the theme button");
    if (toggle) {
        root->ForceLayout(Size{720, 1200});
        ClickAt(root, toggle);
    }
    Expect(vm->IsDarkTheme.Get(), "the command set IsDarkTheme");
    Expect(vm->ThemeLabel.Get().find(L"Dark") != std::wstring::npos, "the label says Dark");
    Expect(vm->ToggleLabel.Get() == L"Switch to light", "the button label swapped");
    Expect(ButtonWithText(root, L"Switch to light") != nullptr, "the button follows the VM");

    std::printf("\n[3] and back again\n");
    if (Widget* back = ButtonWithText(root, L"Switch to light")) {
        root->ForceLayout(Size{720, 1200});
        ClickAt(root, back);
    }
    Expect(!vm->IsDarkTheme.Get(), "the second click returned to Light");
    Expect(vm->ThemeLabel.Get().find(L"Light") != std::wstring::npos, "the label followed");

    std::printf("\n[4] the page really uses tokens and literals\n");
    // A token is stored as a token, not resolved at load time. If the parser ever
    // started baking palettes into values, switching themes could not work.
    auto findLabel = [&](const std::wstring& text) -> Widget* {
        std::function<Widget*(Widget*)> walk = [&](Widget* w) -> Widget* {
            if (!w) return nullptr;
            if (TextOf(w) == text) return w;
            for (auto& c : w->Children()) {
                if (Widget* found = walk(c.get())) return found;
            }
            return nullptr;
        };
        return walk(root);
    };
    Expect(findLabel(L"AppBackground") != nullptr, "the palette lists AppBackground");
    Expect(findLabel(L"Selection") != nullptr, "the palette lists Selection");

    Widget* literal = nullptr;
    std::function<void(Widget*)> findLiteral = [&](Widget* w) {
        if (!w || literal) return;
        if (std::string(w->TypeName()) == "wui/Border") {
            const int slot = w->SlotOf("Background");
            if (slot >= 0) {
                const Color c = w->Get<Color>(slot);
                if (c.token == Token::None && c.r > 0.8f && c.g < 0.3f) literal = w;
            }
        }
        for (auto& c : w->Children()) findLiteral(c.get());
    };
    findLiteral(root);
    Expect(literal != nullptr, "the literal red swatch is present");

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

    if (IsBindingPage(loaded.origin, argPath)) {
        auto vm = std::make_shared<sample::BindingVm>();
        WidgetPtr root = doc->Instantiate(&text);
        root->SetDataContext(std::static_pointer_cast<RefObject>(vm));
        root->ForceLayout(Size{840, 1200});
        return RunBindingChecks(root.get(), vm);
    }

    if (IsThemingPage(loaded.origin, argPath)) {
        auto vm = std::make_shared<sample::ThemingVm>();
        WidgetPtr root = doc->Instantiate(&text);
        root->SetDataContext(std::static_pointer_cast<RefObject>(vm));
        root->ForceLayout(Size{720, 1200});
        return RunThemingChecks(root.get(), vm);
    }

    auto listVm = std::make_shared<sample::ListViewVm>();
    WidgetPtr listRoot = doc->Instantiate(&text);
    listRoot->SetDataContext(std::static_pointer_cast<RefObject>(listVm));
    listRoot->ForceLayout(Size{700, 2000});
    return RunListChecks(listRoot.get(), listVm);
}