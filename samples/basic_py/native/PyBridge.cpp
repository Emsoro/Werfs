// SPDX-License-Identifier: MIT
// The native half of the Python MVVM bridge. See PyBridge.h for the contract.
//
// Every Python attribute that the ViewModel declares in `_wui_props` becomes an
// IPropertySource in the framework's property table. Writes flow both ways:
//   Python -> C++ : ObservableObject.__setattr__ -> _wui._notify -> NotifyFromPython
//   C++ -> Python : Binding -> WriteFromView -> PyObject_SetAttrString
// The `updating_` flag is the echo guard, the same idea Binding uses internally.
#include "PyBridge.h"

#include "SampleHost.h"

#include <windows.h>

#include <algorithm>
#include <cstdio>
#include <utility>

using namespace wui;

namespace wuipy {
namespace {

// One ViewModel per process is all the sample host supports; the bridge is a
// module-level singleton so Python's __setattr__ hook can find it.
std::shared_ptr<PyVm> g_bridge;
PyObject* g_vm = nullptr; // strong reference while a run is in progress

} // namespace

std::wstring ToWide(PyObject* str) {
    if (str == nullptr) return {};
    Py_ssize_t length = 0;
    wchar_t* buffer = PyUnicode_AsWideCharString(str, &length);
    if (buffer == nullptr) {
        PyErr_Clear();
        return {};
    }
    std::wstring result(buffer, static_cast<size_t>(length));
    PyMem_Free(buffer);
    return result;
}

std::string ToUtf8(PyObject* str) { return WideToUtf8(ToWide(str)); }

PyObject* FromWide(const std::wstring& text) {
    return PyUnicode_FromWideChar(text.c_str(), static_cast<Py_ssize_t>(text.size()));
}

PyObject* FromUtf8(const std::string& text) { return FromWide(Utf8ToWide(text)); }

bool ValueFromPy(PyObject* obj, PyKind kind, wui::Value& out) {
    if (obj == nullptr) return false;
    switch (kind) {
    case PyKind::Text:
        out = Value::Text(ToWide(obj));
        return true;
    case PyKind::Int: {
        const long long number = PyLong_AsLongLong(obj);
        if (PyErr_Occurred()) {
            PyErr_Clear();
            return false;
        }
        out = Value::Number(static_cast<double>(number));
        return true;
    }
    case PyKind::Float: {
        const double number = PyFloat_AsDouble(obj);
        if (PyErr_Occurred()) {
            PyErr_Clear();
            return false;
        }
        out = Value::Number(number);
        return true;
    }
    case PyKind::Bool: {
        const int truth = PyObject_IsTrue(obj);
        if (truth < 0) {
            PyErr_Clear();
            return false;
        }
        out = Value::Bool(truth != 0);
        return true;
    }
    case PyKind::Command:
        return false; // commands are read from the stored RefPtr, not from Python
    }
    return false;
}

PyObject* PyFromValue(const wui::Value& value, PyKind kind) {
    wui::Value converted;
    switch (kind) {
    case PyKind::Text:
        if (!ConvertValue(value, wui::VType::Text, converted)) return nullptr;
        return FromWide(converted.text);
    case PyKind::Int:
        if (!ConvertValue(value, wui::VType::Number, converted)) return nullptr;
        // Numbers in the framework are doubles; restore the declared Python type.
        if (converted.num > 9.0e18 || converted.num < -9.0e18) return nullptr;
        return PyLong_FromLongLong(static_cast<long long>(converted.num));
    case PyKind::Float:
        if (!ConvertValue(value, wui::VType::Number, converted)) return nullptr;
        return PyFloat_FromDouble(converted.num);
    case PyKind::Bool:
        if (!ConvertValue(value, wui::VType::Bool, converted)) return nullptr;
        return PyBool_FromLong(converted.flag ? 1 : 0);
    case PyKind::Command:
        return nullptr;
    }
    return nullptr;
}

wui::VType VTypeOf(PyKind kind) {
    switch (kind) {
    case PyKind::Text:    return wui::VType::Text;
    case PyKind::Int:
    case PyKind::Float:   return wui::VType::Number;
    case PyKind::Bool:    return wui::VType::Bool;
    case PyKind::Command: return wui::VType::Ref;
    }
    return wui::VType::Null;
}

// ---------------------------------------------------------------------------
// PyCommand
// ---------------------------------------------------------------------------

PyCommand::PyCommand(PyObject* callable, PyObject* canExecute)
    : callable_(callable), canExecute_(canExecute) {
    Py_XINCREF(callable_);
    Py_XINCREF(canExecute_);
}

PyCommand::~PyCommand() {
    PyGILState_STATE state = PyGILState_Ensure();
    Py_XDECREF(callable_);
    Py_XDECREF(canExecute_);
    PyGILState_Release(state);
}

bool PyCommand::CanExecute() const {
    if (canExecute_ == nullptr) return true;
    PyGILState_STATE state = PyGILState_Ensure();
    PyObject* result = PyObject_CallObject(canExecute_, nullptr);
    bool allowed = true;
    if (result != nullptr) {
        allowed = PyObject_IsTrue(result) != 0;
        Py_DECREF(result);
    } else {
        PyErr_Print();
    }
    PyGILState_Release(state);
    return allowed;
}

void PyCommand::Execute() {
    if (callable_ == nullptr || !CanExecute()) return;
    PyGILState_STATE state = PyGILState_Ensure();
    PyObject* result = PyObject_CallObject(callable_, nullptr);
    if (result != nullptr) Py_DECREF(result);
    else PyErr_Print();
    PyGILState_Release(state);
    RaiseCanExecuteChanged();
}

void PyCommand::Subscribe(ICommandObserver* o) {
    if (o == nullptr) return;
    if (std::find(observers_.begin(), observers_.end(), o) == observers_.end())
        observers_.push_back(o);
}

void PyCommand::Unsubscribe(ICommandObserver* o) {
    observers_.erase(std::remove(observers_.begin(), observers_.end(), o), observers_.end());
}

void PyCommand::RaiseCanExecuteChanged() {
    const auto copy = observers_;
    for (ICommandObserver* o : copy) {
        if (std::find(observers_.begin(), observers_.end(), o) != observers_.end())
            o->OnCanExecuteChanged();
    }
}

// ---------------------------------------------------------------------------
// PyPropertySource
// ---------------------------------------------------------------------------

PyPropertySource::PyPropertySource(PyVm* owner, PyObject* vm, std::string name, PyKind kind,
                                   RefPtr command, bool* updating)
    : owner_(owner), vm_(vm), name_(std::move(name)), kind_(kind),
      command_(std::move(command)), updating_(updating) {}

bool PyPropertySource::Read(Value& out) const {
    if (kind_ == PyKind::Command) {
        out = Value::Ref(command_);
        return true;
    }
    PyObject* attribute = PyObject_GetAttrString(vm_, name_.c_str());
    if (attribute == nullptr) {
        PyErr_Clear();
        return false;
    }
    const bool ok = ValueFromPy(attribute, kind_, out);
    Py_DECREF(attribute);
    return ok;
}

bool PyPropertySource::WriteFromView(const Value& v) {
    if (kind_ == PyKind::Command) return false;
    PyObject* pythonValue = PyFromValue(v, kind_);
    if (pythonValue == nullptr) {
        PyErr_Clear();
        return false;
    }
    // PyObject_SetAttrString runs ObservableObject.__setattr__, which calls
    // back into us. Suppress that echo, then do the notification ourselves.
    *updating_ = true;
    const int result = PyObject_SetAttrString(vm_, name_.c_str(), pythonValue);
    *updating_ = false;
    Py_DECREF(pythonValue);
    if (result != 0) {
        PyErr_Clear();
        return false;
    }
    owner_->NotifyFromPython(name_);
    return true;
}

void PyPropertySource::Subscribe(IPropertyObserver* o) {
    if (o == nullptr) return;
    if (std::find(observers_.begin(), observers_.end(), o) == observers_.end())
        observers_.push_back(o);
}

void PyPropertySource::Unsubscribe(IPropertyObserver* o) {
    observers_.erase(std::remove(observers_.begin(), observers_.end(), o), observers_.end());
}

void PyPropertySource::NotifyObservers(const Value& value) {
    const auto copy = observers_;
    for (IPropertyObserver* o : copy) {
        if (std::find(observers_.begin(), observers_.end(), o) != observers_.end())
            o->OnSourceChanged(value);
    }
}

// ---------------------------------------------------------------------------
// PyVm
// ---------------------------------------------------------------------------

PyVm::PyVm(PyObject* vm) : vm_(vm) { Py_XINCREF(vm_); }

PyVm::~PyVm() {
    PyGILState_STATE state = PyGILState_Ensure();
    Py_CLEAR(vm_);
    PyGILState_Release(state);
}

bool PyVm::Register() {
    PyObject* schema = PyObject_GetAttrString(vm_, "_wui_props");
    if (schema == nullptr || PyDict_Size(schema) < 0) {
        PyErr_Clear();
        Py_XDECREF(schema);
        return false;
    }

    bool ok = true;
    Py_ssize_t position = 0;
    PyObject* key = nullptr;
    PyObject* value = nullptr;
    while (PyDict_Next(schema, &position, &key, &value)) {
        const std::string name = ToUtf8(key);
        const std::string kindName = ToUtf8(value);
        PyKind kind;
        if (kindName == "text")       kind = PyKind::Text;
        else if (kindName == "int")   kind = PyKind::Int;
        else if (kindName == "float") kind = PyKind::Float;
        else if (kindName == "bool")  kind = PyKind::Bool;
        else if (kindName == "command") kind = PyKind::Command;
        else { ok = false; break; }

        PyObject* attribute = PyObject_GetAttrString(vm_, name.c_str());
        if (attribute == nullptr) {
            PyErr_Clear();
            ok = false;
            break;
        }
        RefPtr command;
        if (kind == PyKind::Command) {
            PyObject* canExecute = PyObject_GetAttrString(attribute, "can_execute");
            if (canExecute == nullptr) {
                PyErr_Clear();
                canExecute = nullptr;
            }
            command = std::make_shared<PyCommand>(attribute, canExecute);
            Py_XDECREF(canExecute);
        }
        Py_DECREF(attribute);

        auto source = std::make_unique<PyPropertySource>(this, vm_, name, kind, command, &updating_);
        table_.Add(name, source.get());
        index_.emplace(name, entries_.size());
        entries_.push_back(Entry{std::move(source), kind});
    }
    Py_DECREF(schema);
    return ok;
}

void PyVm::NotifyFromPython(const std::string& name) {
    if (updating_) return;
    const auto found = index_.find(name);
    if (found == index_.end()) return;
    // A derived value that writes back to its own source would otherwise
    // recurse forever. Different properties may still form a chain.
    if (notifyDepth_ > 0 && name == notifying_) return;

    Entry& entry = entries_[found->second];
    Value value;
    if (!entry.source->Read(value)) return;

    const std::string previous = notifying_;
    notifying_ = name;
    ++notifyDepth_;

    entry.source->NotifyObservers(value);

    // Python's on_changed(name, value) plays the role the C++ samples give to
    // IPropertyObserver: derived captions stay in the ViewModel.
    PyObject* callback = PyObject_GetAttrString(vm_, "on_changed");
    if (callback != nullptr) {
        PyObject* pyName = FromUtf8(name);
        PyObject* pyValue = PyFromValue(value, entry.kind);
        if (pyName != nullptr && pyValue != nullptr) {
            PyObject* arguments = PyTuple_New(2);
            if (arguments != nullptr) {
                PyTuple_SetItem(arguments, 0, pyName);
                pyName = nullptr; // the tuple owns it now
                PyTuple_SetItem(arguments, 1, pyValue);
                pyValue = nullptr;
                PyObject* result = PyObject_CallObject(callback, arguments);
                Py_DECREF(arguments);
                if (result != nullptr) Py_DECREF(result);
                else PyErr_Print();
            }
        }
        Py_XDECREF(pyName);
        Py_XDECREF(pyValue);
        Py_DECREF(callback);
    } else {
        PyErr_Clear();
    }

    --notifyDepth_;
    notifying_ = previous;
}

// ---------------------------------------------------------------------------
// Process-level entry points
// ---------------------------------------------------------------------------

int RunVm(PyObject* vm, const std::wstring& markup, const std::wstring& title,
          float width, float height) {
    Py_XINCREF(vm);
    Py_XDECREF(g_vm);
    g_vm = vm;

    auto bridge = std::make_shared<PyVm>(vm);
    if (!bridge->Register()) {
        PyErr_Print();
        bridge.reset();
        Py_CLEAR(g_vm);
        return 1;
    }
    g_bridge = bridge;

    sample::HostOptions options;
    // The host otherwise treats the first non-switch argument as the markup
    // path -- in `python main.py` that argument is main.py itself.
    options.markupPath = WideToUtf8(markup);
    options.title = title;
    options.widthDip = width;
    options.heightDip = height;

    const int result = sample::RunSample(GetModuleHandle(nullptr), options,
                                         std::static_pointer_cast<RefObject>(bridge));

    // PyVm holds Python references. It must die before Py_FinalizeEx() runs.
    sample::Shutdown();
    g_bridge.reset();
    bridge.reset();
    Py_CLEAR(g_vm);
    return result;
}

void NotifyVm(PyObject* vm, const std::string& name) {
    if (g_bridge == nullptr || vm != g_vm) return;
    g_bridge->NotifyFromPython(name);
}

size_t BindingErrors() { return wui::BindingErrorCount(); }

} // namespace wuipy

// ---------------------------------------------------------------------------
// CPython module: _wui
// ---------------------------------------------------------------------------
namespace {

PyObject* ModuleRun(PyObject*, PyObject* args) {
    PyGILState_STATE state = PyGILState_Ensure();
    PyObject* result = nullptr;
    if (PyTuple_Size(args) != 5) {
        PyErr_SetString(PyExc_TypeError, "run(markup, vm, title, width, height) needs 5 arguments");
    } else {
        PyObject* markup = PyTuple_GetItem(args, 0);
        PyObject* vm = PyTuple_GetItem(args, 1);
        PyObject* title = PyTuple_GetItem(args, 2);
        PyObject* width = PyTuple_GetItem(args, 3);
        PyObject* height = PyTuple_GetItem(args, 4);
        const long w = PyLong_AsLong(width);
        const long h = PyLong_AsLong(height);
        if (PyErr_Occurred()) {
            PyErr_Clear();
            PyErr_SetString(PyExc_TypeError, "width and height must be integers");
        } else {
            const int code = wuipy::RunVm(vm, wuipy::ToWide(markup), wuipy::ToWide(title),
                                          static_cast<float>(w), static_cast<float>(h));
            result = PyLong_FromLong(code);
        }
    }
    PyGILState_Release(state);
    return result;
}

PyObject* ModuleNotify(PyObject*, PyObject* args) {
    PyGILState_STATE state = PyGILState_Ensure();
    if (PyTuple_Size(args) == 2) {
        PyObject* vm = PyTuple_GetItem(args, 0);
        PyObject* name = PyTuple_GetItem(args, 1);
        wuipy::NotifyVm(vm, wuipy::ToUtf8(name));
    }
    PyGILState_Release(state);
    Py_RETURN_NONE;
}

PyObject* ModuleBindingErrors(PyObject*, PyObject*) {
    return PyLong_FromSize_t(wuipy::BindingErrors());
}

PyMethodDef kMethods[] = {
    {"run", ModuleRun, METH_VARARGS, "Run the Werfs UI with a Python ViewModel."},
    {"_notify", ModuleNotify, METH_VARARGS, "Notify the bridge after a Python attribute write."},
    {"binding_errors", ModuleBindingErrors, METH_NOARGS, "Number of binding errors seen so far."},
    {nullptr, nullptr, 0, nullptr},
};

PyModuleDef kModule = {PyModuleDef_HEAD_INIT, "_wui", "Werfs Python MVVM bridge", -1, kMethods};

} // namespace

PyMODINIT_FUNC PyInit__wui(void) { return PyModule_Create2(&kModule, PYTHON_API_VERSION); }
