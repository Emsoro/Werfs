// SPDX-License-Identifier: MIT
#include "Image.h"

namespace wui {
namespace {

// The registered service is a plain pointer, mirroring the file dialog: whoever installs
// it owns it and is responsible for outliving its use. A test installs a stack object and
// unregisters it before scope exit.
IImageService* g_service = nullptr;

} // namespace

void SetImageService(IImageService* service) { g_service = service; }

IImageService* ImageService() { return g_service; }

} // namespace wui
