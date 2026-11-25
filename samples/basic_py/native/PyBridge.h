// SPDX-License-Identifier: MIT
// Python <-> MVVM bridge for samples/basic_py.
//
// The framework knows only IPropertySource / ICommand. This file implements
// both in terms of an ordinary Python object, so a ViewModel can be a plain
// class in main.py. Everything that touches the CPython C-API is compiled with
// Py_LIMITED_API (3.8+), and the module is therefore named _wui.abi3.pyd.
#pragma once

#ifndef Py_LIMITED_API
#define Py_LIMITED_API 0x03080000
#endif
#include <Python.h>

#include "Mvvm.h"

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace wuipy {

enum class PyKind { Text, Int, Float, Bool, Command };

// Converts Python strings to std::wstring (the framework's text representation).
// Caller must hold the GIL.
std::wstring ToWide(PyObject* str);
std::string ToUtf8(PyObject* str);
PyObject* FromWide(const std::wstring& text);
PyObject* FromUtf8(const std::string& text);

// Value <-> Python conversion for one declared attribute kind.
bool ValueFromPy(PyObject* obj, PyKind kind, wui::Value& out);
PyObject* PyFromValue(const wui::Value& value, PyKind kind);
wui::VType VTypeOf(PyKind kind);

// A Python callable exposed to WAML as a Command binding.
class PyCommand final : public wui::ICommand {
public:
    // Both objects are retained with their own references.
    PyCommand(PyObject* callable, PyObject* canExecute);
    ~PyCommand() override;

    bool CanExecute() const override;
    void Execute() override;
    void Subscribe(wui::ICommandObserver* o) override;
    void Unsubscribe(wui::ICommandObserver* o) override;
    void RaiseCanExecuteChanged() override;

private:
    PyObject* callable_ = nullptr;
    PyObject* canExecute_ = nullptr;
    std::vector<wui::ICommandObserver*> observers_;
};

// One Python attribute seen as an IPropertySource by the binding engine.
class PyPropertySource final : public wui::IPropertySource {
public:
    PyPropertySource(class PyVm* owner, PyObject* vm, std::string name, PyKind kind,
                     wui::RefPtr command, bool* updating);

    wui::VType Type() const override { return VTypeOf(kind_); }
    bool Read(wui::Value& out) const override;
    bool WriteFromView(const wui::Value& v) override;
    bool CanWrite() const override { return kind_ != PyKind::Command; }
    void Subscribe(wui::IPropertyObserver* o) override;
    void Unsubscribe(wui::IPropertyObserver* o) override;
    void NotifyObservers(const wui::Value& value);

    const std::string& Name() const { return name_; }
    PyKind Kind() const { return kind_; }

private:
    PyVm* owner_ = nullptr;
    PyObject* vm_ = nullptr; // borrowed: PyVm owns the strong reference
    std::string name_;
    PyKind kind_ = PyKind::Text;
    wui::RefPtr command_;
    bool* updating_ = nullptr;
    std::vector<wui::IPropertyObserver*> observers_;
};

// RefObject that owns the Python ViewModel and the generated property table.
class PyVm final : public wui::ObservableObject {
public:
    explicit PyVm(PyObject* vm);
    ~PyVm() override;

    // Reads the `_wui_props` mapping on the Python object and registers every
    // declared attribute. Returns false (with a Python error pending) on a bad
    // schema. Must be called while the GIL is held.
    bool Register();

    // Called from Python's ObservableObject.__setattr__ and from WriteFromView.
    void NotifyFromPython(const std::string& name);

private:
    friend class PyPropertySource;
    struct Entry {
        std::unique_ptr<PyPropertySource> source;
        PyKind kind = PyKind::Text;
    };

    PyObject* vm_ = nullptr; // strong reference
    std::vector<Entry> entries_;
    std::unordered_map<std::string, size_t> index_;
    std::string notifying_;
    int notifyDepth_ = 0;
    bool updating_ = false;
};

// Module-level entry points used by PyBridge.cpp. Both take/return owned refs.
int RunVm(PyObject* vm, const std::wstring& markup, const std::wstring& title,
          float width, float height);
void NotifyVm(PyObject* vm, const std::string& name);
size_t BindingErrors();

} // namespace wuipy
