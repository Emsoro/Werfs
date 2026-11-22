// SPDX-License-Identifier: MIT
// WAML lexer/parser and the compiled document model.
#pragma once

#include "Core.h"
#include "Diagnostics.h"
#include "Types.h"

#include <map>
#include <memory>
#include <string>
#include <vector>

namespace wui {

class TypeInfo;
class Widget;
class ITextService;
class DataTemplate;

// ---------------------------------------------------------------------------
// Compiled document model (immutable after validation, reusable for many
// instances -- this is the "compiled view" of PLAN-WAML-MVVM.md §3.2).
// ---------------------------------------------------------------------------
struct BindingExpr {
    bool isBinding = false;
    std::string path; // raw text before path splitting
    BindingMode mode = BindingMode::OneWay;
    bool modeExplicit = false;
    UpdateSourceTrigger trigger = UpdateSourceTrigger::PropertyChanged;
    std::wstring stringFormat;
    std::string converter; // Name of a registered converter; empty means none.
    bool hasFallback = false;
    Value fallback;
    size_t offset = 0; // Resolved to line/column only when a diagnostic fires.
};

struct NodeDesc;

// ---------------------------------------------------------------------------
// Styles
//
// A style is a named list of (property, value) pairs for one target type. It is
// RESOLVED AT LOAD TIME into slots, not looked up per paint: a Setter naming a
// property the type does not have is a load error with a line number, and a typo
// cannot silently do nothing at runtime.
//
// Property precedence is the one every declarative UI uses, and the reason it has
// to be stated: local value > style > default. Instantiation applies the style
// chain first, then the node's own attributes, so an inline attribute always wins
// over the style that also sets it -- the escape hatch users reach for must work.
//
// BasedOn chains base-first so a derived style overrides its parent, and a
// derived style may not change TargetType (a derived "TextBlock title" that
// suddenly targets a Border is a mistake, not a feature).
// ---------------------------------------------------------------------------
struct StyleSetter {
    std::string propertyName;
    int slot = -1;
    Value value;
    size_t offset = 0;
};

// One named literal from a <ResourceDictionary>: a color, a number, a thickness.
//
// Deliberately NOT an object graph. The thing that was actually missing from
// markup was "write this colour once", and a name -> literal table covers that
// without introducing a value-tree, lifetimes or mutation. A resource is
// substituted into the literal BEFORE type conversion, so it can be used
// anywhere a literal can: a style's Value, an attribute, a thickness component.
struct ResourceValue {
    std::string key;
    std::string literal; // raw text, converted at each use site's type
    size_t offset = 0;
};

// A <Trigger> inside a style: "when this watched property equals this value, also
// apply these setters".
//
// This is the one part of a style that CANNOT be resolved at load time, because the
// answer depends on the running value. Two consequences worth stating:
//
//  1. A trigger's setters are tracked SEPARATELY from the style's own, so switching
//     a trigger off can put back what was there. Overwriting in place and hoping to
//     recompute later leaves a deactivated trigger's values behind -- a checkbox
//     that stays blue after being unchecked.
//  2. A trigger loses to a LOCAL value. Local > style is the rule users rely on, and
//     a trigger is part of the style. Otherwise `IsEnabled="False"` on an element
//     would fight its own style's state trigger.
struct StyleTrigger {
    std::string watchedName;
    int watchedSlot = -1;
    Value when; // compared with Value::Equals, the same comparison slot writes use
    std::vector<StyleSetter> setters;
    size_t offset = 0;
};

struct StyleDesc {
    std::string key;         // x:Key
    std::string targetType;  // qualified, e.g. "wui/TextBlock"
    const TypeInfo* type = nullptr;
    std::string basedOnKey;
    const StyleDesc* basedOn = nullptr; // resolved after the whole document is read
    std::vector<StyleSetter> setters;
    std::vector<StyleTrigger> triggers;
    size_t offset = 0;

    // True when this style, or anything it derives from, targets `t`.
    bool AppliesTo(const std::string& qualifiedTypeName) const;
};

struct PropertyEntry {
    std::string name;      // WAML attribute name
    int slot = -1;
    Value value;           // Converted literal (ignored when binding used)
    BindingExpr binding;   // Only meaningful when binding.isBinding
    std::shared_ptr<NodeDesc> templateNode; // Set for property-element templates.
    size_t offset = 0;
};

struct NodeDesc {
    std::string typeName; // qualified "wui/Button"
    const TypeInfo* type = nullptr;
    std::string name; // x:Name, empty when absent
    std::vector<PropertyEntry> properties;
    std::vector<std::shared_ptr<NodeDesc>> children;
    std::shared_ptr<NodeDesc> templateRoot; // For DataTemplate elements.
    std::wstring inlineText;                // Text content (content property)
    // Style="{StaticResource Key}" on this element, resolved to the definition.
    // Applied before `properties`, so a local attribute overrides it.
    //
    // The style is held as a raw pointer for cheap comparison, but the OWNING vector
    // is kept alive through styleOwner: a widget tree outlives the CompiledDocument it
    // came from (measure it, keep it, drop the document), and a trigger host points
    // into StyleDesc::triggers for as long as the widget lives. Without the shared
    // owner the host reads freed memory and silently sees zero setters -- the exact
    // failure that made every trigger appear to do nothing.
    const StyleDesc* style = nullptr;
    std::shared_ptr<const std::vector<std::shared_ptr<StyleDesc>>> styleOwner;
    size_t offset = 0;
};

struct CompiledDocument {
    std::shared_ptr<NodeDesc> root;
    std::string file;
    // Style definitions by key, in document order. Kept alive with the document
    // because NodeDesc::style points into it. Shared (not owned per node) so a
    // template instance costs a pointer, not a copy.
    std::shared_ptr<const std::vector<std::shared_ptr<StyleDesc>>> styles;
    // Mutable during the collect pass only; frozen into `styles` before validation.
    std::vector<std::shared_ptr<StyleDesc>> pendingStyles;
    // Named literals from <ResourceDictionary>, available to every literal in the
    // document, styles included.
    std::vector<ResourceValue> resources;
    size_t nodeCount = 0;
    size_t bindingCount = 0;
    size_t propertyCount = 0;
    size_t styleCount = 0;

    // Instantiate a fresh widget tree. Bindings are attached but not yet bound
    // to a data context (call SetDataContext on the root).
    WidgetPtr Instantiate(ITextService* text) const;
    const StyleDesc* FindStyle(const std::string& key) const;
    const ResourceValue* FindResource(const std::string& key) const;
};

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------
struct ParseOptions {
    std::string fileName = "<memory>";
};

// Parse + validate in one step. Returns nullptr when the bag has errors.
std::unique_ptr<CompiledDocument> CompileWaml(std::string_view source, const ParseOptions& options,
                                             DiagnosticBag& diagnostics);

// Parse only (exposed for the lexer/parser test matrix).
std::unique_ptr<CompiledDocument> ParseWaml(std::string_view source, const ParseOptions& options,
                                            DiagnosticBag& diagnostics);

// Builds widget tree from a compiled document (used by Instantiate and by tests).
WidgetPtr InstantiateNode(const NodeDesc& node, ITextService* text);

// Parses a color literal or theme token. Returns false when invalid and fills
// `suggestion` with the closest token name.
bool ParseColorLiteral(std::string_view text, Color& out, std::string* suggestion);

// Converts a WAML literal (already unescaped and entity-decoded) to a Value for
// the given descriptor. Returns a diagnostic message on failure.
bool ConvertLiteral(std::string_view literal, const PropertyDescriptor& desc, Value& out,
                    std::string& error, std::vector<std::string>* suggestions);

} // namespace wui
