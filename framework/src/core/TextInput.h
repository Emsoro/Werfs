// SPDX-License-Identifier: MIT
//
// Text composition: the state between "the user pressed a key" and "a character exists".
//
// This exists because the framework draws its own text fields, so there is no native EDIT
// control to hand a Chinese or Japanese IME to. With a native edit box the OS owns the
// whole interaction; here the IME talks to US and we have to model the middle state
// ourselves.
//
// The three states a composition moves through:
//
//   composing   the IME is building a string ("zhong"), nothing is committed yet.
//               The field shows this text as a PREVIEW -- it is not in the value.
//   committed   the user picked a candidate ("中"). This replaces the composition in the
//               field and IS a real edit, so it writes the Text slot.
//   cancelled   the user pressed Escape or clicked away. The preview vanishes and the
//               value is untouched.
//
// Why the preview is not just "insert as you go": the composition changes on every
// keystroke (zhong -> zhongg -> zhongguo), so writing it into the value would fire the
// binding and the ViewModel on each character and require us to retract them on cancel.
// Keeping it separate means the value only ever sees committed characters.
//
// The service interface exists so this logic is testable with no IME installed: a headless
// harness drives composition text directly and asserts the slot never sees the preview.
#pragma once

#include "Core.h"

#include <string>

namespace wui {

// Where the candidate window should appear, in WINDOW coordinates (DIPs, same space as
// Widget bounds). The host converts to screen pixels; keeping the framework in DIPs is what
// lets the position be asserted without a window.
struct CompositionCaret {
    float x = 0.0f;
    float y = 0.0f;
    float lineHeight = 16.0f;
};

// Implemented by the platform layer (Win32 IME) and by the headless harness.
class ITextInputService {
public:
    virtual ~ITextInputService() = default;

    // Moves the candidate window to follow the caret. Called on every composition update:
    // a candidate list that stays where the composition started ends up floating over the
    // wrong word as the user keeps typing.
    virtual void MoveCandidateWindow(const CompositionCaret& caret) = 0;

    // Optional: tells the platform whether a composition is needed at all. A field inside a
    // password box or a numeric box should not offer a candidate window.
    virtual void SetCompositionEnabled(bool enabled) { (void)enabled; }
};

class TextInputSystem {
public:
    static void Set(ITextInputService* service);
    static ITextInputService* Get();
};

// The framework does not depend on Win32, so the platform service is created through this
// factory: declared here with a void* handle to keep windows.h out of the framework headers,
// defined in ImageWin32-style platform code.
ITextInputService* CreateWin32TextInputService(void* hwnd);

} // namespace wui
