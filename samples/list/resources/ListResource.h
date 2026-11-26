// SPDX-License-Identifier: MIT
// Sample resource identifiers: ids for THIS sample's markup, icon and version
// block. Framework code never includes this file.
//
// Each sample has its own Resource.h with distinct ids so two samples can be
// linked into the same image without colliding, and so a sample can be moved
// out of the tree without touching the framework.
#pragma once

// ---------------------------------------------------------------------------
// Custom resource types.
// ---------------------------------------------------------------------------
#define WAML_RESOURCE_TYPE  L"WAML_DOC"

// Numeric ids for the packaged documents.
#define IDR_WAML_LIST       301

// Icons.
#define IDI_APP             201

// ---------------------------------------------------------------------------
// Version resource. 4 x 16-bit fields: major, minor, patch, build.
// ---------------------------------------------------------------------------
#ifndef WUI_VERSION_MAJOR
#define WUI_VERSION_MAJOR 0
#endif
#ifndef WUI_VERSION_MINOR
#define WUI_VERSION_MINOR 13
#endif
#ifndef WUI_VERSION_PATCH
#define WUI_VERSION_PATCH 2
#endif
#ifndef WUI_VERSION_BUILD
#define WUI_VERSION_BUILD 0
#endif

#define WUI_VERSION_COMMA WUI_VERSION_MAJOR, WUI_VERSION_MINOR, WUI_VERSION_PATCH, WUI_VERSION_BUILD

#define WUI_STR2(x) #x
#define WUI_STR(x) WUI_STR2(x)
#define WUI_VERSION_STR \
    WUI_STR(WUI_VERSION_MAJOR) "." WUI_STR(WUI_VERSION_MINOR) "." WUI_STR(WUI_VERSION_PATCH) "." WUI_STR(WUI_VERSION_BUILD)
