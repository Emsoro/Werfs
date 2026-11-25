// SPDX-License-Identifier: MIT
// waml_basic_py.exe: boot the SYSTEM Python, then let main.py drive the UI.
//
// Deliberately small. It links python3.lib (the stable-ABI import library) and
// uses only entry points that library exports -- PyConfig is NOT among them on
// Windows, so program name is set with Py_SetProgramName instead. No Python
// DLL, stdlib or .py file is bundled into the binary: main.py, wui.py, the
// markup and the extension sit next to the exe as loose payload files.
#ifndef Py_LIMITED_API
#define Py_LIMITED_API 0x03080000
#endif
#include <Python.h>

#include <windows.h>

#include <string>

#include "PythonPaths.h"

namespace {

std::wstring ExeDirectory() {
    wchar_t path[MAX_PATH]{};
    const DWORD length = GetModuleFileNameW(nullptr, path, MAX_PATH);
    std::wstring result(path, length);
    const size_t slash = result.find_last_of(L"\\/");
    if (slash != std::wstring::npos) result.resize(slash + 1);
    return result;
}

// A GUI subsystem has no console, so a failed import would otherwise be
// invisible. Point Python's stderr at a log next to the exe using only stable
// ABI calls (PyRun_SimpleString is not exported by python3.lib).
void RedirectPythonStderr(const std::wstring& exeDirectory) {
    PyObject* builtins = PyImport_ImportModule("builtins");
    if (builtins == nullptr) { PyErr_Clear(); return; }
    PyObject* open = PyObject_GetAttrString(builtins, "open");
    if (open == nullptr) {
        PyErr_Clear();
        Py_DECREF(builtins);
        return;
    }
    const std::wstring logPath = exeDirectory + L"python_error.log";
    PyObject* path = PyUnicode_FromWideChar(logPath.c_str(),
                                            static_cast<Py_ssize_t>(logPath.size()));
    PyObject* mode = PyUnicode_FromString("w");
    PyObject* arguments = PyTuple_New(2);
    if (path && mode && arguments) {
        PyTuple_SetItem(arguments, 0, path);
        path = nullptr; // the tuple owns it now
        PyTuple_SetItem(arguments, 1, mode);
        mode = nullptr;
        PyObject* file = PyObject_CallObject(open, arguments);
        if (file != nullptr) {
            PySys_SetObject("stderr", file); // increments the reference
            Py_DECREF(file);
        }
        PyErr_Clear();
    }
    Py_XDECREF(path);
    Py_XDECREF(mode);
    Py_XDECREF(arguments);
    Py_DECREF(open);
    Py_DECREF(builtins);
}

} // namespace

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) {
    wchar_t* program = Py_DecodeLocale(WUI_PYTHON_EXE_UTF8, nullptr);
    if (program == nullptr) return 1;
#pragma warning(push)
#pragma warning(disable : 4996) // PyConfig is not exported by python3.lib
    Py_SetProgramName(program); // Python uses it to locate the stdlib
    Py_InitializeEx(0);
#pragma warning(pop)
    // Py_DecodeLocale allocates with PyMem_RawMalloc, which is not in the
    // stable ABI, so this one small buffer stays alive for the process. It is
    // needed by Py_SetProgramName until initialization anyway.
    (void)program;

    // Embedding does not put the exe's own directory on sys.path. Do it here so
    // `import _wui` and `import main` resolve next to the exe, not next to
    // python.exe in the interpreter's install directory.
    const std::wstring exeDirectory = ExeDirectory();
    RedirectPythonStderr(exeDirectory);

    PyObject* searchPath = PySys_GetObject("path"); // borrowed
    PyObject* directory = PyUnicode_FromWideChar(
        exeDirectory.c_str(), static_cast<Py_ssize_t>(exeDirectory.size()));
    if (searchPath != nullptr && directory != nullptr) PyList_Insert(searchPath, 0, directory);
    Py_XDECREF(directory);

    int exitCode = 1;
    PyObject* module = PyImport_ImportModule("main");
    if (module == nullptr) {
        PyErr_Print();
    } else {
        PyObject* entry = PyObject_GetAttrString(module, "main");
        if (entry == nullptr) {
            PyErr_Print();
        } else {
            PyObject* result = PyObject_CallObject(entry, nullptr);
            if (result != nullptr) {
                exitCode = static_cast<int>(PyLong_AsLong(result));
                Py_DECREF(result);
            } else {
                PyErr_Print();
            }
            Py_DECREF(entry);
        }
        Py_DECREF(module);
    }

    Py_FinalizeEx();
    return exitCode;
}
