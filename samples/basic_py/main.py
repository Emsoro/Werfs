# SPDX-License-Identifier: MIT
"""samples/basic_py — a ViewModel written in Python, driving the Werfs UI.

Run it either with the system interpreter:

    python main.py

or through the CMake-built launcher (which embeds the same system interpreter):

    waml_basic_py.exe

Both paths load the same _wui.abi3.pyd and the same BasicPy.waml.
"""

import pathlib
import sys

_HERE = pathlib.Path(__file__).resolve().parent
if str(_HERE) not in sys.path:
    sys.path.insert(0, str(_HERE))

import wui  # noqa: E402  (path must be fixed up before importing)


class MainVm(wui.ObservableObject):
    """One property per binding in BasicPy.waml.

    Kinds are explicit so the bridge knows whether to write back an int, a
    float, a bool or a string -- Python itself would turn a Slider's Number
    into a float otherwise.
    """

    _wui_props = {
        "heading": "text",
        "click_count": "int",
        "summary": "text",
        "greeting": "text",
        "echo": "text",
        "agreed": "bool",
        "agreement_text": "text",
        "volume": "float",
        "volume_text": "text",
        "click": "command",
        "reset": "command",
    }

    def __init__(self):
        super().__init__()
        self.heading = "Python 驱动的 MVVM"
        self.click_count = 0
        self.summary = "按钮调用 Python 方法，写回属性后标签自动更新。"
        self.greeting = "Hello from Python"
        self.echo = "在文本框里输入，这行会跟着变。"
        self.agreed = False
        self.agreement_text = "尚未勾选。"
        self.volume = 0.4
        self.volume_text = "音量 40%"

    # The native bridge calls this after a view (or a command) writes a value.
    # Derived text stays in the ViewModel, exactly like the C++ samples.
    def on_changed(self, name, value):
        if name == "greeting":
            self.echo = "回显：" + str(value)
        elif name == "agreed":
            self.agreement_text = "已勾选 — 这行文字由 Python 派生。" if value else "尚未勾选。"
        elif name == "volume":
            self.volume_text = "音量 {:.0f}%".format(float(value) * 100.0)

    def click(self):
        self.click_count += 1
        self.summary = "第 {} 次点击 — 这个值由 Python 计算。".format(self.click_count)

    def reset(self):
        self.click_count = 0
        self.summary = "已重置回零。"


def main():
    return wui.run(
        markup=_HERE / "BasicPy.waml",
        vm=MainVm(),
        title="Werfs — Python MVVM",
        width=560,
        height=480,
    )


if __name__ == "__main__":
    raise SystemExit(main())
