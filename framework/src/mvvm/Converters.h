// SPDX-License-Identifier: MIT
// Value converters for data binding.
//
// A converter sits between a source value and a target property, turning one
// type into another. The motivating case is almost always the same: a ViewModel
// exposes `bool IsDirty`, the UI needs `Visibility`, and there is no sensible
// implicit conversion between them -- "true" is not a layout value.
//
// Converters are registered under a name so markup can refer to them:
//
//   <TextBlock Text="dirty" Visibility="{Binding IsDirty, Converter=BoolToVisibility}"/>
//
// Registry rather than instances-in-markup because WAML has no object-creation
// syntax, and a name is what a hand-written page can type.
#pragma once

#include "Core.h"

#include <functional>
#include <string>
#include <string_view>
#include <unordered_map>

namespace wui {

// Converts a source value. Returns false to report that this converter cannot
// handle the input; the binding then keeps the previous target value and (when
// diagnostics are attached) reports an error rather than silently doing nothing.
using ConvertFn = std::function<bool(const Value& in, Value& out)>;

class ConverterRegistry {
public:
    static ConverterRegistry& Instance();

    void Register(std::string name, ConvertFn fn);
    const ConvertFn* Find(std::string_view name) const;
    bool Has(std::string_view name) const;
    // Names of every registered converter, sorted -- used in diagnostics so a
    // typo can suggest the real spelling.
    std::vector<std::string> Names() const;

private:
    ConverterRegistry() = default;
    std::unordered_map<std::string, ConvertFn> converters_;
};

// Registers the built-in converters. Called once from TypeRegistry setup; safe to
// call repeatedly.
//
// Built-ins:
//   BoolToVisibility   true -> Collapsed index 1? No: true -> Visible(0),
//                      false -> Collapsed(1)
//   BoolToVisibilityInverted  the reverse, for "hide while busy"
//   Not                logical negation, for {Binding IsEnabled, Converter=Not}
//   NumberToText       explicit stringification (implicit conversion exists, but
//                      naming it documents intent)
void RegisterBuiltinConverters();

} // namespace wui
