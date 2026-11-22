// SPDX-License-Identifier: MIT
// Type registry and property descriptors: the bridge between WAML names and
// typed C++ properties. Everything a WAML document can reference lives here.
#pragma once

#include "Core.h"

#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace wui {

class Widget;

// Content model for an element (see PLAN-WAML-MVVM.md §4.7).
enum class ContentModel {
    None,        // No children, no text.
    SingleChild, // At most one child element.
    ManyChildren,
    TextContent, // Text only, mapped onto ContentProperty.
    SingleTemplate, // DataTemplate: exactly one root.
};

struct PropertyDescriptor {
    std::string name;
    VType type = VType::Null;
    uint32_t flags = 0;
    BindingMode defaultMode = BindingMode::OneWay;
    bool writableInWaml = true;
    bool allowThemeToken = false; // Colors may name a theme token.
    Value defaultValue;
    std::vector<std::string> enumNames; // Enum literal -> index.
    int slot = -1;

    bool IsEnum() const { return !enumNames.empty(); }
};

class TypeInfo;

using CreateFn = std::function<WidgetPtr()>;

class TypeInfo {
public:
    TypeInfo(std::string name, ContentModel model, CreateFn create)
        : name_(std::move(name)), contentModel_(model), create_(std::move(create)) {}

    const std::string& Name() const { return name_; }
    ContentModel Model() const { return contentModel_; }

    TypeInfo& SetContentProperty(std::string name) {
        contentProperty_ = std::move(name);
        return *this;
    }
    const std::string& ContentProperty() const { return contentProperty_; }

    WidgetPtr Create() const { return create_ ? create_() : nullptr; }

    // Registration: called once per type during registry setup.
    TypeInfo& Prop(std::string name, VType type, Value defaultValue, uint32_t flags,
                   BindingMode defaultMode = BindingMode::OneWay, bool writable = true,
                   bool allowThemeToken = false) {
        PropertyDescriptor d;
        d.name = std::move(name);
        d.type = type;
        d.flags = flags;
        d.defaultMode = defaultMode;
        d.writableInWaml = writable;
        d.allowThemeToken = allowThemeToken;
        d.defaultValue = std::move(defaultValue);
        d.slot = static_cast<int>(properties_.size());
        properties_.push_back(std::move(d));
        byName_.clear();
        return *this;
    }

    TypeInfo& EnumProp(std::string name, std::vector<std::string> names, int defaultValue, uint32_t flags,
                       bool writable = true) {
        PropertyDescriptor d;
        d.name = std::move(name);
        d.type = VType::Enum;
        d.enumNames = std::move(names);
        d.defaultValue = Value::Enum(defaultValue);
        d.flags = flags;
        d.writableInWaml = writable;
        d.slot = static_cast<int>(properties_.size());
        properties_.push_back(std::move(d));
        byName_.clear();
        return *this;
    }

    const std::vector<PropertyDescriptor>& Properties() const { return properties_; }
    int SlotCount() const { return static_cast<int>(properties_.size()); }

    const PropertyDescriptor* FindProperty(std::string_view name) const {
        if (byName_.empty())
            for (const auto& p : properties_) byName_.emplace(p.name, &p);
        auto it = byName_.find(std::string(name));
        return it == byName_.end() ? nullptr : it->second;
    }

    PropertyDescriptor* MutableProperty(int slot) {
        if (slot < 0 || slot >= static_cast<int>(properties_.size())) return nullptr;
        return &properties_[static_cast<size_t>(slot)];
    }

    const PropertyDescriptor* PropertyAt(int slot) const {
        if (slot < 0 || slot >= static_cast<int>(properties_.size())) return nullptr;
        return &properties_[static_cast<size_t>(slot)];
    }

    std::vector<std::string> PropertyNames() const {
        std::vector<std::string> out;
        for (const auto& p : properties_) out.push_back(p.name);
        return out;
    }

private:
    std::string name_;
    ContentModel contentModel_;
    CreateFn create_;
    std::string contentProperty_;
    std::vector<PropertyDescriptor> properties_;
    mutable std::map<std::string, const PropertyDescriptor*> byName_;
};

// Global registry (single module; see PLAN §9 R7 for the multi-module note).
class TypeRegistry {
public:
    static TypeRegistry& Instance();

    void Register(const std::shared_ptr<TypeInfo>& info);
    const TypeInfo* Find(const std::string& qualifiedName) const; // "wui/Button"
    std::vector<std::string> AllNames() const;

    // Registers every built-in control; safe to call more than once.
    void RegisterBuiltins();

private:
    TypeRegistry() = default;
    bool builtins_ = false;
    std::vector<std::shared_ptr<TypeInfo>> types_;
    mutable std::map<std::string, const TypeInfo*> byName_;
};

// Make<T>() helper used by the registry.
template <class T>
std::shared_ptr<T> MakeWidget() {
    return std::make_shared<T>();
}

} // namespace wui
