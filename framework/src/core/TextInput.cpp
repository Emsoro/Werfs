// SPDX-License-Identifier: MIT
#include "TextInput.h"

namespace wui {

namespace {
// A single process-wide pointer, like TextSystem: the host owns the implementation and the
// framework only borrows it. No ownership here on purpose -- a test installs one on the
// stack and the host installs one for the life of the window.
ITextInputService* g_service = nullptr;
} // namespace

void TextInputSystem::Set(ITextInputService* service) { g_service = service; }

ITextInputService* TextInputSystem::Get() { return g_service; }

} // namespace wui
