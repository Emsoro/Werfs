// SPDX-License-Identifier: MIT
// Sample: the editor page's ViewModel -- a multi-document text editor skeleton.
//
// SAMPLE code. Exists only to give samples/editor/Editor.waml something to bind to.
//
// What this sample is FOR: it is the integration point. Every capability the
// framework grew for real applications meets here in one page --
//   * file dialogs (open / save as)                        §10b.1
//   * a menu bar with a submenu and disabled items          §10c batch C
//   * a command bar that overflows when the window narrows  §10c batch C
//   * a tab strip: switch, close, new, dirty marker         §10c batch B
//   * an unsaved-changes prompt (save / don't save / cancel) §10c batch D
//
// The three rules this model encodes, each of which is somewhere a naive editor
// gets it wrong:
//
//   1. The ViewModel decides WHETHER to ask; the view decides HOW. No dialog is
//      ever created here -- the model raises a request and the markup's
//      ContentDialog renders it. That is what keeps this class testable headlessly.
//   2. Closing a dirty document must not lose data. Every path (× button, menu,
//      window close) funnels through RequestClose, and a cancelled prompt leaves
//      the tab exactly where it was.
//   3. "Dirty" is content, not a flag someone remembered to set: it compares the
//      text against the snapshot taken when the document was opened or saved.
//      Typing a character and deleting it again leaves the tab clean.
#pragma once

#include "Controls.h"  // IBoundTitle, TabEntryModel-free tab titles
#include "FileDialog.h"
#include "Mvvm.h"

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace sample {

// What the user chose on an unsaved-changes prompt. The numbers are the values
// ContentDialog publishes, so the mapping is explicit rather than a cast.
enum class CloseChoice { Cancel = 0, Save = 1, Discard = 2 };

// One open document. A tab in the strip, and the model behind the editor area.
class Document final : public wui::ObservableObject, public wui::IBoundTitle {
public:
    explicit Document(std::wstring title, std::wstring path = std::wstring()) {
        Title.Set(std::move(title));
        Path.Set(std::move(path));
        Content.Set(L"");
        Make("Title", Title);
        Make("Path", Path);
        Make("Content", Content);
        Make("IsDirty", IsDirty);
        // A derived label so the markup does not need a multi-binding or a
        // converter just to show "name — modified".
        Make("TabLabel", TabLabel);
        // A document is born clean: its baseline IS its current text. Taking the
        // baseline here rather than leaving it empty is what stops every newly
        // created tab from opening already marked dirty.
        MarkSaved();
    }

    // The tab strip reads this instead of stringifying the item. Both this and
    // the WAML's {Binding Title} point at the same field, so renaming a document
    // keeps them in step.
    std::wstring BoundTitle() const override { return Title.Get(); }

    // Where the text came from / goes back to. Empty means "never saved".
    bool HasPath() const { return !Path.Get().empty(); }

    // The dirty flag is DERIVED, never assigned. A stored flag drifts: an undo
    // back to the original text leaves a stored flag set forever, and the user
    // gets asked to save a document they did not change.
    void RecomputeDirty() {
        const bool dirty = Content.Get() != baseline_;
        IsDirty.Set(dirty);
        std::wstring label = Title.Get();
        if (dirty) label += L" \u2022"; // the same dot the tab strip draws
        TabLabel.Set(label);
    }

    // Call after a successful save: the current text becomes the new baseline.
    void MarkSaved() {
        baseline_ = Content.Get();
        RecomputeDirty();
    }

    // Text change from the editor box. Goes through here rather than straight
    // into Content so the dirty flag cannot be forgotten.
    void SetContent(std::wstring text) {
        if (Content.Get() == text) return;
        Content.Set(std::move(text));
        RecomputeDirty();
    }

    wui::ObservableProperty<std::wstring> Title{"Title"};
    wui::ObservableProperty<std::wstring> Path{"Path"};
    wui::ObservableProperty<std::wstring> Content{"Content"};
    wui::ObservableProperty<bool> IsDirty{"IsDirty", false};
    wui::ObservableProperty<std::wstring> TabLabel{"TabLabel"};

private:
    // The text as of the last open/save. Compared against, never published.
    std::wstring baseline_;
};

// The whole page. Every property name here is bound by name in Editor.waml.
class EditorVm final : public wui::ObservableObject, private wui::IPropertyObserver {
public:
    EditorVm();
    ~EditorVm() override;

    // --- document management ------------------------------------------------
    std::shared_ptr<Document> AddDocument(const std::wstring& title,
                                          const std::wstring& path = std::wstring(),
                                          const std::wstring& content = std::wstring());

    // The single entry point for "the user wants this tab gone". Shows a prompt
    // when the document is dirty; the tab is removed only after the user agrees.
    void RequestClose(int index);
    void ResolveClose(CloseChoice choice);

    // Ids, so a test can drive the flow without a dialog.
    int PendingCloseIndex() const { return pendingClose_; }

    // --- commands -----------------------------------------------------------
    void NewDocument();
    void OpenDocument();
    void SaveDocument();
    void SaveDocumentAs();
    void CloseActiveDocument();
    void ReopenLastClosed();

    std::shared_ptr<Document> ActiveDoc() const;
    void SyncActive();

    // Called by the close observer when the tab strip publishes a close request.
    void OnCloseRequestedImpl();
    // Called by the prompt observer when the dialog publishes a choice.
    void OnPromptResultImpl();

    // Property observer for ActiveIndex: reacts to the selection changing from ANY
    // source (tab click, New, close). A single observer rather than a call at each
    // assignment site is what keeps the derived labels from going stale.
    void OnSourceChanged(const wui::Value& value) override;

    // --- properties ---------------------------------------------------------
    wui::ObservableProperty<wui::RefPtr> Documents{"Documents"};
    wui::ObservableProperty<int> ActiveIndex{"ActiveIndex", -1};
    wui::ObservableProperty<int> CloseRequested{"CloseRequested", -1};

    // The active document, surfaced for the editor area and the status line.
    wui::ObservableProperty<std::wstring> ActiveTitle{"ActiveTitle"};
    wui::ObservableProperty<std::wstring> ActiveContent{"ActiveContent"};
    wui::ObservableProperty<std::wstring> ActivePath{"ActivePath"};
    wui::ObservableProperty<std::wstring> StatusText{"StatusText"};
    wui::ObservableProperty<std::wstring> WindowTitle{"WindowTitle"};

    // Unsaved-changes prompt. The model only raises it; the markup's
    // ContentDialog renders it, and `PromptMessage` is what the prompt asks.
    wui::ObservableProperty<bool> PromptOpen{"PromptOpen", false};
    wui::ObservableProperty<std::wstring> PromptTitle{"PromptTitle"};
    wui::ObservableProperty<std::wstring> PromptMessage{"PromptMessage"};
    // Chosen result from the dialog, observed so the model reacts to the click.
    wui::ObservableProperty<int> PromptResult{"PromptResult", -100};

    // Is there anything to save? Drives IsEnabled on the Save menu item and the
    // toolbar button, which is the visible proof that menu enabled state is live.
    wui::ObservableProperty<bool> CanSave{"CanSave", false};

    // Text size of the tab CONTENT, not of the UI chrome. Bound OneWay to the
    // editor TextBox; the View menu reaches it through third-level submenus.
    wui::ObservableProperty<int> EditorFontSize{"EditorFontSize", 14};
    wui::ObservableProperty<std::wstring> FontSizeLabel{"FontSizeLabel", L"Font size (14 pt)"};

    // Commands.
    wui::ObservableProperty<wui::RefPtr> NewCommand{"NewCommand"};
    wui::ObservableProperty<wui::RefPtr> OpenCommand{"OpenCommand"};
    wui::ObservableProperty<wui::RefPtr> SaveCommand{"SaveCommand"};
    wui::ObservableProperty<wui::RefPtr> SaveAsCommand{"SaveAsCommand"};
    wui::ObservableProperty<wui::RefPtr> CloseTabCommand{"CloseTabCommand"};
    wui::ObservableProperty<wui::RefPtr> ReopenCommand{"ReopenCommand"};
    wui::ObservableProperty<wui::RefPtr> Font12Command{"Font12Command"};
    wui::ObservableProperty<wui::RefPtr> Font14Command{"Font14Command"};
    wui::ObservableProperty<wui::RefPtr> Font16Command{"Font16Command"};
    wui::ObservableProperty<wui::RefPtr> Font18Command{"Font18Command"};

    // The dialog service, injectable so tests never open a real file dialog.
    void SetDialogService(wui::IFileDialogService* service) { dialogs_ = service; }

    // Platform helpers, virtual so a test can substitute an in-memory file system
    // instead of touching the disk.
    virtual bool ReadTextFile(const std::wstring& path, std::wstring& out);
    virtual bool WriteTextFile(const std::wstring& path, const std::wstring& text);

    // Test access.
    size_t DocumentCount() const;
    std::shared_ptr<Document> DocAt(size_t index) const;

private:
    void SetFontSize(int size);
    void RefreshProperties();
    void RemoveAt(int index);
    void SeedDocuments();
    // Saves `doc`, asking for a path when it has none. Returns false when the
    // user cancelled or the write failed -- the caller must not close the tab then.
    bool SaveDocumentAsImpl(std::shared_ptr<Document> doc);

    std::shared_ptr<wui::ObservableList<Document>> docs_;
    wui::IFileDialogService* dialogs_ = nullptr;

    int pendingClose_ = -1;
    // Last closed document, so "Reopen" can bring it back. A one-slot undo; a
    // real editor would keep a stack, and the framework has no opinion either way.
    std::shared_ptr<Document> lastClosed_;

    bool syncing_ = false;

    // Distinct observers, because each channel needs different handling and a
    // shared switch made the close path depend on evaluation order. Defined in the
    // .cpp -- they are an implementation detail no caller should name.
    class CloseObserver final : public wui::IPropertyObserver {
    public:
        explicit CloseObserver(EditorVm* owner) : owner_(owner) {}
        void OnSourceChanged(const wui::Value&) override;
    private:
        EditorVm* owner_;
    };
    class PromptObserver final : public wui::IPropertyObserver {
    public:
        explicit PromptObserver(EditorVm* owner) : owner_(owner) {}
        void OnSourceChanged(const wui::Value&) override;
    private:
        EditorVm* owner_;
    };
    // Typing reaches ActiveContent through the TextBox's TwoWay binding. Something
    // has to move that value into the DOCUMENT, or the dirty flag never flips and a
    // changed tab closes without asking -- silent data loss. This observer is that
    // something.
    class ContentObserver final : public wui::IPropertyObserver {
    public:
        explicit ContentObserver(EditorVm* owner) : owner_(owner) {}
        void OnSourceChanged(const wui::Value&) override;
    private:
        EditorVm* owner_;
    };
    std::unique_ptr<CloseObserver> closeObserver_;
    std::unique_ptr<PromptObserver> promptObserver_;
    std::unique_ptr<ContentObserver> contentObserver_;

    // The observers are nested types but act on a DIFFERENT EditorVm instance (the
    // one that owns them), which is a friend relationship, not a member one.
    friend class CloseObserver;
    friend class PromptObserver;
    friend class ContentObserver;
};

} // namespace sample
