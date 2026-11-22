// SPDX-License-Identifier: MIT
// MVVM layer: observable properties, property tables, binding primitives.
#pragma once

#include "Core.h"

#include <algorithm>
#include <map>
#include <type_traits>

namespace wui {

class Widget;
struct PropertyDescriptor;

// ---------------------------------------------------------------------------
// Value codecs: the only place where C++ types meet the type-erased Value.
// ---------------------------------------------------------------------------
template <class T>
struct ValueCodec;

template <>
struct ValueCodec<std::wstring> {
    static constexpr VType kType = VType::Text;
    static Value To(const std::wstring& v) { return Value::Text(v); }
    static bool From(const Value& v, std::wstring& out) {
        Value tmp;
        if (!ConvertValue(v, VType::Text, tmp)) return false;
        out = tmp.text;
        return true;
    }
    static bool Equals(const std::wstring& a, const std::wstring& b) { return a == b; }
};

template <>
struct ValueCodec<int> {
    static constexpr VType kType = VType::Number;
    static Value To(int v) { return Value::Number(v); }
    static bool From(const Value& v, int& out) {
        Value tmp;
        if (!ConvertValue(v, VType::Number, tmp)) return false;
        out = static_cast<int>(tmp.num);
        return true;
    }
    static bool Equals(int a, int b) { return a == b; }
};

template <>
struct ValueCodec<double> {
    static constexpr VType kType = VType::Number;
    static Value To(double v) { return Value::Number(v); }
    static bool From(const Value& v, double& out) {
        Value tmp;
        if (!ConvertValue(v, VType::Number, tmp)) return false;
        out = tmp.num;
        return true;
    }
    static bool Equals(double a, double b) { return a == b; }
};

// All WAML numbers are stored as double; float slots convert through it.
template <>
struct ValueCodec<float> {
    static constexpr VType kType = VType::Number;
    static Value To(float v) { return Value::Number(v); }
    static bool From(const Value& v, float& out) {
        Value tmp;
        if (!ConvertValue(v, VType::Number, tmp)) return false;
        out = static_cast<float>(tmp.num);
        return true;
    }
    static bool Equals(float a, float b) { return a == b; }
};

template <>
struct ValueCodec<bool> {
    static constexpr VType kType = VType::Bool;
    static Value To(bool v) { return Value::Bool(v); }
    static bool From(const Value& v, bool& out) {
        Value tmp;
        if (!ConvertValue(v, VType::Bool, tmp)) return false;
        out = tmp.flag;
        return true;
    }
    static bool Equals(bool a, bool b) { return a == b; }
};

template <>
struct ValueCodec<Color> {
    static constexpr VType kType = VType::Color;
    static Value To(const Color& v) { return Value::MakeColor(v); }
    static bool From(const Value& v, Color& out) {
        if (v.type != VType::Color) return false;
        out = v.color;
        return true;
    }
    static bool Equals(const Color& a, const Color& b) { return a == b; }
};

template <>
struct ValueCodec<Thickness> {
    static constexpr VType kType = VType::Thickness;
    static Value To(const Thickness& v) { return Value::MakeThickness(v); }
    static bool From(const Value& v, Thickness& out) {
        if (v.type != VType::Thickness) return false;
        out = v.thickness;
        return true;
    }
    static bool Equals(const Thickness& a, const Thickness& b) { return a == b; }
};

template <>
struct ValueCodec<RefPtr> {
    static constexpr VType kType = VType::Ref;
    static Value To(const RefPtr& v) { return Value::Ref(v); }
    static bool From(const Value& v, RefPtr& out) {
        if (v.type != VType::Ref && v.type != VType::Null) return false;
        out = v.ref;
        return true;
    }
    static bool Equals(const RefPtr& a, const RefPtr& b) { return a == b; }
};

// ---------------------------------------------------------------------------
// Observers.
// ---------------------------------------------------------------------------
class IPropertyObserver {
public:
    virtual ~IPropertyObserver() = default;
    virtual void OnSourceChanged(const Value& value) = 0;
};

class IPropertySource {
public:
    virtual ~IPropertySource() = default;
    virtual VType Type() const = 0;
    virtual bool Read(Value& out) const = 0;
    virtual bool WriteFromView(const Value& v) = 0;
    virtual bool CanWrite() const = 0;
    virtual void Subscribe(IPropertyObserver* observer) = 0;
    virtual void Unsubscribe(IPropertyObserver* observer) = 0;
};

template <class T>
class ObservableProperty final : public IPropertySource {
public:
    explicit ObservableProperty(std::string name = std::string(), T initial = T{})
        : name_(std::move(name)), value_(std::move(initial)) {}

    const T& Get() const { return value_; }
    const std::string& Name() const { return name_; }

    // Assign and notify. Returns true when the value changed.
    bool Set(T value) {
        if (ValueCodec<T>::Equals(value_, value)) return false;
        value_ = std::move(value);
        Notify();
        return true;
    }
    bool SetFromView(T value) { return Set(std::move(value)); }

    // IPropertySource
    VType Type() const override { return ValueCodec<T>::kType; }
    bool Read(Value& out) const override {
        out = ValueCodec<T>::To(value_);
        return true;
    }
    bool WriteFromView(const Value& v) override {
        T converted{};
        if (!ValueCodec<T>::From(v, converted)) return false;
        Set(std::move(converted));
        return true;
    }
    bool CanWrite() const override { return true; }
    void Subscribe(IPropertyObserver* observer) override {
        if (!observer) return;
        if (std::find(observers_.begin(), observers_.end(), observer) == observers_.end())
            observers_.push_back(observer);
    }
    void Unsubscribe(IPropertyObserver* observer) override {
        observers_.erase(std::remove(observers_.begin(), observers_.end(), observer), observers_.end());
    }
    size_t ObserverCount() const { return observers_.size(); }

private:
    void Notify() {
        const Value v = ValueCodec<T>::To(value_);
        // Copy: an observer may unsubscribe (or subscribe) during notification.
        auto copy = observers_;
        for (IPropertyObserver* o : copy) {
            if (std::find(observers_.begin(), observers_.end(), o) != observers_.end()) o->OnSourceChanged(v);
        }
    }

    std::string name_;
    T value_;
    std::vector<IPropertyObserver*> observers_;
};

// ---------------------------------------------------------------------------
// Property table: explicit registration, no reflection, no string hashing in
// the hot path (bindings resolve once, then hold the IPropertySource*).
// ---------------------------------------------------------------------------
class PropertyTable {
public:
    struct Entry {
        std::string name;
        IPropertySource* source = nullptr;
    };

    template <class T>
    void Add(std::string name, ObservableProperty<T>& property) {
        entries_.push_back(Entry{std::move(name), &property});
        index_.clear();
    }

    const Entry* Find(std::string_view name) const {
        if (index_.empty())
            for (const auto& e : entries_) index_.emplace(e.name, &e);
        auto it = index_.find(std::string(name));
        return it == index_.end() ? nullptr : it->second;
    }

    std::vector<std::string> Names() const {
        std::vector<std::string> out;
        for (const auto& e : entries_) out.push_back(e.name);
        return out;
    }

    const std::vector<Entry>& Entries() const { return entries_; }

private:
    std::vector<Entry> entries_;
    mutable std::map<std::string, const Entry*> index_;
};

// ---------------------------------------------------------------------------
// Commands.
// ---------------------------------------------------------------------------
class ICommand : public RefObject {
public:
    virtual bool CanExecute() const = 0;
    virtual void Execute() = 0;
    virtual void Subscribe(class ICommandObserver* o) = 0;
    virtual void Unsubscribe(class ICommandObserver* o) = 0;
    virtual void RaiseCanExecuteChanged() = 0;
};

class ICommandObserver {
public:
    virtual ~ICommandObserver() = default;
    virtual void OnCanExecuteChanged() = 0;
};

class RelayCommand final : public ICommand {
public:
    explicit RelayCommand(std::function<bool()> canExecute, std::function<void()> execute)
        : canExecute_(std::move(canExecute)), execute_(std::move(execute)) {}

    explicit RelayCommand(std::function<void()> execute)
        : canExecute_([] { return true; }), execute_(std::move(execute)) {}

    bool CanExecute() const override { return canExecute_ ? canExecute_() : true; }
    void Execute() override {
        if (CanExecute() && execute_) execute_();
    }
    void Subscribe(ICommandObserver* o) override {
        observers_.push_back(o);
    }
    void Unsubscribe(ICommandObserver* o) override {
        observers_.erase(std::remove(observers_.begin(), observers_.end(), o), observers_.end());
    }
    void RaiseCanExecuteChanged() override {
        auto copy = observers_;
        for (auto* o : copy) o->OnCanExecuteChanged();
    }

private:
    std::function<bool()> canExecute_;
    std::function<void()> execute_;
    std::vector<ICommandObserver*> observers_;
};

// ---------------------------------------------------------------------------
// Observable list.
// ---------------------------------------------------------------------------
struct ListChange {
    enum Kind { Insert, Remove, Reset } kind = Reset;
    size_t index = 0;
    size_t count = 0;
};

class IListObserver {
public:
    virtual ~IListObserver() = default;
    virtual void OnListChanged(const ListChange& change) = 0;
};

class ObservableListBase : public RefObject {
public:
    virtual size_t Count() const = 0;
    virtual RefPtr ItemAt(size_t index) const = 0;
    virtual void Subscribe(IListObserver* o) = 0;
    virtual void Unsubscribe(IListObserver* o) = 0;

protected:
    void Notify(const ListChange& c) {
        auto copy = observers_;
        for (auto* o : copy) o->OnListChanged(c);
    }
    void AddObserver(IListObserver* o) { observers_.push_back(o); }
    void RemoveObserver(IListObserver* o) {
        observers_.erase(std::remove(observers_.begin(), observers_.end(), o), observers_.end());
    }

private:
    std::vector<IListObserver*> observers_;
};

template <class T>
class ObservableList final : public ObservableListBase {
    static_assert(std::is_base_of_v<RefObject, T>, "list items must derive from RefObject");

public:
    ObservableList() = default;

    void Add(std::shared_ptr<T> item) {
        const size_t index = items_.size();
        items_.push_back(std::move(item));
        Notify(ListChange{ListChange::Insert, index, 1});
    }
    void RemoveAt(size_t index) {
        if (index >= items_.size()) return;
        items_.erase(items_.begin() + static_cast<long>(index));
        Notify(ListChange{ListChange::Remove, index, 1});
    }
    void Clear() {
        if (items_.empty()) return;
        items_.clear();
        Notify(ListChange{ListChange::Reset, 0, 0});
    }
    std::shared_ptr<T> At(size_t i) const { return items_[i]; }

    size_t Count() const override { return items_.size(); }
    RefPtr ItemAt(size_t index) const override {
        return index < items_.size() ? std::static_pointer_cast<RefObject>(items_[index]) : nullptr;
    }
    void Subscribe(IListObserver* o) override { AddObserver(o); }
    void Unsubscribe(IListObserver* o) override { RemoveObserver(o); }

private:
    std::vector<std::shared_ptr<T>> items_;
};

// ---------------------------------------------------------------------------
// ViewModel base: owns its property table, exposes explicit registration.
// ---------------------------------------------------------------------------
class ObservableObject : public RefObject {
public:
    PropertyTable* GetPropertyTable() override { return &table_; }

protected:
    template <class T>
    ObservableProperty<T>& Make(std::string name, ObservableProperty<T>& property) {
        table_.Add(std::move(name), property);
        return property;
    }

    PropertyTable table_;
};

// ---------------------------------------------------------------------------
// Binding diagnostics.
//
// A binding path is resolved against a DataContext at runtime, so it can only
// be partially checked at load time. Historically a typo therefore failed
// silently: the target kept its old value and nothing was reported. This hook
// makes those cases observable (logged, or turned into an error by the host).
// ---------------------------------------------------------------------------
class IBindingDiagnostics {
public:
    virtual ~IBindingDiagnostics() = default;
    // `scope` is the widget type name, `path` the dotted binding path.
    virtual void OnBindingError(std::string_view scope, std::string_view path,
                               std::string_view message) = 0;
};

void SetBindingDiagnostics(IBindingDiagnostics* sink);
IBindingDiagnostics* GetBindingDiagnostics();
// Counts errors raised since process start (including while no sink was set).
size_t BindingErrorCount();

// ---------------------------------------------------------------------------
// Binding target: a property slot on a widget.
// ---------------------------------------------------------------------------
class ISlotObserver {
public:
    virtual ~ISlotObserver() = default;
    virtual void OnSlotChanged(int slot, const Value& value) = 0;
};

struct BindingDesc {
    std::vector<std::string> path;
    BindingMode mode = BindingMode::OneWay;
    UpdateSourceTrigger trigger = UpdateSourceTrigger::PropertyChanged;
    std::wstring stringFormat;
    bool hasSource = false;
    RefPtr explicitSource;
    // Name of a registered converter applied before the implicit type conversion.
    // Empty means none. See Converters.h for the built-ins.
    std::string converter;
};

class Binding final : public IPropertyObserver, public ISlotObserver {
public:
    Binding(Widget* target, int slot, const struct PropertyDescriptor* descriptor, BindingDesc desc);
    ~Binding() override;

    void Attach(ITextService* text);
    void Detach();
    void SetSource(RefPtr source);
    const BindingDesc& Desc() const { return desc_; }
    BindingMode Mode() const { return desc_.mode; }
    int Slot() const { return slot_; }
    // TwoWay with LostFocus trigger: push the current target value into the source.
    void Flush();
    const std::string& LastError() const { return lastError_; }
    bool Attached() const { return attached_; }
    IPropertySource* Leaf() const { return leaf_; }

    // IPropertyObserver (root source object replaced, unused for leaf paths)
    void OnSourceChanged(const Value& value) override;
    // ISlotObserver (target side)
    void OnSlotChanged(int slot, const Value& value) override;

private:
    class HopObserver;
    friend class HopObserver;
    struct Hop {
        IPropertySource* Source = nullptr;
        std::unique_ptr<HopObserver> Observer;
    };

    void ResolveChain();
    void ClearChain();
    void OnHopChanged(size_t index, const Value& value);
    void ApplyToTarget(const Value& value);
    bool PushToSource();
    void RecordError(const std::string& message);

    Widget* target_;
    int slot_;
    const PropertyDescriptor* descriptor_;
    BindingDesc desc_;
    RefPtr source_;
    IPropertySource* leaf_ = nullptr;
    std::vector<Hop> hops_;
    bool applying_ = false;
    bool attached_ = false;
    bool lostFocusPending_ = false;
    Value pendingValue_;
    std::string lastError_;
    ITextService* text_ = nullptr;
};

} // namespace wui
