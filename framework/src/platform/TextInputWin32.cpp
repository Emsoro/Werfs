// SPDX-License-Identifier: MIT
//
// Win32 IME plumbing.
//
// This is the platform half of text composition: it turns WM_IME_* messages into calls on
// the focused TextBox, and it moves the candidate window to follow the caret.
//
// One decision matters more than the rest and is worth stating: we call
// ImmAssociateContext / ImmSetCompositionWindow by hand rather than hosting a native EDIT
// control. The "native EDIT island" the plan mentioned as a fallback would have worked, but
// it would have meant the editor's text field was a different control from every other text
// field in the framework -- a second code path for focus, painting, scrolling and theming,
// for one feature. Talking to the IME directly keeps ONE text field.
//
// What this file deliberately does NOT do: draw the composition itself. The IME client
// (Microsoft Pinyin, Microsoft IME) draws its own candidate list in its own window. We only
// position it. Drawing candidates ourselves would mean reimplementing every IME's UI.
#include "TextInput.h"

#include <windows.h>
#include <imm.h>

#include <string>

#pragma comment(lib, "imm32.lib")

namespace wui {

namespace {

// Converts a DIP caret rect into the IMESetCompositionWindow form. IME positions are in
// SCREEN pixels, not client DIPs, so both the window origin and the DPI scale have to be
// applied -- the classic bug here is a candidate window that sits under the field at 100%
// scaling and floats halfway across it at 200%.
void ApplyCaretToIme(HWND hwnd, const CompositionCaret& caret) {
    if (hwnd == nullptr) return;
    HIMC context = ImmGetContext(hwnd);
    if (context == nullptr) return;

    const UINT dpi = GetDpiForWindow(hwnd);
    const float scale = dpi > 0 ? static_cast<float>(dpi) / 96.0f : 1.0f;

    POINT origin{};
    ClientToScreen(hwnd, &origin);

    COMPOSITIONFORM form{};
    // CFS_POINT is enough for most IMEs and is what keeps the candidate list anchored to the
    // caret rather than to the field's corner. CFS_FORCE_POSITION is added because some IMEs
    // ignore CFS_POINT alone when the composition has not started yet.
    form.dwStyle = CFS_POINT | CFS_FORCE_POSITION;
    form.ptCurrentPos.x = origin.x + static_cast<LONG>(caret.x * scale);
    form.ptCurrentPos.y = origin.y + static_cast<LONG>(caret.y * scale);
    ImmSetCompositionWindow(context, &form);

    // The candidate list needs its own rect; without it some IMEs put the list at the top
    // left of the screen.
    CANDIDATEFORM candidate{};
    candidate.dwIndex = 0;
    candidate.dwStyle = CFS_CANDIDATEPOS;
    candidate.ptCurrentPos = form.ptCurrentPos;
    ImmSetCandidateWindow(context, &candidate);

    ImmReleaseContext(hwnd, context);
}

class Win32TextInputService final : public ITextInputService {
public:
    void MoveCandidateWindow(const CompositionCaret& caret) override {
        if (hwnd_ == nullptr) return;
        // The default IME window paints the composition string at our caret if we tell it
        // where the caret is; without this call the IME draws at the field's origin.
        ApplyCaretToIme(hwnd_, caret);
    }

    void SetCompositionEnabled(bool enabled) override {
        if (hwnd_ == nullptr) return;
        HIMC context = ImmGetContext(hwnd_);
        if (context == nullptr) return;
        ImmSetOpenStatus(context, enabled ? TRUE : FALSE);
        ImmReleaseContext(hwnd_, context);
    }

    void Attach(HWND hwnd) { hwnd_ = hwnd; }

private:
    HWND hwnd_ = nullptr;
};

Win32TextInputService& Service() {
    static Win32TextInputService service;
    return service;
}

} // namespace

// Installs the service for the life of the window. Called by the host once it has an HWND.
ITextInputService* CreateWin32TextInputService(void* hwnd) {
    Service().Attach(static_cast<HWND>(hwnd));
    return &Service();
}

} // namespace wui
