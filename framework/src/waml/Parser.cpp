// SPDX-License-Identifier: MIT
// WAML lexer + parser + validator.
//
// The pipeline is deliberately split in two halves:
//   1. ParseWaml()  : source text -> raw node tree -> validated NodeDesc tree.
//                     All type/property/value resolution and diagnostics happen
//                     here, so runtime instantiation never touches strings.
//   2. Instantiate  : NodeDesc -> widget tree (see Waml.cpp).
#include "Waml.h"

#include "Controls.h"
#include "Converters.h"
#include "StyleTriggers.h"
#include "Widget.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>

namespace wui {

namespace {

// ---------------------------------------------------------------------------
// Character helpers
// ---------------------------------------------------------------------------
bool IsNameStart(char c) {
    return std::isalpha(static_cast<unsigned char>(c)) || c == '_' ||
           static_cast<unsigned char>(c) >= 0x80;
}
bool IsNameChar(char c) {
    return IsNameStart(c) || std::isdigit(static_cast<unsigned char>(c)) || c == '-' || c == '.' ||
           c == ':';
}
bool IsSpace(char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; }

std::string TrimCopy(std::string_view s) { return std::string(TrimAscii(s)); }

void AppendUtf8(std::string& out, uint32_t cp) {
    if (cp < 0x80) {
        out.push_back(static_cast<char>(cp));
    } else if (cp < 0x800) {
        out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else if (cp < 0x10000) {
        out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else {
        out.push_back(static_cast<char>(0xF0 | (cp >> 18)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    }
}

bool DecodeEntity(std::string_view s, size_t& pos, std::string& out) {
    const size_t semi = s.find(';', pos + 1);
    if (semi == std::string_view::npos || semi - pos > 12) return false;
    const std::string_view body = s.substr(pos + 1, semi - pos - 1);
    if (body == "amp") out.push_back('&');
    else if (body == "lt") out.push_back('<');
    else if (body == "gt") out.push_back('>');
    else if (body == "quot") out.push_back('"');
    else if (body == "apos") out.push_back('\'');
    else if (!body.empty() && body[0] == '#') {
        uint32_t cp = 0;
        if (body.size() > 1 && (body[1] == 'x' || body[1] == 'X')) {
            for (size_t i = 2; i < body.size(); ++i) {
                const char c = body[i];
                int digit = -1;
                if (c >= '0' && c <= '9') digit = c - '0';
                else if (c >= 'a' && c <= 'f') digit = c - 'a' + 10;
                else if (c >= 'A' && c <= 'F') digit = c - 'A' + 10;
                if (digit < 0) return false;
                cp = cp * 16 + static_cast<uint32_t>(digit);
            }
        } else {
            for (size_t i = 1; i < body.size(); ++i) {
                if (!std::isdigit(static_cast<unsigned char>(body[i]))) return false;
                cp = cp * 10 + static_cast<uint32_t>(body[i] - '0');
            }
        }
        if (cp == 0 || cp > 0x10FFFF) return false;
        AppendUtf8(out, cp);
    } else {
        return false;
    }
    pos = semi + 1;
    return true;
}

// Decodes &...; entities in a text span. Returns false on a malformed entity.
bool DecodeEntities(std::string_view raw, std::string& out, size_t& badOffset, size_t base) {
    out.clear();
    out.reserve(raw.size());
    for (size_t i = 0; i < raw.size();) {
        if (raw[i] == '&') {
            size_t pos = i;
            if (!DecodeEntity(raw, pos, out)) {
                badOffset = base + i;
                return false;
            }
            i = pos;
        } else {
            out.push_back(raw[i++]);
        }
    }
    return true;
}

// ---------------------------------------------------------------------------
// Source positions
// ---------------------------------------------------------------------------
class SourceMap {
public:
    explicit SourceMap(std::string_view src) : src_(src) {
        lineStarts_.push_back(0);
        for (size_t i = 0; i < src.size(); ++i) {
            if (src[i] == '\n') lineStarts_.push_back(i + 1);
        }
    }
    void Locate(size_t offset, int& line, int& column) const {
        size_t lo = 0;
        size_t hi = lineStarts_.size();
        while (lo + 1 < hi) {
            const size_t mid = (lo + hi) / 2;
            if (lineStarts_[mid] <= offset) lo = mid;
            else hi = mid;
        }
        const size_t start = lineStarts_[lo];
        int col = 1;
        for (size_t i = start; i < offset && i < src_.size(); ++i) {
            if ((static_cast<unsigned char>(src_[i]) & 0xC0) != 0x80) ++col;
        }
        line = static_cast<int>(lo) + 1;
        column = col;
    }

private:
    std::string_view src_;
    std::vector<size_t> lineStarts_;
};

// ---------------------------------------------------------------------------
// Raw syntax tree
// ---------------------------------------------------------------------------
// Offsets are stored instead of line/column: computing a column requires
// scanning from the start of the line, so doing it for every element made
// single-line documents quadratic. Locations are resolved lazily, only when a
// diagnostic is actually reported.
struct RawAttr {
    std::string name;
    std::string value;
    size_t offset = 0;
};

struct RawNode {
    std::string tag;
    std::vector<RawAttr> attrs;
    std::vector<std::unique_ptr<RawNode>> children;
    std::string text;
    size_t offset = 0;      // position of '<'
    size_t nameOffset = 0;  // position of the tag name (better diagnostic anchor)
};

class Parser {
public:
    Parser(std::string_view src, DiagnosticBag& diag)
        : src_(src), diag_(diag), map_(src) {}

    std::unique_ptr<RawNode> ParseDocument() {
        SkipProlog();
        if (AtEnd()) {
            Err(DiagCode::Structure, 0, "document has no root element");
            return nullptr;
        }
        auto root = ParseElement(0);
        if (!root) return nullptr;
        SkipMisc();
        if (!AtEnd()) {
            Err(DiagCode::Structure, pos_, "unexpected content after the root element");
        }
        return root;
    }

private:
    bool AtEnd() const { return pos_ >= src_.size(); }
    char Peek(size_t ahead = 0) const { return pos_ + ahead < src_.size() ? src_[pos_ + ahead] : '\0'; }
    bool Match(std::string_view lit) const {
        return src_.compare(pos_, lit.size(), lit) == 0;
    }

    void Err(DiagCode code, size_t offset, std::string message) {
        int line = 1;
        int col = 1;
        map_.Locate(offset, line, col);
        diag_.Error(code, line, col, std::move(message));
    }

    void Warn(DiagCode code, size_t offset, std::string message) {
        int line = 1;
        int col = 1;
        map_.Locate(offset, line, col);
        diag_.Warning(code, line, col, std::move(message));
    }

    void SkipProlog() {
        // Optional UTF-8 BOM.
        if (src_.size() >= 3 && static_cast<unsigned char>(src_[0]) == 0xEF &&
            static_cast<unsigned char>(src_[1]) == 0xBB && static_cast<unsigned char>(src_[2]) == 0xBF)
            pos_ = 3;
        SkipSpaceAndComments();
        if (Match("<?")) {
            const size_t end = src_.find("?>", pos_);
            if (end == std::string_view::npos) {
                Err(DiagCode::Lexical, pos_, "unterminated processing instruction");
                pos_ = src_.size();
                return;
            }
            pos_ = end + 2;
            SkipSpaceAndComments();
        }
    }

    void SkipMisc() {
        while (!AtEnd()) {
            SkipSpaceAndComments();
            if (Match("<?")) {
                const size_t end = src_.find("?>", pos_);
                if (end == std::string_view::npos) {
                    Err(DiagCode::Lexical, pos_, "unterminated processing instruction");
                    pos_ = src_.size();
                    return;
                }
                pos_ = end + 2;
                continue;
            }
            break;
        }
    }

    void SkipSpaceAndComments() {
        for (;;) {
            while (!AtEnd() && IsSpace(Peek())) ++pos_;
            if (Match("<!--")) {
                const size_t end = src_.find("-->", pos_ + 4);
                if (end == std::string_view::npos) {
                    Err(DiagCode::Lexical, pos_, "unterminated comment");
                    pos_ = src_.size();
                    return;
                }
                pos_ = end + 3;
                continue;
            }
            break;
        }
    }

    // Parses one element starting at '<'. Returns null on a fatal syntax error.
    std::unique_ptr<RawNode> ParseElement(int depth) {
        if (depth > 64) {
            Err(DiagCode::Structure, pos_, "element nesting too deep (limit 64)");
            return nullptr;
        }
        if (!Match("<") || Peek(1) == '/' || Peek(1) == '!' || Peek(1) == '?') {
            Err(DiagCode::Structure, pos_, "expected an element");
            return nullptr;
        }
        auto node = std::make_unique<RawNode>();
        node->offset = pos_;
        ++pos_; // '<'
        // Anchor element diagnostics at the tag name rather than at '<'.
        node->nameOffset = pos_;

        if (!ReadName(node->tag)) {
            Err(DiagCode::Lexical, pos_, "expected an element name");
            return nullptr;
        }

        // Attributes
        for (;;) {
            const size_t beforeSpace = pos_;
            while (!AtEnd() && IsSpace(Peek())) ++pos_;
            if (AtEnd()) {
                Err(DiagCode::Structure, beforeSpace, "unexpected end of file inside a tag");
                return nullptr;
            }
            if (Match("/>")) {
                pos_ += 2;
                return node;
            }
            if (Peek() == '>') {
                ++pos_;
                break;
            }
            // Attribute
            RawAttr attr;
            attr.offset = pos_;
            if (!ReadName(attr.name)) {
                Err(DiagCode::Lexical, pos_, "expected an attribute name");
                return nullptr;
            }
            while (!AtEnd() && IsSpace(Peek())) ++pos_;
            if (Peek() != '=') {
                Err(DiagCode::BadAttribute, pos_, "expected '=' after attribute '" + attr.name + "'");
                return nullptr;
            }
            ++pos_;
            while (!AtEnd() && IsSpace(Peek())) ++pos_;
            const char quote = Peek();
            if (quote != '"' && quote != '\'') {
                Err(DiagCode::BadAttribute, pos_,
                    "value of attribute '" + attr.name + "' must be quoted");
                return nullptr;
            }
            ++pos_;
            const size_t valueStart = pos_;
            while (!AtEnd() && Peek() != quote) {
                if (Peek() == '<') {
                    Err(DiagCode::BadAttribute, pos_, "'<' is not allowed in an attribute value");
                    return nullptr;
                }
                ++pos_;
            }
            if (AtEnd()) {
                Err(DiagCode::Lexical, valueStart, "unterminated attribute value");
                return nullptr;
            }
            const std::string_view rawValue = src_.substr(valueStart, pos_ - valueStart);
            ++pos_; // closing quote
            size_t badOffset = 0;
            if (!DecodeEntities(rawValue, attr.value, badOffset, valueStart)) {
                Err(DiagCode::Lexical, badOffset, "unknown or malformed entity");
                return nullptr;
            }
            for (const auto& existing : node->attrs) {
                if (existing.name == attr.name) {
                    Err(DiagCode::DuplicateAttr, attr.offset,
                        "duplicate attribute '" + attr.name + "'");
                    return nullptr;
                }
            }
            node->attrs.push_back(std::move(attr));
        }

        // Content
        std::string textBuffer;
        for (;;) {
            if (AtEnd()) {
                Err(DiagCode::Structure, node->offset,
                    "unexpected end of file: <" + node->tag + "> is not closed");
                return nullptr;
            }
            if (Match("</")) {
                const size_t closeOffset = pos_;
                pos_ += 2;
                std::string closeName;
                if (!ReadName(closeName)) {
                    Err(DiagCode::Structure, pos_, "expected an element name in the closing tag");
                    return nullptr;
                }
                while (!AtEnd() && IsSpace(Peek())) ++pos_;
                if (Peek() != '>') {
                    Err(DiagCode::Structure, pos_, "expected '>' to close </" + closeName + ">");
                    return nullptr;
                }
                ++pos_;
                if (closeName != node->tag) {
                    Err(DiagCode::Structure, closeOffset,
                        "closing tag </" + closeName + "> does not match <" + node->tag + ">");
                    return nullptr;
                }
                node->text = std::move(textBuffer);
                return node;
            }
            if (Match("<!--")) {
                const size_t end = src_.find("-->", pos_ + 4);
                if (end == std::string_view::npos) {
                    Err(DiagCode::Lexical, pos_, "unterminated comment");
                    return nullptr;
                }
                pos_ = end + 3;
                continue;
            }
            if (Match("<![CDATA[")) {
                const size_t end = src_.find("]]>", pos_ + 9);
                if (end == std::string_view::npos) {
                    Err(DiagCode::Lexical, pos_, "unterminated CDATA section");
                    return nullptr;
                }
                textBuffer.append(src_.substr(pos_ + 9, end - pos_ - 9));
                pos_ = end + 3;
                continue;
            }
            if (Match("<?")) {
                const size_t end = src_.find("?>", pos_);
                if (end == std::string_view::npos) {
                    Err(DiagCode::Lexical, pos_, "unterminated processing instruction");
                    return nullptr;
                }
                pos_ = end + 2;
                continue;
            }
            if (Peek() == '<') {
                auto child = ParseElement(depth + 1);
                if (!child) return nullptr;
                node->children.push_back(std::move(child));
                continue;
            }
            const size_t textStart = pos_;
            while (!AtEnd() && Peek() != '<') ++pos_;
            const std::string_view rawText = src_.substr(textStart, pos_ - textStart);
            size_t badOffset = 0;
            std::string decoded;
            if (!DecodeEntities(rawText, decoded, badOffset, textStart)) {
                Err(DiagCode::Lexical, badOffset, "unknown or malformed entity");
                return nullptr;
            }
            textBuffer += decoded;
        }
    }

    bool ReadName(std::string& out) {
        out.clear();
        if (AtEnd() || !IsNameStart(Peek())) return false;
        const size_t start = pos_;
        while (!AtEnd() && IsNameChar(Peek())) ++pos_;
        out = std::string(src_.substr(start, pos_ - start));
        return true;
    }

    std::string_view src_;
    DiagnosticBag& diag_;
    SourceMap map_;
    size_t pos_ = 0;
};

// ---------------------------------------------------------------------------
// Markup extensions
// ---------------------------------------------------------------------------
struct MarkupArg {
    std::string name;
    std::string value;
};

struct MarkupExtension {
    std::string name;
    std::vector<MarkupArg> args;
    bool isLiteralEscape = false;
    std::string literalText;
    bool ok = true;
    std::string error;
};

// Finds the index of the '}' matching value[0] == '{', honoring quotes.
size_t FindClosingBrace(std::string_view value) {
    size_t depth = 0;
    bool sq = false;
    bool dq = false;
    for (size_t j = 0; j < value.size(); ++j) {
        const char c = value[j];
        if (sq) {
            if (c == '\'') sq = false;
            continue;
        }
        if (dq) {
            if (c == '"') dq = false;
            continue;
        }
        if (c == '\'') sq = true;
        else if (c == '"') dq = true;
        else if (c == '{') ++depth;
        else if (c == '}') {
            --depth;
            if (depth == 0) return j;
        }
    }
    return std::string_view::npos;
}

MarkupExtension ParseMarkup(std::string_view value) {
    MarkupExtension out;
    if (value.size() >= 2 && value[0] == '{' && value[1] == '}') {
        out.isLiteralEscape = true;
        out.literalText = std::string(value.substr(2));
        return out;
    }
    const size_t close = FindClosingBrace(value);
    if (close == std::string_view::npos) {
        out.ok = false;
        out.error = "unterminated markup extension (missing '}')";
        return out;
    }
    const std::string_view inner = value.substr(1, close - 1);
    size_t i = 0;
    while (i < inner.size() && IsSpace(inner[i])) ++i;
    const size_t nameStart = i;
    while (i < inner.size() && !IsSpace(inner[i]) && inner[i] != ',') ++i;
    out.name = std::string(inner.substr(nameStart, i - nameStart));
    if (out.name.empty()) {
        out.ok = false;
        out.error = "missing markup extension name";
        return out;
    }
    bool hasPositional = false;
    while (i < inner.size()) {
        while (i < inner.size() && (IsSpace(inner[i]) || inner[i] == ',')) ++i;
        if (i >= inner.size()) break;
        MarkupArg arg;
        const size_t argStart = i;
        size_t j = i;
        while (j < inner.size() && IsNameChar(inner[j])) ++j;
        size_t k = j;
        while (k < inner.size() && IsSpace(inner[k])) ++k;
        const bool named = (k < inner.size() && inner[k] == '=' && j > i);
        if (named) {
            arg.name = std::string(inner.substr(i, j - i));
            i = k + 1;
        } else {
            i = argStart;
        }
        while (i < inner.size() && IsSpace(inner[i])) ++i;
        if (i < inner.size() && (inner[i] == '\'' || inner[i] == '"')) {
            const char quote = inner[i++];
            const size_t start = i;
            while (i < inner.size() && inner[i] != quote) ++i;
            if (i >= inner.size()) {
                out.ok = false;
                out.error = "unterminated quoted argument in markup extension";
                return out;
            }
            arg.value = std::string(inner.substr(start, i - start));
            ++i;
        } else if (i < inner.size() && inner[i] == '{') {
            const size_t start = i;
            size_t depth = 0;
            for (; i < inner.size(); ++i) {
                if (inner[i] == '{') ++depth;
                else if (inner[i] == '}') {
                    --depth;
                    if (depth == 0) {
                        ++i;
                        break;
                    }
                }
            }
            arg.value = std::string(inner.substr(start, i - start));
        } else {
            const size_t start = i;
            while (i < inner.size() && inner[i] != ',' && !IsSpace(inner[i])) ++i;
            arg.value = std::string(inner.substr(start, i - start));
        }
        if (arg.name.empty()) {
            if (hasPositional) {
                out.ok = false;
                out.error = "only one positional argument is allowed";
                return out;
            }
            hasPositional = true;
        }
        out.args.push_back(std::move(arg));
    }
    return out;
}

std::vector<std::string> SplitPath(const std::string& path) {
    std::vector<std::string> out;
    size_t start = 0;
    for (;;) {
        const size_t dot = path.find('.', start);
        out.push_back(path.substr(start, dot == std::string::npos ? std::string::npos : dot - start));
        if (dot == std::string::npos) break;
        start = dot + 1;
    }
    return out;
}

bool IsValidPathSegment(const std::string& s) {
    if (s.empty()) return false;
    if (!IsNameStart(s[0])) return false;
    for (char c : s) {
        if (!(std::isalnum(static_cast<unsigned char>(c)) || c == '_')) return false;
    }
    return true;
}

BindingMode ParseModeName(std::string_view s, bool& ok) {
    ok = true;
    if (s == "OneTime") return BindingMode::OneTime;
    if (s == "OneWay") return BindingMode::OneWay;
    if (s == "TwoWay") return BindingMode::TwoWay;
    if (s == "OneWayToSource") return BindingMode::OneWayToSource;
    ok = false;
    return BindingMode::OneWay;
}

UpdateSourceTrigger ParseTriggerName(std::string_view s, bool& ok) {
    ok = true;
    if (s == "PropertyChanged") return UpdateSourceTrigger::PropertyChanged;
    if (s == "LostFocus") return UpdateSourceTrigger::LostFocus;
    ok = false;
    return UpdateSourceTrigger::PropertyChanged;
}

// ---------------------------------------------------------------------------
// Literal conversion
// ---------------------------------------------------------------------------
struct TokenName {
    const char* name;
    Token token;
};

const TokenName kTokens[] = {
    {"AppBackground", Token::AppBackground},
    {"CardBackground", Token::CardBackground},
    {"CardBorder", Token::CardBorder},
    {"TextPrimary", Token::TextPrimary},
    {"TextSecondary", Token::TextSecondary},
    {"TextDisabled", Token::TextDisabled},
    {"Accent", Token::Accent},
    {"TextOnAccent", Token::TextOnAccent},
    {"ControlFill", Token::ControlFill},
    {"ControlFillHover", Token::ControlFillHover},
    {"ControlFillPressed", Token::ControlFillPressed},
    {"Selection", Token::Selection},
    {"None", Token::None},
};

std::vector<std::string> TokenNames() {
    std::vector<std::string> out;
    for (const auto& t : kTokens) out.push_back(t.name);
    return out;
}

int HexDigit(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

bool ParseNumberLiteral(std::string_view s, double& out) {
    if (s.empty()) return false;
    const std::string tmp(s);
    char* end = nullptr;
    const double v = strtod(tmp.c_str(), &end);
    if (end == tmp.c_str()) return false;
    while (end && *end == ' ') ++end;
    if (end && *end != '\0') return false;
    out = v;
    return true;
}

bool ParseBoolLiteral(std::string_view s, bool& out) {
    if (s == "True" || s == "true" || s == "1") {
        out = true;
        return true;
    }
    if (s == "False" || s == "false" || s == "0") {
        out = false;
        return true;
    }
    return false;
}

bool ParseThicknessLiteral(std::string_view s, Thickness& out) {
    std::vector<double> parts;
    size_t start = 0;
    for (;;) {
        const size_t comma = s.find(',', start);
        const std::string_view piece =
            TrimAscii(s.substr(start, comma == std::string_view::npos ? std::string_view::npos : comma - start));
        double v = 0;
        if (!ParseNumberLiteral(piece, v)) return false;
        parts.push_back(v);
        if (comma == std::string_view::npos) break;
        start = comma + 1;
    }
    switch (parts.size()) {
    case 1:
        out = Thickness{static_cast<float>(parts[0]), static_cast<float>(parts[0]),
                        static_cast<float>(parts[0]), static_cast<float>(parts[0])};
        return true;
    case 2:
        out = Thickness{static_cast<float>(parts[0]), static_cast<float>(parts[1]),
                        static_cast<float>(parts[0]), static_cast<float>(parts[1])};
        return true;
    case 4:
        out = Thickness{static_cast<float>(parts[0]), static_cast<float>(parts[1]),
                        static_cast<float>(parts[2]), static_cast<float>(parts[3])};
        return true;
    default:
        return false;
    }
}

// ---------------------------------------------------------------------------
// Validator: raw tree -> NodeDesc
// ---------------------------------------------------------------------------
class Validator {
public:
    Validator(DiagnosticBag& diag, CompiledDocument& doc, const SourceMap& map)
        : diag_(diag), doc_(doc), map_(map) {}

    // Collects every <Style> found anywhere in the document into doc_.styles.
    //
    // Walked as a SEPARATE PASS before validation because a style may be declared
    // after the element that uses it -- markup reads top-down, but a style block at
    // the bottom of a page is perfectly reasonable, and requiring declaration before
    // use would be a rule with no other justification.
    void CollectStyles(const RawNode& raw) {
        for (const auto& child : raw.children) {
            if (child->tag == "Style" || child->tag == "wui/Page.Style" ||
                child->tag == "Page.Style") {
                CollectStyle(*child);
            } else if (child->tag == "ResourceDictionary") {
                CollectResourceDictionary(*child);
            }
            CollectStyles(*child);
        }
    }

    // <ResourceDictionary><Color x:Key="Brand">#0A84FF</Color> ... </ResourceDictionary>
    //
    // The element name only groups the declarations; the VALUE is whatever the
    // element body says. That keeps one syntax working for colours, numbers and
    // thicknesses without inventing a typed resource system nobody asked for.
    void CollectResourceDictionary(const RawNode& raw) {
        for (const auto& child : raw.children) {
            if (child->tag == "Style") {
                CollectStyle(*child); // styles may be nested in the dictionary
                continue;
            }
            ResourceValue res;
            res.offset = child->nameOffset;
            std::string text;
            for (const auto& attr : child->attrs) {
                if (attr.name == "x:Key" || attr.name == "Key") {
                    res.key = attr.value;
                } else if (attr.name == "Value") {
                    text = attr.value;
                } else if (attr.name == "xmlns" || attr.name.rfind("xmlns:", 0) == 0) {
                    continue;
                } else {
                    Error(DiagCode::BadAttribute, attr,
                          "resource '" + child->tag + "' has no attribute '" + attr.name +
                              "' (expected x:Key, Value, or text content)");
                }
            }
            // Text content wins when there is no Value attribute: <Color x:Key="Brand">
            // #0A84FF</Color> reads better than forcing it into an attribute.
            if (text.empty()) text = TrimCopy(child->text);
            if (res.key.empty()) {
                Error(DiagCode::MissingProperty, *child,
                      "a resource requires x:Key so markup can refer to it");
                continue;
            }
            if (text.empty()) {
                Error(DiagCode::MissingProperty, *child,
                      "resource '" + res.key + "' has no value");
                continue;
            }
            if (doc_.FindResource(res.key)) {
                Error(DiagCode::DuplicateStyle, *child,
                      "resource '" + res.key + "' is already defined");
                continue;
            }
            res.literal = text;
            doc_.resources.push_back(std::move(res));
        }
    }

    // Replaces every {StaticResource Key} inside a literal with its text.
    //
    // Substitution happens BEFORE type conversion, so a resource can stand in for a
    // colour, a number or a whole thickness ("{StaticResource PadMargin}"). Repeated
    // up to a small bound so a resource referring to another resource works, and a
    // cycle stops with a diagnostic instead of looping.
    bool ExpandResources(std::string& literal, size_t offset) {
        constexpr int kMaxDepth = 8;
        for (int pass = 0; pass < kMaxDepth; ++pass) {
            const size_t open = literal.find("{StaticResource ");
            if (open == std::string::npos) return true;
            const size_t close = literal.find('}', open);
            if (close == std::string::npos) {
                Report(DiagSeverity::Error, DiagCode::BadMarkupExtension, offset,
                       "unterminated {StaticResource ...} in '" + literal + "'");
                return false;
            }
            const std::string key =
                TrimCopy(literal.substr(open + 16, close - open - 16));
            const ResourceValue* res = doc_.FindResource(key);
            if (!res) {
                std::vector<std::string> keys;
                for (const auto& r : doc_.resources) keys.push_back(r.key);
                auto suggestions = SuggestNames(key, keys);
                std::string msg = "resource '" + key + "' is not defined";
                if (!suggestions.empty()) msg += " (did you mean '" + suggestions.front() + "'?)";
                else if (keys.empty()) msg += " (no <ResourceDictionary> values are declared)";
                Report(DiagSeverity::Error, DiagCode::MissingStyle, offset, msg);
                return false;
            }
            literal.replace(open, close - open + 1, res->literal);
        }
        Report(DiagSeverity::Error, DiagCode::StyleCycle, offset,
               "resource substitution did not settle in '" + literal +
                   "' -- do resources refer to each other in a cycle?");
        return false;
    }

    // Resolves BasedOn links and reports cycles / unknown keys. Runs after all
    // styles are collected, because a base style may be declared later too.
    void ResolveStyleChains() {
        auto& styles = doc_.pendingStyles;
        for (auto& style : styles) {
            if (style->basedOnKey.empty()) continue;
            const StyleDesc* base = doc_.FindStyle(style->basedOnKey);
            if (!base) {
                Report(DiagSeverity::Error, DiagCode::MissingStyle, style->offset,
                       "BasedOn style '" + style->basedOnKey + "' is not defined" +
                           SuggestStyleHint(style->basedOnKey));
                continue;
            }
            if (base == style.get()) {
                Report(DiagSeverity::Error, DiagCode::StyleCycle, style->offset,
                       "style '" + style->key + "' cannot derive from itself");
                continue;
            }
            // A cycle is detected by walking to the root: a chain is at most
            // styles.size() long, so a longer walk must have looped.
            bool cyclic = false;
            const StyleDesc* walk = base;
            size_t guard = 0;
            while (walk && guard++ <= styles.size()) {
                if (walk == style.get()) {
                    cyclic = true;
                    break;
                }
                walk = walk->basedOn;
            }
            if (cyclic) {
                Report(DiagSeverity::Error, DiagCode::StyleCycle, style->offset,
                       "BasedOn chain for style '" + style->key + "' is circular");
                continue;
            }
            if (base->targetType != style->targetType) {
                Report(DiagSeverity::Error, DiagCode::StyleTargetMismatch, style->offset,
                       "style '" + style->key + "' targets " + style->targetType +
                           " but its BasedOn style targets " + base->targetType);
                continue;
            }
            style->basedOn = base;
        }
    }

    std::shared_ptr<NodeDesc> Validate(const RawNode& raw, const std::vector<std::string>& nsAliases) {
        auto node = std::make_shared<NodeDesc>();
        node->offset = raw.nameOffset;
        ++doc_.nodeCount;

        // Resolve the type.
        std::string prefix;
        std::string localName;
        SplitQualified(raw.tag, prefix, localName);
        if (!prefix.empty() && prefix != "wui" && std::find(nsAliases.begin(), nsAliases.end(), prefix) == nsAliases.end() &&
            prefix != "x") {
            Error(DiagCode::UnknownElement, raw, "unknown namespace prefix '" + prefix + "'");
            return node;
        }
        const std::string qualified = (prefix.empty() ? "wui/" : prefix + "/") + localName;
        const TypeInfo* type = (prefix == "x") ? nullptr : TypeRegistry::Instance().Find(qualified);
        if (!type) {
            std::vector<std::string> names = TypeRegistry::Instance().AllNames();
            for (auto& n : names) n = n.substr(n.find('/') + 1);
            auto suggestions = SuggestNames(localName, names);
            std::string msg = "'" + raw.tag + "' is not a known WAML element";
            if (!suggestions.empty()) msg += " (did you mean '" + suggestions.front() + "'?)";
            Error(DiagCode::UnknownElement, raw, msg);
            return node;
        }
        node->type = type;
        node->typeName = qualified;

        // Attributes
        for (const auto& attr : raw.attrs) {
            std::string ap;
            std::string al;
            SplitQualified(attr.name, ap, al);
            if (attr.name == "xmlns") continue;
            if (ap == "xmlns") continue;
            if (ap == "x") {
                if (al == "Name") {
                    if (!node->name.empty()) {
                        Warn(DiagCode::DuplicateName, attr, "duplicate x:Name '" + node->name + "'");
                    }
                    node->name = attr.value;
                } else {
                    Error(DiagCode::UnknownProperty, attr, "unknown directive 'x:" + al + "'");
                }
                continue;
            }
            // Style is resolved before the property table: it is not a slot on any
            // type, it is a reference to a StyleDesc. Having both a slot and a style
            // pointer would be two sources of truth for the same thing.
            if (attr.name == "Style") {
                std::string key = attr.value;
                const std::string prefix = "{StaticResource ";
                if (key.size() > prefix.size() + 1 && key.compare(0, prefix.size(), prefix) == 0 &&
                    key.back() == '}') {
                    key = key.substr(prefix.size(), key.size() - prefix.size() - 1);
                }
                const StyleDesc* style = doc_.FindStyle(key);
                if (!style) {
                    std::vector<std::string> keys;
                    CollectStyleKeys(keys);
                    auto suggestions = SuggestNames(key, keys);
                    std::string msg = "style '" + key + "' is not defined";
                    if (!suggestions.empty()) {
                        msg += " (did you mean '" + suggestions.front() + "'?)";
                    } else if (keys.empty()) {
                        msg += " (no <Style> elements are declared in this document)";
                    }
                    Error(DiagCode::MissingStyle, attr, msg);
                    continue;
                }
                if (!style->AppliesTo(node->typeName)) {
                    Error(DiagCode::StyleTargetMismatch, attr,
                          "style '" + key + "' targets " + style->targetType +
                              " and cannot be applied to " + node->typeName);
                    continue;
                }
                node->style = style;
                node->styleOwner = doc_.styles;
                continue;
            }
            const PropertyDescriptor* desc = type->FindProperty(attr.name);
            if (!desc) {
                auto suggestions = SuggestNames(attr.name, type->PropertyNames());
                std::string msg = "'" + type->Name() + "' has no property '" + attr.name + "'";
                if (!suggestions.empty()) msg += " (did you mean '" + suggestions.front() + "'?)";
                Error(DiagCode::UnknownProperty, attr, msg);
                continue;
            }
            if (!desc->writableInWaml) {
                Error(DiagCode::ReadOnlyProperty, attr, "property '" + attr.name + "' is read-only");
                continue;
            }
            PropertyEntry entry;
            entry.name = attr.name;
            entry.slot = desc->slot;
            entry.offset = attr.offset;
            // {StaticResource Key} is SUBSTITUTED FIRST, before any binding
            // parsing. It is not a markup extension that survives into the document:
            // the literal is rewritten and then converted like any other literal.
            // Handling it earlier is what lets it stand in for a colour, a number or
            // a thickness in the same attribute slot.
            std::string literal = attr.value;
            if (!ExpandResources(literal, attr.offset)) continue;
            if (!literal.empty() && literal[0] == '{') {
                BindingExpr expr;
                std::string mkError;
                if (!ParseBindingExpr(literal, *desc, attr, expr, mkError)) {
                    Error(DiagCode::BadMarkupExtension, attr, mkError);
                    continue;
                }
                if (expr.isBinding) {
                    entry.binding = expr;
                    ++doc_.bindingCount;
                } else {
                    entry.value = expr.fallback; // literal escape
                }
            } else {
                Value v;
                std::string err;
                std::vector<std::string> suggestions;
                if (!ConvertLiteral(literal, *desc, v, err, &suggestions)) {
                    std::string msg = err;
                    if (!suggestions.empty()) msg += " (did you mean '" + suggestions.front() + "'?)";
                    Error(DiagCode::BadValue, attr, msg);
                    continue;
                }
                entry.value = v;
            }
            ++doc_.propertyCount;
            node->properties.push_back(std::move(entry));
        }

        // Children, including property elements (<Type.Prop>).
        const ContentModel model = type->Model();
        size_t logicalChildren = 0;
        for (const auto& childRaw : raw.children) {
            const size_t dot = childRaw->tag.find('.');
            if (dot != std::string::npos) {
                HandlePropertyElement(*childRaw, *node, *type, nsAliases);
                continue;
            }
            // <Style> is a RESOURCE, not a child. It is collected in its own pass
            // and reserves no space, so it must not count toward "at most one child"
            // or be validated as a widget -- otherwise declaring a style inside a
            // Border (which holds a single child) would be a load error.
            if (childRaw->tag == "Style" || childRaw->tag == "ResourceDictionary") continue;
            ++logicalChildren;
        }

        // Text content.
        const std::string text = TrimCopy(raw.text);
        if (!text.empty()) {
            if (model != ContentModel::TextContent) {
                Error(DiagCode::BadContent, raw,
                      "'" + raw.tag + "' does not accept text content" +
                          (type->ContentProperty().empty()
                               ? ""
                               : " (content property: " + type->ContentProperty() + ")"));
            } else {
                Value v = Value::Text(Utf8ToWide(text));
                const PropertyDescriptor* desc = type->FindProperty(type->ContentProperty());
                if (desc) {
                    PropertyEntry entry;
                    entry.name = type->ContentProperty();
                    entry.slot = desc->slot;
                    entry.value = v;
                    entry.offset = raw.offset;
                    // Text content never overrides an explicit attribute.
                    bool exists = false;
                    for (const auto& p : node->properties)
                        if (p.name == entry.name) exists = true;
                    if (!exists) {
                        node->properties.push_back(std::move(entry));
                        node->inlineText = Utf8ToWide(text);
                        ++doc_.propertyCount;
                    }
                }
            }
        }

        if (model == ContentModel::SingleChild && logicalChildren > 1) {
            Error(DiagCode::BadContent, raw,
                  "'" + raw.tag + "' accepts at most one child element (found " +
                      std::to_string(logicalChildren) + ")");
        }
        if (model == ContentModel::SingleTemplate) {
            if (logicalChildren != 1) {
                Error(DiagCode::BadTemplate, raw,
                      "'DataTemplate' needs exactly one root element (found " +
                          std::to_string(logicalChildren) + ")");
            } else {
                node->templateRoot =
                    Validate(*raw.children[0], nsAliases);
                node->children.clear();
            }
        }

        if (model != ContentModel::SingleTemplate) {
            for (const auto& childRaw : raw.children) {
                if (childRaw->tag.find('.') != std::string::npos) continue;
                // Styles were collected already; they are not widgets.
                if (childRaw->tag == "Style" || childRaw->tag == "ResourceDictionary") continue;
                auto child = Validate(*childRaw, nsAliases);
                if (child) node->children.push_back(std::move(child));
            }
        }

        if (!type->ContentProperty().empty() && model == ContentModel::SingleChild && node->children.empty() &&
            node->properties.empty()) {
            // Nothing to report: an empty Border is legal.
        }
        return node;
    }

private:
    static void SplitQualified(const std::string& name, std::string& prefix, std::string& local) {
        const size_t colon = name.find(':');
        if (colon == std::string::npos) {
            prefix.clear();
            local = name;
        } else {
            prefix = name.substr(0, colon);
            local = name.substr(colon + 1);
        }
    }

    // Resolve a byte offset to line/column at report time only.
    void Report(DiagSeverity severity, DiagCode code, size_t offset, std::string message) {
        int line = 1;
        int column = 1;
        map_.Locate(offset, line, column);
        if (severity == DiagSeverity::Error) diag_.Error(code, line, column, std::move(message));
        else diag_.Warning(code, line, column, std::move(message));
    }

    void Error(DiagCode code, const RawNode& node, std::string message) {
        Report(DiagSeverity::Error, code, node.nameOffset, std::move(message));
    }
    void Error(DiagCode code, const RawAttr& attr, std::string message) {
        Report(DiagSeverity::Error, code, attr.offset, std::move(message));
    }
    void Warn(DiagCode code, const RawAttr& attr, std::string message) {
        Report(DiagSeverity::Warning, code, attr.offset, std::move(message));
    }

    // Reads one <Style x:Key="..." TargetType="..." BasedOn="..."> block.
    void CollectStyle(const RawNode& raw) {
        auto style = std::make_shared<StyleDesc>();
        style->offset = raw.nameOffset;

        std::string targetLocal;
        for (const auto& attr : raw.attrs) {
            if (attr.name == "x:Key" || attr.name == "Key") {
                style->key = attr.value;
            } else if (attr.name == "TargetType") {
                targetLocal = attr.value;
            } else if (attr.name == "BasedOn") {
                style->basedOnKey = attr.value;
                // BasedOn="{StaticResource Base}" and BasedOn="Base" mean the same
                // thing; accepting both keeps the markup readable either way.
                const std::string prefix = "{StaticResource ";
                if (style->basedOnKey.size() > prefix.size() + 1 &&
                    style->basedOnKey.compare(0, prefix.size(), prefix) == 0 &&
                    style->basedOnKey.back() == '}') {
                    style->basedOnKey =
                        style->basedOnKey.substr(prefix.size(),
                                                 style->basedOnKey.size() - prefix.size() - 1);
                }
            } else if (attr.name == "xmlns" || attr.name.rfind("xmlns:", 0) == 0) {
                // namespace declarations are not properties
            } else {
                Error(DiagCode::BadAttribute, attr,
                      "'Style' has no attribute '" + attr.name +
                          "' (expected x:Key, TargetType, BasedOn)");
            }
        }

        if (style->key.empty()) {
            Error(DiagCode::MissingProperty, raw,
                  "'Style' requires x:Key so elements can refer to it");
            return;
        }
        if (targetLocal.empty()) {
            Error(DiagCode::MissingProperty, raw,
                  "style '" + style->key + "' requires TargetType");
            return;
        }
        // A style key must be unique: two definitions of the same key means
        // whichever was found first silently wins, which is undebuggable.
        if (doc_.FindStyle(style->key)) {
            Error(DiagCode::DuplicateStyle, raw,
                  "style '" + style->key + "' is already defined");
            return;
        }

        const std::string qualified = "wui/" + targetLocal;
        const TypeInfo* target = TypeRegistry::Instance().Find(qualified);
        if (!target) {
            std::vector<std::string> names = TypeRegistry::Instance().AllNames();
            for (auto& n : names) n = n.substr(n.find('/') + 1);
            auto suggestions = SuggestNames(targetLocal, names);
            std::string msg = "'" + targetLocal + "' is not a known WAML element";
            if (!suggestions.empty()) msg += " (did you mean '" + suggestions.front() + "'?)";
            Error(DiagCode::UnknownElement, raw, "TargetType: " + msg);
            return;
        }
        style->targetType = qualified;
        style->type = target;

        // Setters, then triggers. Each is validated against the target type
        // immediately: a Setter naming a property the type does not have is a load
        // error, not a silent no-op. That is the whole reason resolution happens now
        // rather than at paint time.
        for (const auto& child : raw.children) {
            if (child->tag == "Setter") {
                CollectSetter(*child, *style, *target);
            } else if (child->tag == "Trigger") {
                CollectTrigger(*child, *style, *target);
            } else {
                Error(DiagCode::BadContent, *child,
                      "'Style' accepts only <Setter> and <Trigger> children (found '" +
                          child->tag + "')");
            }
        }

        doc_.pendingStyles.push_back(std::move(style));
        ++doc_.styleCount;
    }

    void CollectSetter(const RawNode& raw, StyleDesc& style, const TypeInfo& target) {
        CollectSetterInto(raw, style.setters, target);
    }

    void CollectSetter(const RawNode& raw, StyleTrigger& trigger, const TypeInfo& target) {
        CollectSetterInto(raw, trigger.setters, target);
    }

    // One implementation for both containers: a Setter inside a <Style> and a Setter
    // inside a <Trigger> are the same syntax with the same validation, and having two
    // copies would be a way for them to drift.
    bool CollectSetterInto(const RawNode& raw, std::vector<StyleSetter>& out,
                           const TypeInfo& target) {
        StyleSetter setter;
        setter.offset = raw.nameOffset;
        std::string literal;
        bool hasLiteral = false;

        for (const auto& attr : raw.attrs) {
            if (attr.name == "Property") {
                setter.propertyName = attr.value;
            } else if (attr.name == "Value") {
                literal = attr.value;
                hasLiteral = true;
            } else if (attr.name == "xmlns" || attr.name.rfind("xmlns:", 0) == 0) {
                continue;
            } else {
                Error(DiagCode::BadAttribute, attr,
                      "'Setter' has no attribute '" + attr.name +
                          "' (expected Property, Value)");
            }
        }

        if (setter.propertyName.empty()) {
            Error(DiagCode::MissingProperty, raw, "'Setter' requires Property");
            return false;
        }
        if (!hasLiteral) {
            Error(DiagCode::MissingProperty, raw,
                  "setter for '" + setter.propertyName + "' requires Value");
            return false;
        }

        const PropertyDescriptor* desc = target.FindProperty(setter.propertyName);
        if (!desc) {
            auto suggestions = SuggestNames(setter.propertyName, target.PropertyNames());
            std::string msg = "'" + target.Name() + "' has no property '" +
                              setter.propertyName + "'";
            if (!suggestions.empty()) msg += " (did you mean '" + suggestions.front() + "'?)";
            Error(DiagCode::UnknownProperty, raw, msg);
            return false;
        }
        if (!desc->writableInWaml) {
            Error(DiagCode::ReadOnlyProperty, raw,
                  "property '" + setter.propertyName + "' is read-only and cannot be styled");
            return false;
        }
        // Expand BEFORE the "is this a binding" test. A setter whose value is a
        // resource is a literal once substituted, so it must not be mistaken for a
        // binding -- and a genuine binding is still refused, which is the honest
        // answer because a style holds one literal for every user of it.
        if (!ExpandResources(literal, raw.nameOffset)) return false;
        if (!literal.empty() && literal[0] == '{') {
            Error(DiagCode::BadValue, raw,
                  "setter for '" + setter.propertyName +
                      "' must be a literal; a binding in a style is not supported");
            return false;
        }
        Value v;
        std::string err;
        std::vector<std::string> suggestions;
        if (!ConvertLiteral(literal, *desc, v, err, &suggestions)) {
            std::string msg = err;
            if (!suggestions.empty()) msg += " (did you mean '" + suggestions.front() + "'?)";
            Error(DiagCode::BadValue, raw, msg);
            return false;
        }
        setter.slot = desc->slot;
        setter.value = std::move(v);
        out.push_back(std::move(setter));
        return true;
    }

    // Style keys, for "did you mean" suggestions. Reads BOTH containers because
    // this runs during validation, by which time the collect pass has already moved
    // its results into doc_.styles -- consulting only pendingStyles made every
    // suggestion empty, which reads as "you get no help" rather than a bug.
    void CollectStyleKeys(std::vector<std::string>& keys) const {
        for (const auto& s : doc_.pendingStyles) keys.push_back(s->key);
        if (doc_.styles) {
            for (const auto& s : *doc_.styles) keys.push_back(s->key);
        }
    }

    // <Trigger Property="IsChecked" Value="True">
    //   <Setter Property="Background" Value="{StaticResource CheckedTint}"/>
    // </Trigger>
    //
    // The watched property and the "when" value are validated here exactly like a
    // Setter's, because a trigger watching a property that does not exist would
    // otherwise be permanently inactive with no explanation -- the same silent
    // failure a mistyped Setter would have.
    void CollectTrigger(const RawNode& raw, StyleDesc& style, const TypeInfo& target) {
        StyleTrigger trigger;
        trigger.offset = raw.nameOffset;
        std::string propertyName;
        std::string literal;
        bool hasValue = false;

        for (const auto& attr : raw.attrs) {
            if (attr.name == "Property") {
                propertyName = attr.value;
            } else if (attr.name == "Value") {
                literal = attr.value;
                hasValue = true;
            } else if (attr.name == "xmlns" || attr.name.rfind("xmlns:", 0) == 0) {
                continue;
            } else {
                Error(DiagCode::BadAttribute, attr,
                      "'Trigger' has no attribute '" + attr.name +
                          "' (expected Property, Value)");
            }
        }

        if (propertyName.empty()) {
            Error(DiagCode::MissingProperty, raw, "'Trigger' requires Property");
            return;
        }
        if (!hasValue) {
            Error(DiagCode::MissingProperty, raw,
                  "trigger on '" + propertyName + "' requires Value");
            return;
        }
        const PropertyDescriptor* watched = target.FindProperty(propertyName);
        if (!watched) {
            auto suggestions = SuggestNames(propertyName, target.PropertyNames());
            std::string msg = "'" + target.Name() + "' has no property '" + propertyName +
                              "' to trigger on";
            if (!suggestions.empty()) msg += " (did you mean '" + suggestions.front() + "'?)";
            Error(DiagCode::UnknownProperty, raw, msg);
            return;
        }
        if (watched->type == VType::Ref) {
            Error(DiagCode::BadValue, raw,
                  "trigger on '" + propertyName +
                      "': object-valued properties cannot be compared by value");
            return;
        }
        if (!ExpandResources(literal, raw.nameOffset)) return;
        Value when;
        std::string err;
        std::vector<std::string> suggestions;
        if (!ConvertLiteral(literal, *watched, when, err, &suggestions)) {
            std::string msg = err;
            if (!suggestions.empty()) msg += " (did you mean '" + suggestions.front() + "'?)";
            Error(DiagCode::BadValue, raw, msg);
            return;
        }
        trigger.watchedName = propertyName;
        trigger.watchedSlot = watched->slot;
        trigger.when = std::move(when);

        for (const auto& child : raw.children) {
            if (child->tag != "Setter") {
                Error(DiagCode::BadContent, *child,
                      "'Trigger' accepts only <Setter> children (found '" + child->tag + "')");
                continue;
            }
            CollectSetter(*child, trigger, target);
        }
        if (trigger.setters.empty()) {
            // A trigger with no setters does nothing. Reporting it beats letting the
            // author wonder why their condition has no effect.
            Error(DiagCode::MissingProperty, raw,
                  "trigger on '" + propertyName + "' has no <Setter> children");
            return;
        }
        style.triggers.push_back(std::move(trigger));
    }

    std::string SuggestStyleHint(const std::string& key) const {
        std::vector<std::string> keys;
        CollectStyleKeys(keys);
        auto suggestions = SuggestNames(key, keys);
        return suggestions.empty() ? std::string() : " (did you mean '" + suggestions.front() + "'?)";
    }

    // <ListBox.ItemTemplate> <DataTemplate> ... </DataTemplate> </ListBox.ItemTemplate>
    void HandlePropertyElement(const RawNode& raw, NodeDesc& parent, const TypeInfo& parentType,
                               const std::vector<std::string>& nsAliases) {
        const size_t dot = raw.tag.find('.');
        const std::string typePart = raw.tag.substr(0, dot);
        const std::string propPart = raw.tag.substr(dot + 1);
        std::string local;
        std::string prefix;
        SplitQualified(typePart, prefix, local);
        if (!local.empty() && local != parentType.Name().substr(parentType.Name().find('/') + 1)) {
            Error(DiagCode::BadContent, raw,
                  "property element '" + raw.tag + "' does not match parent type '" +
                      parentType.Name() + "'");
            return;
        }
        const PropertyDescriptor* desc = parentType.FindProperty(propPart);
        if (!desc) {
            auto suggestions = SuggestNames(propPart, parentType.PropertyNames());
            std::string msg = "'" + parentType.Name() + "' has no property '" + propPart + "'";
            if (!suggestions.empty()) msg += " (did you mean '" + suggestions.front() + "'?)";
            Error(DiagCode::UnknownProperty, raw, msg);
            return;
        }
        if (raw.children.size() != 1) {
            Error(DiagCode::BadContent, raw,
                  "property element '" + raw.tag + "' must contain exactly one element");
            return;
        }
        const RawNode& valueNode = *raw.children[0];
        if (desc->type != VType::Ref) {
            Error(DiagCode::BadValue, raw,
                  "property element syntax is only valid for object-valued properties ('" + propPart +
                      "')");
            return;
        }
        // v0.5: the only object property with a property-element form is the
        // DataTemplate payload.
        if (valueNode.tag != "DataTemplate") {
            Error(DiagCode::BadContent, valueNode,
                  "property element '" + raw.tag + "' expects a <DataTemplate> child");
            return;
        }
        auto tmpl = std::make_shared<NodeDesc>();
        tmpl->type = TypeRegistry::Instance().Find("wui/DataTemplate");
        tmpl->typeName = "wui/DataTemplate";
        tmpl->offset = valueNode.nameOffset;
        if (valueNode.children.size() != 1) {
            Error(DiagCode::BadTemplate, valueNode,
                  "'DataTemplate' needs exactly one root element (found " +
                      std::to_string(valueNode.children.size()) + ")");
            return;
        }
        tmpl->templateRoot = Validate(*valueNode.children[0], nsAliases);
        ++doc_.nodeCount;

        PropertyEntry entry;
        entry.name = propPart;
        entry.slot = desc->slot;
        entry.offset = raw.offset;
        entry.templateNode = std::move(tmpl);
        ++doc_.propertyCount;
        // Later assignment wins (last property element for the same name).
        for (auto& p : parent.properties) {
            if (p.name == entry.name) {
                p = std::move(entry);
                return;
            }
        }
        parent.properties.push_back(std::move(entry));
    }

    // Parses "{Binding ...}" into a BindingExpr. Returns false with `error` set.
    bool ParseBindingExpr(const std::string& rawValue, const PropertyDescriptor& desc, const RawAttr& attr,
                          BindingExpr& out, std::string& error) {
        out.offset = attr.offset;
        MarkupExtension mk = ParseMarkup(rawValue);
        if (!mk.ok) {
            error = mk.error;
            return false;
        }
        if (mk.isLiteralEscape) {
            Value v;
            std::string err;
            if (!ConvertLiteral(mk.literalText, desc, v, err, nullptr)) {
                error = "escaped literal is not a valid " + std::string(VTypeName(desc.type)) + ": " + err;
                return false;
            }
            out.isBinding = false;
            out.fallback = v;
            return true;
        }
        if (mk.name != "Binding") {
            if (mk.name == "x:Null") {
                Value v;
                if (desc.type == VType::Ref) {
                    v = Value::Null();
                } else if (!ConvertValue(Value::Null(), desc.type, v)) {
                    error = "{x:Null} cannot be applied to a " + std::string(VTypeName(desc.type)) + " property";
                    return false;
                }
                out.isBinding = false;
                out.fallback = v;
                return true;
            }
            error = "unsupported markup extension '" + mk.name + "'";
            return false;
        }

        out.isBinding = true;
        std::string path;
        bool modeSet = false;
        bool triggerSet = false;
        for (const auto& arg : mk.args) {
            if (arg.name.empty()) {
                path = arg.value;
                continue;
            }
            if (arg.name == "Path") {
                path = arg.value;
            } else if (arg.name == "Mode") {
                bool ok = false;
                out.mode = ParseModeName(arg.value, ok);
                if (!ok) {
                    error = "invalid Mode '" + arg.value +
                            "' (expected OneTime, OneWay, TwoWay or OneWayToSource)";
                    return false;
                }
                out.modeExplicit = true;
                modeSet = true;
            } else if (arg.name == "UpdateSourceTrigger") {
                bool ok = false;
                out.trigger = ParseTriggerName(arg.value, ok);
                if (!ok) {
                    error = "invalid UpdateSourceTrigger '" + arg.value +
                            "' (expected PropertyChanged or LostFocus)";
                    return false;
                }
                triggerSet = true;
            } else if (arg.name == "StringFormat") {
                out.stringFormat = Utf8ToWide(arg.value);
            } else if (arg.name == "FallbackValue") {
                Value v;
                std::string err;
                if (!ConvertLiteral(arg.value, desc, v, err, nullptr)) {
                    error = "invalid FallbackValue: " + err;
                    return false;
                }
                out.fallback = v;
                out.hasFallback = true;
            } else if (arg.name == "Converter") {
                // Validated here rather than at apply time so a typo is a load
                // error with a suggestion, not a binding that silently does
                // nothing at runtime.
                if (!ConverterRegistry::Instance().Has(arg.value)) {
                    error = "unknown converter '" + arg.value + "'";
                    const auto known = ConverterRegistry::Instance().Names();
                    if (!known.empty()) {
                        auto suggestions = SuggestNames(arg.value, known);
                        if (!suggestions.empty()) error += " (did you mean '" + suggestions.front() + "'?)";
                    }
                    return false;
                }
                out.converter = arg.value;
            } else if (arg.name == "Source") {
                error = "Source= is not supported in v0.5 (use DataContext inheritance)";
                return false;
            } else {
                auto suggestions = SuggestNames(arg.name, {"Path", "Mode", "UpdateSourceTrigger",
                                                           "StringFormat", "FallbackValue", "Converter"});
                error = "unknown binding argument '" + arg.name + "'";
                if (!suggestions.empty()) error += " (did you mean '" + suggestions.front() + "'?)";
                return false;
            }
        }
        if (!modeSet) out.mode = desc.defaultMode;
        if (path.empty()) {
            error = "binding path is empty (v0.5 does not support '{Binding}' on the data context itself)";
            return false;
        }
        if (path.find('[') != std::string::npos) {
            error = "indexers are not supported in v0.5 binding paths";
            return false;
        }
        auto segments = SplitPath(path);
        for (const auto& seg : segments) {
            if (!IsValidPathSegment(seg)) {
                error = "invalid binding path '" + path + "' (segment '" + seg + "')";
                return false;
            }
        }
        out.path = path;
        if (out.mode == BindingMode::TwoWay && !desc.writableInWaml) {
            error = "TwoWay binding requires a writable target property ('" + desc.name + "')";
            return false;
        }
        if (triggerSet && out.mode != BindingMode::TwoWay && out.mode != BindingMode::OneWayToSource) {
            error = "UpdateSourceTrigger is only meaningful for TwoWay or OneWayToSource bindings";
            return false;
        }
        (void)segments;
        return true;
    }

    DiagnosticBag& diag_;
    CompiledDocument& doc_;
    const SourceMap& map_;
};

} // namespace

// ---------------------------------------------------------------------------
// Public: literal conversion
// ---------------------------------------------------------------------------
bool ParseColorLiteral(std::string_view text, Color& out, std::string* suggestion) {
    if (text.empty()) return false;
    if (text[0] == '#') {
        std::string_view hex = text.substr(1);
        std::vector<int> digits;
        for (char c : hex) {
            const int d = HexDigit(c);
            if (d < 0) return false;
            digits.push_back(d);
        }
        int r = 0;
        int g = 0;
        int b = 0;
        int a = 0xFF;
        switch (digits.size()) {
        case 3:
            r = digits[0] * 17;
            g = digits[1] * 17;
            b = digits[2] * 17;
            break;
        case 4:
            a = digits[0] * 17;
            r = digits[1] * 17;
            g = digits[2] * 17;
            b = digits[3] * 17;
            break;
        case 6:
            r = digits[0] * 16 + digits[1];
            g = digits[2] * 16 + digits[3];
            b = digits[4] * 16 + digits[5];
            break;
        case 8:
            a = digits[0] * 16 + digits[1];
            r = digits[2] * 16 + digits[3];
            g = digits[4] * 16 + digits[5];
            b = digits[6] * 16 + digits[7];
            break;
        default:
            return false;
        }
        out = Color{static_cast<float>(r) / 255.0f, static_cast<float>(g) / 255.0f,
                    static_cast<float>(b) / 255.0f, static_cast<float>(a) / 255.0f, Token::None};
        return true;
    }
    for (const auto& t : kTokens) {
        if (t.name == text) {
            out = Color{};
            out.token = t.token;
            return true;
        }
    }
    if (suggestion) {
        auto suggestions = SuggestNames(std::string(text), TokenNames());
        if (!suggestions.empty()) *suggestion = suggestions.front();
    }
    return false;
}

bool ConvertLiteral(std::string_view literal, const PropertyDescriptor& desc, Value& out,
                    std::string& error, std::vector<std::string>* suggestions) {
    const std::string text = TrimCopy(literal);
    switch (desc.type) {
    case VType::Null:
        out = Value::Null();
        return true;
    case VType::Text:
        out = Value::Text(Utf8ToWide(literal));
        return true;
    case VType::Number: {
        double v = 0;
        if (!ParseNumberLiteral(text, v)) {
            error = "'" + text + "' is not a valid number";
            return false;
        }
        out = Value::Number(v);
        return true;
    }
    case VType::Bool: {
        bool v = false;
        if (!ParseBoolLiteral(text, v)) {
            error = "'" + text + "' is not a valid boolean (expected True or False)";
            return false;
        }
        out = Value::Bool(v);
        return true;
    }
    case VType::Color: {
        Color c;
        std::string suggestion;
        if (!ParseColorLiteral(text, c, &suggestion)) {
            error = "'" + text + "' is not a valid color or theme token";
            if (!suggestion.empty() && suggestions) suggestions->push_back(suggestion);
            return false;
        }
        out = Value::MakeColor(c);
        return true;
    }
    case VType::Thickness: {
        Thickness t;
        if (!ParseThicknessLiteral(text, t)) {
            error = "'" + text + "' is not a valid Thickness (1, 2 or 4 numbers)";
            return false;
        }
        out = Value::MakeThickness(t);
        return true;
    }
    case VType::Enum: {
        const auto& names = desc.enumNames;
        for (size_t i = 0; i < names.size(); ++i) {
            if (names[i] == text) {
                out = Value::Enum(static_cast<int64_t>(i));
                return true;
            }
        }
        error = "'" + text + "' is not a valid value for " + desc.name;
        if (suggestions) {
            auto s = SuggestNames(text, names);
            if (!s.empty()) suggestions->push_back(s.front());
        }
        return false;
    }
    case VType::Ref:
        error = "'" + desc.name + "' takes an object (use {Binding}) and cannot be set from a literal";
        return false;
    }
    return false;
}

// ---------------------------------------------------------------------------
// Public: parse + validate
// ---------------------------------------------------------------------------
std::unique_ptr<CompiledDocument> ParseWaml(std::string_view source, const ParseOptions& options,
                                            DiagnosticBag& diagnostics) {
    diagnostics.SetFile(options.fileName);
    Parser parser(source, diagnostics);
    std::unique_ptr<RawNode> raw = parser.ParseDocument();
    if (!raw || diagnostics.HasErrors()) return nullptr;

    auto doc = std::make_unique<CompiledDocument>();
    doc->file = options.fileName;
    const SourceMap map(source);
    Validator validator(diagnostics, *doc, map);
    // Three passes, in this order:
    //   1. collect every <Style> BEFORE validating elements, so an element can
    //      refer to a style declared later in the file;
    //   2. resolve BasedOn BEFORE validating elements too, so NodeDesc::style
    //      points at a chain whose links are already filled in -- resolving
    //      afterwards would leave every node holding a half-built chain;
    //   3. validate the element tree, which needs FindStyle to be complete.
    validator.CollectStyles(*raw);
    validator.ResolveStyleChains();
    // Freeze the collected styles into the document. Ownership moves to the
    // shared_ptr so NodeDesc::style pointers stay valid for the document's whole
    // life, including inside template instances that outlive this function.
    auto styles = std::make_shared<std::vector<std::shared_ptr<StyleDesc>>>();
    styles->swap(doc->pendingStyles);
    doc->styles = styles;
    // Resources expand while the tree is validated, so they must all be collected
    // by now -- including any declared at the bottom of the file, which is why the
    // collect pass above covers the whole tree rather than stopping at the first
    // widget.
    doc->root = validator.Validate(*raw, {});
    if (diagnostics.HasErrors()) return nullptr;
    return doc;
}

std::unique_ptr<CompiledDocument> CompileWaml(std::string_view source, const ParseOptions& options,
                                              DiagnosticBag& diagnostics) {
    return ParseWaml(source, options, diagnostics);
}

// ---------------------------------------------------------------------------
// Public: instantiation
// ---------------------------------------------------------------------------
namespace {

// Deep clone used for templates so that instances never depend on the
// lifetime of the CompiledDocument that produced them.
std::shared_ptr<NodeDesc> CloneTree(const NodeDesc& src) {
    auto copy = std::make_shared<NodeDesc>();
    copy->typeName = src.typeName;
    copy->type = src.type;
    copy->name = src.name;
    copy->properties = src.properties;
    copy->inlineText = src.inlineText;
    copy->offset = src.offset;
    // The style pointer is shared, not cloned: StyleDesc lives in the
    // CompiledDocument, which outlives every template instance. Dropping it here was
    // the difference between a styled list row and an unstyled one.
    copy->style = src.style;
    copy->styleOwner = src.styleOwner;
    if (src.templateRoot) copy->templateRoot = CloneTree(*src.templateRoot);
    for (const auto& c : src.children) copy->children.push_back(CloneTree(*c));
    for (auto& p : copy->properties) {
        if (p.templateNode) p.templateNode = CloneTree(*p.templateNode);
    }
    return copy;
}

std::shared_ptr<DataTemplate> BuildTemplate(const NodeDesc& tmplNode, ITextService* text) {
    auto tmpl = std::make_shared<DataTemplate>();
    tmpl->origin = "template@" + std::to_string(tmplNode.offset);
    if (!tmplNode.templateRoot) return tmpl;
    auto rootCopy = CloneTree(*tmplNode.templateRoot);
    tmpl->instantiate = [rootCopy, text]() -> WidgetPtr { return InstantiateNode(*rootCopy, text); };
    return tmpl;
}

} // namespace

// ---------------------------------------------------------------------------
// StyleDesc
// ---------------------------------------------------------------------------
bool StyleDesc::AppliesTo(const std::string& qualifiedTypeName) const {
    // Walking the BasedOn chain is what makes `BasedOn` mean "inherit these" rather
    // than "copy these": a style derived from a TextBlock style is still a
    // TextBlock style, and ApplyTo must agree with that for a derived style used on
    // the base type's element.
    for (const StyleDesc* s = this; s != nullptr; s = s->basedOn) {
        if (s->targetType == qualifiedTypeName) return true;
    }
    return false;
}

const StyleDesc* CompiledDocument::FindStyle(const std::string& key) const {
    // During the collect pass only pendingStyles is populated; after it, only
    // `styles` is. Checking both keeps one lookup routine valid in every pass
    // without the caller having to know which phase it is in.
    for (const auto& s : pendingStyles) {
        if (s->key == key) return s.get();
    }
    if (!styles) return nullptr;
    for (const auto& s : *styles) {
        if (s->key == key) return s.get();
    }
    return nullptr;
}

const ResourceValue* CompiledDocument::FindResource(const std::string& key) const {
    for (const auto& r : resources) {
        if (r.key == key) return &r;
    }
    return nullptr;
}

WidgetPtr InstantiateNode(const NodeDesc& node, ITextService* text) {
    if (!node.type) return nullptr;
    if (node.typeName == "wui/DataTemplate") return nullptr; // Built by BuildTemplate().
    WidgetPtr widget = node.type->Create();
    if (!widget) return nullptr;
    widget->BindType(node.type);
    if (!node.name.empty()) widget->SetName(node.name);

    // Styles first, base-most first, so a derived style overrides its parent and
    // the node's own attributes override both. This is the precedence users expect
    // and the reason a style never has to be "turned off": writing the attribute
    // inline wins.
    if (node.style) {
        // Collect the chain root-first. Depth is bounded by the cycle check in the
        // validator, so a small fixed array avoids an allocation per node.
        const StyleDesc* chain[16];
        size_t depth = 0;
        for (const StyleDesc* s = node.style; s != nullptr && depth < 16; s = s->basedOn) {
            chain[depth++] = s;
        }
        while (depth > 0) {
            --depth;
            for (const auto& setter : chain[depth]->setters) {
                widget->SetSlotValue(setter.slot, setter.value);
            }
        }
    }

    for (const auto& entry : node.properties) {
        if (entry.templateNode) {
            auto tmpl = BuildTemplate(*entry.templateNode, text);
            widget->SetSlotValue(entry.slot, Value::Ref(tmpl));
            continue;
        }
        if (!entry.binding.isBinding) {
            widget->SetSlotValue(entry.slot, entry.value);
            continue;
        }
        const PropertyDescriptor* desc = node.type->PropertyAt(entry.slot);
        BindingDesc bd;
        bd.path = SplitPath(entry.binding.path);
        bd.mode = entry.binding.mode;
        bd.trigger = entry.binding.trigger;
        bd.stringFormat = entry.binding.stringFormat;
        bd.converter = entry.binding.converter;
        auto binding = std::make_unique<Binding>(widget.get(), entry.slot, desc, std::move(bd));
        widget->AddBinding(std::move(binding));
    }

    for (const auto& child : node.children) {
        WidgetPtr childWidget = InstantiateNode(*child, text);
        if (childWidget) widget->AddChild(childWidget);
    }
    widget->AttachBindings(text);

    // Triggers attach LAST, after the style chain, the element's own attributes and
    // its bindings. Order matters both ways:
    //  * after the style, so a trigger's baseline is the styled value it should
    //    restore when it deactivates -- not the raw default;
    //  * after local attributes, so "local beats style" already holds and a trigger
    //    cannot quietly override an inline value;
    //  * after bindings, so a trigger that watches a BOUND property sees the bound
    //    value immediately instead of the default (a checkbox bound to a VM whose
    //    IsChecked starts true must look checked on the first paint).
    if (node.style) {
        std::vector<const StyleDesc*> chain;
        for (const StyleDesc* s = node.style; s != nullptr; s = s->basedOn) chain.push_back(s);
        // Attach wants base-most first; the walk above produced derived-first.
        std::reverse(chain.begin(), chain.end());
        // Slots this element set inline. A trigger must not write them: local >
        // style, and a trigger belongs to the style.
        std::vector<int> localSlots;
        for (const auto& entry : node.properties) {
            if (!entry.binding.isBinding) localSlots.push_back(entry.slot);
        }
        widget->SetTriggerHost(
            StyleTriggerHost::Attach(widget.get(), chain, localSlots, node.styleOwner));
    }
    return widget;
}

WidgetPtr CompiledDocument::Instantiate(ITextService* text) const {
    if (!root) return nullptr;
    return InstantiateNode(*root, text);
}

} // namespace wui
