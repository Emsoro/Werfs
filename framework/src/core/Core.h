// SPDX-License-Identifier: MIT
// WAML / MVVM spike -- core types shared by the engine, the markup layer and tests.
#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace wui {

class RefObject;
class Widget;
class PropertyTable;
class IPropertyObserver;
class PropertyObserverList;

using RefPtr = std::shared_ptr<RefObject>;
using WidgetPtr = std::shared_ptr<Widget>;

// Virtual-key codes used by the controls. Mirrors the Win32 values so the
// input layer can pass VK_* straight through, but keeps windows.h out of the
// headers (PLAN.md ADR-9: public headers stay lightweight and portable).
enum VirtualKey : int {
    KeyLeft = 0x25,
    KeyUp = 0x26,
    KeyRight = 0x27,
    KeyDown = 0x28,
    KeyHome = 0x24,
    KeyEnd = 0x23,
    KeyBack = 0x08,
    KeyTab = 0x09,
    KeyReturn = 0x0D,
    KeyEscape = 0x1B,
    KeyPageUp = 0x21,
    KeyPageDown = 0x22,
    KeySpace = 0x20,
    KeyDelete = 0x2E,
};

// Modifier bits for key dispatch. Deliberately not the Win32 MOD_* values: the
// core layer must not require windows.h, and the host converts on the way in.
constexpr int ModCtrl = 1 << 0;
constexpr int ModShift = 1 << 1;
constexpr int ModAlt = 1 << 2;

// Parses a WAML accelerator ("Ctrl+S", "Ctrl+Shift+S", "Alt+F4", "F5") into a
// virtual-key code and the modifier mask above. Returns false when the text is
// not an accelerator, so a typo is a load-time diagnostic rather than a
// shortcut that silently never fires.
bool ParseAccelerator(std::string_view text, int& vk, int& modifiers);
// Human-readable form used by the menu painter: "Ctrl+S".
std::wstring FormatAccelerator(int vk, int modifiers);

// ---------------------------------------------------------------------------
// Binding description types live in core so that property descriptors can
// declare their default mode without depending on the MVVM layer.
// ---------------------------------------------------------------------------
enum class BindingMode { OneTime, OneWay, TwoWay, OneWayToSource };
enum class UpdateSourceTrigger { PropertyChanged, LostFocus };
enum class VType { Null, Number, Bool, Text, Color, Thickness, Enum, Ref };
enum class TextAlign { Left, Center, Right };
enum class ThemeKind { Light, Dark };

// Property change flags (mirrors PLAN.md ADR-2).
constexpr uint32_t AffectsMeasure = 1u << 0;
constexpr uint32_t AffectsRender = 1u << 1;

struct Point {
    float x = 0;
    float y = 0;
};

struct Size {
    float w = 0;
    float h = 0;
};

struct Rect {
    float x = 0;
    float y = 0;
    float w = 0;
    float h = 0;

    float Right() const { return x + w; }
    float Bottom() const { return y + h; }
    bool Contains(const Point& p) const {
        return p.x >= x && p.y >= y && p.x < x + w && p.y < y + h;
    }
    Rect Inflate(float d) const { return Rect{x - d, y - d, w + 2 * d, h + 2 * d}; }
    Rect Deflate(const struct Thickness& t) const;
};

struct Thickness {
    float l = 0;
    float t = 0;
    float r = 0;
    float b = 0;

    float H() const { return l + r; }
    float V() const { return t + b; }
    bool operator==(const Thickness& o) const {
        return l == o.l && t == o.t && r == o.r && b == o.b;
    }
};

// A color either carries literal components or a theme token (token != None).
// Token colors are resolved by the paint context, which keeps theme switching
// possible without reloading the document.
enum class Token : uint16_t {
    None = 0,
    AppBackground,
    CardBackground,
    CardBorder,
    TextPrimary,
    TextSecondary,
    TextDisabled,
    Accent,
    TextOnAccent,
    ControlFill,
    ControlFillHover,
    ControlFillPressed,
    Selection,
    Count
};

struct Color {
    float r = 0;
    float g = 0;
    float b = 0;
    float a = 1;
    Token token = Token::None;

    bool operator==(const Color& o) const {
        if (token != o.token) return false;
        if (token != Token::None) return true;
        return r == o.r && g == o.g && b == o.b && a == o.a;
    }
};

struct ThemePalette {
    Color colors[static_cast<int>(Token::Count)];
};

ThemePalette LightPalette();
ThemePalette DarkPalette();
ThemePalette MakePalette(ThemeKind kind);
Color ResolveColor(const Color& c, const ThemePalette& palette);

// ---------------------------------------------------------------------------
// Value: the type-erased carrier used for property slots and binding payloads.
// It is intentionally a plain tagged struct: no variant visitation in the hot
// path, cheap equality and no dependency on std::any.
// ---------------------------------------------------------------------------
class DataTemplate;

struct Value {
    VType type = VType::Null;
    double num = 0;
    bool flag = false;
    int64_t enumValue = 0;
    std::wstring text;
    Color color;
    Thickness thickness;
    RefPtr ref;

    static Value Null() { return Value{}; }
    static Value Number(double v);
    static Value Bool(bool v);
    static Value Text(std::wstring v);
    static Value MakeColor(const Color& c);
    static Value MakeThickness(const Thickness& t);
    static Value Enum(int64_t v);
    static Value Ref(const RefPtr& r);

    bool Equals(const Value& o) const;
};

std::wstring FormatValueText(const Value& v);
bool ConvertValue(const Value& from, VType to, Value& out);
const char* VTypeName(VType t);

// ---------------------------------------------------------------------------
// Resource base: every ViewModel, command, list and DataTemplate is one.
// ---------------------------------------------------------------------------
class RefObject {
public:
    virtual ~RefObject() = default;
    virtual PropertyTable* GetPropertyTable() { return nullptr; }
};

// ---------------------------------------------------------------------------
// Text measurement abstraction: DWrite in the app, deterministic fake in tests.
// ---------------------------------------------------------------------------
class ITextService {
public:
    virtual ~ITextService() = default;
    virtual Size Measure(const std::wstring& text, float fontSize, int weight) = 0;
    virtual float LineHeight(float fontSize) = 0;
};

// Default font weights (index into this table is the WAML enum value).
enum FontWeight : int { WeightNormal = 0, WeightMedium = 1, WeightSemiBold = 2, WeightBold = 3 };
int FontWeightToDWrite(int w);

// ---------------------------------------------------------------------------
// Painting abstraction: D2D/WIC in the app and snapshot tool, recording in tests.
// ---------------------------------------------------------------------------
class PaintCtx {
public:
    virtual ~PaintCtx() = default;
    virtual void FillRect(const Rect& r, const Color& c) = 0;
    virtual void FillRoundRect(const Rect& r, float radius, const Color& c) = 0;
    virtual void StrokeRect(const Rect& r, const Color& c, float width, float radius) = 0;
    virtual void DrawText(const std::wstring& text, const Rect& r, const Color& c, float fontSize,
                          int weight, TextAlign align) = 0;
    virtual void PushClip(const Rect& r) = 0;
    virtual void PopClip() = 0;
};

// ---------------------------------------------------------------------------
// Overlay layer: everything that draws ABOVE the document instead of inside it.
//
// Why this cannot live in the child tree: a menu opens over whatever happens to
// be there, and a ScrollViewer clips its subtree. Parenting an overlay to the
// control that opened it makes it inherit that clip and that scroll offset --
// the classic "the dropdown is cut off at the panel edge" defect.
//
// So an overlay is hosted on the ROOT, painted after the whole tree, and keeps
// its geometry in window space. A widget that wants to show one calls
// Widget::SetOverlay().
//
// One at a time. Stacking (a submenu inside, a flyout over a dialog) needs a
// stack and real focus rules; the menu control draws its own submenus inside its
// single layer instead, which is enough until something actually needs nesting.
// ---------------------------------------------------------------------------
class OverlayLayer {
public:
    virtual ~OverlayLayer() = default;
    virtual Rect Bounds() const = 0;
    virtual void Paint(PaintCtx& ctx, const ThemePalette& palette) = 0;
    // Pointer contract, in the same window space Bounds() reports. A press
    // outside a light-dismiss layer consumes it by closing, and returns true so
    // the host stops dispatching to the document underneath.
    virtual bool OnPointerDown(const Point& p) = 0;
    virtual void OnPointerUp(const Point& p) = 0;
    virtual void OnPointerMove(const Point& p) {}
    // Returns true when the key was consumed.
    virtual bool OnKeyDown(int vk, int modifiers) = 0;
    // True while the layer still wants pointer capture.
    virtual bool Active() const = 0;
    // False for a modal: presses outside are swallowed rather than dismissing it.
    virtual bool LightDismiss() const { return true; }
};

// ---------------------------------------------------------------------------
// Small helper event used by commands.
// ---------------------------------------------------------------------------
class Event {
public:
    using Handler = std::function<void()>;

    int Subscribe(Handler h) {
        const int id = nextId_++;
        handlers_.push_back({id, std::move(h)});
        return id;
    }
    void Unsubscribe(int id) {
        for (auto it = handlers_.begin(); it != handlers_.end(); ++it) {
            if (it->first == id) {
                handlers_.erase(it);
                return;
            }
        }
    }
    void Raise() {
        // Copy so handlers may subscribe/unsubscribe while running.
        auto copy = handlers_;
        for (auto& h : copy) h.second();
    }

private:
    std::vector<std::pair<int, Handler>> handlers_;
    int nextId_ = 1;
};

// ---------------------------------------------------------------------------
// String helpers (WAML sources are UTF-8, UI strings are UTF-16).
// ---------------------------------------------------------------------------
std::wstring Utf8ToWide(std::string_view s);
std::string WideToUtf8(std::wstring_view s);
std::string ToLowerAscii(std::string s);
std::string_view TrimAscii(std::string_view s);
int Levenshtein(std::string_view a, std::string_view b);
std::vector<std::string> SuggestNames(const std::string& input, const std::vector<std::string>& candidates,
                                      int maxResults = 2);

} // namespace wui
