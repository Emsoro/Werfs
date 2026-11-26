// SPDX-License-Identifier: MIT
// Sample: the editor page's ViewModel implementation. See EditorVm.h for the
// contract and the rules it encodes.
#include "EditorVm.h"

// Standard headers first, before windows.h. windows.h drags in a pile of C
// library headers in its own order; including <fstream> after it made
// <stdexcept> see "exception" as an undefined base (C2504) because windows.h
// pulls <exception> out of order in this translation unit.
#include <algorithm>
#include <exception>
#include <fstream>
#include <sstream>

#include <windows.h>

namespace sample {

namespace {

std::string ToUtf8(const std::wstring& text) { return wui::WideToUtf8(text); }
std::wstring FromUtf8(const std::string& text) { return wui::Utf8ToWide(text); }

// Strips a UTF-8 BOM. Without this a file saved by Notepad compares unequal to
// the text the box shows, so every freshly opened document looks dirty.
std::string StripBom(std::string text) {
    if (text.size() >= 3 && static_cast<unsigned char>(text[0]) == 0xEF &&
        static_cast<unsigned char>(text[1]) == 0xBB &&
        static_cast<unsigned char>(text[2]) == 0xBF) {
        return text.substr(3);
    }
    return text;
}

std::wstring FileNameOf(const std::wstring& path) {
    const size_t slash = path.find_last_of(L"\\/");
    return slash == std::wstring::npos ? path : path.substr(slash + 1);
}

} // namespace

// ---------------------------------------------------------------------------
// EditorVm: construction
// ---------------------------------------------------------------------------
EditorVm::EditorVm()
    : closeObserver_(std::make_unique<CloseObserver>(this)),
      promptObserver_(std::make_unique<PromptObserver>(this)),
      contentObserver_(std::make_unique<ContentObserver>(this)) {
    NewCommand.Set(std::make_shared<wui::RelayCommand>([this] { NewDocument(); }));
    OpenCommand.Set(std::make_shared<wui::RelayCommand>([this] { OpenDocument(); }));
    // Save is the one command with a real precondition. Letting it be disabled is
    // what proves menu enabled state is read from CanExecute at OPEN time rather
    // than baked in at load time.
    SaveCommand.Set(std::make_shared<wui::RelayCommand>([this] { return CanSave.Get(); },
                                                       [this] { SaveDocument(); }));
    SaveAsCommand.Set(std::make_shared<wui::RelayCommand>([this] { SaveDocumentAs(); }));
    CloseTabCommand.Set(std::make_shared<wui::RelayCommand>([this] { CloseActiveDocument(); }));
    ReopenCommand.Set(std::make_shared<wui::RelayCommand>(
        [this] { return lastClosed_ != nullptr; }, [this] { ReopenLastClosed(); }));
    // Four explicit commands instead of a parameterised one: RelayCommand has no
    // CommandParameter, and a menu item is a row with a command, not a form.
    Font12Command.Set(std::make_shared<wui::RelayCommand>([this] { SetFontSize(12); }));
    Font14Command.Set(std::make_shared<wui::RelayCommand>([this] { SetFontSize(14); }));
    Font16Command.Set(std::make_shared<wui::RelayCommand>([this] { SetFontSize(16); }));
    Font18Command.Set(std::make_shared<wui::RelayCommand>([this] { SetFontSize(18); }));

    Make("Documents", Documents);
    Make("ActiveIndex", ActiveIndex);
    Make("CloseRequested", CloseRequested);
    Make("ActiveTitle", ActiveTitle);
    Make("ActiveContent", ActiveContent);
    Make("ActivePath", ActivePath);
    Make("StatusText", StatusText);
    Make("WindowTitle", WindowTitle);
    Make("PromptOpen", PromptOpen);
    Make("PromptTitle", PromptTitle);
    Make("PromptMessage", PromptMessage);
    Make("PromptResult", PromptResult);
    Make("CanSave", CanSave);
    Make("EditorFontSize", EditorFontSize);
    Make("FontSizeLabel", FontSizeLabel);
    Make("NewCommand", NewCommand);
    Make("OpenCommand", OpenCommand);
    Make("SaveCommand", SaveCommand);
    Make("SaveAsCommand", SaveAsCommand);
    Make("CloseTabCommand", CloseTabCommand);
    Make("ReopenCommand", ReopenCommand);
    Make("Font12Command", Font12Command);
    Make("Font14Command", Font14Command);
    Make("Font16Command", Font16Command);
    Make("Font18Command", Font18Command);

    SeedDocuments();

    // Three channels can move the model, each with different handling, so each
    // gets its own observer rather than one switch that depends on read order.
    ActiveIndex.Subscribe(this);
    CloseRequested.Subscribe(closeObserver_.get());
    PromptResult.Subscribe(promptObserver_.get());
    ActiveContent.Subscribe(contentObserver_.get());
}

EditorVm::~EditorVm() {
    ActiveIndex.Unsubscribe(this);
    CloseRequested.Unsubscribe(closeObserver_.get());
    PromptResult.Unsubscribe(promptObserver_.get());
    ActiveContent.Unsubscribe(contentObserver_.get());
}

void EditorVm::SeedDocuments() {
    docs_ = std::make_shared<wui::ObservableList<Document>>();
    Documents.Set(std::static_pointer_cast<wui::RefObject>(docs_));

    AddDocument(L"Untitled 1", L"",
                L"Type here. This tab starts clean, and starts asking as soon as\n"
                L"you change it: closing it will then prompt before discarding.\n");
    AddDocument(L"notes.txt", L"",
                L"A second document, so switching tabs has something to switch to.\n");
    AddDocument(L"README.txt", L"",
                L"The tab strip is data: ItemsSource + SelectedIndex + CloseRequested.\n"
                L"Try the File menu, the toolbar, and closing a tab with edits.\n");

    ActiveIndex.Set(0);
    SyncActive();
}

std::shared_ptr<Document> EditorVm::AddDocument(const std::wstring& title,
                                               const std::wstring& path,
                                               const std::wstring& content) {
    auto doc = std::make_shared<Document>(title, path);
    doc->SetContent(content);
    doc->MarkSaved(); // freshly opened == clean
    docs_->Add(doc);
    return doc;
}

size_t EditorVm::DocumentCount() const { return docs_ ? docs_->Count() : 0; }

std::shared_ptr<Document> EditorVm::DocAt(size_t index) const {
    if (!docs_ || index >= docs_->Count()) return nullptr;
    return std::dynamic_pointer_cast<Document>(docs_->ItemAt(index));
}

std::shared_ptr<Document> EditorVm::ActiveDoc() const {
    const int index = ActiveIndex.Get();
    if (index < 0) return nullptr;
    return DocAt(static_cast<size_t>(index));
}

// ---------------------------------------------------------------------------
// EditorVm: derived state
// ---------------------------------------------------------------------------
void EditorVm::SyncActive() {
    if (syncing_) return;
    syncing_ = true;
    // Clamp the selection: after a tab is removed the index may point past the
    // end. Clamping in the model, not only in the control, is what makes the model
    // self-consistent when driven headlessly.
    const int count = static_cast<int>(DocumentCount());
    int index = ActiveIndex.Get();
    if (index >= count) index = count - 1;
    if (index < 0 && count > 0) index = 0;
    if (index != ActiveIndex.Get()) ActiveIndex.Set(index);

    auto doc = ActiveDoc();
    if (doc) {
        ActiveTitle.Set(doc->Title.Get());
        ActiveContent.Set(doc->Content.Get());
        ActivePath.Set(doc->HasPath() ? doc->Path.Get() : L"(never saved)");
        WindowTitle.Set(doc->TabLabel.Get());
    } else {
        ActiveTitle.Set(L"(no document)");
        ActiveContent.Set(L"");
        ActivePath.Set(L"\u2014");
        WindowTitle.Set(L"Werfs Editor");
    }
    RefreshProperties();
    syncing_ = false;
}

void EditorVm::SetFontSize(int size) {
    const int clamped = std::clamp(size, 10, 24);
    if (EditorFontSize.Get() == clamped) return;
    EditorFontSize.Set(clamped);
    FontSizeLabel.Set(L"Font size (" + std::to_wstring(clamped) + L" pt)");
}

void EditorVm::RefreshProperties() {
    auto doc = ActiveDoc();
    CanSave.Set(doc != nullptr);

    std::wstring status;
    if (doc) {
        status = doc->HasPath() ? doc->Path.Get() : std::wstring(L"New document");
        status += doc->IsDirty.Get() ? L"  \u2014  modified" : L"  \u2014  saved";
        status += L"   |   " + std::to_wstring(docs_ ? docs_->Count() : 0) + L" tab(s) open";
    } else {
        status = L"No document open. Use File \u203a New.";
    }
    StatusText.Set(status);
}

// ---------------------------------------------------------------------------
// EditorVm: the close flow -- the heart of the sample
// ---------------------------------------------------------------------------
void EditorVm::RequestClose(int index) {
    if (index < 0 || index >= static_cast<int>(DocumentCount())) return;
    if (PromptOpen.Get()) return; // one prompt at a time

    auto doc = DocAt(static_cast<size_t>(index));
    if (!doc) return;

    if (!doc->IsDirty.Get()) {
        // Clean: nothing to ask. Closing without a prompt is the entire reason the
        // dirty flag exists.
        RemoveAt(index);
        return;
    }

    // Dirty: ask. The model does not create a dialog -- it raises a request and
    // waits. `pendingClose_` is what makes "Cancel" possible: nothing has been
    // removed yet, so cancelling is simply declining to proceed.
    pendingClose_ = index;
    PromptTitle.Set(L"Unsaved changes");
    PromptMessage.Set(L"\"" + doc->Title.Get() +
                      L"\" has changes that have not been saved.\n"
                      L"Save them before closing, discard them, or go back to the tab.");
    PromptResult.Set(-100);
    PromptOpen.Set(true);
}

void EditorVm::ResolveClose(CloseChoice choice) {
    const int index = pendingClose_;
    pendingClose_ = -1;
    PromptOpen.Set(false);
    if (index < 0) return;

    switch (choice) {
    case CloseChoice::Cancel:
        // Nothing to undo: the tab was never removed, so it is still there with its
        // content intact. This is the branch that makes the prompt trustworthy.
        StatusText.Set(L"Close cancelled \u2014 the tab is still open.");
        return;
    case CloseChoice::Save: {
        auto doc = DocAt(static_cast<size_t>(index));
        if (!doc) return;
        bool saved = false;
        if (!doc->HasPath()) {
            // A document that was never saved has nowhere to go: ask for a path
            // first. Cancelling that dialog cancels the close too, rather than
            // closing a tab whose "Save" silently did nothing.
            saved = SaveDocumentAsImpl(doc);
        } else {
            saved = WriteTextFile(doc->Path.Get(), doc->Content.Get());
            if (saved) doc->MarkSaved();
            else StatusText.Set(L"Could not save " + doc->Path.Get() + L" \u2014 the tab is still open.");
        }
        if (!saved) {
            if (!doc->HasPath()) StatusText.Set(L"Save cancelled \u2014 the tab is still open.");
            return;
        }
        RemoveAt(index);
        return;
    }
    case CloseChoice::Discard:
        RemoveAt(index);
        return;
    }
}

void EditorVm::RemoveAt(int index) {
    if (index < 0 || index >= static_cast<int>(DocumentCount())) return;
    // Keep the document alive for Reopen before the list drops its only reference:
    // a closed tab whose text is gone cannot genuinely be brought back.
    lastClosed_ = DocAt(static_cast<size_t>(index));
    docs_->RemoveAt(static_cast<size_t>(index));

    const int count = static_cast<int>(DocumentCount());
    if (count == 0) {
        ActiveIndex.Set(-1);
    } else if (index >= count) {
        ActiveIndex.Set(count - 1);
    } else if (index != ActiveIndex.Get()) {
        // The same index now holds the next document, which is what a tabbed
        // editor does. Jumping back to the first tab after every close disorients.
        ActiveIndex.Set(index);
    }
    SyncActive();
}

void EditorVm::ReopenLastClosed() {
    if (!lastClosed_) return;
    docs_->Add(lastClosed_);
    lastClosed_.reset();
    ActiveIndex.Set(static_cast<int>(DocumentCount()) - 1);
    SyncActive();
    StatusText.Set(L"Reopened the last closed document.");
}

// ---------------------------------------------------------------------------
// EditorVm: commands
// ---------------------------------------------------------------------------
void EditorVm::NewDocument() {
    // A distinct default title per document: three tabs all called "Untitled"
    // makes the strip useless.
    const std::wstring title = L"Untitled " + std::to_wstring(DocumentCount() + 1);
    AddDocument(title, L"", L"");
    ActiveIndex.Set(static_cast<int>(DocumentCount()) - 1);
    SyncActive();
    StatusText.Set(L"Created " + title + L".");
}

void EditorVm::OpenDocument() {
    if (!dialogs_) {
        StatusText.Set(L"No file dialog service is installed.");
        return;
    }
    wui::FileDialogRequest request;
    request.title = L"Open a text file";
    request.defaultExtension = L"txt";
    request.allowMultiple = true;
    wui::FileFilter text;
    text.label = L"Text files";
    text.pattern = L"*.txt;*.waml;*.md;*.log";
    request.filters.push_back(text);
    wui::FileFilter all;
    all.label = L"All files";
    all.pattern = L"*.*";
    request.filters.push_back(all);

    const wui::FileDialogResult result = dialogs_->ShowOpen(request);
    if (!result.accepted || result.paths.empty()) {
        StatusText.Set(L"Open cancelled.");
        return;
    }
    // Every accepted path becomes its own tab, so a multi-select open behaves the
    // way the file dialog promised.
    int opened = 0;
    for (const auto& path : result.paths) {
        std::wstring content;
        if (!ReadTextFile(path, content)) continue;
        auto doc = AddDocument(FileNameOf(path), path, content);
        doc->MarkSaved();
        ++opened;
    }
    if (opened == 0) {
        StatusText.Set(L"Nothing could be read from the selection.");
        return;
    }
    ActiveIndex.Set(static_cast<int>(DocumentCount()) - 1);
    SyncActive();
    StatusText.Set(L"Opened " + std::to_wstring(opened) + L" file(s).");
}

void EditorVm::SaveDocument() {
    auto doc = ActiveDoc();
    if (!doc) return;
    if (!doc->HasPath()) {
        SaveDocumentAs();
        return;
    }
    if (!WriteTextFile(doc->Path.Get(), doc->Content.Get())) {
        StatusText.Set(L"Could not save " + doc->Path.Get() + L".");
        return;
    }
    doc->MarkSaved();
    SyncActive();
    StatusText.Set(L"Saved " + doc->Path.Get() + L".");
}

void EditorVm::SaveDocumentAs() {
    if (auto doc = ActiveDoc()) SaveDocumentAsImpl(doc);
}

bool EditorVm::SaveDocumentAsImpl(std::shared_ptr<Document> doc) {
    if (!doc) return false;
    if (!dialogs_) {
        StatusText.Set(L"No file dialog service is installed.");
        return false;
    }
    wui::FileDialogRequest request;
    request.title = L"Save as";
    request.defaultExtension = L"txt";
    request.suggestedName = doc->HasPath() ? doc->Path.Get() : doc->Title.Get() + L".txt";
    wui::FileFilter filter;
    filter.label = L"Text files";
    filter.pattern = L"*.txt";
    request.filters.push_back(filter);

    const wui::FileDialogResult result = dialogs_->ShowSave(request);
    if (!result.accepted || result.paths.empty()) return false; // cancelled

    const std::wstring path = result.paths.front();
    if (!WriteTextFile(path, doc->Content.Get())) {
        StatusText.Set(L"Could not write " + path + L".");
        return false;
    }
    doc->Path.Set(path);
    doc->Title.Set(FileNameOf(path));
    doc->MarkSaved();
    SyncActive();
    StatusText.Set(L"Saved as " + path + L".");
    return true;
}

void EditorVm::CloseActiveDocument() { RequestClose(ActiveIndex.Get()); }

// ---------------------------------------------------------------------------
// EditorVm: observation
// ---------------------------------------------------------------------------
void EditorVm::CloseObserver::OnSourceChanged(const wui::Value&) {
    owner_->OnCloseRequestedImpl();
}

void EditorVm::PromptObserver::OnSourceChanged(const wui::Value&) {
    owner_->OnPromptResultImpl();
}

void EditorVm::ContentObserver::OnSourceChanged(const wui::Value& value) {
    // Typing in the editor box writes ActiveContent. Push that into the active
    // DOCUMENT so its derived dirty flag updates -- otherwise the tab never shows
    // the unsaved dot and closing it would discard edits without asking, which is
    // exactly the data loss this sample exists to prevent.
    //
    // Two guards, both necessary:
    //  * syncing_ : SyncActive() sets ActiveContent itself when the selection
    //    changes; without the guard a tab switch would write the new document's text
    //    back into itself and could mark it dirty on the way.
    //  * equality: SetContent already short-circuits on identical text, but the check
    //    here keeps the observer from doing any work for a no-op echo.
    if (owner_->syncing_) return;
    if (value.type != wui::VType::Text) return;
    auto doc = owner_->ActiveDoc();
    if (!doc) return;
    if (doc->Content.Get() == value.text) return;
    doc->SetContent(value.text);
    // The status line reports "saved"/"modified", so it has to follow.
    owner_->RefreshProperties();
}

void EditorVm::OnCloseRequestedImpl() {
    const int index = CloseRequested.Get();
    // The control publishes the index and then resets to -1 (see
    // TabView::RequestClose). Only the non-negative edge is a real request; the
    // reset exists so closing the same index twice is two distinct notifications.
    if (index >= 0) RequestClose(index);
}

void EditorVm::OnPromptResultImpl() {
    // The dialog published a choice. Read it, then clear it so the same choice can
    // be made again later: a property that never resets only fires once, which is
    // how "Cancel then Discard" would silently do nothing the second time.
    const int result = PromptResult.Get();
    if (result < 0) return;
    PromptResult.Set(-100);
    if (!PromptOpen.Get()) return;
    ResolveClose(result == 1 ? CloseChoice::Save
                : result == 2 ? CloseChoice::Discard
                              : CloseChoice::Cancel);
}

void EditorVm::OnSourceChanged(const wui::Value&) {
    if (syncing_) return;
    SyncActive();
}

// ---------------------------------------------------------------------------
// EditorVm: platform helpers
// ---------------------------------------------------------------------------
bool EditorVm::ReadTextFile(const std::wstring& path, std::wstring& out) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return false;
    std::ostringstream ss;
    ss << in.rdbuf();
    out = FromUtf8(StripBom(ss.str()));
    return true;
}

bool EditorVm::WriteTextFile(const std::wstring& path, const std::wstring& text) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) return false;
    out << ToUtf8(text);
    return true;
}

} // namespace sample
