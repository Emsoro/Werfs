// SPDX-License-Identifier: MIT
// WAML diagnostics: every error carries file/line/column and a stable code.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace wui {

enum class DiagSeverity { Error, Warning };

enum class DiagCode : uint16_t {
    Lexical = 1,        // WAML0001
    Structure = 2,      // WAML0002
    DuplicateAttr = 3,  // WAML0003
    BadAttribute = 4,   // WAML0004
    UnknownElement = 101,   // WAML0101
    UnknownProperty = 102,  // WAML0102
    ReadOnlyProperty = 103, // WAML0103
    BadValue = 104,         // WAML0104
    BadContent = 105,       // WAML0105
    MissingProperty = 106,  // WAML0106
    BadMarkupExtension = 201, // WAML0201
    BadBindingMode = 202,     // WAML0202
    UnresolvedPath = 203,     // WAML0203
    DuplicateName = 204,      // WAML0204
    MissingStyle = 205,       // WAML0205  Style="{StaticResource X}" with no such key
    StyleTargetMismatch = 206,// WAML0206  Style targets a different type than the element
    StyleCycle = 207,         // WAML0207  BasedOn chain loops
    DuplicateStyle = 208,     // WAML0208  two styles share a key
    BadTemplate = 301,        // WAML0301
};

struct Diagnostic {
    DiagSeverity severity = DiagSeverity::Error;
    DiagCode code = DiagCode::Lexical;
    int line = 1;
    int column = 1;
    std::string message;
    std::string file;
};

const char* DiagSeverityName(DiagSeverity s);
std::string DiagCodeName(DiagCode code);
// "file(line,col): error WAML0102: message"
std::string FormatDiagnostic(const Diagnostic& d);

class DiagnosticBag {
public:
    void Add(Diagnostic d);
    void Error(DiagCode code, int line, int column, std::string message);
    void Warning(DiagCode code, int line, int column, std::string message);
    bool HasErrors() const { return errorCount_ > 0; }
    size_t ErrorCount() const { return errorCount_; }
    size_t WarningCount() const { return warningCount_; }
    const std::vector<Diagnostic>& Items() const { return items_; }
    std::string ToText() const;
    void SetFile(std::string file) { file_ = std::move(file); }

private:
    std::vector<Diagnostic> items_;
    size_t errorCount_ = 0;
    size_t warningCount_ = 0;
    std::string file_;
};

} // namespace wui
