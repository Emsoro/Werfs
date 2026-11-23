// SPDX-License-Identifier: MIT
#include "Mvvm.h"

#include "Converters.h"
#include "Types.h"
#include "Widget.h"

#include <algorithm>
#include <sstream>

#include <windows.h>

namespace wui {

// ---------------------------------------------------------------------------
// Binding diagnostics sink
// ---------------------------------------------------------------------------
namespace {
IBindingDiagnostics* g_sink = nullptr;
size_t g_errorCount = 0;
} // namespace

void SetBindingDiagnostics(IBindingDiagnostics* sink) { g_sink = sink; }
IBindingDiagnostics* GetBindingDiagnostics() { return g_sink; }
size_t BindingErrorCount() { return g_errorCount; }

namespace {
std::string JoinPath(const std::vector<std::string>& path) {
    std::string out;
    for (size_t i = 0; i < path.size(); ++i) {
        if (i) out += '.';
        out += path[i];
    }
    return out;
}
} // namespace

// ---------------------------------------------------------------------------
// HopObserver: forwards a change on an intermediate path segment.
// ---------------------------------------------------------------------------
class Binding::HopObserver final : public IPropertyObserver {
public:
    HopObserver(Binding* owner, size_t index) : owner_(owner), index_(index) {}
    void OnSourceChanged(const Value& value) override { owner_->OnHopChanged(index_, value); }

private:
    Binding* owner_;
    size_t index_;
};

Binding::Binding(Widget* target, int slot, const PropertyDescriptor* descriptor, BindingDesc desc)
    : target_(target), slot_(slot), descriptor_(descriptor), desc_(std::move(desc)) {}

Binding::~Binding() { Detach(); }

void Binding::RecordError(const std::string& message) {
    lastError_ = message;
    ++g_errorCount;
    const std::string path = desc_.path.empty() ? std::string() : JoinPath(desc_.path);
    const std::string scope = target_ ? target_->TypeName() : "?";
    std::string line = "[wui] binding '" + path + "' on " + scope + ": " + message;
    OutputDebugStringW((Utf8ToWide(line) + L"\n").c_str());
    if (g_sink) g_sink->OnBindingError(scope, path, message);
}

bool Binding::ApplyFallback(const std::string& reason) {
    if (!desc_.hasFallback || !target_ || slot_ < 0) return false;
    const std::string path = desc_.path.empty() ? std::string() : JoinPath(desc_.path);
    const std::string scope = target_ ? target_->TypeName() : "?";
    const std::string line = "[wui] binding '" + path + "' on " + scope + ": " + reason +
                             " (using FallbackValue)";
    OutputDebugStringW((Utf8ToWide(line) + L"\n").c_str());
    lastError_.clear();
    applying_ = true;
    target_->SetSlotValue(slot_, desc_.fallback, true);
    applying_ = false;
    return true;
}

void Binding::Attach(ITextService* text) {
    text_ = text;
    if (attached_) return;
    attached_ = true;
    if (target_ && slot_ >= 0) target_->AddSlotObserver(slot_, this);
    if (source_) ResolveChain();
}

void Binding::Detach() {
    if (!attached_) return;
    attached_ = false;
    if (target_ && slot_ >= 0) target_->RemoveSlotObserver(slot_, this);
    ClearChain();
}

void Binding::ClearChain() {
    for (auto& hop : hops_) {
        if (hop.Source && hop.Observer) hop.Source->Unsubscribe(hop.Observer.get());
    }
    hops_.clear();
    if (leaf_) {
        leaf_->Unsubscribe(this);
        leaf_ = nullptr;
    }
}

void Binding::SetSource(RefPtr source) {
    // Re-resolve even when the source pointer is unchanged: the previous attempt
    // may have failed (for example a null DataContext), and the host is asking
    // again. Skipping that would hide the failure and never recover.
    if (source_ == source && leaf_ != nullptr) return;
    ClearChain();
    source_ = std::move(source);
    if (!attached_) return;
    ResolveChain();
}

void Binding::ResolveChain() {
    ClearChain();
    if (!source_) {
        if (ApplyFallback("data context is null")) return;
        RecordError("data context is null");
        return;
    }
    if (desc_.path.empty()) {
        if (ApplyFallback("empty binding path")) return;
        RecordError("empty binding path");
        return;
    }

    RefPtr current = source_;
    for (size_t i = 0; i < desc_.path.size(); ++i) {
        PropertyTable* table = current->GetPropertyTable();
        if (!table) {
            const std::string reason = "path segment '" + desc_.path[i] + "' is not on an observable object";
            if (ApplyFallback(reason)) return;
            RecordError(reason);
            return;
        }
        const PropertyTable::Entry* entry = table->Find(desc_.path[i]);
        if (!entry) {
            std::string msg = "'" + desc_.path[i] + "' not found";
            auto suggestions = SuggestNames(desc_.path[i], table->Names());
            if (!suggestions.empty()) msg += " (did you mean '" + suggestions.front() + "'?)";
            if (ApplyFallback(msg)) return;
            RecordError(msg);
            return;
        }
        IPropertySource* src = entry->source;
        const bool isLast = i + 1 == desc_.path.size();
        if (isLast) {
            leaf_ = src;
            src->Subscribe(this);
        } else {
            Hop hop;
            hop.Source = src;
            hop.Observer = std::make_unique<HopObserver>(this, i);
            src->Subscribe(hop.Observer.get());
            hops_.push_back(std::move(hop));

            Value intermediate;
            if (!src->Read(intermediate) || intermediate.type != VType::Ref || !intermediate.ref) {
                // The intermediate object is null: stop here, the chain will be
                // rebuilt when it is assigned. A fallback applies while it waits.
                ApplyFallback("path segment '" + desc_.path[i] + "' is null");
                return;
            }
            current = intermediate.ref;
        }
    }
    lastError_.clear();
    if (!leaf_) return;
    if (desc_.mode != BindingMode::OneWayToSource) {
        Value initial;
        if (leaf_->Read(initial)) ApplyToTarget(initial);
    }
}

void Binding::OnHopChanged(size_t index, const Value& value) {
    // The intermediate object changed: rebuild the tail of the chain.
    if (index + 1 >= desc_.path.size()) return;
    // Drop the old leaf subscription; the prefix hops stay valid.
    if (leaf_) {
        leaf_->Unsubscribe(this);
        leaf_ = nullptr;
    }
    if (value.type != VType::Ref || !value.ref) return;
    if (hops_.empty() || index >= hops_.size()) return;
    RefPtr current = value.ref;
    for (size_t i = index + 1; i < desc_.path.size(); ++i) {
        PropertyTable* table = current->GetPropertyTable();
        if (!table) return;
        const PropertyTable::Entry* entry = table->Find(desc_.path[i]);
        if (!entry) return;
        IPropertySource* src = entry->source;
        const bool isLast = i + 1 == desc_.path.size();
        if (isLast) {
            leaf_ = src;
            src->Subscribe(this);
            if (desc_.mode != BindingMode::OneWayToSource) {
                Value v;
                if (leaf_->Read(v)) ApplyToTarget(v);
            }
            return;
        }
        Value intermediate;
        if (!src->Read(intermediate) || intermediate.type != VType::Ref || !intermediate.ref) return;
        current = intermediate.ref;
    }
}

void Binding::ApplyToTarget(const Value& value) {
    if (!target_) return;
    applying_ = true;
    Value tmp = value;
    const VType targetType = descriptor_ ? descriptor_->type : value.type;

    // A converter runs before the implicit conversion, because its whole purpose
    // is to bridge types that have no sensible default conversion -- bool to
    // Visibility being the case that motivates the feature.
    if (!desc_.converter.empty()) {
        const ConvertFn* fn = ConverterRegistry::Instance().Find(desc_.converter);
        if (!fn) {
            RecordError("unknown converter '" + desc_.converter + "'");
            applying_ = false;
            return;
        }
        Value converted;
        if (!(*fn)(tmp, converted)) {
            if (ApplyFallback("converter '" + desc_.converter + "' cannot convert " + VTypeName(tmp.type)))
                return;
            RecordError("converter '" + desc_.converter + "' cannot convert " + VTypeName(tmp.type));
            applying_ = false;
            return;
        }
        tmp = converted;
    }

    if (tmp.type != targetType) {
        Value converted;
        if (!ConvertValue(tmp, targetType, converted)) {
            if (ApplyFallback(std::string("cannot convert ") + VTypeName(tmp.type) + " to " +
                              VTypeName(targetType)))
                return;
            RecordError(std::string("cannot convert ") + VTypeName(tmp.type) + " to " + VTypeName(targetType));
            applying_ = false;
            return;
        }
        tmp = converted;
    }
    if (!desc_.stringFormat.empty()) {
        std::wstring formatted = desc_.stringFormat;
        const std::wstring placeholder = L"{0}";
        const size_t pos = formatted.find(placeholder);
        if (pos != std::wstring::npos) formatted.replace(pos, placeholder.size(), FormatValueText(tmp));
        tmp = Value::Text(formatted);
    }
    target_->SetSlotValue(slot_, tmp, true);
    applying_ = false;
}

void Binding::OnSourceChanged(const Value& value) {
    if (applying_) return;
    if (desc_.mode == BindingMode::OneTime) return;
    if (desc_.mode == BindingMode::OneWayToSource) return;
    ApplyToTarget(value);
}

void Binding::OnSlotChanged(int slot, const Value& value) {
    if (!attached_ || slot != slot_) return;
    if (applying_) return;
    if (desc_.mode != BindingMode::TwoWay && desc_.mode != BindingMode::OneWayToSource) return;
    if (desc_.trigger == UpdateSourceTrigger::LostFocus) {
        // Defer until Flush(). Keep the latest value for the flush.
        pendingValue_ = value;
        lostFocusPending_ = true;
        return;
    }
    if (!leaf_ || !leaf_->CanWrite()) {
        if (!leaf_) RecordError("source is not resolved");
        return;
    }
    applying_ = true;
    Value tmp = value;
    const VType sourceType = leaf_->Type();
    if (tmp.type != sourceType) {
        Value converted;
        if (!ConvertValue(tmp, sourceType, converted)) {
            RecordError(std::string("cannot write back ") + VTypeName(tmp.type) + " to " +
                        VTypeName(sourceType));
            applying_ = false;
            return;
        }
        tmp = converted;
    }
    leaf_->WriteFromView(tmp);
    applying_ = false;
}

void Binding::Flush() {
    if (!lostFocusPending_) return;
    lostFocusPending_ = false;
    if (!leaf_ || !leaf_->CanWrite()) return;
    applying_ = true;
    Value tmp = pendingValue_;
    if (tmp.type != leaf_->Type()) {
        Value converted;
        if (ConvertValue(tmp, leaf_->Type(), converted)) tmp = converted;
        else {
            applying_ = false;
            return;
        }
    }
    leaf_->WriteFromView(tmp);
    applying_ = false;
}

} // namespace wui
