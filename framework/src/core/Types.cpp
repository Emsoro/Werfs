// SPDX-License-Identifier: MIT
#include "Types.h"
#include "Converters.h"

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

    // Visibility is a layout property, not a paint flag: a Collapsed widget takes
    // no space at all, so it must be skipped by Measure/Arrange rather than just
    // skipped when drawing. Declared here so every widget has it -- "show this
    // only when there is something to show" applies to any control.
    t.EnumProp("Visibility", {"Visible", "Collapsed"}, 0, AffectsMeasure);

    // Alignment inside the slot the parent gives this element. Stretch is the default
    // because it is what a StackPanel already does, so adding these changed no existing
    // layout -- Left/Center/Right are the opt-in.
    //
    // Both axes exist as enums rather than booleans: "centre" is not expressible as
    // "not left and not right", and a panel that honours one but not the other is a
    // defect the author cannot work around.
    t.EnumProp("HorizontalAlignment", {"Stretch", "Left", "Center", "Right"}, 0, 0);
    t.EnumProp("VerticalAlignment", {"Stretch", "Top", "Center", "Bottom"}, 0, 0);

    // Panel-placement properties. These are written on the CHILD
    // (Grid.Row="1"), so they must exist on every widget that can be a child,
    // not on the panel itself -- the validator checks the child's property
    // table, and a real attached-property mechanism does not exist here.
    // They are harmless on widgets that are never placed in a panel.
    t.Prop("Grid.Row", VType::Number, Value::Number(0), AffectsMeasure);
    t.Prop("Grid.Column", VType::Number, Value::Number(0), AffectsMeasure);
    t.Prop("Grid.RowSpan", VType::Number, Value::Number(1), AffectsMeasure);
    t.Prop("Grid.ColumnSpan", VType::Number, Value::Number(1), AffectsMeasure);
    t.Prop("Canvas.Left", VType::Number, Value::Number(0), AffectsMeasure);
    t.Prop("Canvas.Top", VType::Number, Value::Number(0), AffectsMeasure);
    t.Prop("VariableSizedWrapGrid.ColumnSpan", VType::Number, Value::Number(1), AffectsMeasure);
    t.Prop("VariableSizedWrapGrid.RowSpan", VType::Number, Value::Number(1), AffectsMeasure);
    // RelativePanel anchors, for the same reason as Grid.Row: they are written on the
    // CHILD (RelativePanel.RightOf="other"), so every potential child must declare them.
    // Putting them on the panel alone made the validator reject the child -- which is what
    // the first version of this did.
    t.Prop("RelativePanel.RightOf", VType::Text, Value::Text(L""), AffectsMeasure);
    t.Prop("RelativePanel.LeftOf", VType::Text, Value::Text(L""), AffectsMeasure);
    t.Prop("RelativePanel.Below", VType::Text, Value::Text(L""), AffectsMeasure);
    t.Prop("RelativePanel.Above", VType::Text, Value::Text(L""), AffectsMeasure);
    t.Prop("RelativePanel.AlignRightWithPanel", VType::Bool, Value::Bool(false), AffectsMeasure);
    t.Prop("RelativePanel.AlignLeftWithPanel", VType::Bool, Value::Bool(false), AffectsMeasure);
    t.Prop("RelativePanel.AlignBottomWithPanel", VType::Bool, Value::Bool(false), AffectsMeasure);
    t.Prop("RelativePanel.AlignTopWithPanel", VType::Bool, Value::Bool(false), AffectsMeasure);
    t.Prop("RelativePanel.AlignHorizontalCenterWithPanel", VType::Bool, Value::Bool(false),
           AffectsMeasure);
    t.Prop("RelativePanel.AlignVerticalCenterWithPanel", VType::Bool, Value::Bool(false),
           AffectsMeasure);

    // DataContext is not a layout property, but this is the one table every
    // visual widget is built from, and every visual widget can carry a data
    // source. The core mirrors writes to this slot onto Widget::dataContext_
    // (see Widget::SetSlotValue), which is what makes a subtree switch sources:
    //   <Border DataContext="{Binding SelectedServer}"> ... </Border>
    // It has no layout/render flag because a context change is not, by itself,
    // a visual change -- the bindings that read it invalidate what they touch.
    t.Prop("DataContext", VType::Ref, Value::Null(), 0, BindingMode::OneWay);
}

void TypeRegistry::RegisterBuiltins() {
    if (builtins_) return;
    builtins_ = true;

    // Converters are registered alongside the types because both are
    // process-wide, name-keyed tables that markup refers to.
    RegisterBuiltinConverters();

    auto add = [this](std::shared_ptr<TypeInfo> t) { Register(std::move(t)); };

    {
        // ManyChildren, not SingleChild: a real application page is a row of
        // co-existing regions (menu bar, toolbar, content, status line). The
        // single-child form forced every sample to wrap everything in one Grid,
        // which is exactly the kind of ceremony that makes markup worse than code.
        // One child still works -- it is just a column of one.
        auto t = std::make_shared<TypeInfo>("wui/Page", ContentModel::ManyChildren,
                                            []() -> WidgetPtr { return std::make_shared<Page>(); });
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
        // Monospace is what code snippets need: without it a snippet cannot be aligned in
        // columns, which is most of what a snippet is for. Empty means the system default,
        // so existing markup is unchanged.
        t->Prop("FontFamily", VType::Text, Value::Text(L""), AffectsMeasure);
        t->EnumProp("FontWeight", {"Normal", "Medium", "SemiBold", "Bold"}, WeightNormal, AffectsMeasure);
        t->Prop("Foreground", VType::Color, Value::MakeColor(Color{0, 0, 0, 1, Token::TextPrimary}),
                AffectsRender, BindingMode::OneWay, true, true);
        t->EnumProp("TextAlignment", {"Left", "Center", "Right"}, 0, AffectsMeasure);
        t->Prop("Padding", VType::Thickness, Value::MakeThickness(Thickness{}), AffectsMeasure);
        t->EnumProp("AutomationRole", {"Text", "Heading", "Status"}, 0, 0);
        // Wrap makes the block size to the width it is given instead of to its
        // text, which is what long prose paragraphs need.
        t->EnumProp("TextWrapping", {"NoWrap", "Wrap"}, 0, AffectsMeasure);
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
        // Thickness, not Number: a card wants a hairline all round, but a
        // divider wants one edge only ("0,1,0,0"). One uniform number cannot
        // express that, which is why the status bar in samples/editor failed to
        // compile until this became a Thickness.
        t->Prop("BorderThickness", VType::Thickness, Value::MakeThickness(Thickness{}),
                AffectsMeasure);
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
        // Transparent by default so the row looks unremarkable until a style or
        // trigger tints it.
        t->Prop("Background", VType::Color,
                Value::MakeColor(Color{0, 0, 0, 0, Token::None}), AffectsRender, BindingMode::OneWay,
                true, true);
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
        // A document editor wants Enter to create a line; a one-line field wants
        // Enter to commit and leave. Default stays single-line for every other page.
        t->Prop("AcceptsReturn", VType::Bool, Value::Bool(false), AffectsRender);
        t->Prop("FontSize", VType::Number, Value::Number(14), AffectsMeasure);
        t->Prop("Foreground", VType::Color, Value::MakeColor(Color{0, 0, 0, 1, Token::TextPrimary}),
                AffectsRender, BindingMode::OneWay, true, true);
        t->Prop("Padding", VType::Thickness, Value::MakeThickness(Thickness{10, 6, 10, 6}),
                AffectsMeasure);
        t->Prop("MinWidth", VType::Number, Value::Number(160), AffectsMeasure);
        add(t);
    }
    {
        auto t = std::make_shared<TypeInfo>("wui/TabView", ContentModel::None,
                                            []() -> WidgetPtr { return std::make_shared<TabView>(); });
        AddLayoutProps(*t);
        t->Prop("ItemsSource", VType::Ref, Value::Null(), AffectsMeasure | AffectsRender,
                BindingMode::OneWay);
        // Both selection entries are TwoWay: clicking a tab must reach the
        // ViewModel, and restoring a selection must reach the control.
        t->Prop("SelectedIndex", VType::Number, Value::Number(-1),
                AffectsMeasure | AffectsRender, BindingMode::TwoWay);
        t->Prop("SelectedItem", VType::Ref, Value::Null(),
                AffectsMeasure | AffectsRender, BindingMode::TwoWay);
        // The + button invokes this; the ViewModel decides what a new tab is.
        t->Prop("AddCommand", VType::Ref, Value::Null(), AffectsRender, BindingMode::OneWay);
        // Set to the index the user asked to close; -1 when there is no pending
        // request. This MUST be TwoWay. Declared OneWay it looked like a
        // publish-only property, but a OneWay binding ignores slot changes made
        // by the target -- so the × published into nothing and no tab could ever
        // be closed. It is the control's write-back channel to the ViewModel,
        // exactly like SelectedIndex, and only the ViewModel can decide whether
        // closing is safe.
        t->Prop("CloseRequested", VType::Number, Value::Number(-1),
                AffectsRender, BindingMode::TwoWay);
        t->Prop("Background", VType::Color, Value::MakeColor(Color{0, 0, 0, 0, Token::None}),
                AffectsRender, BindingMode::OneWay, true, true);
        t->Prop("Padding", VType::Thickness, Value::MakeThickness(Thickness{}), AffectsMeasure);
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
        // Selection: TwoWay on both, because the canonical use is "click a row,
        // the ViewModel learns" and the reverse (restore selection) must work too.
        // -1 means nothing selected.
        t->Prop("SelectedIndex", VType::Number, Value::Number(-1),
                AffectsRender, BindingMode::TwoWay);
        t->Prop("SelectedItem", VType::Ref, Value::Null(),
                AffectsRender, BindingMode::TwoWay);
        // Virtualisation surface. These four are CONTROL-TO-VIEWMODEL outputs: the
        // list publishes where its window is and how big it is, and a page may show
        // that. They must be TwoWay for the same reason CloseRequested is -- a OneWay
        // binding silently ignores the target's write (§7.16) -- and ScrollOffset is
        // additionally an INPUT: a ViewModel may set it to scroll the list. Marked
        // writable because the loader rejects TwoWay bindings to read-only targets,
        // not because setting RealizedCount means anything: the control overwrites it
        // on the next layout.
        t->Prop("RealizedCount", VType::Number, Value::Number(0), AffectsRender,
                BindingMode::TwoWay);
        t->Prop("IsVirtualized", VType::Bool, Value::Bool(false), AffectsRender,
                BindingMode::TwoWay);
        t->Prop("FirstVisibleIndex", VType::Number, Value::Number(0), AffectsRender,
                BindingMode::TwoWay);
        t->Prop("ScrollOffset", VType::Number, Value::Number(0), AffectsRender,
                BindingMode::TwoWay);
        // What ScrollOffset clamps to. Published rather than derived by the page from
        // "rows x itemHeight" in markup, because the page does not know the viewport
        // height the host actually granted.
        t->Prop("ScrollMax", VType::Number, Value::Number(0), AffectsRender,
                BindingMode::TwoWay);
        add(t);
    }
    {
        auto t = std::make_shared<TypeInfo>("wui/Grid", ContentModel::ManyChildren,
                                            []() -> WidgetPtr { return std::make_shared<Grid>(); });
        AddLayoutProps(*t);
        // Track specs: "Auto,2*,1*,80". See Grid::Measure for the grammar.
        t->Prop("ColumnDefinitions", VType::Text, Value::Text(L"1*"), AffectsMeasure);
        t->Prop("RowDefinitions", VType::Text, Value::Text(L"Auto"), AffectsMeasure);
        t->Prop("ColumnSpacing", VType::Number, Value::Number(0), AffectsMeasure);
        t->Prop("RowSpacing", VType::Number, Value::Number(0), AffectsMeasure);
        t->Prop("Padding", VType::Thickness, Value::MakeThickness(Thickness{}), AffectsMeasure);
        t->Prop("Background", VType::Color, Value::MakeColor(Color{0, 0, 0, 0, Token::None}),
                AffectsRender, BindingMode::OneWay, true, true);

        add(t);
    }
    {
        auto t = std::make_shared<TypeInfo>("wui/Canvas", ContentModel::ManyChildren,
                                            []() -> WidgetPtr { return std::make_shared<Canvas>(); });
        AddLayoutProps(*t);
        t->Prop("Background", VType::Color, Value::MakeColor(Color{0, 0, 0, 0, Token::None}),
                AffectsRender, BindingMode::OneWay, true, true);

        add(t);
    }
    {
        auto t = std::make_shared<TypeInfo>("wui/WrapPanel", ContentModel::ManyChildren,
                                            []() -> WidgetPtr { return std::make_shared<WrapPanel>(); });
        AddLayoutProps(*t);
        t->EnumProp("Orientation", {"Horizontal", "Vertical"}, 0, AffectsMeasure);
        t->Prop("Spacing", VType::Number, Value::Number(0), AffectsMeasure);
        t->Prop("Padding", VType::Thickness, Value::MakeThickness(Thickness{}), AffectsMeasure);
        t->Prop("Background", VType::Color, Value::MakeColor(Color{0, 0, 0, 0, Token::None}),
                AffectsRender, BindingMode::OneWay, true, true);
        add(t);
    }
    {
        auto t = std::make_shared<TypeInfo>("wui/VariableSizedWrapGrid", ContentModel::ManyChildren,
                                            []() -> WidgetPtr { return std::make_shared<VariableSizedWrapGrid>(); });
        AddLayoutProps(*t);
        t->Prop("ItemWidth", VType::Number, Value::Number(120), AffectsMeasure);
        t->Prop("ItemHeight", VType::Number, Value::Number(90), AffectsMeasure);
        t->Prop("Background", VType::Color, Value::MakeColor(Color{0, 0, 0, 0, Token::None}),
                AffectsRender, BindingMode::OneWay, true, true);

        add(t);
    }
    {
        auto t = std::make_shared<TypeInfo>("wui/Viewbox", ContentModel::SingleChild,
                                            []() -> WidgetPtr { return std::make_shared<Viewbox>(); });
        t->SetContentProperty("Child");
        AddLayoutProps(*t);
        t->Prop("Stretch", VType::Bool, Value::Bool(false), AffectsMeasure);
        t->Prop("Background", VType::Color, Value::MakeColor(Color{0, 0, 0, 0, Token::None}),
                AffectsRender, BindingMode::OneWay, true, true);
        add(t);
    }
    {
        auto t = std::make_shared<TypeInfo>("wui/Expander", ContentModel::ManyChildren,
                                            []() -> WidgetPtr { return std::make_shared<Expander>(); });
        AddLayoutProps(*t);
        t->Prop("Header", VType::Text, Value::Text(L"Header"), AffectsMeasure);
        t->Prop("HeaderHeight", VType::Number, Value::Number(36), AffectsMeasure | AffectsRender);
        t->Prop("FontSize", VType::Number, Value::Number(14), AffectsMeasure);
        t->EnumProp("FontWeight", {"Normal", "Medium", "SemiBold", "Bold"}, WeightNormal, AffectsMeasure);
        t->Prop("Foreground", VType::Color, Value::MakeColor(Color{0, 0, 0, 1, Token::TextPrimary}),
                AffectsRender, BindingMode::OneWay, true, true);
        t->Prop("Background", VType::Color, Value::MakeColor(Color{0, 0, 0, 1, Token::CardBackground}),
                AffectsRender, BindingMode::OneWay, true, true);
        t->Prop("Padding", VType::Thickness, Value::MakeThickness(Thickness{}),
                AffectsMeasure);
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
    // --- application shell ------------------------------------------------
    {
        auto t = std::make_shared<TypeInfo>("wui/MenuBar", ContentModel::ManyChildren,
                                            []() -> WidgetPtr { return std::make_shared<MenuBar>(); });
        AddLayoutProps(*t);
        t->Prop("Background", VType::Color,
                Value::MakeColor(Color{0, 0, 0, 0, Token::None}), AffectsRender,
                BindingMode::OneWay, true, true);
        t->Prop("Foreground", VType::Color,
                Value::MakeColor(Color{0, 0, 0, 1, Token::TextPrimary}), AffectsRender,
                BindingMode::OneWay, true, true);
        t->Prop("FontSize", VType::Number, Value::Number(13), AffectsMeasure);
        t->Prop("Height", VType::Number, Value::Number(34), AffectsMeasure);
        add(t);
    }
    {
        auto t = std::make_shared<TypeInfo>("wui/MenuBarItem", ContentModel::ManyChildren,
                                            []() -> WidgetPtr { return std::make_shared<MenuBarItemImpl>(); });
        AddLayoutProps(*t);
        t->Prop("Title", VType::Text, Value::Text(L"Menu"), AffectsMeasure);
        add(t);
    }
    {
        // The flyout level carries the items; it is not a widget that draws, which
        // is why it has no appearance properties at all.
        auto t = std::make_shared<TypeInfo>("wui/MenuFlyout", ContentModel::ManyChildren,
                                            []() -> WidgetPtr { return std::make_shared<MenuFlyout>(); });
        add(t);
    }
    {
        auto t = std::make_shared<TypeInfo>("wui/MenuFlyoutItem", ContentModel::None,
                                            []() -> WidgetPtr { return std::make_shared<MenuFlyoutItem>(); });
        t->Prop("Text", VType::Text, Value::Text(L""), 0);
        // Display-only accelerator text. Key dispatch is host-level, because a
        // control cannot know what else in the app wants the same chord.
        t->Prop("Hint", VType::Text, Value::Text(L""), 0);
        t->Prop("Icon", VType::Text, Value::Text(L""), 0);
        t->Prop("Command", VType::Ref, Value::Null(), 0, BindingMode::OneWay);
        t->Prop("IsChecked", VType::Bool, Value::Bool(false), 0, BindingMode::OneWay);
        t->EnumProp("Kind", {"Command", "Toggle"}, 0, 0);
        add(t);
    }
    {
        auto t = std::make_shared<TypeInfo>("wui/MenuFlyoutSeparator", ContentModel::None,
                                            []() -> WidgetPtr { return std::make_shared<MenuFlyoutSeparator>(); });
        add(t);
    }
    {
        auto t = std::make_shared<TypeInfo>("wui/MenuFlyoutSubItem", ContentModel::ManyChildren,
                                            []() -> WidgetPtr { return std::make_shared<MenuFlyoutSubItem>(); });
        t->Prop("Text", VType::Text, Value::Text(L""), 0);
        add(t);
    }
    {
        auto t = std::make_shared<TypeInfo>("wui/CommandBar", ContentModel::ManyChildren,
                                            []() -> WidgetPtr { return std::make_shared<CommandBar>(); });
        AddLayoutProps(*t);
        t->Prop("Height", VType::Number, Value::Number(44), AffectsMeasure);
        t->Prop("Background", VType::Color,
                Value::MakeColor(Color{0, 0, 0, 1, Token::CardBackground}), AffectsRender,
                BindingMode::OneWay, true, true);
        t->Prop("Padding", VType::Thickness, Value::MakeThickness(Thickness{8, 6, 8, 6}),
                AffectsMeasure);
        add(t);
    }
    {
        auto t = std::make_shared<TypeInfo>("wui/AppBarButton", ContentModel::None,
                                            []() -> WidgetPtr { return std::make_shared<AppBarButton>(); });
        AddLayoutProps(*t);
        // A short glyph string rather than an icon descriptor: the control draws
        // it through the same text path as everything else, so no icon format and
        // no image decoding is needed for the toolbar to exist.
        t->Prop("Icon", VType::Text, Value::Text(L""), 0);
        t->Prop("Label", VType::Text, Value::Text(L""), AffectsMeasure | AffectsRender);
        t->Prop("Command", VType::Ref, Value::Null(), AffectsRender, BindingMode::OneWay);
        t->Prop("IsEnabled", VType::Bool, Value::Bool(true), AffectsRender);
        t->Prop("ToolTip", VType::Text, Value::Text(L""), 0);
        // FontSize and Foreground exist so a toolbar button can be STYLED like any
        // other text-bearing control. Leaving them out meant a style targeting
        // AppBarButton could only set layout properties, which is the sort of gap
        // that makes a style system feel half-finished.
        t->Prop("FontSize", VType::Number, Value::Number(12), AffectsMeasure | AffectsRender);
        t->Prop("Foreground", VType::Color,
                Value::MakeColor(Color{0, 0, 0, 1, Token::TextPrimary}), AffectsRender,
                BindingMode::OneWay, true, true);
        add(t);
    }
    {
        auto t = std::make_shared<TypeInfo>("wui/AppBarSeparator", ContentModel::None,
                                            []() -> WidgetPtr { return std::make_shared<AppBarButton>(); });
        t->Prop("IsSeparator", VType::Bool, Value::Bool(true), 0);
        add(t);
    }
    {
        auto t = std::make_shared<TypeInfo>("wui/ContentDialog", ContentModel::None,
                                            []() -> WidgetPtr { return std::make_shared<ContentDialog>(); });
        AddLayoutProps(*t);
        t->Prop("Title", VType::Text, Value::Text(L""), 0);
        t->Prop("Message", VType::Text, Value::Text(L""), 0);
        t->Prop("PrimaryButtonText", VType::Text, Value::Text(L"OK"), 0);
        t->Prop("SecondaryButtonText", VType::Text, Value::Text(L""), 0);
        t->Prop("CloseButtonText", VType::Text, Value::Text(L"Cancel"), 0);
        // IsOpen is TwoWay: the ViewModel asks, the dialog reports (the user closed
        // it by choosing a button). Result carries which button, as a number the
        // sample maps back to its own enum.
        t->Prop("IsOpen", VType::Bool, Value::Bool(false), 0, BindingMode::TwoWay);
        t->Prop("Result", VType::Number, Value::Number(ContentDialog::ResultNone), 0,
                BindingMode::OneWay);
        add(t);
    }
    {
        auto t = std::make_shared<TypeInfo>("wui/ProgressBar", ContentModel::None,
                                            []() -> WidgetPtr { return std::make_shared<ProgressBar>(); });
        AddLayoutProps(*t);
        // Value is a FRACTION (0..1), not a count: a bar has no idea what it measures,
        // so the caller divides once instead of every control inventing a
        // Minimum/Maximum/Value triple that nothing else shares.
        t->Prop("Value", VType::Number, Value::Number(0), AffectsRender);
        t->Prop("Foreground", VType::Color,
                Value::MakeColor(Color{0, 0, 0, 1, Token::Accent}), AffectsRender, BindingMode::OneWay,
                true, true);
        add(t);
    }
    {
        auto t = std::make_shared<TypeInfo>("wui/ProgressRing", ContentModel::None,
                                            []() -> WidgetPtr { return std::make_shared<ProgressRing>(); });
        AddLayoutProps(*t);
        t->Prop("Value", VType::Number, Value::Number(0), AffectsRender);
        t->Prop("Size", VType::Number, Value::Number(32), AffectsMeasure);
        t->Prop("Thickness", VType::Number, Value::Number(3), AffectsMeasure);
        t->Prop("Foreground", VType::Color,
                Value::MakeColor(Color{0, 0, 0, 1, Token::Accent}), AffectsRender, BindingMode::OneWay,
                true, true);
        add(t);
    }
    {
        auto t = std::make_shared<TypeInfo>("wui/InfoBar", ContentModel::None,
                                            []() -> WidgetPtr { return std::make_shared<InfoBar>(); });
        AddLayoutProps(*t);
        t->Prop("Title", VType::Text, Value::Text(L""), AffectsMeasure);
        t->Prop("Message", VType::Text, Value::Text(L""), AffectsMeasure);
        // Informational is the default (index 0) so the least alarming severity is what
        // an author gets before deciding otherwise.
        t->EnumProp("Severity", {"Informational", "Success", "Warning", "Error"}, 0, AffectsRender);
        t->Prop("IsClosable", VType::Bool, Value::Bool(false), AffectsRender);
        // IsOpen is TwoWay for the same reason ContentDialog's is: the bar reports that
        // the user dismissed it, and whether it comes back is the ViewModel's decision.
        t->Prop("IsOpen", VType::Bool, Value::Bool(true), AffectsMeasure | AffectsRender,
                BindingMode::TwoWay);
        t->Prop("Foreground", VType::Color,
                Value::MakeColor(Color{0, 0, 0, 1, Token::TextPrimary}), AffectsRender,
                BindingMode::OneWay, true, true);
        add(t);
    }
    {
        auto t = std::make_shared<TypeInfo>("wui/InfoBadge", ContentModel::None,
                                            []() -> WidgetPtr { return std::make_shared<InfoBadge>(); });
        AddLayoutProps(*t);
        t->Prop("Text", VType::Text, Value::Text(L""), AffectsMeasure | AffectsRender);
        t->Prop("FontSize", VType::Number, Value::Number(11), AffectsMeasure);
        t->Prop("Background", VType::Color,
                Value::MakeColor(Color{0, 0, 0, 1, Token::Accent}), AffectsRender, BindingMode::OneWay,
                true, true);
        t->Prop("Foreground", VType::Color,
                Value::MakeColor(Color{0, 0, 0, 1, Token::TextOnAccent}), AffectsRender,
                BindingMode::OneWay, true, true);
        add(t);
    }
    {
        auto t = std::make_shared<TypeInfo>("wui/Line", ContentModel::None,
                                            []() -> WidgetPtr { return std::make_shared<Line>(); });
        AddLayoutProps(*t);
        // Endpoints rather than a start point plus a size: a diagonal has no meaningful
        // width/height pair.
        t->Prop("StartX", VType::Number, Value::Number(0), AffectsMeasure | AffectsRender);
        t->Prop("StartY", VType::Number, Value::Number(0), AffectsMeasure | AffectsRender);
        t->Prop("EndX", VType::Number, Value::Number(100), AffectsMeasure | AffectsRender);
        t->Prop("EndY", VType::Number, Value::Number(0), AffectsMeasure | AffectsRender);
        t->Prop("StrokeThickness", VType::Number, Value::Number(1), AffectsMeasure | AffectsRender);
        t->Prop("Stroke", VType::Color,
                Value::MakeColor(Color{0, 0, 0, 1, Token::CardBorder}), AffectsRender,
                BindingMode::OneWay, true, true);
        add(t);
    }
    {
        auto t = std::make_shared<TypeInfo>("wui/Shape", ContentModel::None,
                                            []() -> WidgetPtr { return std::make_shared<Shape>(); });
        AddLayoutProps(*t);
        // One class with a Kind enum instead of a class per geometry: layout and paint
        // are identical, and splitting would duplicate five methods to change two lines.
        t->EnumProp("Kind", {"Rectangle", "Ellipse"}, 0, AffectsRender);
        t->Prop("ShapeWidth", VType::Number, Value::Number(40), AffectsMeasure);
        t->Prop("ShapeHeight", VType::Number, Value::Number(40), AffectsMeasure);
        t->Prop("CornerRadius", VType::Number, Value::Number(0), AffectsRender);
        t->Prop("StrokeThickness", VType::Number, Value::Number(0), AffectsRender);
        t->Prop("Fill", VType::Color,
                Value::MakeColor(Color{0, 0, 0, 1, Token::ControlFill}), AffectsRender,
                BindingMode::OneWay, true, true);
        t->Prop("Stroke", VType::Color,
                Value::MakeColor(Color{0, 0, 0, 1, Token::CardBorder}), AffectsRender,
                BindingMode::OneWay, true, true);
        add(t);
    }
    {
        auto t = std::make_shared<TypeInfo>("wui/ToggleButton", ContentModel::TextContent,
                                            []() -> WidgetPtr { return std::make_shared<ToggleButton>(); });
        t->SetContentProperty("Content");
        AddLayoutProps(*t);
        t->Prop("Content", VType::Text, Value::Text(L"ToggleButton"), AffectsMeasure);
        // TwoWay: clicking is the user changing the value, and the ViewModel has to
        // hear about it.
        t->Prop("IsChecked", VType::Bool, Value::Bool(false), AffectsRender, BindingMode::TwoWay);
        t->Prop("IsEnabled", VType::Bool, Value::Bool(true), AffectsRender);
        t->Prop("FontSize", VType::Number, Value::Number(14), AffectsMeasure);
        t->Prop("Foreground", VType::Color,
                Value::MakeColor(Color{0, 0, 0, 1, Token::TextPrimary}), AffectsRender,
                BindingMode::OneWay, true, true);
        add(t);
    }
    {
        auto t = std::make_shared<TypeInfo>("wui/ToggleSwitch", ContentModel::TextContent,
                                            []() -> WidgetPtr { return std::make_shared<ToggleSwitch>(); });
        t->SetContentProperty("Content");
        AddLayoutProps(*t);
        t->Prop("Content", VType::Text, Value::Text(L""), AffectsMeasure);
        t->Prop("IsChecked", VType::Bool, Value::Bool(false), AffectsRender, BindingMode::TwoWay);
        t->Prop("IsEnabled", VType::Bool, Value::Bool(true), AffectsRender);
        t->Prop("FontSize", VType::Number, Value::Number(13), AffectsMeasure);
        t->Prop("Foreground", VType::Color,
                Value::MakeColor(Color{0, 0, 0, 1, Token::TextPrimary}), AffectsRender,
                BindingMode::OneWay, true, true);
        add(t);
    }
    {
        auto t = std::make_shared<TypeInfo>("wui/HyperlinkButton", ContentModel::TextContent,
                                            []() -> WidgetPtr { return std::make_shared<HyperlinkButton>(); });
        t->SetContentProperty("Content");
        AddLayoutProps(*t);
        t->Prop("Content", VType::Text, Value::Text(L"Link"), AffectsMeasure);
        t->Prop("Command", VType::Ref, Value::Null(), AffectsRender, BindingMode::OneWay);
        t->Prop("IsEnabled", VType::Bool, Value::Bool(true), AffectsRender);
        t->Prop("FontSize", VType::Number, Value::Number(14), AffectsMeasure);
        t->Prop("Foreground", VType::Color,
                Value::MakeColor(Color{0, 0, 0, 1, Token::Accent}), AffectsRender, BindingMode::OneWay,
                true, true);
        add(t);
    }
    {
        auto t = std::make_shared<TypeInfo>("wui/Slider", ContentModel::None,
                                            []() -> WidgetPtr { return std::make_shared<Slider>(); });
        AddLayoutProps(*t);
        // A real range, unlike ProgressBar's fraction: a slider's bounds are part of what
        // it means (volume 0..100). Value is TwoWay -- dragging is the user setting it.
        t->Prop("Minimum", VType::Number, Value::Number(0), AffectsRender);
        t->Prop("Maximum", VType::Number, Value::Number(100), AffectsRender);
        t->Prop("Value", VType::Number, Value::Number(0), AffectsRender, BindingMode::TwoWay);
        // 0 means "no snapping"; a slider bound to a frequency list sets one.
        t->Prop("StepFrequency", VType::Number, Value::Number(0), AffectsRender);
        t->Prop("IsEnabled", VType::Bool, Value::Bool(true), AffectsRender);
        add(t);
    }
    {
        auto t = std::make_shared<TypeInfo>("wui/RepeatButton", ContentModel::TextContent,
                                            []() -> WidgetPtr { return std::make_shared<RepeatButton>(); });
        t->SetContentProperty("Content");
        AddLayoutProps(*t);
        t->Prop("Content", VType::Text, Value::Text(L"Repeat"), AffectsMeasure);
        t->Prop("Command", VType::Ref, Value::Null(), AffectsRender, BindingMode::OneWay);
        t->Prop("IsEnabled", VType::Bool, Value::Bool(true), AffectsRender);
        t->Prop("FontSize", VType::Number, Value::Number(14), AffectsMeasure);
        add(t);
    }
    {
        auto t = std::make_shared<TypeInfo>("wui/PasswordBox", ContentModel::None,
                                            []() -> WidgetPtr { return std::make_shared<PasswordBox>(); });
        AddLayoutProps(*t);
        // The real characters live in Text, so a ViewModel still reads what was typed --
        // the mask is a paint concern only.
        t->Prop("Text", VType::Text, Value::Text(L""), AffectsMeasure | AffectsRender,
                BindingMode::TwoWay);
        t->Prop("PlaceholderText", VType::Text, Value::Text(L""), AffectsRender);
        t->Prop("FontSize", VType::Number, Value::Number(14), AffectsMeasure);
        t->Prop("Padding", VType::Thickness, Value::MakeThickness(Thickness{8, 6, 8, 6}),
                AffectsMeasure);
        t->Prop("MinHeight", VType::Number, Value::Number(30), AffectsMeasure);
        t->Prop("Foreground", VType::Color,
                Value::MakeColor(Color{0, 0, 0, 1, Token::TextPrimary}), AffectsRender,
                BindingMode::OneWay, true, true);
        add(t);
    }
    {
        auto t = std::make_shared<TypeInfo>("wui/NumberBox", ContentModel::None,
                                            []() -> WidgetPtr { return std::make_shared<NumberBox>(); });
        AddLayoutProps(*t);
        t->Prop("Text", VType::Text, Value::Text(L""), AffectsMeasure | AffectsRender,
                BindingMode::TwoWay);
        t->Prop("Value", VType::Number, Value::Number(0), AffectsRender, BindingMode::TwoWay);
        // IsValid is PUBLISHED, not enforced: a user typing "-" or "1e" mid-number must be
        // allowed to, and the ViewModel decides whether an invalid entry blocks anything.
        t->Prop("IsValid", VType::Bool, Value::Bool(true), AffectsRender);
        t->Prop("Minimum", VType::Number, Value::Number(-1.0e18), AffectsRender);
        t->Prop("Maximum", VType::Number, Value::Number(1.0e18), AffectsRender);
        t->Prop("PlaceholderText", VType::Text, Value::Text(L""), AffectsRender);
        t->Prop("FontSize", VType::Number, Value::Number(14), AffectsMeasure);
        t->Prop("Padding", VType::Thickness, Value::MakeThickness(Thickness{8, 6, 8, 6}),
                AffectsMeasure);
        t->Prop("MinHeight", VType::Number, Value::Number(30), AffectsMeasure);
        t->Prop("Foreground", VType::Color,
                Value::MakeColor(Color{0, 0, 0, 1, Token::TextPrimary}), AffectsRender,
                BindingMode::OneWay, true, true);
        add(t);
    }
    {
        auto t = std::make_shared<TypeInfo>("wui/RadioButton", ContentModel::TextContent,
                                            []() -> WidgetPtr { return std::make_shared<RadioButton>(); });
        t->SetContentProperty("Content");
        AddLayoutProps(*t);
        t->Prop("Content", VType::Text, Value::Text(L"Option"), AffectsMeasure);
        // GroupName is a string, not a per-parent scope: radios in one group are routinely
        // spread across panels, and scoping to the parent would make a re-layout silently
        // change which radios are exclusive with each other.
        t->Prop("GroupName", VType::Text, Value::Text(L"default"), 0);
        t->Prop("IsChecked", VType::Bool, Value::Bool(false), AffectsRender, BindingMode::TwoWay);
        t->Prop("IsEnabled", VType::Bool, Value::Bool(true), AffectsRender);
        t->Prop("FontSize", VType::Number, Value::Number(14), AffectsMeasure);
        t->Prop("Foreground", VType::Color,
                Value::MakeColor(Color{0, 0, 0, 1, Token::TextPrimary}), AffectsRender,
                BindingMode::OneWay, true, true);
        add(t);
    }
    {
        auto t = std::make_shared<TypeInfo>("wui/RelativePanel", ContentModel::ManyChildren,
                                            []() -> WidgetPtr { return std::make_shared<RelativePanel>(); });
        AddLayoutProps(*t);
        t->Prop("Padding", VType::Thickness, Value::MakeThickness(Thickness{}), AffectsMeasure);
                // The anchors themselves are declared in AddLayoutProps, because they are written
        add(t);
    }
    {
        auto t = std::make_shared<TypeInfo>("wui/Image", ContentModel::None,
                                            []() -> WidgetPtr { return std::make_shared<Image>(); });
        AddLayoutProps(*t);
        t->Prop("Source", VType::Text, Value::Text(L""), AffectsMeasure | AffectsRender);
        // 0 = Uniform (keep the aspect ratio), 1 = Fill (stretch). The default keeps the
        // ratio because a squashed photo is the most visible layout bug a picture can have.
        t->EnumProp("Stretch", {"Uniform", "Fill", "None"}, 0, AffectsMeasure);
        add(t);
    }
    {
        auto t = std::make_shared<TypeInfo>("wui/PersonPicture", ContentModel::None,
                                            []() -> WidgetPtr { return std::make_shared<PersonPicture>(); });
        AddLayoutProps(*t);
        t->Prop("Source", VType::Text, Value::Text(L""), AffectsMeasure | AffectsRender);
        t->Prop("DisplayName", VType::Text, Value::Text(L""), AffectsRender);
        // One Diameter rather than Width/Height: a person picture that is not round looks
        // like a bug, so the control does not offer the choice.
        t->Prop("Diameter", VType::Number, Value::Number(40), AffectsMeasure);
        add(t);
    }
    {
        auto t = std::make_shared<TypeInfo>("wui/Flyout", ContentModel::SingleChild,
                                            []() -> WidgetPtr { return std::make_shared<Flyout>(); });
        AddLayoutProps(*t);
        t->Prop("Content", VType::Text, Value::Text(L"Open"), AffectsMeasure);
        // IsOpen is TwoWay: the ViewModel can open it, and the layer reports that the user
        // dismissed it. Both directions must land in the same slot or the caret lies.
        t->Prop("IsOpen", VType::Bool, Value::Bool(false), AffectsRender, BindingMode::TwoWay);
        t->Prop("IsEnabled", VType::Bool, Value::Bool(true), AffectsRender);
        t->Prop("FontSize", VType::Number, Value::Number(13), AffectsMeasure);
        add(t);
    }
    {
        auto t = std::make_shared<TypeInfo>("wui/Popup", ContentModel::SingleChild,
                                            []() -> WidgetPtr { return std::make_shared<Popup>(); });
        AddLayoutProps(*t);
        t->Prop("Content", VType::Text, Value::Text(L"Open"), AffectsMeasure);
        t->Prop("IsOpen", VType::Bool, Value::Bool(false), AffectsRender, BindingMode::TwoWay);
        t->Prop("IsEnabled", VType::Bool, Value::Bool(true), AffectsRender);
        t->Prop("FontSize", VType::Number, Value::Number(13), AffectsMeasure);
        add(t);
    }
    {
        auto t = std::make_shared<TypeInfo>("wui/AutoSuggestBox", ContentModel::None,
                                            []() -> WidgetPtr { return std::make_shared<AutoSuggestBox>(); });
        AddLayoutProps(*t);
        t->Prop("Text", VType::Text, Value::Text(L""), AffectsRender, BindingMode::TwoWay);
        // ';' again, shared with ComboBox through one SplitItems so the two cannot drift.
        t->Prop("Items", VType::Text, Value::Text(L""), AffectsMeasure);
        t->Prop("PlaceholderText", VType::Text, Value::Text(L"Search..."), AffectsRender);
        t->Prop("IsOpen", VType::Bool, Value::Bool(false), AffectsRender, BindingMode::TwoWay);
        t->Prop("IsEnabled", VType::Bool, Value::Bool(true), AffectsRender);
        t->Prop("FontSize", VType::Number, Value::Number(13), AffectsMeasure);
        add(t);
    }
    {
        auto t = std::make_shared<TypeInfo>("wui/ToolTip", ContentModel::SingleChild,
                                            []() -> WidgetPtr { return std::make_shared<ToolTip>(); });
        AddLayoutProps(*t);
        t->Prop("Text", VType::Text, Value::Text(L""), AffectsMeasure);
        add(t);
    }
    {
        auto t = std::make_shared<TypeInfo>("wui/ComboBox", ContentModel::None,
                                            []() -> WidgetPtr { return std::make_shared<ComboBox>(); });
        AddLayoutProps(*t);
        // Items are a ';'-separated string: an item template needs a templated collection,
        // and a comma separator would break on "Paris, France".
        t->Prop("Items", VType::Text, Value::Text(L""), AffectsMeasure);
        t->Prop("SelectedIndex", VType::Number, Value::Number(-1), AffectsRender,
                BindingMode::TwoWay);
        // Derived from SelectedIndex, so binding either one is safe and they cannot
        // disagree.
        t->Prop("SelectedText", VType::Text, Value::Text(L""), AffectsRender, BindingMode::OneWay);
        t->Prop("PlaceholderText", VType::Text, Value::Text(L"Select..."), AffectsRender);
        t->Prop("IsOpen", VType::Bool, Value::Bool(false), AffectsRender, BindingMode::TwoWay);
        t->Prop("IsEnabled", VType::Bool, Value::Bool(true), AffectsRender);
        t->Prop("FontSize", VType::Number, Value::Number(13), AffectsMeasure);
        add(t);
    }
}

} // namespace wui
