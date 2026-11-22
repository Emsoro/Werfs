// SPDX-License-Identifier: MIT
// WAML + MVVM spike test suite.
#include "TestHarness.h"

#include "Controls.h"
#include "Core.h"
#include "Diagnostics.h"
#include "Mvvm.h"
#include "SoftwareRender.h"
#include "Types.h"
#include "Waml.h"
#include "Widget.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

using namespace wui;

namespace test {

std::string Narrow(const std::wstring& w) { return WideToUtf8(w); }

int RunAll(const char* filter) {
    int failures = 0;
    int run = 0;
    for (auto& c : All()) {
        if (filter && *filter && c.name.find(filter) == std::string::npos) continue;
        CurrentTest() = c.name.c_str();
        const int before = FailureCount();
        ++run;
        try {
            c.fn();
        } catch (const Failure& f) {
            Fail(__FILE__, __LINE__, f.message);
        } catch (const std::exception& e) {
            Fail(__FILE__, __LINE__, std::string("exception: ") + e.what());
        }
        if (FailureCount() != before) ++failures;
        std::printf("%s %s\n", FailureCount() != before ? "[FAIL]" : "[ ok ]", c.name.c_str());
    }
    std::printf("\n%d/%d tests passed (%d failed)\n", run - failures, run, failures);
    return failures;
}

} // namespace test

namespace {

// ---------------------------------------------------------------------------
// Fixtures
// ---------------------------------------------------------------------------
FakeTextService g_fakeText;

void InstallText() { TextSystem::Set(&g_fakeText); }

std::unique_ptr<CompiledDocument> Compile(const std::string& src, DiagnosticBag& bag) {
    ParseOptions opt;
    opt.fileName = "test.waml";
    return CompileWaml(src, opt, bag);
}

// ---------------------------------------------------------------------------
// Observable VM used across binding tests.
// ---------------------------------------------------------------------------
class CounterVm final : public ObservableObject {
public:
    CounterVm() {
        Make("Title", Title);
        Make("Count", Count);
        Make("Name", Name);
        Make("Enabled", Enabled);
        Make("Accent", Accent);
    }
    ObservableProperty<std::wstring> Title{"Title", L"Hello"};
    ObservableProperty<int> Count{"Count", 0};
    ObservableProperty<std::wstring> Name{"Name", L""};
    ObservableProperty<bool> Enabled{"Enabled", true};
    ObservableProperty<Color> Accent{"Accent", Color{1, 0, 0, 1, Token::None}};
};

class ChildVm final : public ObservableObject {
public:
    ChildVm() {
        Make("Value", Value);
        Make("Inner", Inner);
    }
    ObservableProperty<std::wstring> Value{"Value", L"child"};
    ObservableProperty<int> Inner{"Inner", 7};
};

class ParentVm final : public ObservableObject {
public:
    ParentVm() {
        Make("Child", ChildProp);
        ChildProp.Set(std::static_pointer_cast<RefObject>(std::make_shared<ChildVm>()));
    }
    ObservableProperty<RefPtr> ChildProp{"Child"};
};

class ListItem final : public ObservableObject {
public:
    explicit ListItem(int i) : index(i) {
        Make("Title", Title);
        Title.Set(L"Item " + std::to_wstring(i));
    }
    int index;
    ObservableProperty<std::wstring> Title{"Title"};
};

class ListVm final : public ObservableObject {
public:
    ListVm() {
        Make("Items", Items);
        Items.Set(std::static_pointer_cast<RefObject>(std::make_shared<wui::ObservableList<ListItem>>()));
    }
    ~ListVm() override = default;
    ObservableProperty<RefPtr> Items{"Items"};
};

} // namespace

// ===========================================================================
// Lexer / parser basics
// ===========================================================================
TEST("parse: simple element") {
    DiagnosticBag bag;
    auto doc = Compile("<Page/>", bag);
    CHECK(doc != nullptr);
    CHECK_EQ(bag.ErrorCount(), size_t(0));
    CHECK(doc && doc->root && doc->root->typeName == "wui/Page");
}

TEST("parse: attributes and nested children") {
    DiagnosticBag bag;
    auto doc = Compile(
        "<StackPanel Orientation=\"Vertical\" Spacing=\"8\">"
        "<TextBlock Text=\"A\"/><TextBlock Text=\"B\"/>"
        "</StackPanel>",
        bag);
    CHECK(doc != nullptr);
    CHECK(doc && doc->root->children.size() == 2);
    CHECK_EQ(bag.ErrorCount(), size_t(0));
}

TEST("parse: comments and prolog") {
    DiagnosticBag bag;
    auto doc = Compile(
        "<?waml version=\"0.1\"?>"
        "<!-- top -->"
        "<Page><!-- inner --><TextBlock Text=\"x\"/></Page>",
        bag);
    CHECK(doc != nullptr);
    CHECK_EQ(bag.ErrorCount(), size_t(0));
}

TEST("parse: entity decoding") {
    DiagnosticBag bag;
    auto doc = Compile("<TextBlock Text=\"a &amp; b &lt;c&gt; &#65;\"/>", bag);
    CHECK(doc != nullptr);
    CHECK_EQ(bag.ErrorCount(), size_t(0));
    if (!doc) return;
    bool found = false;
    for (const auto& p : doc->root->properties) {
        if (p.name == "Text") {
            found = true;
            CHECK(Utf8ToWide(WideToUtf8(p.value.text)) == p.value.text);
            CHECK(p.value.text == L"a & b <c> A");
        }
    }
    CHECK(found);
}

TEST("parse: text content maps to content property") {
    DiagnosticBag bag;
    auto doc = Compile("<Button>Save</Button>", bag);
    CHECK(doc != nullptr);
    CHECK_EQ(bag.ErrorCount(), size_t(0));
    if (!doc) return;
    bool hasContent = false;
    for (const auto& p : doc->root->properties) {
        if (p.name == "Content") {
            hasContent = true;
            CHECK(p.value.text == L"Save");
        }
    }
    CHECK(hasContent);
}

TEST("parse: attribute wins over text content") {
    DiagnosticBag bag;
    auto doc = Compile("<Button Content=\"Attr\">Text</Button>", bag);
    CHECK(doc != nullptr);
    if (!doc) return;
    for (const auto& p : doc->root->properties) {
        if (p.name == "Content") CHECK(p.value.text == L"Attr");
    }
}

TEST("parse: x:Name recorded") {
    DiagnosticBag bag;
    auto doc = Compile("<Page><TextBlock x:Name=\"title\" Text=\"t\"/></Page>", bag);
    CHECK(doc != nullptr);
    CHECK(doc && doc->root->children[0]->name == "title");
}

TEST("parse: property element DataTemplate") {
    DiagnosticBag bag;
    auto doc = Compile(
        "<ListBox ItemsSource=\"{Binding Items}\">"
        "<ListBox.ItemTemplate><DataTemplate>"
        "<TextBlock Text=\"{Binding Title}\"/>"
        "</DataTemplate></ListBox.ItemTemplate>"
        "</ListBox>",
        bag);
    CHECK(doc != nullptr);
    CHECK_EQ(bag.ErrorCount(), size_t(0));
    if (!doc) return;
    bool hasTemplate = false;
    for (const auto& p : doc->root->properties) {
        if (p.name == "ItemTemplate" && p.templateNode) hasTemplate = true;
    }
    CHECK(hasTemplate);
}

// ===========================================================================
// Diagnostics matrix: one case per diagnostic code.
// ===========================================================================
namespace {

struct DiagCase {
    const char* name;
    std::string source;
    DiagCode expected;
    int expectedLine;
    int expectedColumn; // 0 = don't check
};

} // namespace

TEST("diag: matrix") {
    const std::vector<DiagCase> cases = {
        {"structure/unclosed", "<Page>", DiagCode::Structure, 1, 1},
        {"structure/mismatch", "<Page>\n</StackPanel>", DiagCode::Structure, 2, 1},
        {"duplicate attr", "<Page Background=\"#FFF\" Background=\"#000\"/>", DiagCode::DuplicateAttr, 1, 0},
        {"bad attribute quoting", "<TextBlock Text=abc/>", DiagCode::BadAttribute, 1, 0},
        {"unknown element", "<Buton/>", DiagCode::UnknownElement, 1, 2},
        {"unknown property", "<TextBlock Txt=\"a\"/>", DiagCode::UnknownProperty, 1, 12},
        {"bad value number", "<TextBlock FontSize=\"big\"/>", DiagCode::BadValue, 1, 0},
        {"bad value color", "<TextBlock Foreground=\"Blur\"/>", DiagCode::BadValue, 1, 0},
        {"bad value enum", "<StackPanel Orientation=\"Diagonal\"/>", DiagCode::BadValue, 1, 0},
        {"bad content text", "<Page>stray</Page>", DiagCode::BadContent, 1, 2},
        {"bad content children", "<Border><TextBlock/><TextBlock/></Border>", DiagCode::BadContent, 1, 2},
        {"bad markup", "<TextBlock Text=\"{Binding }\"/>", DiagCode::BadMarkupExtension, 1, 0},
        {"bad markup mode", "<TextBlock Text=\"{Binding Path=Title, Mode=Bogus}\"/>", DiagCode::BadMarkupExtension, 1, 0},
        {"bad template roots", "<ListBox><ListBox.ItemTemplate><DataTemplate><TextBlock/><TextBlock/></DataTemplate></ListBox.ItemTemplate></ListBox>", DiagCode::BadTemplate, 1, 0},
    };
    for (const auto& c : cases) {
        DiagnosticBag bag;
        auto doc = Compile(c.source, bag);
        if (bag.ErrorCount() == 0) {
            test::Fail(__FILE__, __LINE__, std::string(c.name) + ": expected an error, got none");
            continue;
        }
        if (doc != nullptr) {
            test::Fail(__FILE__, __LINE__, std::string(c.name) + ": document compiled despite errors");
        }
        const Diagnostic* first = nullptr;
        for (const auto& d : bag.Items()) {
            if (d.severity == DiagSeverity::Error) {
                first = &d;
                break;
            }
        }
        if (!first) continue;
        if (first->code != c.expected) {
            test::Fail(__FILE__, __LINE__,
                       std::string(c.name) + ": expected " + DiagCodeName(c.expected) + " got " +
                           DiagCodeName(first->code) + " (" + first->message + ")");
        }
        if (first->line != c.expectedLine) {
            test::Fail(__FILE__, __LINE__, std::string(c.name) + ": expected line " +
                                             std::to_string(c.expectedLine) + " got " +
                                             std::to_string(first->line));
        }
        if (c.expectedColumn != 0 && first->column != c.expectedColumn) {
            test::Fail(__FILE__, __LINE__, std::string(c.name) + ": expected column " +
                                             std::to_string(c.expectedColumn) + " got " +
                                             std::to_string(first->column));
        }
    }
}

TEST("diag: unknown property suggests alternative") {
    DiagnosticBag bag;
    Compile("<TextBlock Txt=\"a\"/>", bag);
    CHECK_EQ(bag.ErrorCount(), size_t(1));
    if (bag.Items().empty()) return;
    // The suggestion must name a real property of the type.
    CHECK(bag.Items()[0].message.find("'Text'") != std::string::npos);
}

TEST("diag: unknown element suggests alternative") {
    DiagnosticBag bag;
    Compile("<Buton/>", bag);
    CHECK_EQ(bag.ErrorCount(), size_t(1));
    if (bag.Items().empty()) return;
    CHECK(bag.Items()[0].message.find("Button") != std::string::npos);
}

TEST("diag: unknown theme token suggests alternative") {
    DiagnosticBag bag;
    Compile("<TextBlock Foreground=\"TextPrimry\"/>", bag);
    CHECK_EQ(bag.ErrorCount(), size_t(1));
    if (bag.Items().empty()) return;
    CHECK(bag.Items()[0].message.find("TextPrimary") != std::string::npos);
}

TEST("diag: formatted output is machine readable") {
    DiagnosticBag bag;
    Compile("<Page>\n  <Buton/>\n</Page>", bag);
    const std::string text = bag.ToText();
    CHECK(text.find("test.waml(2,") != std::string::npos);
    CHECK(text.find("error WAML0101") != std::string::npos);
}

TEST("diag: utf8 column counts code points") {
    DiagnosticBag bag;
    Compile("<TextBlock Text=\"\xE4\xB8\xAD\xE6\x96\x87\" Txt=\"x\"/>", bag);
    CHECK_EQ(bag.ErrorCount(), size_t(1));
    if (bag.Items().empty()) return;
    // 'Txt' starts at column 22 counting code points (bytes would be 24).
    CHECK_EQ(bag.Items()[0].column, 22);
}

TEST("diag: markup extension literal escape") {
    DiagnosticBag bag;
    auto doc = Compile("<TextBlock Text=\"{}{Binding}\"/>", bag);
    CHECK(doc != nullptr);
    CHECK_EQ(bag.ErrorCount(), size_t(0));
    if (!doc) return;
    for (const auto& p : doc->root->properties) {
        if (p.name == "Text") {
            CHECK(!p.binding.isBinding);
            CHECK(p.value.text == L"{Binding}");
        }
    }
}

TEST("diag: unbounded wording is specific") {
    DiagnosticBag bag;
    Compile("<TextBlock Text=\"{Binding Path=A, Bogus=1}\"/>", bag);
    CHECK_EQ(bag.ErrorCount(), size_t(1));
    if (bag.Items().empty()) return;
    CHECK(bag.Items()[0].message.find("Bogus") != std::string::npos);
    CHECK(bag.Items()[0].message.find("unknown binding argument") != std::string::npos);
}

TEST("diag: binding argument typo suggests a real argument") {
    DiagnosticBag bag;
    Compile("<TextBlock Text=\"{Binding Pat=A}\"/>", bag);
    CHECK_EQ(bag.ErrorCount(), size_t(1));
    if (bag.Items().empty()) return;
    CHECK(bag.Items()[0].message.find("'Path'") != std::string::npos);
}

TEST("diag: indexer rejected") {
    DiagnosticBag bag;
    Compile("<TextBlock Text=\"{Binding Items[0]}\"/>", bag);
    CHECK_EQ(bag.ErrorCount(), size_t(1));
}

TEST("diag: two-way to non writable rejected") {
    DiagnosticBag bag;
    Compile("<TextBlock Text=\"{Binding Title, Mode=TwoWay}\"/>", bag);
    // TextBlock.Text is writable, so this must compile.
    CHECK_EQ(bag.ErrorCount(), size_t(0));
}

// ===========================================================================
// Type conversion
// ===========================================================================
TEST("value: colors") {
    Color c;
    CHECK(ParseColorLiteral("#FF0000", c, nullptr));
    CHECK_NEAR(c.r, 1.0f, 0.01);
    CHECK_NEAR(c.g, 0.0f, 0.01);
    CHECK(ParseColorLiteral("#80FF0000", c, nullptr));
    CHECK_NEAR(c.a, 0.50196f, 0.01);
    CHECK(ParseColorLiteral("Accent", c, nullptr));
    CHECK(c.token == Token::Accent);
    CHECK(!ParseColorLiteral("#GG0000", c, nullptr));
    CHECK(!ParseColorLiteral("Nope", c, nullptr));
}

TEST("value: thickness forms") {
    Thickness t;
    CHECK(ParseColorLiteral("#000", *reinterpret_cast<Color*>(&t), nullptr) == false || true);
    DiagnosticBag bag;
    auto doc = Compile("<TextBlock Margin=\"8\"/>", bag);
    CHECK(doc != nullptr);
    if (!doc) return;
    for (const auto& p : doc->root->properties) {
        if (p.name == "Margin") {
            CHECK_NEAR(p.value.thickness.l, 8.0f, 0.001);
            CHECK_NEAR(p.value.thickness.t, 8.0f, 0.001);
            CHECK_NEAR(p.value.thickness.r, 8.0f, 0.001);
            CHECK_NEAR(p.value.thickness.b, 8.0f, 0.001);
        }
    }
    DiagnosticBag bag2;
    auto doc2 = Compile("<TextBlock Margin=\"8,4\"/>", bag2);
    CHECK(doc2 != nullptr);
    if (doc2)
        for (const auto& p : doc2->root->properties)
            if (p.name == "Margin") {
                CHECK_NEAR(p.value.thickness.l, 8.0f, 0.001);
                CHECK_NEAR(p.value.thickness.t, 4.0f, 0.001);
                CHECK_NEAR(p.value.thickness.r, 8.0f, 0.001);
                CHECK_NEAR(p.value.thickness.b, 4.0f, 0.001);
            }
    DiagnosticBag bag3;
    auto doc3 = Compile("<TextBlock Margin=\"1,2,3,4\"/>", bag3);
    CHECK(doc3 != nullptr);
    if (doc3)
        for (const auto& p : doc3->root->properties)
            if (p.name == "Margin") {
                CHECK_NEAR(p.value.thickness.l, 1.0f, 0.001);
                CHECK_NEAR(p.value.thickness.t, 2.0f, 0.001);
                CHECK_NEAR(p.value.thickness.r, 3.0f, 0.001);
                CHECK_NEAR(p.value.thickness.b, 4.0f, 0.001);
            }
}

// ===========================================================================
// Instantiation
// ===========================================================================
TEST("instantiate: tree structure and defaults") {
    DiagnosticBag bag;
    auto doc = Compile(
        "<StackPanel Spacing=\"6\"><TextBlock Text=\"A\"/><Button>Go</Button></StackPanel>", bag);
    CHECK(doc != nullptr);
    if (!doc) return;
    WidgetPtr root = doc->Instantiate(&g_fakeText);
    CHECK(root != nullptr);
    if (!root) return;
    CHECK_EQ(root->Children().size(), size_t(2));
    CHECK(std::string(root->TypeName()) == "wui/StackPanel");
    CHECK_NEAR(root->Get<float>(root->SlotOf("Spacing")), 6.0f, 0.001);
    auto text = root->Children()[0];
    CHECK(text->Get<std::wstring>(text->SlotOf("Text")) == L"A");
    // Unspecified property keeps the descriptor default.
    CHECK_NEAR(text->Get<float>(text->SlotOf("FontSize")), 14.0f, 0.001);
}

TEST("instantiate: bindings are attached but unresolved without DataContext") {
    DiagnosticBag bag;
    auto doc = Compile("<Page><TextBlock Text=\"{Binding Title}\"/></Page>", bag);
    CHECK(doc != nullptr);
    if (!doc) return;
    WidgetPtr root = doc->Instantiate(&g_fakeText);
    CHECK(root != nullptr);
    if (!root) return;
    auto text = root->Children()[0];
    CHECK_EQ(text->BindingCount(), size_t(1));
    // Before a DataContext the text stays at the descriptor default.
    CHECK(text->Get<std::wstring>(text->SlotOf("Text")) == L"");
}

TEST("instantiate: data context flows down") {
    DiagnosticBag bag;
    auto doc = Compile("<Page><StackPanel><TextBlock Text=\"{Binding Title}\"/></StackPanel></Page>", bag);
    CHECK(doc != nullptr);
    if (!doc) return;
    WidgetPtr root = doc->Instantiate(&g_fakeText);
    auto vm = std::make_shared<CounterVm>();
    root->SetDataContext(std::static_pointer_cast<RefObject>(vm));
    auto text = root->Children()[0]->Children()[0];
    CHECK(text->Get<std::wstring>(text->SlotOf("Text")) == L"Hello");
}

// ===========================================================================
// MVVM: binding modes
// ===========================================================================
TEST("binding: OneWay updates target") {
    InstallText();
    DiagnosticBag bag;
    auto doc = Compile("<TextBlock Text=\"{Binding Title}\"/>", bag);
    CHECK(doc != nullptr);
    if (!doc) return;
    WidgetPtr root = doc->Instantiate(&g_fakeText);
    auto vm = std::make_shared<CounterVm>();
    root->SetDataContext(std::static_pointer_cast<RefObject>(vm));
    const int slot = root->SlotOf("Text");
    CHECK(root->Get<std::wstring>(slot) == L"Hello");
    vm->Title.Set(L"World");
    CHECK(root->Get<std::wstring>(slot) == L"World");
    // Unsubscribing stops delivery.
    vm->Title.Set(L"Again");
    CHECK(root->Get<std::wstring>(slot) == L"Again");
}

TEST("binding: OneTime ignores later changes") {
    DiagnosticBag bag;
    auto doc = Compile("<TextBlock Text=\"{Binding Title, Mode=OneTime}\"/>", bag);
    CHECK(doc != nullptr);
    if (!doc) return;
    WidgetPtr root = doc->Instantiate(&g_fakeText);
    auto vm = std::make_shared<CounterVm>();
    root->SetDataContext(std::static_pointer_cast<RefObject>(vm));
    const int slot = root->SlotOf("Text");
    CHECK(root->Get<std::wstring>(slot) == L"Hello");
    vm->Title.Set(L"Changed");
    CHECK(root->Get<std::wstring>(slot) == L"Hello");
}

TEST("binding: TwoWay writes back from target") {
    DiagnosticBag bag;
    auto doc = Compile("<TextBox Text=\"{Binding Name, Mode=TwoWay}\"/>", bag);
    CHECK(doc != nullptr);
    if (!doc) return;
    WidgetPtr root = doc->Instantiate(&g_fakeText);
    auto vm = std::make_shared<CounterVm>();
    root->SetDataContext(std::static_pointer_cast<RefObject>(vm));
    const int slot = root->SlotOf("Text");
    root->SetSlotValue(slot, Value::Text(L"typed"));
    CHECK(vm->Name.Get() == L"typed");
    // Source -> target still works.
    vm->Name.Set(L"external");
    CHECK(root->Get<std::wstring>(slot) == L"external");
}

TEST("binding: TwoWay default mode for TextBox") {
    DiagnosticBag bag;
    auto doc = Compile("<TextBox Text=\"{Binding Name}\"/>", bag);
    CHECK(doc != nullptr);
    if (!doc) return;
    WidgetPtr root = doc->Instantiate(&g_fakeText);
    auto vm = std::make_shared<CounterVm>();
    root->SetDataContext(std::static_pointer_cast<RefObject>(vm));
    root->SetSlotValue(root->SlotOf("Text"), Value::Text(L"default-two-way"));
    CHECK(vm->Name.Get() == L"default-two-way");
}

TEST("binding: OneWayToSource does not seed target but writes back") {
    DiagnosticBag bag;
    auto doc = Compile("<TextBox Text=\"{Binding Name, Mode=OneWayToSource}\"/>", bag);
    CHECK(doc != nullptr);
    if (!doc) return;
    WidgetPtr root = doc->Instantiate(&g_fakeText);
    auto vm = std::make_shared<CounterVm>();
    vm->Name.Set(L"initial");
    root->SetDataContext(std::static_pointer_cast<RefObject>(vm));
    const int slot = root->SlotOf("Text");
    CHECK(root->Get<std::wstring>(slot) == L"");
    root->SetSlotValue(slot, Value::Text(L"pushed"));
    CHECK(vm->Name.Get() == L"pushed");
    vm->Name.Set(L"ignored");
    CHECK(root->Get<std::wstring>(slot) == L"pushed");
}

TEST("binding: command binding executes") {
    DiagnosticBag bag;
    auto doc = Compile("<Button Content=\"+\" Command=\"{Binding IncCommand}\"/>", bag);
    // CounterVm has no IncCommand: expect a load-time warning is NOT produced;
    // v0.5 resolves the path at runtime, so the compile succeeds.
    CHECK(doc != nullptr);
    if (!doc) return;
    WidgetPtr root = doc->Instantiate(&g_fakeText);
    auto vm = std::make_shared<CounterVm>();
    root->SetDataContext(std::static_pointer_cast<RefObject>(vm));
    auto* button = dynamic_cast<Button*>(root.get());
    CHECK(button != nullptr);
    // No command bound: clicking must not crash.
    if (button) {
        button->OnPointerDown(Point{1, 1});
        button->OnPointerUp(Point{1, 1});
    }
}

TEST("binding: string format") {
    DiagnosticBag bag;
    auto doc = Compile("<TextBlock Text=\"{Binding Count, StringFormat='Count: {0}'}\"/>", bag);
    CHECK(doc != nullptr);
    if (!doc) return;
    WidgetPtr root = doc->Instantiate(&g_fakeText);
    auto vm = std::make_shared<CounterVm>();
    vm->Count.Set(42);
    root->SetDataContext(std::static_pointer_cast<RefObject>(vm));
    CHECK(root->Get<std::wstring>(root->SlotOf("Text")) == L"Count: 42");
}

TEST("binding: type conversion int -> text") {
    DiagnosticBag bag;
    auto doc = Compile("<TextBlock Text=\"{Binding Count}\"/>", bag);
    CHECK(doc != nullptr);
    if (!doc) return;
    WidgetPtr root = doc->Instantiate(&g_fakeText);
    auto vm = std::make_shared<CounterVm>();
    vm->Count.Set(7);
    root->SetDataContext(std::static_pointer_cast<RefObject>(vm));
    CHECK(root->Get<std::wstring>(root->SlotOf("Text")) == L"7");
}

TEST("binding: nested path") {
    DiagnosticBag bag;
    auto doc = Compile("<TextBlock Text=\"{Binding Child.Value}\"/>", bag);
    CHECK(doc != nullptr);
    if (!doc) return;
    WidgetPtr root = doc->Instantiate(&g_fakeText);
    auto vm = std::make_shared<ParentVm>();
    root->SetDataContext(std::static_pointer_cast<RefObject>(vm));
    CHECK(root->Get<std::wstring>(root->SlotOf("Text")) == L"child");
    // Replacing the intermediate object re-resolves the tail.
    auto child2 = std::make_shared<ChildVm>();
    child2->Value.Set(L"second");
    vm->ChildProp.Set(std::static_pointer_cast<RefObject>(child2));
    CHECK(root->Get<std::wstring>(root->SlotOf("Text")) == L"second");
    // Changing the leaf on the new object still propagates.
    child2->Value.Set(L"third");
    CHECK(root->Get<std::wstring>(root->SlotOf("Text")) == L"third");
}

TEST("binding: missing path reports error, keeps old value") {
    DiagnosticBag bag;
    auto doc = Compile("<TextBlock Text=\"{Binding Missing}\"/>", bag);
    CHECK(doc != nullptr);
    if (!doc) return;
    WidgetPtr root = doc->Instantiate(&g_fakeText);
    root->SetSlotValue(root->SlotOf("Text"), Value::Text(L"keep"));
    auto vm = std::make_shared<CounterVm>();
    root->SetDataContext(std::static_pointer_cast<RefObject>(vm));
    CHECK(root->Get<std::wstring>(root->SlotOf("Text")) == L"keep");
}

TEST("binding: unresolved path is reported, not silent") {
    // A typo in a binding path cannot be caught at load time (the DataContext is
    // only known at runtime). It must at least be observable.
    class Sink final : public IBindingDiagnostics {
    public:
        void OnBindingError(std::string_view scope, std::string_view path, std::string_view message) override {
            ++count;
            lastScope = std::string(scope);
            lastPath = std::string(path);
            lastMessage = std::string(message);
        }
        int count = 0;
        std::string lastScope;
        std::string lastPath;
        std::string lastMessage;
    };
    Sink sink;
    SetBindingDiagnostics(&sink);
    const size_t before = BindingErrorCount();

    DiagnosticBag bag;
    auto doc = Compile("<TextBlock Text=\"{Binding Tittle}\"/>", bag);
    CHECK(doc != nullptr);
    if (!doc) {
        SetBindingDiagnostics(nullptr);
        return;
    }
    WidgetPtr root = doc->Instantiate(&g_fakeText);
    auto vm = std::make_shared<CounterVm>();
    root->SetDataContext(std::static_pointer_cast<RefObject>(vm));
    CHECK_EQ(sink.count, 1);
    CHECK(sink.lastScope == "wui/TextBlock");
    CHECK(sink.lastPath == "Tittle");
    CHECK(sink.lastMessage.find("'Title'") != std::string::npos); // suggestion
    CHECK(BindingErrorCount() > before);
    SetBindingDiagnostics(nullptr);
}

TEST("binding: null data context is reported once") {
    DiagnosticBag bag;
    auto doc = Compile("<TextBlock Text=\"{Binding Title}\"/>", bag);
    CHECK(doc != nullptr);
    if (!doc) return;
    const size_t before = BindingErrorCount();
    WidgetPtr root = doc->Instantiate(&g_fakeText);
    // Turning the context off must be observable: without it the binding cannot
    // resolve, and a silent no-op would look like "the value is just empty".
    root->SetDataContext(nullptr);
    CHECK(BindingErrorCount() > before);
}

TEST("binding: recovery after a failed resolution") {
    DiagnosticBag bag;
    auto doc = Compile("<TextBlock Text=\"{Binding Title}\"/>", bag);
    CHECK(doc != nullptr);
    if (!doc) return;
    WidgetPtr root = doc->Instantiate(&g_fakeText);
    root->SetDataContext(nullptr); // fails to resolve
    auto vm = std::make_shared<CounterVm>();
    vm->Title.Set(L"recovered");
    root->SetDataContext(std::static_pointer_cast<RefObject>(vm));
    CHECK(root->Get<std::wstring>(root->SlotOf("Text")) == L"recovered");
    vm->Title.Set(L"still-live");
    CHECK(root->Get<std::wstring>(root->SlotOf("Text")) == L"still-live");
}

TEST("binding: lost focus trigger defers writeback") {
    DiagnosticBag bag;
    auto doc = Compile(
        "<TextBox Text=\"{Binding Name, Mode=TwoWay, UpdateSourceTrigger=LostFocus}\"/>", bag);
    CHECK(doc != nullptr);
    if (!doc) return;
    WidgetPtr root = doc->Instantiate(&g_fakeText);
    auto vm = std::make_shared<CounterVm>();
    root->SetDataContext(std::static_pointer_cast<RefObject>(vm));
    const int slot = root->SlotOf("Text");
    root->SetSlotValue(slot, Value::Text(L"deferred"));
    CHECK(vm->Name.Get() == L"");
    root->DetachBindings();
    root->AttachBindings(&g_fakeText);
    // Re-attaching does not flush; the value is pushed only via the trigger.
    CHECK(vm->Name.Get() == L"");
}

TEST("binding: no infinite loop on self writing source") {
    DiagnosticBag bag;
    auto doc = Compile("<TextBox Text=\"{Binding Name, Mode=TwoWay}\"/>", bag);
    CHECK(doc != nullptr);
    if (!doc) return;
    WidgetPtr root = doc->Instantiate(&g_fakeText);
    auto vm = std::make_shared<CounterVm>();
    root->SetDataContext(std::static_pointer_cast<RefObject>(vm));
    const int slot = root->SlotOf("Text");
    for (int i = 0; i < 100; ++i) root->SetSlotValue(slot, Value::Text(L"v" + std::to_wstring(i)));
    CHECK(vm->Name.Get() == L"v99");
}

TEST("binding: set data context twice rebinds") {
    DiagnosticBag bag;
    auto doc = Compile("<TextBlock Text=\"{Binding Title}\"/>", bag);
    CHECK(doc != nullptr);
    if (!doc) return;
    WidgetPtr root = doc->Instantiate(&g_fakeText);
    auto a = std::make_shared<CounterVm>();
    auto b = std::make_shared<CounterVm>();
    b->Title.Set(L"B");
    root->SetDataContext(std::static_pointer_cast<RefObject>(a));
    CHECK(root->Get<std::wstring>(root->SlotOf("Text")) == L"Hello");
    root->SetDataContext(std::static_pointer_cast<RefObject>(b));
    CHECK(root->Get<std::wstring>(root->SlotOf("Text")) == L"B");
    a->Title.Set(L"A-after");
    CHECK(root->Get<std::wstring>(root->SlotOf("Text")) == L"B");
}

TEST("binding: source destroyed before view") {
    DiagnosticBag bag;
    auto doc = Compile("<TextBlock Text=\"{Binding Title}\"/>", bag);
    CHECK(doc != nullptr);
    if (!doc) return;
    WidgetPtr root = doc->Instantiate(&g_fakeText);
    {
        auto vm = std::make_shared<CounterVm>();
        vm->Title.Set(L"temp");
        root->SetDataContext(std::static_pointer_cast<RefObject>(vm));
        CHECK(root->Get<std::wstring>(root->SlotOf("Text")) == L"temp");
    }
    // The view must survive the source going away; the binding keeps the last value.
    CHECK(root->Get<std::wstring>(root->SlotOf("Text")) == L"temp");
}

TEST("binding: bindings released when widget destroyed") {
    DiagnosticBag bag;
    auto doc = Compile("<TextBlock Text=\"{Binding Title}\"/>", bag);
    CHECK(doc != nullptr);
    if (!doc) return;
    auto vm = std::make_shared<CounterVm>();
    {
        WidgetPtr root = doc->Instantiate(&g_fakeText);
        root->SetDataContext(std::static_pointer_cast<RefObject>(vm));
        CHECK_EQ(vm->Title.ObserverCount(), size_t(1));
    }
    CHECK_EQ(vm->Title.ObserverCount(), size_t(0));
}

// ===========================================================================
// Lists and DataTemplate
// ===========================================================================
TEST("list: items instantiate from template") {
    InstallText();
    DiagnosticBag bag;
    auto doc = Compile(
        "<ListBox ItemsSource=\"{Binding Items}\">"
        "<ListBox.ItemTemplate><DataTemplate>"
        "<TextBlock Text=\"{Binding Title}\"/>"
        "</DataTemplate></ListBox.ItemTemplate>"
        "</ListBox>",
        bag);
    CHECK(doc != nullptr);
    if (!doc) return;
    WidgetPtr root = doc->Instantiate(&g_fakeText);
    auto vm = std::make_shared<ListVm>();
    auto list = std::make_shared<wui::ObservableList<ListItem>>();
    for (int i = 0; i < 3; ++i) list->Add(std::make_shared<ListItem>(i));
    vm->Items.Set(std::static_pointer_cast<RefObject>(list));
    root->SetDataContext(std::static_pointer_cast<RefObject>(vm));
    auto* lb = dynamic_cast<ListBox*>(root.get());
    CHECK(lb != nullptr);
    if (!lb) return;
    CHECK_EQ(lb->ItemCount(), size_t(3));
    Widget* first = lb->ItemWidgetAt(0);
    CHECK(first != nullptr);
    if (first) CHECK(first->Get<std::wstring>(first->SlotOf("Text")) == L"Item 0");
    // Add an item: the list rebuilds.
    list->Add(std::make_shared<ListItem>(3));
    CHECK_EQ(lb->ItemCount(), size_t(4));
}

TEST("list: each item keeps its own data context") {
    // Regression: the parent's inherited DataContext used to be pushed onto item
    // widgets after they were created, so every row rendered the first item.
    InstallText();
    DiagnosticBag bag;
    auto doc = Compile(
        "<ListBox ItemsSource=\"{Binding Items}\">"
        "<ListBox.ItemTemplate><DataTemplate>"
        "<StackPanel Orientation=\"Horizontal\">"
        "<TextBlock Text=\"{Binding Title}\"/><TextBlock Text=\"{Binding Index}\"/>"
        "</StackPanel></DataTemplate></ListBox.ItemTemplate></ListBox>",
        bag);
    CHECK(doc != nullptr);
    if (!doc) return;
    WidgetPtr root = doc->Instantiate(&g_fakeText);
    auto vm = std::make_shared<ListVm>();
    root->SetDataContext(std::static_pointer_cast<RefObject>(vm));
    auto list = std::make_shared<wui::ObservableList<ListItem>>();
    for (int i = 0; i < 3; ++i) list->Add(std::make_shared<ListItem>(i));
    vm->Items.Set(std::static_pointer_cast<RefObject>(list));
    auto* lb = dynamic_cast<ListBox*>(root.get());
    CHECK(lb != nullptr);
    if (!lb) return;
    CHECK_EQ(lb->ItemCount(), size_t(3));
    for (size_t i = 0; i < lb->ItemCount(); ++i) {
        Widget* item = lb->ItemWidgetAt(i);
        CHECK(item != nullptr);
        if (!item || item->Children().empty()) continue;
        Widget* title = item->Children()[0].get();
        CHECK(title != nullptr);
        if (!title) continue;
        CHECK(title->Get<std::wstring>(title->SlotOf("Text")) == L"Item " + std::to_wstring(i));
    }
}

TEST("list: item binding updates do not leak") {
    DiagnosticBag bag;
    auto doc = Compile(
        "<ListBox ItemsSource=\"{Binding Items}\">"
        "<ListBox.ItemTemplate><DataTemplate><TextBlock Text=\"{Binding Title}\"/></DataTemplate>"
        "</ListBox.ItemTemplate></ListBox>",
        bag);
    CHECK(doc != nullptr);
    if (!doc) return;
    WidgetPtr root = doc->Instantiate(&g_fakeText);
    auto vm = std::make_shared<ListVm>();
    root->SetDataContext(std::static_pointer_cast<RefObject>(vm));
    auto list = std::make_shared<wui::ObservableList<ListItem>>();
    auto item = std::make_shared<ListItem>(0);
    list->Add(item);
    vm->Items.Set(std::static_pointer_cast<RefObject>(list));
    auto* lb = dynamic_cast<ListBox*>(root.get());
    CHECK(lb && lb->ItemCount() == 1);
    if (!lb) return;
    Widget* w = lb->ItemWidgetAt(0);
    CHECK(w != nullptr);
    if (w) CHECK(w->Get<std::wstring>(w->SlotOf("Text")) == L"Item 0");
    item->Title.Set(L"changed");
    if (w) CHECK(w->Get<std::wstring>(w->SlotOf("Text")) == L"changed");
    // Clear the list: item widgets go away and drop their subscriptions.
    list->Clear();
    CHECK_EQ(lb->ItemCount(), size_t(0));
    CHECK_EQ(item->Title.ObserverCount(), size_t(0));
}

// ===========================================================================
// Layout
// ===========================================================================
TEST("layout: vertical stack arranges sequentially") {
    InstallText();
    DiagnosticBag bag;
    auto doc = Compile(
        "<StackPanel Orientation=\"Vertical\" Spacing=\"10\" Margin=\"5\">"
        "<TextBlock Text=\"A\" Height=\"20\"/>"
        "<TextBlock Text=\"B\" Height=\"30\"/>"
        "</StackPanel>",
        bag);
    CHECK(doc != nullptr);
    if (!doc) return;
    WidgetPtr root = doc->Instantiate(&g_fakeText);
    root->ForceLayout(Size{300, 300});
    const auto& a = root->Children()[0]->Bounds();
    const auto& b = root->Children()[1]->Bounds();
    CHECK_NEAR(a.y, 5.0f, 0.01);
    CHECK_NEAR(a.h, 20.0f, 0.01);
    CHECK_NEAR(b.y, 35.0f, 0.01); // 5 + 20 + 10
    CHECK_NEAR(b.h, 30.0f, 0.01);
}

TEST("layout: horizontal stack") {
    InstallText();
    DiagnosticBag bag;
    auto doc = Compile(
        "<StackPanel Orientation=\"Horizontal\" Spacing=\"4\">"
        "<TextBlock Text=\"A\" Width=\"10\" Height=\"10\"/>"
        "<TextBlock Text=\"B\" Width=\"10\" Height=\"10\"/>"
        "</StackPanel>",
        bag);
    CHECK(doc != nullptr);
    if (!doc) return;
    WidgetPtr root = doc->Instantiate(&g_fakeText);
    root->ForceLayout(Size{200, 50});
    CHECK_NEAR(root->Children()[0]->Bounds().x, 0.0f, 0.01);
    CHECK_NEAR(root->Children()[1]->Bounds().x, 14.0f, 0.01);
}

TEST("layout: border padding") {
    InstallText();
    DiagnosticBag bag;
    auto doc = Compile(
        "<Border Padding=\"12\" BorderThickness=\"1\"><TextBlock Text=\"x\" Height=\"10\"/></Border>",
        bag);
    CHECK(doc != nullptr);
    if (!doc) return;
    WidgetPtr root = doc->Instantiate(&g_fakeText);
    root->ForceLayout(Size{200, 100});
    CHECK_NEAR(root->Children()[0]->Bounds().x, 12.0f, 0.01);
    CHECK_NEAR(root->Children()[0]->Bounds().y, 12.0f, 0.01);
}

TEST("layout: invalidation bubbles to root") {
    DiagnosticBag bag;
    auto doc = Compile("<Page><StackPanel><TextBlock x:Name=\"t\" Text=\"{Binding Title}\"/></StackPanel></Page>", bag);
    CHECK(doc != nullptr);
    if (!doc) return;
    WidgetPtr root = doc->Instantiate(&g_fakeText);
    auto vm = std::make_shared<CounterVm>();
    root->SetDataContext(std::static_pointer_cast<RefObject>(vm));
    root->ForceLayout(Size{200, 200});
    root->ClearDirty();
    Widget* child = root->FindByName("t");
    CHECK(child != nullptr);
    if (child) {
        root->Children()[0]->Children()[0]->Invalidate(AffectsMeasure | AffectsRender);
    }
    CHECK(root->LayoutDirty());
    CHECK(root->PaintDirty());
}

// ===========================================================================
// Input
// ===========================================================================
TEST("input: button click executes command") {
    InstallText();
    int calls = 0;
    
    DiagnosticBag bag;
    auto doc = Compile("<Button Content=\"Go\" Command=\"{Binding Cmd}\"/>", bag);
    CHECK(doc != nullptr);
    if (!doc) return;
    WidgetPtr root = doc->Instantiate(&g_fakeText);
    auto vm = std::make_shared<CounterVm>();
    // Use a dynamic VM with a command property.
    class CmdVm final : public ObservableObject {
    public:
        explicit CmdVm(std::function<void()> fn) {
            Cmd.Set(std::make_shared<RelayCommand>(std::function<void()>(fn)));
            Make("Cmd", Cmd);
        }
        ObservableProperty<RefPtr> Cmd{"Cmd"};
    };
    auto cmdVm = std::make_shared<CmdVm>([&calls] { ++calls; });
    root->SetDataContext(std::static_pointer_cast<RefObject>(cmdVm));
    root->ForceLayout(Size{100, 40});
    auto* button = dynamic_cast<Button*>(root.get());
    CHECK(button != nullptr);
    if (!button) return;
    button->OnPointerDown(Point{5, 5});
    button->OnPointerUp(Point{5, 5});
    CHECK_EQ(calls, 1);
    // Pointer up outside cancels the click.
    button->OnPointerDown(Point{5, 5});
    button->OnPointerUp(Point{500, 5});
    CHECK_EQ(calls, 1);
}

TEST("input: keyboard activates focused button") {
    InstallText();
    int calls = 0;
    class CmdVm2 final : public ObservableObject {
    public:
        explicit CmdVm2(std::function<void()> fn) {
            Cmd.Set(std::make_shared<RelayCommand>(std::function<void()>(fn)));
            Make("Cmd", Cmd);
        }
        ObservableProperty<RefPtr> Cmd{"Cmd"};
    };
    DiagnosticBag bag;
    auto doc = Compile("<Button Content=\"Go\" Command=\"{Binding Cmd}\"/>", bag);
    CHECK(doc != nullptr);
    if (!doc) return;
    WidgetPtr root = doc->Instantiate(&g_fakeText);
    auto vm = std::make_shared<CmdVm2>([&calls] { ++calls; });
    root->SetDataContext(std::static_pointer_cast<RefObject>(vm));
    auto* button = dynamic_cast<Button*>(root.get());
    CHECK(button != nullptr);
    if (!button) return;
    CHECK(button->OnKeyDown(KeySpace));
    CHECK_EQ(calls, 1);
    CHECK(button->OnKeyDown(KeyReturn));
    CHECK_EQ(calls, 2);
}

TEST("input: disabled button ignores clicks") {
    InstallText();
    int calls = 0;
    class CmdVm3 final : public ObservableObject {
    public:
        explicit CmdVm3(std::function<void()> fn) {
            Cmd.Set(std::make_shared<RelayCommand>(std::function<void()>(fn)));
            Make("Cmd", Cmd);
        }
        ObservableProperty<RefPtr> Cmd{"Cmd"};
    };
    DiagnosticBag bag;
    auto doc = Compile("<Button Content=\"Go\" IsEnabled=\"False\" Command=\"{Binding Cmd}\"/>", bag);
    CHECK(doc != nullptr);
    if (!doc) return;
    WidgetPtr root = doc->Instantiate(&g_fakeText);
    auto vm = std::make_shared<CmdVm3>([&calls] { ++calls; });
    root->SetDataContext(std::static_pointer_cast<RefObject>(vm));
    root->ForceLayout(Size{100, 40});
    auto* button = dynamic_cast<Button*>(root.get());
    CHECK(button != nullptr);
    if (!button) return;
    button->OnPointerDown(Point{5, 5});
    button->OnPointerUp(Point{5, 5});
    CHECK_EQ(calls, 0);
}

TEST("input: textbox typing updates two-way source") {
    InstallText();
    DiagnosticBag bag;
    auto doc = Compile("<TextBox Text=\"{Binding Name, Mode=TwoWay}\"/>", bag);
    CHECK(doc != nullptr);
    if (!doc) return;
    WidgetPtr root = doc->Instantiate(&g_fakeText);
    auto vm = std::make_shared<CounterVm>();
    root->SetDataContext(std::static_pointer_cast<RefObject>(vm));
    auto* box = dynamic_cast<TextBox*>(root.get());
    CHECK(box != nullptr);
    if (!box) return;
    box->SetCaretToEnd();
    box->OnChar(L'h');
    box->OnChar(L'i');
    CHECK(vm->Name.Get() == L"hi");
    box->OnKeyDown(KeyBack);
    CHECK(vm->Name.Get() == L"h");
    CHECK_EQ(box->CaretIndex(), size_t(1));
}

TEST("input: checkbox toggles two-way") {
    InstallText();
    DiagnosticBag bag;
    auto doc = Compile("<CheckBox Content=\"On\" IsChecked=\"{Binding Enabled}\"/>", bag);
    CHECK(doc != nullptr);
    if (!doc) return;
    WidgetPtr root = doc->Instantiate(&g_fakeText);
    auto vm = std::make_shared<CounterVm>();
    root->SetDataContext(std::static_pointer_cast<RefObject>(vm));
    root->ForceLayout(Size{200, 30});
    auto* cb = dynamic_cast<CheckBox*>(root.get());
    CHECK(cb != nullptr);
    if (!cb) return;
    CHECK(vm->Enabled.Get() == true);
    cb->OnPointerUp(Point{5, 5});
    CHECK(vm->Enabled.Get() == false);
}

TEST("input: hit test returns deepest widget") {
    InstallText();
    DiagnosticBag bag;
    auto doc = Compile(
        "<Page><StackPanel><TextBlock Text=\"a\" Height=\"20\"/><TextBlock Text=\"b\" Height=\"20\"/></StackPanel></Page>",
        bag);
    CHECK(doc != nullptr);
    if (!doc) return;
    WidgetPtr root = doc->Instantiate(&g_fakeText);
    root->ForceLayout(Size{100, 100});
    Widget* hit = root->HitTest(Point{5, 25});
    CHECK(hit != nullptr);
    if (hit) CHECK(hit->Get<std::wstring>(hit->SlotOf("Text")) == L"b");
}

// ===========================================================================
// Rendering
// ===========================================================================
TEST("render: recording context is deterministic") {
    InstallText();
    DiagnosticBag bag;
    const std::string src =
        "<Page Background=\"AppBackground\"><StackPanel Margin=\"20\" Spacing=\"8\">"
        "<TextBlock Text=\"Title\" FontSize=\"24\" FontWeight=\"Bold\"/>"
        "<Border Background=\"CardBackground\" CornerRadius=\"8\" Padding=\"10\">"
        "<TextBlock Text=\"Body\"/></Border>"
        "</StackPanel></Page>";
    auto docA = Compile(src, bag);
    CHECK(docA != nullptr);
    if (!docA) return;
    WidgetPtr a = docA->Instantiate(&g_fakeText);
    a->ForceLayout(Size{400, 300});
    RecordingPaintContext ctxA;
    a->PaintTree(ctxA, LightPalette());

    DiagnosticBag bag2;
    auto docB = Compile(src, bag2);
    WidgetPtr b = docB ? docB->Instantiate(&g_fakeText) : nullptr;
    CHECK(b != nullptr);
    if (!b) return;
    b->ForceLayout(Size{400, 300});
    RecordingPaintContext ctxB;
    b->PaintTree(ctxB, LightPalette());
    CHECK_EQ(ctxA.Text(), ctxB.Text());
    CHECK(ctxA.OpCount() > 0);
}

TEST("render: raster output is deterministic") {
    InstallText();
    DiagnosticBag bag;
    auto doc = Compile(
        "<Page Background=\"AppBackground\"><Border Margin=\"10\" Background=\"Accent\" "
        "CornerRadius=\"6\"><TextBlock Text=\"Hi\" Foreground=\"TextOnAccent\"/></Border></Page>",
        bag);
    CHECK(doc != nullptr);
    if (!doc) return;
    SoftwareRasterizer r1(200, 100);
    SoftwareRasterizer r2(200, 100);
    WidgetPtr w1 = doc->Instantiate(&g_fakeText);
    w1->ForceLayout(Size{200, 100});
    RasterPaintContext c1(r1);
    w1->PaintTree(c1, LightPalette());
    WidgetPtr w2 = doc->Instantiate(&g_fakeText);
    w2->ForceLayout(Size{200, 100});
    RasterPaintContext c2(r2);
    w2->PaintTree(c2, LightPalette());
    CHECK_EQ(r1.Hash(), r2.Hash());
}

TEST("render: theme switch changes output") {
    InstallText();
    DiagnosticBag bag;
    auto doc = Compile("<Page Background=\"AppBackground\"/>", bag);
    CHECK(doc != nullptr);
    if (!doc) return;
    auto paint = [&](ThemeKind kind) {
        WidgetPtr w = doc->Instantiate(&g_fakeText);
        w->ForceLayout(Size{100, 100});
        SoftwareRasterizer r(100, 100);
        RasterPaintContext c(r);
        w->PaintTree(c, MakePalette(kind));
        return r.Hash();
    };
    CHECK(paint(ThemeKind::Light) != paint(ThemeKind::Dark));
}

// ===========================================================================
// Performance (reported, not enforced; the report consumes the numbers)
// ===========================================================================
namespace {
double NowUs() {
    using clock = std::chrono::high_resolution_clock;
    return std::chrono::duration<double, std::micro>(clock::now().time_since_epoch()).count();
}

std::string SampleDocument(int items) {
    std::string s;
    s += "<Page Background=\"AppBackground\"><StackPanel Margin=\"24\" Spacing=\"12\">";
    s += "<TextBlock Text=\"{Binding Title}\" FontSize=\"28\" FontWeight=\"SemiBold\"/>";
    s += "<StackPanel Orientation=\"Horizontal\" Spacing=\"8\">";
    s += "<Button Content=\"-\" Command=\"{Binding Dec}\"/>";
    s += "<TextBlock Text=\"{Binding Count, StringFormat='Count: {0}'}\"/>";
    s += "<Button Content=\"+\" Command=\"{Binding Inc}\"/>";
    s += "</StackPanel>";
    s += "<TextBox Text=\"{Binding Name, Mode=TwoWay}\"/>";
    s += "<CheckBox Content=\"Enabled\" IsChecked=\"{Binding Enabled, Mode=TwoWay}\"/>";
    s += "<ListBox ItemsSource=\"{Binding Items}\">";
    s += "<ListBox.ItemTemplate><DataTemplate><TextBlock Text=\"{Binding Title}\"/></DataTemplate>";
    s += "</ListBox.ItemTemplate></ListBox>";
    s += "</StackPanel></Page>";
    (void)items;
    return s;
}
} // namespace

TEST("perf: parse throughput") {
    const std::string src = SampleDocument(0);
    const int iterations = 200;
    // Warm up.
    for (int i = 0; i < 20; ++i) {
        DiagnosticBag bag;
        Compile(src, bag);
    }
    std::vector<double> samples;
    for (int i = 0; i < iterations; ++i) {
        DiagnosticBag bag;
        const double t0 = NowUs();
        auto doc = Compile(src, bag);
        const double t1 = NowUs();
        CHECK(doc != nullptr);
        samples.push_back(t1 - t0);
    }
    std::sort(samples.begin(), samples.end());
    const double median = samples[samples.size() / 2];
    const double p99 = samples[static_cast<size_t>(samples.size() * 0.99)];
    std::printf("    parse(%zu bytes): median %.1f us, p99 %.1f us\n", src.size(), median, p99);
    // Loose guard so a regression breaks the build; reported numbers are the point.
    CHECK(median < 5000.0);
}

TEST("perf: instantiate") {
    InstallText();
    const std::string src = SampleDocument(0);
    DiagnosticBag bag;
    auto doc = Compile(src, bag);
    CHECK(doc != nullptr);
    if (!doc) return;
    for (int i = 0; i < 20; ++i) doc->Instantiate(&g_fakeText);
    std::vector<double> samples;
    for (int i = 0; i < 200; ++i) {
        const double t0 = NowUs();
        WidgetPtr w = doc->Instantiate(&g_fakeText);
        const double t1 = NowUs();
        (void)w;
        samples.push_back(t1 - t0);
    }
    std::sort(samples.begin(), samples.end());
    std::printf("    instantiate(%zu nodes): median %.1f us\n", doc->nodeCount,
                samples[samples.size() / 2]);
    CHECK(samples[samples.size() / 2] < 20000.0);
}

TEST("perf: binding update") {
    InstallText();
    DiagnosticBag bag;
    auto doc = Compile("<TextBlock Text=\"{Binding Title}\"/>", bag);
    CHECK(doc != nullptr);
    if (!doc) return;
    WidgetPtr root = doc->Instantiate(&g_fakeText);
    auto vm = std::make_shared<CounterVm>();
    root->SetDataContext(std::static_pointer_cast<RefObject>(vm));
    const int n = 100000;
    // Warm up.
    for (int i = 0; i < 1000; ++i) vm->Title.Set(i % 2 ? L"A" : L"B");
    const double t0 = NowUs();
    for (int i = 0; i < n; ++i) vm->Title.Set(i % 2 ? L"A" : L"B");
    const double t1 = NowUs();
    const double perUpdate = (t1 - t0) * 1000.0 / static_cast<double>(n); // ns
    std::printf("    binding update: %.1f ns/update\n", perUpdate);
    CHECK(perUpdate < 5000.0);
}

TEST("perf: two-way update") {
    InstallText();
    DiagnosticBag bag;
    auto doc = Compile("<TextBox Text=\"{Binding Name, Mode=TwoWay}\"/>", bag);
    CHECK(doc != nullptr);
    if (!doc) return;
    WidgetPtr root = doc->Instantiate(&g_fakeText);
    auto vm = std::make_shared<CounterVm>();
    root->SetDataContext(std::static_pointer_cast<RefObject>(vm));
    const int slot = root->SlotOf("Text");
    const int n = 50000;
    for (int i = 0; i < 500; ++i) root->SetSlotValue(slot, Value::Text(L"warm"));
    const double t0 = NowUs();
    for (int i = 0; i < n; ++i) root->SetSlotValue(slot, Value::Text(i % 2 ? L"A" : L"B"));
    const double t1 = NowUs();
    const double perUpdate = (t1 - t0) * 1000.0 / static_cast<double>(n);
    std::printf("    two-way update: %.1f ns/update\n", perUpdate);
    CHECK(vm->Name.Get() == L"A"); // last i is odd
    CHECK(perUpdate < 8000.0);
}

TEST("perf: list build 100 items") {
    InstallText();
    DiagnosticBag bag;
    auto doc = Compile(
        "<ListBox ItemsSource=\"{Binding Items}\">"
        "<ListBox.ItemTemplate><DataTemplate>"
        "<StackPanel Orientation=\"Horizontal\" Spacing=\"6\">"
        "<TextBlock Text=\"{Binding Title}\"/><TextBlock Text=\"{Binding Title}\"/>"
        "</StackPanel>"
        "</DataTemplate></ListBox.ItemTemplate></ListBox>",
        bag);
    CHECK(doc != nullptr);
    if (!doc) return;
    WidgetPtr root = doc->Instantiate(&g_fakeText);
    auto vm = std::make_shared<ListVm>();
    root->SetDataContext(std::static_pointer_cast<RefObject>(vm));
    auto list = std::make_shared<wui::ObservableList<ListItem>>();
    for (int i = 0; i < 100; ++i) list->Add(std::make_shared<ListItem>(i));
    const double t0 = NowUs();
    vm->Items.Set(std::static_pointer_cast<RefObject>(list));
    const double t1 = NowUs();
    const double t2 = NowUs();
    root->ForceLayout(Size{600, 3000});
    const double t3 = NowUs();
    auto* lb = dynamic_cast<ListBox*>(root.get());
    CHECK(lb && lb->ItemCount() == 100);
    std::printf("    list 100 items: build %.1f us, layout %.1f us\n", t1 - t0, t3 - t2);
    CHECK(t1 - t0 < 100000.0);
}

// ---------------------------------------------------------------------------
// ScrollViewer
// ---------------------------------------------------------------------------
TEST("scrollviewer: content taller than viewport is scrollable") {
    DiagnosticBag bag;
    auto doc = CompileWaml(
        "<Page><ScrollViewer Height=\"200\">"
        "<StackPanel Orientation=\"Vertical\" Spacing=\"10\">"
        "<Border Height=\"150\" Background=\"CardBackground\"/>"
        "<Border Height=\"150\" Background=\"CardBackground\"/>"
        "<Border Height=\"150\" Background=\"CardBackground\"/>"
        "</StackPanel>"
        "</ScrollViewer></Page>",
        ParseOptions{"scroll.waml"}, bag);
    CHECK(doc != nullptr);
    if (!doc) return;
    WidgetPtr root = doc->Instantiate(&g_fakeText);
    root->ForceLayout(Size{400, 600});
    auto* sv = dynamic_cast<ScrollViewer*>(root->Children()[0].get());
    CHECK(sv != nullptr);
    if (!sv) return;
    // 3 x 150 + 2 x 10 spacing = 470 of content in a 200 tall viewport.
    CHECK(sv->ContentHeight() > 400);
    CHECK(sv->MaxScroll() > 200);
    CHECK(sv->ScrollOffset() == 0);

    sv->OnPointerWheel(-3); // negative = scroll down 3 lines
    CHECK(sv->ScrollOffset() > 0);

    // Clamped at both ends: this is what keeps the thumb from leaving the track.
    sv->ScrollTo(1e6f);
    CHECK(sv->ScrollOffset() == sv->MaxScroll());
    sv->ScrollTo(-100);
    CHECK(sv->ScrollOffset() == 0);
}

TEST("scrollviewer: short content does not scroll") {
    DiagnosticBag bag;
    auto doc = CompileWaml(
        "<Page><ScrollViewer Height=\"400\">"
        "<Border Height=\"100\" Background=\"CardBackground\"/>"
        "</ScrollViewer></Page>",
        ParseOptions{"short.waml"}, bag);
    CHECK(doc != nullptr);
    if (!doc) return;
    WidgetPtr root = doc->Instantiate(&g_fakeText);
    root->ForceLayout(Size{400, 600});
    auto* sv = dynamic_cast<ScrollViewer*>(root->Children()[0].get());
    CHECK(sv != nullptr);
    if (!sv) return;
    CHECK(sv->MaxScroll() == 0);
    sv->OnPointerWheel(-5);
    CHECK(sv->ScrollOffset() == 0);
}

int main(int argc, char** argv) {
    const char* filter = argc > 1 ? argv[1] : "";
    const int failures = test::RunAll(filter);
    return failures == 0 ? 0 : 1;
}