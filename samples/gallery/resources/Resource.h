// SPDX-License-Identifier: MIT
// Sample resource identifiers: ids for THIS sample's markup, icon and version
// block. Framework code never includes this file -- it only knows the generic
// "load an RCDATA payload by type name and id" API in Resources.h.
//
// Keep this file free of C++ constructs: it is included by the resource
// compiler (rc.exe) as well as by C++.
#pragma once

// ---------------------------------------------------------------------------
// Custom resource types.
//
// WAML documents are compiled into the executable as RCDATA under a private
// type name, so they are never mistaken for anything the shell or comctl32
// might interpret. The TypeName form keeps the package format self-describing:
//
//   rc:     IDR_WAML_GALLERY  WAML_DOC  "Gallery.waml"
//   code:   FindResourceW(hmod, MAKEINTRESOURCEW(id), WAML_RESOURCE_TYPE)
// ---------------------------------------------------------------------------
#define WAML_RESOURCE_TYPE  L"WAML_DOC"

// Numeric ids for the packaged documents.
#define IDR_WAML_GALLERY    101

// Icons.
#define IDI_APP             201

// ---------------------------------------------------------------------------
// Version resource. FILEVERSION / PRODUCTVERSION are 4 x 16-bit fields:
//   major, minor, patch, build
// The *STR form feeds the string table and must use dots.
//
// Populate these from the build (see build.bat or cmake/version.cmake) rather
// than editing by hand; the resource compiler cannot do arithmetic on comma
// lists.
// ---------------------------------------------------------------------------
#ifndef WUI_VERSION_MAJOR
#define WUI_VERSION_MAJOR 0
#endif
#ifndef WUI_VERSION_MINOR
#define WUI_VERSION_MINOR 5
#endif
#ifndef WUI_VERSION_PATCH
#define WUI_VERSION_PATCH 0
#endif
#ifndef WUI_VERSION_BUILD
#define WUI_VERSION_BUILD 0
#endif

#define WUI_VERSION_COMMA WUI_VERSION_MAJOR, WUI_VERSION_MINOR, WUI_VERSION_PATCH, WUI_VERSION_BUILD

// The *STR macros need a second level of expansion so the numbers become text.
#define WUI_STR2(x) #x
#define WUI_STR(x) WUI_STR2(x)
#define WUI_VERSION_STR \
    WUI_STR(WUI_VERSION_MAJOR) "." WUI_STR(WUI_VERSION_MINOR) "." WUI_STR(WUI_VERSION_PATCH) "." WUI_STR(WUI_VERSION_BUILD)
