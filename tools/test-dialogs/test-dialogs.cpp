// Test harness for the File Dialog Favorites mod.
//
// Opens each kind of File Dialog through both the modern IFileDialog API and
// the legacy GetOpenFileName/GetSaveFileName API, and logs what the dialog
// returned (path, file type index) so that a test run can confirm that
// Navigating to a Favorite neither Committed the dialog nor changed the Typed
// File Name or the File Type Filter.

#include <windows.h>
#include <commdlg.h>
#include <shobjidl.h>

#include <string>

namespace {

enum CommandId {
    kOpenModern = 100,
    kSaveModern,
    kPickFolderModern,
    kOpenLegacy,
    kSaveLegacy,
    kSaveModernThread,
};

struct ButtonSpec {
    CommandId id;
    PCWSTR label;
};

constexpr ButtonSpec kButtons[] = {
    {kOpenModern, L"Open (IFileDialog)"},
    {kSaveModern, L"Save (IFileDialog)"},
    {kPickFolderModern, L"Pick folder (IFileDialog)"},
    {kOpenLegacy, L"Open (GetOpenFileName)"},
    {kSaveLegacy, L"Save (GetSaveFileName)"},
    {kSaveModernThread, L"Save (IFileDialog, new thread)"},
};

constexpr COMDLG_FILTERSPEC kFileTypes[] = {
    {L"Text files (*.txt)", L"*.txt"},
    {L"CSV files (*.csv)", L"*.csv"},
    {L"All files (*.*)", L"*.*"},
};

// Second entry selected by default, so that a reset to the first entry is
// visible.
constexpr UINT kDefaultFileTypeIndex = 2;
constexpr PCWSTR kDefaultSaveName = L"typed-name-must-survive.csv";

constexpr WCHAR kLegacyFilter[] =
    L"Text files (*.txt)\0*.txt\0CSV files (*.csv)\0*.csv\0"
    L"All files (*.*)\0*.*\0";

HWND g_log;

void Log(const std::wstring& line) {
    std::wstring text = line + L"\r\n";
    int length = GetWindowTextLengthW(g_log);
    SendMessageW(g_log, EM_SETSEL, length, length);
    SendMessageW(g_log, EM_REPLACESEL, FALSE, (LPARAM)text.c_str());
}

std::wstring HrText(HRESULT hr) {
    WCHAR buffer[32];
    wsprintfW(buffer, L"0x%08X", (unsigned)hr);
    return buffer;
}

void RunModern(HWND owner, CommandId id) {
    const CLSID& clsid =
        id == kSaveModern ? CLSID_FileSaveDialog : CLSID_FileOpenDialog;
    IFileDialog* dialog = nullptr;
    HRESULT hr = CoCreateInstance(clsid, nullptr, CLSCTX_INPROC_SERVER,
                                  IID_PPV_ARGS(&dialog));
    if (FAILED(hr)) {
        Log(L"CoCreateInstance failed: " + HrText(hr));
        return;
    }

    if (id == kPickFolderModern) {
        FILEOPENDIALOGOPTIONS options = 0;
        dialog->GetOptions(&options);
        dialog->SetOptions(options | FOS_PICKFOLDERS);
    } else {
        dialog->SetFileTypes(ARRAYSIZE(kFileTypes), kFileTypes);
        dialog->SetFileTypeIndex(kDefaultFileTypeIndex);
        if (id == kSaveModern) {
            dialog->SetFileName(kDefaultSaveName);
            dialog->SetDefaultExtension(L"csv");
        }
    }

    hr = dialog->Show(owner);
    if (hr == HRESULT_FROM_WIN32(ERROR_CANCELLED)) {
        Log(L"  Cancelled");
    } else if (FAILED(hr)) {
        Log(L"  Show failed: " + HrText(hr));
    } else {
        IShellItem* item = nullptr;
        PWSTR path = nullptr;
        if (SUCCEEDED(dialog->GetResult(&item)) &&
            SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &path))) {
            Log(std::wstring(L"  Result: ") + path);
            CoTaskMemFree(path);
        }
        if (item) {
            item->Release();
        }
        UINT typeIndex = 0;
        if (id != kPickFolderModern &&
            SUCCEEDED(dialog->GetFileTypeIndex(&typeIndex))) {
            Log(L"  File type index: " + std::to_wstring(typeIndex));
        }
    }

    dialog->Release();
}

void RunLegacy(HWND owner, CommandId id) {
    WCHAR fileName[MAX_PATH] = L"";
    if (id == kSaveLegacy) {
        lstrcpynW(fileName, kDefaultSaveName, MAX_PATH);
    }

    OPENFILENAMEW ofn = {sizeof(ofn)};
    ofn.hwndOwner = owner;
    ofn.lpstrFilter = kLegacyFilter;
    ofn.nFilterIndex = kDefaultFileTypeIndex;
    ofn.lpstrFile = fileName;
    ofn.nMaxFile = MAX_PATH;
    ofn.lpstrDefExt = L"csv";
    ofn.Flags = OFN_EXPLORER | OFN_ENABLESIZING;

    BOOL ok;
    if (id == kSaveLegacy) {
        ofn.Flags |= OFN_OVERWRITEPROMPT;
        ok = GetSaveFileNameW(&ofn);
    } else {
        ofn.Flags |= OFN_FILEMUSTEXIST;
        ok = GetOpenFileNameW(&ofn);
    }

    if (ok) {
        Log(std::wstring(L"  Result: ") + fileName);
        Log(L"  File type index: " + std::to_wstring(ofn.nFilterIndex));
    } else {
        DWORD error = CommDlgExtendedError();
        Log(error ? L"  Failed: CommDlgExtendedError " + std::to_wstring(error)
                  : L"  Cancelled");
    }
}

// Electron (and so VS Code) shows each File Dialog on a new STA thread that
// ends when the dialog closes, while the owner window stays responsive.
DWORD WINAPI DialogThread(void* owner) {
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    RunModern((HWND)owner, kSaveModern);
    CoUninitialize();
    return 0;
}

void RunCommand(HWND owner, CommandId id) {
    for (const ButtonSpec& button : kButtons) {
        if (button.id == id) {
            Log(button.label);
        }
    }

    if (id == kSaveModernThread) {
        HANDLE thread = CreateThread(nullptr, 0, DialogThread, owner, 0, nullptr);
        if (thread) {
            CloseHandle(thread);
        }
    } else if (id == kOpenLegacy || id == kSaveLegacy) {
        RunLegacy(owner, id);
    } else {
        RunModern(owner, id);
    }
}

void Layout(HWND hwnd) {
    RECT client;
    GetClientRect(hwnd, &client);
    UINT dpi = GetDpiForWindow(hwnd);
    int margin = MulDiv(10, dpi, 96);
    int buttonHeight = MulDiv(30, dpi, 96);
    int buttonWidth = MulDiv(220, dpi, 96);

    int y = margin;
    for (const ButtonSpec& button : kButtons) {
        MoveWindow(GetDlgItem(hwnd, button.id), margin, y, buttonWidth,
                   buttonHeight, TRUE);
        y += buttonHeight + margin / 2;
    }

    int logLeft = margin * 2 + buttonWidth;
    MoveWindow(g_log, logLeft, margin, client.right - logLeft - margin,
               client.bottom - margin * 2, TRUE);
}

void ApplyFont(HWND hwnd) {
    UINT dpi = GetDpiForWindow(hwnd);
    NONCLIENTMETRICSW metrics = {sizeof(metrics)};
    SystemParametersInfoForDpi(SPI_GETNONCLIENTMETRICS, sizeof(metrics),
                               &metrics, 0, dpi);
    static HFONT font;
    HFONT newFont = CreateFontIndirectW(&metrics.lfMessageFont);
    for (const ButtonSpec& button : kButtons) {
        SendMessageW(GetDlgItem(hwnd, button.id), WM_SETFONT, (WPARAM)newFont,
                     TRUE);
    }
    SendMessageW(g_log, WM_SETFONT, (WPARAM)newFont, TRUE);
    if (font) {
        DeleteObject(font);
    }
    font = newFont;
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_CREATE: {
            HINSTANCE instance = ((CREATESTRUCTW*)lParam)->hInstance;
            for (const ButtonSpec& button : kButtons) {
                CreateWindowExW(0, L"BUTTON", button.label,
                                WS_CHILD | WS_VISIBLE | WS_TABSTOP, 0, 0, 0, 0,
                                hwnd, (HMENU)(INT_PTR)button.id, instance,
                                nullptr);
            }
            g_log = CreateWindowExW(
                WS_EX_CLIENTEDGE, L"EDIT", L"",
                WS_CHILD | WS_VISIBLE | WS_VSCROLL | ES_MULTILINE |
                    ES_AUTOVSCROLL | ES_READONLY,
                0, 0, 0, 0, hwnd, nullptr, instance, nullptr);
            ApplyFont(hwnd);
            Log(sizeof(void*) == 8 ? L"64-bit process" : L"32-bit process");
            return 0;
        }

        case WM_SIZE:
            Layout(hwnd);
            return 0;

        case WM_DPICHANGED: {
            RECT* suggested = (RECT*)lParam;
            SetWindowPos(hwnd, nullptr, suggested->left, suggested->top,
                         suggested->right - suggested->left,
                         suggested->bottom - suggested->top,
                         SWP_NOZORDER | SWP_NOACTIVATE);
            ApplyFont(hwnd);
            Layout(hwnd);
            return 0;
        }

        case WM_COMMAND:
            if (HIWORD(wParam) == BN_CLICKED && LOWORD(wParam) >= kOpenModern &&
                LOWORD(wParam) <= kSaveModernThread) {
                RunCommand(hwnd, (CommandId)LOWORD(wParam));
            }
            return 0;

        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

}  // namespace

// Real applications embed a manifest that selects Common Controls v6, which
// changes how the File Dialog looks. The bundled compiler has no resource
// compiler, so borrow the v6 manifest that shell32.dll embeds instead.
static void ActivateCommonControlsV6() {
    WCHAR shell32[MAX_PATH];
    GetSystemDirectoryW(shell32, MAX_PATH);
    lstrcatW(shell32, L"\\shell32.dll");

    ACTCTXW context = {sizeof(context)};
    context.dwFlags = ACTCTX_FLAG_RESOURCE_NAME_VALID;
    context.lpSource = shell32;
    context.lpResourceName = MAKEINTRESOURCEW(124);
    HANDLE handle = CreateActCtxW(&context);
    ULONG_PTR cookie;
    if (handle != INVALID_HANDLE_VALUE) {
        ActivateActCtx(handle, &cookie);
    }
}

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int showCommand) {
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    ActivateCommonControlsV6();
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);

    WNDCLASSEXW wc = {sizeof(wc)};
    wc.lpfnWndProc = WndProc;
    wc.hInstance = instance;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    wc.lpszClassName = L"FileDialogFavoritesTestDialogs";
    RegisterClassExW(&wc);

    PCWSTR title = sizeof(void*) == 8 ? L"File Dialog Favorites test (x64)"
                                      : L"File Dialog Favorites test (x86)";
    HWND hwnd = CreateWindowExW(0, wc.lpszClassName, title,
                                WS_OVERLAPPEDWINDOW, CW_USEDEFAULT,
                                CW_USEDEFAULT, 760, 420, nullptr, nullptr,
                                instance, nullptr);
    ShowWindow(hwnd, showCommand);

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0)) {
        if (!IsDialogMessageW(hwnd, &msg)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }

    CoUninitialize();
    return 0;
}
