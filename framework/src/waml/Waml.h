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
    bool hasFallback = false;
    Value fallback;
    size_t offset = 0; // Resolved to line/column only when a diagnostic fires.
};

struct NodeDesc;

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
    size_t offset = 0;
};

struct CompiledDocument {
    std::shared_ptr<NodeDesc> root;
    std::string file;
    size_t nodeCount = 0;
    size_t bindingCount = 0;
    size_t propertyCount = 0;

    // Instantiate a fresh widget tree. Bindings are attached but not yet bound
    // to a data context (call SetDataContext on the root).
    WidgetPtr Instantiate(ITextService* text) const;
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
