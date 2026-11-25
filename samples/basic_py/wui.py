# SPDX-License-Identifier: MIT
"""Python-side helpers for the Werfs MVVM bridge.

The native module `_wui` does the plumbing; this file keeps the Python API
small. A ViewModel declares the attributes WAML binds in `_wui_props`:

    "text" | "int" | "float" | "bool" | "command"

ObservableObject.__setattr__ forwards writes to the native bridge so OneWay
bindings follow Python state. The bridge calls `on_changed(name, value)` after
the View writes a value, which is where derived text belongs.
"""

import _wui


class ObservableObject:
    """Base class for a ViewModel driven by the C++ UI host."""

    _wui_props = {}

    def __setattr__(self, name, value):
        object.__setattr__(self, name, value)
        if name in type(self)._wui_props:
            # No-op before run(): the native bridge is not installed yet.
            _wui._notify(self, name)


def run(markup, vm, title="Werfs — Python MVVM", width=560, height=520):
    """Create the window and run until it closes. Returns the exit code."""
    return _wui.run(str(markup), vm, str(title), int(width), int(height))


def binding_errors():
    """Binding errors seen so far (zero is what --selftest expects)."""
    return _wui.binding_errors()
