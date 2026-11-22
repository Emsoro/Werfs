// SPDX-License-Identifier: MIT
#include "Core.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cwctype>

#include <windows.h>

namespace wui {

Rect Rect::Deflate(const Thickness& t) const {
    return Rect{x + t.l, y + t.t, w - t.H(), h - t.V()};
}

// ---------------------------------------------------------------------------
// Value
// ---------------------------------------------------------------------------
Value Value::Number(double v) {
    Value r;
    r.type = VType::Number;
    r.num = v;
    return r;
}

Value Value::Bool(bool v) {
    Value r;
    r.type = VType::Bool;
    r.flag = v;
    return r;
}

Value Value::Text(std::wstring v) {
    Value r;
    r.type = VType::Text;
    r.text = std::move(v);
    return r;
}

Value Value::MakeColor(const Color& c) {
    Value r;
    r.type = VType::Color;
    r.color = c;
    return r;
}

Value Value::MakeThickness(const Thickness& t) {
    Value r;
    r.type = VType::Thickness;
    r.thickness = t;
    return r;
}

Value Value::Enum(int64_t v) {
    Value r;
    r.type = VType::Enum;
    r.enumValue = v;
    return r;
}

Value Value::Ref(const RefPtr& p) {
    Value r;
    r.type = VType::Ref;
    r.ref = p;
    return r;
}

bool Value::Equals(const Value& o) const {
    if (type != o.type) return false;
    switch (type) {
    case VType::Null: return true;
    case VType::Number: return num == o.num;
    case VType::Bool: return flag == o.flag;
    case VType::Text: return text == o.text;
    case VType::Color: return color == o.color;
    case VType::Thickness: return thickness == o.thickness;
    case VType::Enum: return enumValue == o.enumValue;
    case VType::Ref: return ref == o.ref;
    }
    return false;
}

const char* VTypeName(VType t) {
    switch (t) {
    case VType::Null: return "null";
    case VType::Number: return "number";
    case VType::Bool: return "bool";
    case VType::Text: return "text";
    case VType::Color: return "color";
    case VType::Thickness: return "thickness";
    case VType::Enum: return "enum";
    case VType::Ref: return "object";
    }
    return "?";
}

namespace {

std::wstring FormatDouble(double v) {
    wchar_t buf[64];
    if (v == static_cast<double>(static_cast<int64_t>(v))) {
        swprintf_s(buf, L"%lld", static_cast<long long>(v));
    } else {
        swprintf_s(buf, L"%.3f", v);
    }
    std::wstring s(buf);
    // Trim trailing zeros of the fractional part.
    const auto dot = s.find(L'.');
    if (dot != std::wstring::npos) {
        while (!s.empty() && s.back() == L'0') s.pop_back();
        if (!s.empty() && s.back() == L'.') s.pop_back();
    }
    return s;
}

} // namespace

std::wstring FormatValueText(const Value& v) {
    switch (v.type) {
    case VType::Null: return L"";
    case VType::Number: return FormatDouble(v.num);
    case VType::Bool: return v.flag ? L"True" : L"False";
    case VType::Text: return v.text;
    case VType::Thickness: {
        Thickness t = v.thickness;
        if (t.l == t.t && t.t == t.r && t.r == t.b) return FormatDouble(t.l);
        if (t.t == t.b && t.l == t.r) return FormatDouble(t.l) + L"," + FormatDouble(t.t);
        return FormatDouble(t.l) + L"," + FormatDouble(t.t) + L"," + FormatDouble(t.r) + L"," +
               FormatDouble(t.b);
    }
    case VType::Color: return L"<color>";
    case VType::Enum: return FormatDouble(static_cast<double>(v.enumValue));
    case VType::Ref: return L"<object>";
    }
    return L"";
}

static bool TextToNumber(const std::wstring& s, double& out) {
    if (s.empty()) return false;
    wchar_t* end = nullptr;
    const double v = wcstod(s.c_str(), &end);
    if (end == s.c_str()) return false;
    while (end && *end == L' ') ++end;
    if (end && *end != 0) return false;
    out = v;
    return std::isfinite(v);
}

static bool TextToBool(const std::wstring& s, bool& out) {
    std::wstring lower;
    lower.reserve(s.size());
    for (wchar_t c : s) lower.push_back(static_cast<wchar_t>(towlower(c)));
    if (lower == L"true" || lower == L"1") {
        out = true;
        return true;
    }
    if (lower == L"false" || lower == L"0") {
        out = false;
        return true;
    }
    return false;
}

bool ConvertValue(const Value& from, VType to, Value& out) {
    if (from.type == to) {
        out = from;
        return true;
    }
    const bool fromNull = from.type == VType::Null;
    switch (to) {
    case VType::Null:
        out = Value::Null();
        return true;
    case VType::Text: {
        if (fromNull) {
            out = Value::Text(L"");
            return true;
        }
        out = Value::Text(FormatValueText(from));
        return from.type != VType::Ref;
    }
    case VType::Number: {
        if (fromNull) {
            out = Value::Number(0);
            return true;
        }
        double d = 0;
        if (from.type == VType::Number) d = from.num;
        else if (from.type == VType::Bool) d = from.flag ? 1 : 0;
        else if (from.type == VType::Enum) d = static_cast<double>(from.enumValue);
        else if (from.type == VType::Text && TextToNumber(from.text, d)) {
        } else {
            return false;
        }
        out = Value::Number(d);
        return true;
    }
    case VType::Bool: {
        if (fromNull) {
            out = Value::Bool(false);
            return true;
        }
        bool b = false;
        if (from.type == VType::Bool) b = from.flag;
        else if (from.type == VType::Number) b = from.num != 0;
        else if (from.type == VType::Text && TextToBool(from.text, b)) {
        } else {
            return false;
        }
        out = Value::Bool(b);
        return true;
    }
    case VType::Enum: {
        if (fromNull) {
            out = Value::Enum(0);
            return true;
        }
        if (from.type == VType::Enum) {
            out = from;
            return true;
        }
        if (from.type == VType::Number) {
            out = Value::Enum(static_cast<int64_t>(from.num));
            return true;
        }
        if (from.type == VType::Bool) {
            out = Value::Enum(from.flag ? 1 : 0);
            return true;
        }
        if (from.type == VType::Text) {
            double d = 0;
            if (TextToNumber(from.text, d)) {
                out = Value::Enum(static_cast<int64_t>(d));
                return true;
            }
        }
        return false;
    }
    case VType::Color: {
        if (fromNull) {
            out = Value::MakeColor(Color{});
            return true;
        }
        return false; // Colors never implicitly convert.
    }
    case VType::Thickness: {
        if (fromNull) {
            out = Value::MakeThickness(Thickness{});
            return true;
        }
        if (from.type == VType::Number) {
            out = Value::MakeThickness(Thickness{static_cast<float>(from.num),
                                                 static_cast<float>(from.num),
                                                 static_cast<float>(from.num),
                                                 static_cast<float>(from.num)});
            return true;
        }
        return false;
    }
    case VType::Ref:
        return false;
    }
    return false;
}

// ---------------------------------------------------------------------------
// Theme
// ---------------------------------------------------------------------------
static Color Rgba(uint32_t argb) {
    Color c;
    c.a = static_cast<float>((argb >> 24) & 0xFF) / 255.0f;
    c.r = static_cast<float>((argb >> 16) & 0xFF) / 255.0f;
    c.g = static_cast<float>((argb >> 8) & 0xFF) / 255.0f;
    c.b = static_cast<float>(argb & 0xFF) / 255.0f;
    return c;
}

ThemePalette LightPalette() {
    ThemePalette p{};
    p.colors[static_cast<int>(Token::AppBackground)] = Rgba(0xFFF3F3F3);
    p.colors[static_cast<int>(Token::CardBackground)] = Rgba(0xFFFFFFFF);
    p.colors[static_cast<int>(Token::CardBorder)] = Rgba(0xFFE5E5E5);
    p.colors[static_cast<int>(Token::TextPrimary)] = Rgba(0xFF1A1A1A);
    p.colors[static_cast<int>(Token::TextSecondary)] = Rgba(0xFF6B6B6B);
    p.colors[static_cast<int>(Token::TextDisabled)] = Rgba(0xFF9A9A9A);
    p.colors[static_cast<int>(Token::Accent)] = Rgba(0xFF0067C0);
    p.colors[static_cast<int>(Token::TextOnAccent)] = Rgba(0xFFFFFFFF);
    p.colors[static_cast<int>(Token::ControlFill)] = Rgba(0xFFFBFBFB);
    p.colors[static_cast<int>(Token::ControlFillHover)] = Rgba(0xFFF0F0F0);
    p.colors[static_cast<int>(Token::ControlFillPressed)] = Rgba(0xFFE4E4E4);
    p.colors[static_cast<int>(Token::Selection)] = Rgba(0x400067C0);
    return p;
}

ThemePalette DarkPalette() {
    ThemePalette p{};
    p.colors[static_cast<int>(Token::AppBackground)] = Rgba(0xFF202020);
    p.colors[static_cast<int>(Token::CardBackground)] = Rgba(0xFF2B2B2B);
    p.colors[static_cast<int>(Token::CardBorder)] = Rgba(0xFF3A3A3A);
    p.colors[static_cast<int>(Token::TextPrimary)] = Rgba(0xFFF0F0F0);
    p.colors[static_cast<int>(Token::TextSecondary)] = Rgba(0xFFA8A8A8);
    p.colors[static_cast<int>(Token::TextDisabled)] = Rgba(0xFF787878);
    p.colors[static_cast<int>(Token::Accent)] = Rgba(0xFF60CDFF);
    p.colors[static_cast<int>(Token::TextOnAccent)] = Rgba(0xFF121212);
    p.colors[static_cast<int>(Token::ControlFill)] = Rgba(0xFF333333);
    p.colors[static_cast<int>(Token::ControlFillHover)] = Rgba(0xFF3D3D3D);
    p.colors[static_cast<int>(Token::ControlFillPressed)] = Rgba(0xFF484848);
    p.colors[static_cast<int>(Token::Selection)] = Rgba(0x4060CDFF);
    return p;
}

ThemePalette MakePalette(ThemeKind kind) {
    return kind == ThemeKind::Dark ? DarkPalette() : LightPalette();
}

Color ResolveColor(const Color& c, const ThemePalette& palette) {
    if (c.token == Token::None) return c;
    const int idx = static_cast<int>(c.token);
    if (idx <= 0 || idx >= static_cast<int>(Token::Count)) return c;
    return palette.colors[idx];
}

// ---------------------------------------------------------------------------
// Strings
// ---------------------------------------------------------------------------
std::wstring Utf8ToWide(std::string_view s) {
    if (s.empty()) return L"";
    const int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
    std::wstring out(static_cast<size_t>(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), out.data(), n);
    return out;
}

std::string WideToUtf8(std::wstring_view s) {
    if (s.empty()) return "";
    const int n = WideCharToMultiByte(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0,
                                      nullptr, nullptr);
    std::string out(static_cast<size_t>(n), '\0');
    WideCharToMultiByte(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), out.data(), n, nullptr,
                        nullptr);
    return out;
}

std::string ToLowerAscii(std::string s) {
    for (char& c : s) {
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    }
    return s;
}

std::string_view TrimAscii(std::string_view s) {
    size_t b = 0;
    size_t e = s.size();
    while (b < e && (s[b] == ' ' || s[b] == '\t' || s[b] == '\r' || s[b] == '\n')) ++b;
    while (e > b && (s[e - 1] == ' ' || s[e - 1] == '\t' || s[e - 1] == '\r' || s[e - 1] == '\n')) --e;
    return s.substr(b, e - b);
}

int Levenshtein(std::string_view a, std::string_view b) {
    const size_t n = a.size();
    const size_t m = b.size();
    std::vector<size_t> prev(m + 1);
    std::vector<size_t> cur(m + 1);
    for (size_t j = 0; j <= m; ++j) prev[j] = j;
    for (size_t i = 1; i <= n; ++i) {
        cur[0] = i;
        for (size_t j = 1; j <= m; ++j) {
            const size_t cost = (a[i - 1] == b[j - 1]) ? 0 : 1;
            cur[j] = std::min({prev[j] + 1, cur[j - 1] + 1, prev[j - 1] + cost});
        }
        std::swap(prev, cur);
    }
    return static_cast<int>(prev[m]);
}

std::vector<std::string> SuggestNames(const std::string& input, const std::vector<std::string>& candidates,
                                      int maxResults) {
    struct Scored {
        std::string name;
        int distance;
    };
    std::vector<Scored> scored;
    const std::string lowerInput = ToLowerAscii(input);
    for (const auto& c : candidates) {
        const int d = Levenshtein(lowerInput, ToLowerAscii(c));
        // Only suggest reasonably close names.
        const int limit = std::max(2, static_cast<int>(input.size()) / 2 + 1);
        if (d <= limit) scored.push_back({c, d});
    }
    std::stable_sort(scored.begin(), scored.end(),
                     [](const Scored& a, const Scored& b) { return a.distance < b.distance; });
    std::vector<std::string> out;
    for (size_t i = 0; i < scored.size() && static_cast<int>(i) < maxResults; ++i)
        out.push_back(scored[i].name);
    return out;
}

int FontWeightToDWrite(int w) {
    switch (w) {
    case WeightMedium: return 500;
    case WeightSemiBold: return 600;
    case WeightBold: return 700;
    default: return 400;
    }
}

} // namespace wui
