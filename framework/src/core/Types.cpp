// SPDX-License-Identifier: MIT
#include "Types.h"

#include "Controls.h"
#include "Widget.h"

#include <algorithm>

namespace wui {

TypeRegistry& TypeRegistry::Instance() {
    static TypeRegistry registry;
    static const bool once = [] {
        registry.RegisterBuiltins();
        return true;
    }();
    (void)once;
    return registry;
}

void TypeRegistry::Register(const std::shared_ptr<TypeInfo>& info) {
    if (!info) return;
    for (const auto& existing : types_) {
        if (existing->Name() == info->Name()) return;
    }
    types_.push_back(info);
    byName_.clear();
}

const TypeInfo* TypeRegistry::Find(const std::string& qualifiedName) const {
    if (byName_.empty())
        for (const auto& t : types_) byName_.emplace(t->Name(), t.get());
    auto it = byName_.find(qualifiedName);
    return it == byName_.end() ? nullptr : it->second;
}

std::vector<std::string> TypeRegistry::AllNames() const {
    std::vector<std::string> out;
    for (const auto& t : types_) out.push_back(t->Name());
    return out;
}

// ---------------------------------------------------------------------------
// Common property groups
// ---------------------------------------------------------------------------
static void AddLayoutProps(TypeInfo& t) {
    t.Prop("Margin", VType::Thickness, Value::MakeThickness(Thickness{}), AffectsMeasure);
    t.Prop("Width", VType::Number, Value::Number(0), AffectsMeasure);
    t.Prop("Height", VType::Number, Value::Number(0), AffectsMeasure);
    t.Prop("MinWidth", VType::Number, Value::Number(0), AffectsMeasure);
    t.Prop("MinHeight", VType::Number, Value::Number(0), AffectsMeasure);
    t.Prop("MaxWidth", VType::Number, Value::Number(0), AffectsMeasure);
    t.Prop("MaxHeight", VType::Number, Value::Number(0), AffectsMeasure);
}

void TypeRegistry::RegisterBuiltins() {
    if (builtins_) return;
    builtins_ = true;

    auto add = [this](std::shared_ptr<TypeInfo> t) { Register(std::move(t)); };

    {
        auto t = std::make_shared<TypeInfo>("wui/Page", ContentModel::SingleChild,
                                            []() -> WidgetPtr { return std::make_shared<Page>(); });
        t->SetContentProperty("Child");
        AddLayoutProps(*t);
        t->Prop("Background", VType::Color, Value::MakeColor(Color{0, 0, 0, 1, Token::AppBackground}),
                AffectsRender, BindingMode::OneWay, true, true);
        t->Prop("Padding", VType::Thickness, Value::MakeThickness(Thickness{}), AffectsMeasure);
        t->Prop("Title", VType::Text, Value::Text(L""), 0);
        add(t);
    }
    {
        auto t = std::make_shared<TypeInfo>("wui/TextBlock", ContentModel::TextContent,
                                            []() -> WidgetPtr { return std::make_shared<TextBlock>(); });
        t->SetContentProperty("Text");
        AddLayoutProps(*t);
        t->Prop("Text", VType::Text, Value::Text(L""), AffectsMeasure);
        t->Prop("FontSize", VType::Number, Value::Number(14), AffectsMeasure);
        t->EnumProp("FontWeight", {"Normal", "Medium", "SemiBold", "Bold"}, WeightNormal, AffectsMeasure);
        t->Prop("Foreground", VType::Color, Value::MakeColor(Color{0, 0, 0, 1, Token::TextPrimary}),
                AffectsRender, BindingMode::OneWay, true, true);
        t->EnumProp("TextAlignment", {"Left", "Center", "Right"}, 0, AffectsMeasure);
        t->Prop("Padding", VType::Thickness, Value::MakeThickness(Thickness{}), AffectsMeasure);
        t->EnumProp("AutomationRole", {"Text", "Heading", "Status"}, 0, 0);
        add(t);
    }
    {
        auto t = std::make_shared<TypeInfo>("wui/Border", ContentModel::SingleChild,
                                            []() -> WidgetPtr { return std::make_shared<Border>(); });
        t->SetContentProperty("Child");
        AddLayoutProps(*t);
        t->Prop("Background", VType::Color, Value::MakeColor(Color{0, 0, 0, 1, Token::CardBackground}),
                AffectsRender, BindingMode::OneWay, true, true);
        t->Prop("BorderBrush", VType::Color, Value::MakeColor(Color{0, 0, 0, 0, Token::None}),
                AffectsRender, BindingMode::OneWay, true, true);
        t->Prop("BorderThickness", VType::Number, Value::Number(0), AffectsMeasure);
        t->Prop("CornerRadius", VType::Number, Value::Number(0), AffectsRender);
        t->Prop("Padding", VType::Thickness, Value::MakeThickness(Thickness{}), AffectsMeasure);
        add(t);
    }
    {
        auto t = std::make_shared<TypeInfo>("wui/StackPanel", ContentModel::ManyChildren,
                                            []() -> WidgetPtr { return std::make_shared<StackPanel>(); });
        AddLayoutProps(*t);
        t->EnumProp("Orientation", {"Vertical", "Horizontal"}, 0, AffectsMeasure);
        t->Prop("Spacing", VType::Number, Value::Number(0), AffectsMeasure);
        t->Prop("Padding", VType::Thickness, Value::MakeThickness(Thickness{}), AffectsMeasure);
        add(t);
    }
    {
        auto t = std::make_shared<TypeInfo>("wui/Button", ContentModel::TextContent,
                                            []() -> WidgetPtr { return std::make_shared<Button>(); });
        t->SetContentProperty("Content");
        AddLayoutProps(*t);
        t->Prop("Content", VType::Text, Value::Text(L"Button"), AffectsMeasure);
        t->Prop("Command", VType::Ref, Value::Null(), AffectsRender, BindingMode::OneWay);
        t->Prop("IsEnabled", VType::Bool, Value::Bool(true), AffectsRender,
                BindingMode::OneWay);
        t->Prop("FontSize", VType::Number, Value::Number(14), AffectsMeasure);
        t->EnumProp("FontWeight", {"Normal", "Medium", "SemiBold", "Bold"}, WeightNormal, AffectsMeasure);
        t->Prop("Foreground", VType::Color, Value::MakeColor(Color{0, 0, 0, 1, Token::TextOnAccent}),
                AffectsRender, BindingMode::OneWay, true, true);
        t->Prop("Background", VType::Color, Value::MakeColor(Color{0, 0, 0, 1, Token::Accent}),
                AffectsRender, BindingMode::OneWay, true, true);
        t->Prop("Padding", VType::Thickness, Value::MakeThickness(Thickness{12, 6, 12, 6}),
                AffectsMeasure);
        t->Prop("TabIndex", VType::Number, Value::Number(0), 0);
        add(t);
    }
    {
        auto t = std::make_shared<TypeInfo>("wui/CheckBox", ContentModel::TextContent,
                                            []() -> WidgetPtr { return std::make_shared<CheckBox>(); });
        t->SetContentProperty("Content");
        AddLayoutProps(*t);
        t->Prop("Content", VType::Text, Value::Text(L"CheckBox"), AffectsMeasure);
        t->Prop("IsChecked", VType::Bool, Value::Bool(false), AffectsRender, BindingMode::TwoWay);
        t->Prop("IsEnabled", VType::Bool, Value::Bool(true), AffectsRender);
        t->Prop("FontSize", VType::Number, Value::Number(14), AffectsMeasure);
        t->Prop("Foreground", VType::Color, Value::MakeColor(Color{0, 0, 0, 1, Token::TextPrimary}),
                AffectsRender, BindingMode::OneWay, true, true);
        t->Prop("Padding", VType::Thickness, Value::MakeThickness(Thickness{4, 4, 4, 4}),
                AffectsMeasure);
        add(t);
    }
    {
        auto t = std::make_shared<TypeInfo>("wui/TextBox", ContentModel::None,
                                            []() -> WidgetPtr { return std::make_shared<TextBox>(); });
        AddLayoutProps(*t);
        t->Prop("Text", VType::Text, Value::Text(L""), AffectsMeasure | AffectsRender,
                BindingMode::TwoWay);
        t->Prop("PlaceholderText", VType::Text, Value::Text(L""), AffectsRender);
        t->Prop("FontSize", VType::Number, Value::Number(14), AffectsMeasure);
        t->Prop("Foreground", VType::Color, Value::MakeColor(Color{0, 0, 0, 1, Token::TextPrimary}),
                AffectsRender, BindingMode::OneWay, true, true);
        t->Prop("Padding", VType::Thickness, Value::MakeThickness(Thickness{10, 6, 10, 6}),
                AffectsMeasure);
        t->Prop("MinWidth", VType::Number, Value::Number(160), AffectsMeasure);
        add(t);
    }
    {
        auto t = std::make_shared<TypeInfo>("wui/ListBox", ContentModel::None,
                                            []() -> WidgetPtr { return std::make_shared<ListBox>(); });
        AddLayoutProps(*t);
        t->Prop("ItemsSource", VType::Ref, Value::Null(), AffectsMeasure | AffectsRender,
                BindingMode::OneWay);
        t->Prop("ItemTemplate", VType::Ref, Value::Null(), AffectsMeasure | AffectsRender);
        t->Prop("Background", VType::Color, Value::MakeColor(Color{0, 0, 0, 0, Token::None}),
                AffectsRender, BindingMode::OneWay, true, true);
        t->Prop("ItemHeight", VType::Number, Value::Number(26), AffectsMeasure);
        t->Prop("Padding", VType::Thickness, Value::MakeThickness(Thickness{}), AffectsMeasure);
        add(t);
    }
    {
        auto t = std::make_shared<TypeInfo>("wui/ScrollViewer", ContentModel::SingleChild,
                                            []() -> WidgetPtr { return std::make_shared<ScrollViewer>(); });
        t->SetContentProperty("Child");
        AddLayoutProps(*t);
        t->Prop("Padding", VType::Thickness, Value::MakeThickness(Thickness{}), AffectsMeasure);
        t->Prop("Background", VType::Color, Value::MakeColor(Color{0, 0, 0, 0, Token::None}),
                AffectsRender, BindingMode::OneWay, true, true);
        add(t);
    }
    {
        auto t = std::make_shared<TypeInfo>("wui/DataTemplate", ContentModel::SingleTemplate,
                                            []() -> WidgetPtr { return nullptr; });
        add(t);
    }
}

} // namespace wui
