// SPDX-License-Identifier: MIT
// Manual IME probe: opens a real window with a TextBox and reports every IME message.
//
// This exists because composition cannot be tested by asserting on our own state alone --
// the question "does Microsoft Pinyin actually talk to us" only has an answer on a machine
// with an IME installed. The program prints the message traffic so a human can switch to the
// Chinese IME, type, and confirm the preview appears and commits.
//
// It does not automate the IME: driving a real IME from a test is not reproducible (the
// candidate list is owned by the IME's own window), so the split is
//   automated  = our composition state machine (tests/Tests.cpp, no IME needed)
//   manual     = the OS actually delivering messages (this prober)
#include "Controls.h"
#include "Diagnostics.h"
#include "TextInput.h"
#include "Types.h"
#include "Waml.h"

#include <windows.h>
#include <windowsx.h>  // GET_X_LPARAM / GET_Y_LPARAM
#include <imm.h>

#include <cstdio>
#include <string>

#pragma comment(lib, "imm32.lib")

// windows.h defines DrawText as a macro (DrawTextW). PaintCtx has a method by that name, so
// the macro has to go before the framework headers are seen -- the same reason SampleHost
// undefines it.
#ifdef DrawText
#undef DrawText
#endif
#ifdef DrawTextW
#undef DrawTextW
#endif

using namespace wui;

namespace {

class ProbeTextService final : public ITextService {
public:
    Size Measure(const std::wstring& text, float fontSize, int, const std::wstring&) override {
        return Size{static_cast<float>(text.size()) * fontSize * 0.55f, fontSize * 1.33f};
    }
    float LineHeight(float fontSize, const std::wstring&) override { return fontSize * 1.33f; }
};

ProbeTextService g_text;
WidgetPtr g_root;
Widget* g_focused = nullptr;
int g_imeMessages = 0;
int g_compositions = 0;
int g_results = 0;

void PaintTo(HDC dc, int width, int height, const ThemePalette& palette) {
    // A minimal paint context: the probe only needs to show text, so a GDI-ish fallback is
    // enough and keeps the tool free of the D2D setup the real host owns.
    struct GdiCtx final : public PaintCtx {
        HDC dc;
        explicit GdiCtx(HDC d) : dc(d) {}
        void FillRect(const Rect& r, const Color&) override {
            RECT rc{static_cast<LONG>(r.x), static_cast<LONG>(r.y),
                    static_cast<LONG>(r.x + r.w), static_cast<LONG>(r.y + r.h)};
            HBRUSH b = CreateSolidBrush(RGB(255, 255, 255));
            ::FillRect(dc, &rc, b);
            DeleteObject(b);
        }
        void StrokeRect(const Rect&, const Color&, float, float) override {}
        void FillRoundRect(const Rect& r, float, const Color&) override { FillRect(r, Color{}); }
        void FillEllipse(const Rect&, const Color&) override {}
        void DrawLine(const Point&, const Point&, const Color&, float) override {}
        void DrawBitmap(const Bitmap*, const Rect&, const Rect*) override {}
        void DrawText(const std::wstring& t, const Rect& r, const Color&, float size, int fw,
                      TextAlign, const std::wstring&) override {
            SetBkMode(dc, TRANSPARENT);
            SetTextColor(dc, RGB(20, 20, 20));
            HFONT font = CreateFontW(-static_cast<int>(size * 1.33f), 0, 0, 0,
                                     fw >= WeightBold ? FW_BOLD : FW_NORMAL, FALSE, FALSE,
                                     FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                                     CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH,
                                     L"Segoe UI");
            HGDIOBJ old = SelectObject(dc, font);
            RECT rc{static_cast<LONG>(r.x), static_cast<LONG>(r.y),
                    static_cast<LONG>(r.x + r.w), static_cast<LONG>(r.y + r.h)};
            ::DrawTextW(dc, t.c_str(), -1, &rc, DT_LEFT | DT_SINGLELINE | DT_VCENTER);
            SelectObject(dc, old);
            DeleteObject(font);
        }
        void PushClip(const Rect&) override {}
        void PopClip() override {}
    } ctx(dc);
    g_root->PaintTree(ctx, palette);
}

std::wstring ReadImeString(HWND hwnd, DWORD which) {
    HIMC context = ImmGetContext(hwnd);
    if (!context) return {};
    const LONG bytes = ImmGetCompositionStringW(context, which, nullptr, 0);
    std::wstring result;
    if (bytes > 0) {
        result.resize(static_cast<size_t>(bytes) / sizeof(wchar_t));
        ImmGetCompositionStringW(context, which, result.data(), bytes);
    }
    ImmReleaseContext(hwnd, context);
    return result;
}

class ProbeInput final : public ITextInputService {
public:
    void MoveCandidateWindow(const CompositionCaret& caret) override {
        if (!hwnd) return;
        HIMC context = ImmGetContext(hwnd);
        if (!context) return;
        POINT origin{};
        ClientToScreen(hwnd, &origin);
        COMPOSITIONFORM form{};
        form.dwStyle = CFS_POINT | CFS_FORCE_POSITION;
        form.ptCurrentPos.x = origin.x + static_cast<LONG>(caret.x);
        form.ptCurrentPos.y = origin.y + static_cast<LONG>(caret.y);
        ImmSetCompositionWindow(context, &form);
        CANDIDATEFORM candidate{};
        candidate.dwIndex = 0;
        candidate.dwStyle = CFS_CANDIDATEPOS;
        candidate.ptCurrentPos = form.ptCurrentPos;
        ImmSetCandidateWindow(context, &candidate);
        ImmReleaseContext(hwnd, context);
    }
    HWND hwnd = nullptr;
} g_input;

void UpdateImePosition() {
    if (g_focused) g_input.MoveCandidateWindow(g_focused->ImeCaret());
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_CREATE:
        g_input.hwnd = hwnd;
        TextInputSystem::Set(&g_input);
        return 0;

    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(hwnd, &ps);
        if (g_root) {
            ThemePalette palette = MakePalette(ThemeKind::Light);
            PaintTo(dc, 600, 300, palette);
        }
        EndPaint(hwnd, &ps);
        return 0;
    }

    case WM_LBUTTONDOWN: {
        POINT p{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
        if (g_root) {
            if (Widget* hit = g_root->HitTest(Point{static_cast<float>(p.x),
                                                    static_cast<float>(p.y)})) {
                g_focused = hit->Focusable() ? hit : nullptr;
                hit->SetFocused(true);
                UpdateImePosition();
            }
        }
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;
    }

    case WM_IME_STARTCOMPOSITION:
        ++g_imeMessages;
        std::printf("  WM_IME_STARTCOMPOSITION\n");
        UpdateImePosition();
        return 0;

    case WM_IME_COMPOSITION: {
        ++g_imeMessages;
        if (lp & GCS_RESULTSTR) {
            const std::wstring text = ReadImeString(hwnd, GCS_RESULTSTR);
            ++g_results;
            std::printf("  WM_IME_COMPOSITION RESULTSTR len=%zu\n", text.size());
            if (g_focused) {
                g_focused->CommitComposition(text);
                std::printf("    -> value is now %zu chars\n",
                            g_focused->Get<std::wstring>(g_focused->SlotOf("Text")).size());
            }
        } else if (lp & GCS_COMPSTR) {
            const std::wstring text = ReadImeString(hwnd, GCS_COMPSTR);
            ++g_compositions;
            std::printf("  WM_IME_COMPOSITION COMPSTR len=%zu\n", text.size());
            if (g_focused) g_focused->SetComposition(text);
        }
        UpdateImePosition();
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;
    }

    case WM_IME_ENDCOMPOSITION:
        std::printf("  WM_IME_ENDCOMPOSITION (compositions=%d results=%d)\n", g_compositions,
                    g_results);
        if (g_focused && g_focused->Composing()) g_focused->CancelComposition();
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;

    case WM_IME_NOTIFY:
        UpdateImePosition();
        return 0;

    case WM_CHAR:
        if (g_focused && !g_focused->Composing()) {
            if (g_focused->OnChar(static_cast<wchar_t>(wp))) {
                InvalidateRect(hwnd, nullptr, FALSE);
                return 0;
            }
        }
        return DefWindowProcW(hwnd, msg, wp, lp);

    case WM_TIMER:
        if (g_root && (g_root->LayoutDirty() || g_root->PaintDirty()))
            InvalidateRect(hwnd, nullptr, FALSE);
        return 0;

    case WM_DESTROY:
        KillTimer(hwnd, 1);
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

} // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int) {
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    TextSystem::Set(&g_text);

    DiagnosticBag bag;
    auto doc = CompileWaml(R"WAML(
<Page Background="AppBackground" Padding="20">
  <StackPanel Orientation="Vertical" Spacing="12">
    <TextBlock Text="IME probe" FontSize="22" FontWeight="SemiBold"/>
    <TextBlock Text="Click the box, switch to Chinese input (Win+Space), type pinyin. The preview must appear; Enter commits it. This window prints every WM_IME message."
               FontSize="12" TextWrapping="Wrap"/>
    <TextBox x:Name="box" Text="" FontSize="16" Padding="10" Height="44"/>
    <TextBlock x:Name="status" Text="0 IME messages" FontSize="12"/>
  </StackPanel>
</Page>)WAML",
                          ParseOptions{"ImeProbe.waml"}, bag);
    if (!doc) {
        MessageBoxW(nullptr, L"markup failed", L"IME probe", MB_OK);
        return 1;
    }
    g_root = doc->Instantiate(&g_text);
    g_root->ForceLayout(Size{600, 300});
    g_focused = g_root->FindByName("box");

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = WndProc;
    wc.hInstance = instance;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.lpszClassName = L"ImeProbeWindow";
    RegisterClassExW(&wc);

    HWND hwnd = CreateWindowExW(0, wc.lpszClassName, L"Werfs IME probe", WS_OVERLAPPEDWINDOW,
                                CW_USEDEFAULT, CW_USEDEFAULT, 640, 380, nullptr, nullptr,
                                instance, nullptr);
    if (!hwnd) return 1;
    ShowWindow(hwnd, SW_SHOW);
    UpdateWindow(hwnd);
    SetTimer(hwnd, 1, 32, nullptr);

    std::printf("IME probe ready. Click the box and type with a Chinese IME.\n");
    std::printf("(messages appear here; close the window to exit)\n");

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    std::printf("\nTOTAL: %d IME messages, %d compositions, %d results\n", g_imeMessages,
                g_compositions, g_results);
    return 0;
}
