// SPDX-License-Identifier: MIT
//
// Image decoding, behind an interface.
//
// Same shape as the file dialog service, for the same reason: decoding needs WIC, WIC
// needs COM, and the headless harness (tests, snapshot, interact) must never touch
// either. A test installs a fake that returns a fixed-size bitmap and asserts on layout
// and paint calls; the app installs the WIC one.
//
// Decoded images are handed out as a shared, immutable bitmap so a picture used in ten
// places is decoded once. That is the whole caching story -- no eviction, because a
// UI's images are typically few and long-lived, and an LRU here would be a solution to a
// problem nobody has reported.
#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "Core.h"

namespace wui {

class IImageService {
public:
    virtual ~IImageService() = default;
    // Decodes `source`, which is either a file path or a resource reference. Returns
    // nullptr when it cannot be decoded -- never throws and never aborts: a missing
    // image is a blank rectangle, not a crash.
    virtual std::shared_ptr<const Bitmap> Load(const std::string& source) = 0;
};

// Registers the process-wide service. Passing nullptr unregisters it, which is what the
// headless harness does: every Load then returns nullptr and Image paints its
// placeholder instead of crashing.
void SetImageService(IImageService* service);
IImageService* ImageService();

// Creates the WIC-backed implementation, or nullptr when COM/WIC is unavailable. The
// caller owns the result. A factory so the platform file can be left out of headless
// builds without touching this header.
IImageService* CreateWicImageService();

} // namespace wui
