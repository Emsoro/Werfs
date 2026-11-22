// SPDX-License-Identifier: MIT
#include "Diagnostics.h"

#include <cstdio>

namespace wui {

const char* DiagSeverityName(DiagSeverity s) {
    return s == DiagSeverity::Error ? "error" : "warning";
}

std::string DiagCodeName(DiagCode code) {
    char buf[16];
    snprintf(buf, sizeof(buf), "WAML%04u", static_cast<unsigned>(code));
    return buf;
}

std::string FormatDiagnostic(const Diagnostic& d) {
    std::string out;
    if (!d.file.empty()) out += d.file;
    out += "(" + std::to_string(d.line) + "," + std::to_string(d.column) + "): ";
    out += DiagSeverityName(d.severity);
    out += " " + DiagCodeName(d.code) + ": ";
    out += d.message;
    return out;
}

void DiagnosticBag::Add(Diagnostic d) {
    if (d.file.empty()) d.file = file_;
    if (d.severity == DiagSeverity::Error) ++errorCount_;
    else ++warningCount_;
    items_.push_back(std::move(d));
}

void DiagnosticBag::Error(DiagCode code, int line, int column, std::string message) {
    Diagnostic d;
    d.severity = DiagSeverity::Error;
    d.code = code;
    d.line = line;
    d.column = column;
    d.message = std::move(message);
    Add(std::move(d));
}

void DiagnosticBag::Warning(DiagCode code, int line, int column, std::string message) {
    Diagnostic d;
    d.severity = DiagSeverity::Warning;
    d.code = code;
    d.line = line;
    d.column = column;
    d.message = std::move(message);
    Add(std::move(d));
}

std::string DiagnosticBag::ToText() const {
    std::string out;
    for (const auto& d : items_) {
        out += FormatDiagnostic(d);
        out += "\n";
    }
    return out;
}

} // namespace wui
