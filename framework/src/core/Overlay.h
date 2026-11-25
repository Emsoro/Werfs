// SPDX-License-Identifier: MIT
// Overlay implementations: the flyouts that cannot live in the widget tree.
//
// Why a separate header: these are not widgets. A menu is a transient popup with
// its own coordinate space, its own painting and its own dismissal rules, and it
// must not participate in the parent's layout -- putting it in the tree would
// make Measure see it (so a closed menu would still take space) and a
// ScrollViewer would clip it.
//
// The model is a plain data tree (PopupMenuModel) that a ViewModel can build,
// plus a layer that draws and hit-tests it. Keeping them apart is what lets the
// sample construct a menu from its own command objects without the framework
// knowing anything about documents.
#pragma once

#include "Core.h"

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace wui {

class ICommand;

// ---------------------------------------------------------------------------
// Menu model
// ---------------------------------------------------------------------------
struct PopupMenuItem {
    enum class Kind {
        Command,   // invokes `command` when clicked
        Submenu,   // opens `children` to the side
        Separator, // a horizontal rule; not selectable
        Header,    // a non-interactive caption
        Toggle,    // a checkable item; writes `checked` back through `onToggle`
    };

    Kind kind = Kind::Command;
    std::wstring text;
    std::wstring hint;    // accelerator text, e.g. "Ctrl+S"; display only
    std::string icon;     // short glyph, drawn as text (no icon font yet)
    RefPtr command;       // ICommand for Kind::Command
    std::vector<PopupMenuItem> children; // Kind::Submenu
    bool checked = false;  // Kind::Toggle
    bool enabled = true;

    // Implemented out of line in Overlay.cpp so this header does not need
    // Mvvm.h (the command interface) just to declare the struct.
    bool CanInvoke() const;
    void Invoke() const;
};

// Builds a layer that shows `items` with its top-left at `anchor` (window space).
// The layer is returned so the caller stores it; the caller owns it and must not
// destroy it while it is showing. `onClosed` is invoked when the layer closes
// itself (item chosen, Escape, click outside) so the owner can clear its pointer
// and repaint.
std::unique_ptr<OverlayLayer> MakeMenuLayer(std::vector<PopupMenuItem> items, Point anchor,
                                            std::function<void()> onClosed);

// Sizes a layer against the window it will be shown in. A menu otherwise cannot
// know whether it fits and would open past the right edge. Harmless for a layer
// whose concrete type does not need it.
void PrepareMenuLayerSizing(OverlayLayer* layer, Size available);
void PrepareDialogLayerSizing(OverlayLayer* layer, Size available);
// A menu shown by a bar follows the pointer across the other titles; a menu
// opened from a plain button does not, because that would be surprising.
void SetMenuHoverOnOpen(OverlayLayer* layer, bool on);

// Reflects a command's CanExecute into the item's enabled flag. Called by the
// menu bar right before showing, so a disabled "Save" cannot be clicked.
void RefreshMenuEnabled(std::vector<PopupMenuItem>& items);

// ---------------------------------------------------------------------------
// Modal dialog layer
//
// A ContentDialog is a menu layer's opposite in one respect that matters: it must
// NOT light-dismiss. A click outside is swallowed, because "did the user mean to
// cancel, or did they miss the button?" has no safe answer, and for a data-loss
// prompt the safe default is to keep asking.
// ---------------------------------------------------------------------------
struct DialogButton {
    std::wstring text;
    int result = 0;      // reported to the callback
    bool isDefault = false; // drawn as the accent button
    bool isCancel = false;  // drawn as the quiet button
};

// Shows a modal card: title, message, and up to three buttons. `onResult` is
// called with the chosen button's result, then the layer closes. Escape reports
// the cancel button's result (never the default one) so a keyboard user cannot
// accidentally confirm a destructive action.
std::unique_ptr<OverlayLayer> MakeDialogLayer(std::wstring title, std::wstring message,
                                              std::vector<DialogButton> buttons,
                                              std::function<void(int)> onResult,
                                              std::function<void()> onClosed);

// ---------------------------------------------------------------------------
// Flyout: a popup that hosts a real widget subtree
//
// The difference from a menu is what it contains. A menu draws a data model of rows;
// a flyout contains arbitrary CONTROLS -- a small form, a colour picker, a settings
// card -- so it owns a Widget tree, measures it, and forwards pointer and key events
// into it. That is also why it cannot be a widget itself: as a child it would be
// measured by its parent's layout (so a closed flyout would still take space) and
// clipped by any ScrollViewer above it.
// ---------------------------------------------------------------------------
struct FlyoutRequest {
    // The content to host. BORROWED: the popup control owns it for its whole life,
    // so the same tree can be shown again after the layer closes. Must outlive the layer.
    Widget* content = nullptr;
    // Window-space top-left the caller wants. The layer flips or clamps it to fit, so a
    // flyout opened near the right edge does not run off it.
    Point anchor;
    // Optional fixed size; zero means "as large as the content wants".
    Size size{0, 0};
    bool lightDismiss = true;
    // True when the anchor is a control that should keep its "open" appearance.
    std::function<void()> onClosed;
    // Placement relative to the anchor rect: 0 below, 1 above, 2 right, 3 left.
    int placement = 0;
};

std::unique_ptr<OverlayLayer> MakeFlyoutLayer(FlyoutRequest request);

// A transient label. Deliberately non-interactive and NOT light-dismissing: it must not
// swallow the click that dismisses whatever opened it, and a tooltip that closed on the
// pointer press would flicker away exactly when the user acts.
std::unique_ptr<OverlayLayer> MakeToolTipLayer(std::wstring text, Point anchor, Size available);

// Sizes a flyout against the window it will appear in.
void PrepareFlyoutLayerSizing(OverlayLayer* layer, Size available);

} // namespace wui
