// SPDX-License-Identifier: MIT
#include "Converters.h"

#include <algorithm>

namespace wui {

ConverterRegistry& ConverterRegistry::Instance() {
    static ConverterRegistry instance;
    return instance;
}

void ConverterRegistry::Register(std::string name, ConvertFn fn) {
    if (name.empty() || !fn) return;
    converters_[std::move(name)] = std::move(fn);
}

const ConvertFn* ConverterRegistry::Find(std::string_view name) const {
    const auto it = converters_.find(std::string(name));
    return it == converters_.end() ? nullptr : &it->second;
}

bool ConverterRegistry::Has(std::string_view name) const { return Find(name) != nullptr; }

std::vector<std::string> ConverterRegistry::Names() const {
    std::vector<std::string> names;
    names.reserve(converters_.size());
    for (const auto& pair : converters_) names.push_back(pair.first);
    std::sort(names.begin(), names.end());
    return names;
}

namespace {

// Visibility is an enum whose 0 is Visible. Comparing against the number is
// deliberate: the converter must not depend on the widget layer's enum ordering
// staying at 0/1 forever, but it does need a defined pair of values. The contract
// is documented on RegisterBuiltinConverters.
constexpr double kVisible = 0;
constexpr double kCollapsed = 1;

bool ToBool(const Value& in, bool& out) {
    switch (in.type) {
    case VType::Bool:
        out = in.flag;
        return true;
    case VType::Number:
        out = in.num != 0;
        return true;
    case VType::Null:
        out = false;
        return true;
    default: {
        // Text "true"/"false"/"" and numbers-as-text are all reasonable inputs
        // from a loosely typed source.
        Value converted;
        if (!ConvertValue(in, VType::Bool, converted)) return false;
        out = converted.flag;
        return true;
    }
    }
}

} // namespace

void RegisterBuiltinConverters() {
    auto& registry = ConverterRegistry::Instance();

    registry.Register("BoolToVisibility", [](const Value& in, Value& out) {
        bool flag = false;
        if (!ToBool(in, flag)) return false;
        out = Value::Number(flag ? kVisible : kCollapsed);
        return true;
    });

    registry.Register("BoolToVisibilityInverted", [](const Value& in, Value& out) {
        bool flag = false;
        if (!ToBool(in, flag)) return false;
        out = Value::Number(flag ? kCollapsed : kVisible);
        return true;
    });

    // Negation is worth naming because "enable this while that is false" appears
    // in every real form, and writing the inverse property in the ViewModel just
    // to satisfy the UI is how ViewModels end up with five near-duplicate fields.
    registry.Register("Not", [](const Value& in, Value& out) {
        bool flag = false;
        if (!ToBool(in, flag)) return false;
        out = Value::Bool(!flag);
        return true;
    });

    registry.Register("NumberToText", [](const Value& in, Value& out) {
        Value converted;
        if (!ConvertValue(in, VType::Text, converted)) return false;
        out = converted;
        return true;
    });
}

} // namespace wui
