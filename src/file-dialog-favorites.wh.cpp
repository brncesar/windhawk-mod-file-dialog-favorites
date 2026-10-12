// ==WindhawkMod==
// @id              file-dialog-favorites
// @name            File Dialog Favorites
// @description     Adds a favorites button to the title bar of Open/Save dialogs that moves the dialog to a favorite folder
// @version         0.1.1
// @author          Bruno Cesar
// @github          https://github.com/brncesar
// @homepage        https://github.com/brncesar/windhawk-mod-file-dialog-favorites
// @license         MIT
// @include         test-dialogs-x64.exe
// @include         test-dialogs-x86.exe
// @include         notepad.exe
// @include         mspaint.exe
// @architecture    x86
// @architecture    x86-64
// @compilerOptions -lole32 -lshell32 -luuid -lcomctl32 -lgdi32 -ldwmapi -ladvapi32
// ==/WindhawkMod==

// ==WindhawkModReadme==
/*
# File Dialog Favorites

> **Experimental proof of concept.** Only a few programs are targeted, and
> behavior is still being validated.

Adds a folder button to the title bar of the standard Windows 11 **Open**,
**Save** and **Select Folder** dialogs, just left of the standard caption
buttons. Clicking it opens a menu of your favorite folders. Picking one moves
the dialog to that folder: nothing is opened or saved, and the file name you
typed and the selected file type are kept.

Favorites are edited in this mod's settings. A path may contain environment
variables such as `%USERPROFILE%`. If a favorite has no name, the folder name
is shown.

## Limitations

* Only the standard Windows file dialog is supported. Legacy-style dialogs
  (programs that customize `GetOpenFileName`/`GetSaveFileName` with a hook or
  template) and dialogs drawn by the program itself are not.
* A dialog that is already open when the mod is enabled gets no button; it
  appears the next time a dialog is opened.
* Network paths are resolved when the favorite is picked, which can briefly
  freeze the dialog if the share is slow or unavailable.

## Credits

Inspired by FileBox eXtender by Hyperionics (no code from it is used). The
way the dialog object is found follows the technique of the Quick Explorer
Switcher mod by HaVeN80.

Source and issue tracker:
https://github.com/brncesar/windhawk-mod-file-dialog-favorites
*/
// ==/WindhawkModReadme==

// ==WindhawkModSettings==
/*
- favorites:
    - - name: Home
      - path: "%USERPROFILE%"
    - - name: Downloads
      - path: "%USERPROFILE%\\Downloads"
    - - name: Temp
      - path: "%TEMP%"
  $name: Favorites
  $description: >-
    Folders shown in the favorites menu, in this order. The path may contain
    environment variables such as %USERPROFILE%. If the name is empty, the
    folder name is shown.
*/
// ==/WindhawkModSettings==

#include <windows.h>
#include <commdlg.h>
#include <commctrl.h>
#include <dwmapi.h>
#include <shellapi.h>
#include <shlobj.h>
#include <shobjidl.h>

#include <string>
#include <vector>

// Wh_Log goes through the engine's per-mod object, which is gone once the mod
// is unloaded. Code that can still run afterwards (see "Unloading" below)
// must use LOG instead.
#define LOG(...)                \
    do {                        \
        if (!g_unloaded) {      \
            Wh_Log(__VA_ARGS__); \
        }                       \
    } while (0)

// ---------------------------------------------------------------------------
// Unloading
//
// Windhawk frees the mod DLL right after Wh_ModUninit, even if a thread is
// still inside one of our hooks: IFileDialog::Show is modal, and in busy
// processes (e.g. Chromium-based ones) other threads can be blocked inside
// CoCreateInstance or LoadLibraryExW for a long time. Every hook therefore
// counts itself in g_activeCalls; if any call is in flight at uninit, the DLL
// is pinned (kept loaded until the process exits) and all code that can run
// after uninit checks g_unloaded first.
// ---------------------------------------------------------------------------

static volatile bool g_unloading;  // Set in Wh_ModBeforeUninit.
static volatile bool g_unloaded;   // Set in Wh_ModUninit.
static volatile LONG g_activeCalls;
static volatile LONG g_activeShows;

struct ActiveCall {
    ActiveCall() { InterlockedIncrement(&g_activeCalls); }
    ~ActiveCall() { InterlockedDecrement(&g_activeCalls); }
    ActiveCall(const ActiveCall&) = delete;
    ActiveCall& operator=(const ActiveCall&) = delete;
};

// ---------------------------------------------------------------------------
// Favorites
// ---------------------------------------------------------------------------

struct Favorite {
    std::wstring name;
    std::wstring path;
};

static std::vector<Favorite> g_favorites;
static SRWLOCK g_favoritesLock = SRWLOCK_INIT;

static std::wstring TrimAndUnquote(std::wstring s) {
    const wchar_t* spaces = L" \t\r\n";
    size_t first = s.find_first_not_of(spaces);
    if (first == std::wstring::npos) {
        return L"";
    }
    s = s.substr(first, s.find_last_not_of(spaces) - first + 1);
    if (s.size() >= 2 && s.front() == L'"' && s.back() == L'"') {
        s = s.substr(1, s.size() - 2);
    }
    return s;
}

static void LoadSettings() {
    std::vector<Favorite> favorites;
    for (int i = 0;; i++) {
        PCWSTR name = Wh_GetStringSetting(L"favorites[%d].name", i);
        PCWSTR path = Wh_GetStringSetting(L"favorites[%d].path", i);
        bool hasName = *name;
        Favorite favorite{TrimAndUnquote(name), TrimAndUnquote(path)};
        Wh_FreeStringSetting(name);
        Wh_FreeStringSetting(path);

        // The settings API has no array length, so the list ends at the first
        // entry that has neither a name nor a path. An entry with only a name
        // is kept so that it can be reported, but it is skipped below.
        if (!hasName && favorite.path.empty()) {
            break;
        }
        if (favorite.path.empty()) {
            Wh_Log(L"Favorite %d has no path, skipping", i);
            continue;
        }
        favorites.push_back(std::move(favorite));
    }

    AcquireSRWLockExclusive(&g_favoritesLock);
    g_favorites = std::move(favorites);
    ReleaseSRWLockExclusive(&g_favoritesLock);
}

static std::vector<Favorite> SnapshotFavorites() {
    AcquireSRWLockShared(&g_favoritesLock);
    std::vector<Favorite> favorites = g_favorites;
    ReleaseSRWLockShared(&g_favoritesLock);
    return favorites;
}

static std::wstring ExpandPath(const std::wstring& path) {
    DWORD size = ExpandEnvironmentStringsW(path.c_str(), nullptr, 0);
    if (!size) {
        return path;
    }
    std::wstring expanded(size, L'\0');
    size = ExpandEnvironmentStringsW(path.c_str(), expanded.data(), size);
    if (!size) {
        return path;
    }
    expanded.resize(size - 1);
    return expanded;
}

// The folder name shown for a favorite without a name.
static std::wstring LastPathSegment(std::wstring path) {
    while (path.size() > 3 && (path.back() == L'\\' || path.back() == L'/')) {
        path.pop_back();
    }
    size_t slash = path.find_last_of(L"\\/");
    if (slash == std::wstring::npos || slash == path.size() - 1) {
        return path;
    }
    return path.substr(slash + 1);
}

static std::wstring MenuLabel(const Favorite& favorite) {
    std::wstring label = favorite.name.empty()
                             ? LastPathSegment(ExpandPath(favorite.path))
                             : favorite.name;
    // A single '&' would turn the next character into an access key.
    std::wstring escaped;
    for (wchar_t c : label) {
        escaped += c;
        if (c == L'&') {
            escaped += L'&';
        }
    }
    return escaped;
}

// ---------------------------------------------------------------------------
// Per-dialog state
// ---------------------------------------------------------------------------

class FileDialogEvents;

// Lives on the stack of the Show hook, so it is valid for as long as the
// dialog is shown.
struct DialogState {
    IFileDialog* dialog = nullptr;
    bool pickFolders = false;
    DWORD adviseCookie = 0;

    HWND dialogWindow = nullptr;
    HWND button = nullptr;
    // Set when the button is removed for good (dialog destroyed or mod
    // unloaded), so that it is not attached again.
    bool detached = false;
    bool attachFailureLogged = false;

    bool active = true;
    bool enabled = true;
    bool hot = false;
    bool menuOpen = false;
    ULONGLONG menuClosedTick = 0;

    // Typed File Name and File Type Filter saved before Navigating, restored
    // if the navigation changed them.
    bool restorePending = false;
    ULONGLONG restoreDeadline = 0;
    std::wstring savedFileName;
    UINT savedFileTypeIndex = 0;
};

static std::vector<DialogState*> g_dialogs;
static SRWLOCK g_dialogsLock = SRWLOCK_INIT;

static void RegisterDialog(DialogState* state) {
    AcquireSRWLockExclusive(&g_dialogsLock);
    g_dialogs.push_back(state);
    ReleaseSRWLockExclusive(&g_dialogsLock);
}

static void UnregisterDialog(DialogState* state) {
    AcquireSRWLockExclusive(&g_dialogsLock);
    std::erase(g_dialogs, state);
    ReleaseSRWLockExclusive(&g_dialogsLock);
}

// ---------------------------------------------------------------------------
// Favorites Button: drawing
// ---------------------------------------------------------------------------

constexpr wchar_t kFolderGlyph[] = L"";
constexpr int kButtonWidthAt96Dpi = 46;
constexpr int kGlyphSizeAt96Dpi = 12;

// Win11 subtle fill colors used by caption buttons (alpha out of 255).
constexpr BYTE kHoverBackgroundAlpha = 0x0F;
constexpr BYTE kPressedBackgroundAlpha = 0x0A;

static bool IsDarkCaption(HWND dialogWindow) {
    BOOL dark = FALSE;
    if (SUCCEEDED(DwmGetWindowAttribute(dialogWindow,
                                        DWMWA_USE_IMMERSIVE_DARK_MODE, &dark,
                                        sizeof(dark)))) {
        return dark;
    }
    DWORD lightTheme = 1;
    DWORD size = sizeof(lightTheme);
    RegGetValueW(HKEY_CURRENT_USER,
                 L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\"
                 L"Personalize",
                 L"AppsUseLightTheme", RRF_RT_REG_DWORD, nullptr, &lightTheme,
                 &size);
    return lightTheme == 0;
}

static HFONT CreateGlyphFont(UINT dpi) {
    for (PCWSTR face : {L"Segoe Fluent Icons", L"Segoe MDL2 Assets"}) {
        HFONT font = CreateFontW(-MulDiv(kGlyphSizeAt96Dpi, dpi, 96), 0, 0, 0,
                                 FW_NORMAL, FALSE, FALSE, FALSE,
                                 DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                                 CLIP_DEFAULT_PRECIS, ANTIALIASED_QUALITY,
                                 DEFAULT_PITCH, face);
        if (!font) {
            continue;
        }
        // CreateFont silently substitutes a missing face, which would draw
        // the wrong glyph.
        HDC dc = GetDC(nullptr);
        HGDIOBJ old = SelectObject(dc, font);
        wchar_t actual[LF_FACESIZE] = L"";
        GetTextFaceW(dc, LF_FACESIZE, actual);
        SelectObject(dc, old);
        ReleaseDC(nullptr, dc);
        if (lstrcmpiW(actual, face) == 0) {
            return font;
        }
        DeleteObject(font);
    }
    return nullptr;
}

static HBITMAP CreateTopDownDib(HDC dc, int width, int height, DWORD** bits) {
    BITMAPINFO bmi = {};
    bmi.bmiHeader.biSize = sizeof(bmi.bmiHeader);
    bmi.bmiHeader.biWidth = width;
    bmi.bmiHeader.biHeight = -height;
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;
    return CreateDIBSection(dc, &bmi, DIB_RGB_COLORS, (void**)bits, nullptr,
                            0);
}

// Draws the button into its layered window at the given screen rectangle.
// The background is never fully transparent (alpha 1 at rest) because
// layered windows let clicks through pixels whose alpha is 0.
static void RenderButton(DialogState* state, const RECT& rect) {
    int width = rect.right - rect.left;
    int height = rect.bottom - rect.top;
    if (width <= 0 || height <= 0) {
        return;
    }

    UINT dpi = GetDpiForWindow(state->dialogWindow);
    bool dark = IsDarkCaption(state->dialogWindow);
    BYTE ink = dark ? 255 : 0;

    BYTE backgroundAlpha = 1;
    if (state->enabled && state->menuOpen) {
        backgroundAlpha = kPressedBackgroundAlpha;
    } else if (state->enabled && state->hot) {
        backgroundAlpha = kHoverBackgroundAlpha;
    }

    int glyphOpacity = 255;
    if (!state->enabled) {
        glyphOpacity = 90;
    } else if (!state->active) {
        glyphOpacity = 115;
    } else if (state->menuOpen) {
        glyphOpacity = 200;
    }

    HDC screenDc = GetDC(nullptr);
    HDC dc = CreateCompatibleDC(screenDc);
    DWORD* pixels = nullptr;
    HBITMAP bitmap = CreateTopDownDib(screenDc, width, height, &pixels);
    if (!bitmap) {
        DeleteDC(dc);
        ReleaseDC(nullptr, screenDc);
        return;
    }
    HGDIOBJ oldBitmap = SelectObject(dc, bitmap);

    // Glyph coverage mask: white text on black, antialiased in grayscale.
    ZeroMemory(pixels, width * height * sizeof(DWORD));
    HFONT font = CreateGlyphFont(dpi);
    if (font) {
        HGDIOBJ oldFont = SelectObject(dc, font);
        SetTextColor(dc, RGB(255, 255, 255));
        SetBkMode(dc, TRANSPARENT);
        RECT textRect = {0, 0, width, height};
        DrawTextW(dc, kFolderGlyph, -1, &textRect,
                  DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
        SelectObject(dc, oldFont);
        DeleteObject(font);
    }
    GdiFlush();

    // Composite glyph over background into premultiplied ARGB.
    for (int i = 0; i < width * height; i++) {
        int coverage = pixels[i] & 0xFF;
        int glyphAlpha = coverage * glyphOpacity / 255;
        int backAlpha = backgroundAlpha * (255 - glyphAlpha) / 255;
        int alpha = glyphAlpha + backAlpha;
        int color = ink * alpha / 255;
        pixels[i] = (DWORD)alpha << 24 | (DWORD)color << 16 |
                    (DWORD)color << 8 | (DWORD)color;
    }

    POINT position = {rect.left, rect.top};
    SIZE size = {width, height};
    POINT source = {0, 0};
    BLENDFUNCTION blend = {AC_SRC_OVER, 0, 255, AC_SRC_ALPHA};
    UpdateLayeredWindow(state->button, screenDc, &position, &size, dc, &source,
                        0, &blend, ULW_ALPHA);

    SelectObject(dc, oldBitmap);
    DeleteObject(bitmap);
    DeleteDC(dc);
    ReleaseDC(nullptr, screenDc);
}

// Computes where the button goes: immediately left of the caption buttons,
// as tall as they are.
static bool GetButtonRect(HWND dialogWindow, RECT* rect) {
    RECT windowRect;
    RECT captionButtons;
    if (!GetWindowRect(dialogWindow, &windowRect) ||
        FAILED(DwmGetWindowAttribute(dialogWindow,
                                     DWMWA_CAPTION_BUTTON_BOUNDS,
                                     &captionButtons,
                                     sizeof(captionButtons))) ||
        IsRectEmpty(&captionButtons)) {
        return false;
    }

    UINT dpi = GetDpiForWindow(dialogWindow);
    int width = MulDiv(kButtonWidthAt96Dpi, dpi, 96);
    int captionButtonsWidth = captionButtons.right - captionButtons.left;
    if (width > captionButtonsWidth) {
        width = captionButtonsWidth;
    }

    rect->right = windowRect.left + captionButtons.left;
    rect->left = rect->right - width;
    rect->top = windowRect.top + captionButtons.top;
    rect->bottom = windowRect.top + captionButtons.bottom;
    return true;
}

static void UpdateButton(DialogState* state) {
    if (!state->button) {
        return;
    }

    RECT rect;
    if (!IsWindowVisible(state->dialogWindow) ||
        IsIconic(state->dialogWindow) ||
        !GetButtonRect(state->dialogWindow, &rect)) {
        ShowWindow(state->button, SW_HIDE);
        return;
    }

    RenderButton(state, rect);
    if (!IsWindowVisible(state->button)) {
        ShowWindow(state->button, SW_SHOWNOACTIVATE);
    }
}

// ---------------------------------------------------------------------------
// Favorites Menu and navigation
// ---------------------------------------------------------------------------

constexpr UINT kFirstFavoriteCommand = 1;

// Folder icon for menu items, as a premultiplied 32-bit bitmap (the format
// menus draw with transparency). Cached per icon size.
static HBITMAP g_folderBitmap;
static int g_folderBitmapSize;

static HBITMAP GetFolderBitmap(UINT dpi) {
    int size = GetSystemMetricsForDpi(SM_CXSMICON, dpi);
    if (g_folderBitmap && g_folderBitmapSize == size) {
        return g_folderBitmap;
    }

    SHSTOCKICONINFO info = {sizeof(info)};
    if (FAILED(SHGetStockIconInfo(SIID_FOLDER, SHGSI_ICONLOCATION, &info))) {
        return g_folderBitmap;
    }
    HICON icon = nullptr;
    if (FAILED(SHDefExtractIconW(info.szPath, info.iIcon, 0, &icon, nullptr,
                                 MAKELONG(size, size))) ||
        !icon) {
        return g_folderBitmap;
    }

    HDC screenDc = GetDC(nullptr);
    HDC dc = CreateCompatibleDC(screenDc);
    DWORD* pixels = nullptr;
    HBITMAP bitmap = CreateTopDownDib(screenDc, size, size, &pixels);
    if (bitmap) {
        HGDIOBJ old = SelectObject(dc, bitmap);
        ZeroMemory(pixels, size * size * sizeof(DWORD));
        DrawIconEx(dc, 0, 0, icon, size, size, 0, nullptr, DI_NORMAL);
        GdiFlush();
        SelectObject(dc, old);
        if (g_folderBitmap) {
            DeleteObject(g_folderBitmap);
        }
        g_folderBitmap = bitmap;
        g_folderBitmapSize = size;
    }
    DeleteDC(dc);
    ReleaseDC(nullptr, screenDc);
    DestroyIcon(icon);
    return g_folderBitmap;
}

static void NotifyNavigationFailed(int favoriteIndex, HRESULT hr) {
    // TODO: replace the beep with a non-blocking visual notice.
    LOG(L"Navigating to favorite %d failed: 0x%08X", favoriteIndex,
        (unsigned)hr);
    MessageBeep(MB_ICONWARNING);
}

static void SaveTypedState(DialogState* state) {
    state->savedFileName.clear();
    if (!state->pickFolders) {
        PWSTR fileName = nullptr;
        if (SUCCEEDED(state->dialog->GetFileName(&fileName)) && fileName) {
            state->savedFileName = fileName;
            CoTaskMemFree(fileName);
        }
    }
    state->savedFileTypeIndex = 0;
    state->dialog->GetFileTypeIndex(&state->savedFileTypeIndex);
}

// Puts back the Typed File Name and File Type Filter if navigation changed
// them. Only logs whether something changed, never the file name itself.
static void RestoreTypedState(DialogState* state, PCWSTR when) {
    if (!state->pickFolders) {
        PWSTR fileName = nullptr;
        state->dialog->GetFileName(&fileName);
        bool same = state->savedFileName == (fileName ? fileName : L"");
        CoTaskMemFree(fileName);
        if (!same) {
            LOG(L"Typed File Name changed %s navigation, restoring", when);
            state->dialog->SetFileName(state->savedFileName.c_str());
        }
    }

    if (state->savedFileTypeIndex) {
        UINT typeIndex = 0;
        if (SUCCEEDED(state->dialog->GetFileTypeIndex(&typeIndex)) &&
            typeIndex != state->savedFileTypeIndex) {
            LOG(L"File Type Filter changed %s navigation (%u -> %u), "
                L"restoring",
                when, state->savedFileTypeIndex, typeIndex);
            state->dialog->SetFileTypeIndex(state->savedFileTypeIndex);
        }
    }
}

static void NavigateToFavorite(DialogState* state,
                               const Favorite& favorite,
                               int favoriteIndex) {
    std::wstring path = ExpandPath(favorite.path);

    IShellItem* folder = nullptr;
    HRESULT hr = SHCreateItemFromParsingName(path.c_str(), nullptr,
                                             IID_PPV_ARGS(&folder));
    if (SUCCEEDED(hr)) {
        SFGAOF attributes = 0;
        if (FAILED(folder->GetAttributes(SFGAO_FOLDER, &attributes)) ||
            !(attributes & SFGAO_FOLDER)) {
            hr = HRESULT_FROM_WIN32(ERROR_DIRECTORY);
        }
    }
    if (FAILED(hr)) {
        if (folder) {
            folder->Release();
        }
        NotifyNavigationFailed(favoriteIndex, hr);
        return;
    }

    SaveTypedState(state);

    // The dialog may finish navigating asynchronously and reset the typed
    // state afterwards, so restoring is attempted again on the next
    // OnFolderChange, for a short while only (a later folder change is the
    // user's own and must not be touched).
    state->restorePending = true;
    state->restoreDeadline = GetTickCount64() + 2000;

    hr = state->dialog->SetFolder(folder);
    folder->Release();
    if (FAILED(hr)) {
        state->restorePending = false;
        NotifyNavigationFailed(favoriteIndex, hr);
        return;
    }

    LOG(L"Navigated to favorite %d", favoriteIndex);
    RestoreTypedState(state, L"during");
}

static void ShowFavoritesMenu(DialogState* state) {
    std::vector<Favorite> favorites = SnapshotFavorites();
    UINT dpi = GetDpiForWindow(state->dialogWindow);

    HMENU menu = CreatePopupMenu();
    if (!menu) {
        return;
    }
    if (favorites.empty()) {
        AppendMenuW(menu, MF_STRING | MF_GRAYED, 0,
                    L"No favorites — add them in the mod's Windhawk "
                    L"settings");
    } else {
        HBITMAP folderBitmap = GetFolderBitmap(dpi);
        for (size_t i = 0; i < favorites.size(); i++) {
            std::wstring label = MenuLabel(favorites[i]);
            MENUITEMINFOW item = {sizeof(item)};
            item.fMask = MIIM_ID | MIIM_STRING | MIIM_BITMAP;
            item.wID = kFirstFavoriteCommand + (UINT)i;
            item.dwTypeData = label.data();
            item.hbmpItem = folderBitmap;
            InsertMenuItemW(menu, (UINT)i, TRUE, &item);
        }
    }

    RECT buttonRect;
    GetWindowRect(state->button, &buttonRect);

    // The button never takes activation, so make sure the dialog is the
    // foreground window; otherwise the menu would not close properly.
    if (GetForegroundWindow() != state->dialogWindow) {
        SetForegroundWindow(state->dialogWindow);
    }

    state->menuOpen = true;
    UpdateButton(state);

    TPMPARAMS params = {sizeof(params), buttonRect};
    UINT command = TrackPopupMenuEx(
        menu,
        TPM_RETURNCMD | TPM_NONOTIFY | TPM_LEFTALIGN | TPM_TOPALIGN |
            TPM_VERTICAL | TPM_LEFTBUTTON,
        buttonRect.left, buttonRect.bottom, state->dialogWindow, &params);
    DestroyMenu(menu);

    // The button may have been removed while the menu was open.
    if (g_unloaded || state->detached) {
        return;
    }

    state->menuOpen = false;
    state->menuClosedTick = GetTickCount64();
    POINT cursor;
    GetCursorPos(&cursor);
    state->hot = PtInRect(&buttonRect, cursor);
    UpdateButton(state);

    if (command >= kFirstFavoriteCommand &&
        command < kFirstFavoriteCommand + favorites.size()) {
        int index = (int)(command - kFirstFavoriteCommand);
        NavigateToFavorite(state, favorites[index], index);
    }
}

// ---------------------------------------------------------------------------
// Favorites Button: window
// ---------------------------------------------------------------------------

static wchar_t g_buttonClassName[64];

static HMODULE GetModuleInstance() {
    HMODULE module = nullptr;
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                           GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                       (LPCWSTR)&GetModuleInstance, &module);
    return module;
}

static LRESULT CALLBACK ButtonWndProc(HWND hwnd,
                                      UINT msg,
                                      WPARAM wParam,
                                      LPARAM lParam) {
    if (msg == WM_NCCREATE) {
        auto* create = (CREATESTRUCTW*)lParam;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA,
                          (LONG_PTR)create->lpCreateParams);
    }

    auto* state = (DialogState*)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
    if (!state || g_unloaded) {
        return DefWindowProcW(hwnd, msg, wParam, lParam);
    }

    switch (msg) {
        case WM_MOUSEACTIVATE:
            return MA_NOACTIVATE;

        case WM_MOUSEMOVE:
            if (!state->hot) {
                state->hot = true;
                TRACKMOUSEEVENT track = {sizeof(track), TME_LEAVE, hwnd, 0};
                TrackMouseEvent(&track);
                UpdateButton(state);
            }
            return 0;

        case WM_MOUSELEAVE:
            state->hot = false;
            UpdateButton(state);
            return 0;

        case WM_LBUTTONDOWN:
            // Ignore the click that just closed the menu by clicking the
            // button again.
            if (GetTickCount64() - state->menuClosedTick < 150) {
                return 0;
            }
            if (state->enabled && IsWindowEnabled(state->dialogWindow)) {
                ShowFavoritesMenu(state);
            }
            return 0;
    }

    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

static bool EnsureButtonClass() {
    if (g_buttonClassName[0]) {
        return true;
    }

    HMODULE instance = GetModuleInstance();
    wchar_t className[64];
    // The module address makes the name unique per loaded copy of the mod.
    wsprintfW(className, L"FileDialogFavoritesButton_%p", instance);

    WNDCLASSEXW wc = {sizeof(wc)};
    wc.lpfnWndProc = ButtonWndProc;
    wc.hInstance = instance;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.lpszClassName = className;
    if (!RegisterClassExW(&wc) &&
        GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
        LOG(L"RegisterClassEx failed: %u", GetLastError());
        return false;
    }
    lstrcpyW(g_buttonClassName, className);
    return true;
}

// ---------------------------------------------------------------------------
// Attaching to the dialog window
// ---------------------------------------------------------------------------

constexpr wchar_t kButtonProp[] = L"FileDialogFavorites.HasButton";
static UINT g_detachMessage;

static void DetachFromDialog(DialogState* state);

static LRESULT CALLBACK DialogSubclassProc(HWND hwnd,
                                           UINT msg,
                                           WPARAM wParam,
                                           LPARAM lParam,
                                           UINT_PTR,
                                           DWORD_PTR refData) {
    auto* state = (DialogState*)refData;

    if (msg == g_detachMessage && g_detachMessage) {
        // Sent by Wh_ModUninit.
        DetachFromDialog(state);
        if (state->adviseCookie) {
            state->dialog->Unadvise(state->adviseCookie);
            state->adviseCookie = 0;
        }
        return 0;
    }

    switch (msg) {
        case WM_NCDESTROY: {
            LRESULT result = DefSubclassProc(hwnd, msg, wParam, lParam);
            DetachFromDialog(state);
            return result;
        }

        case WM_NCACTIVATE:
            state->active = wParam != FALSE;
            UpdateButton(state);
            break;

        case WM_ENABLE:
            state->enabled = wParam != FALSE;
            UpdateButton(state);
            break;

        case WM_WINDOWPOSCHANGED:
        case WM_SHOWWINDOW:
        case WM_DPICHANGED:
        case WM_THEMECHANGED:
        case WM_SETTINGCHANGE:
        case WM_DWMCOLORIZATIONCOLORCHANGED: {
            LRESULT result = DefSubclassProc(hwnd, msg, wParam, lParam);
            UpdateButton(state);
            return result;
        }
    }

    return DefSubclassProc(hwnd, msg, wParam, lParam);
}

static void AttachToDialog(DialogState* state) {
    if (state->dialogWindow || state->detached || g_unloading) {
        return;
    }

    HWND dialogWindow = nullptr;
    IOleWindow* oleWindow = nullptr;
    HRESULT hr = state->dialog->QueryInterface(IID_PPV_ARGS(&oleWindow));
    if (SUCCEEDED(hr)) {
        hr = oleWindow->GetWindow(&dialogWindow);
        oleWindow->Release();
    }
    if (!dialogWindow) {
        // Expected for events that arrive before the window exists; logged
        // once per dialog to diagnose dialogs that never get a button.
        if (!state->attachFailureLogged) {
            state->attachFailureLogged = true;
            LOG(L"Dialog window not available yet (0x%08X)", (unsigned)hr);
        }
        return;
    }

    // Guards against a second button on the same window, e.g. if the same
    // dialog object is reached through two hooked Show functions.
    if (GetPropW(dialogWindow, kButtonProp)) {
        LOG(L"Dialog %p already has a button", dialogWindow);
        state->detached = true;
        return;
    }

    if (!g_detachMessage) {
        g_detachMessage = RegisterWindowMessageW(L"FileDialogFavorites_Detach");
    }
    if (!EnsureButtonClass()) {
        state->detached = true;
        return;
    }

    HWND button = CreateWindowExW(
        WS_EX_LAYERED | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, g_buttonClassName,
        L"Favorites", WS_POPUP, 0, 0, 0, 0, dialogWindow, nullptr,
        GetModuleInstance(), state);
    if (!button) {
        LOG(L"CreateWindowEx failed: %u", GetLastError());
        state->detached = true;
        return;
    }

    if (!SetWindowSubclass(dialogWindow, DialogSubclassProc, 0,
                           (DWORD_PTR)state)) {
        LOG(L"SetWindowSubclass failed");
        DestroyWindow(button);
        state->detached = true;
        return;
    }

    SetPropW(dialogWindow, kButtonProp, (HANDLE)1);
    state->dialogWindow = dialogWindow;
    state->button = button;
    state->active = GetForegroundWindow() == dialogWindow;
    state->enabled = IsWindowEnabled(dialogWindow);
    LOG(L"Attached to dialog %p (pickFolders=%d)", dialogWindow,
        state->pickFolders);

    UpdateButton(state);
}

static void DetachFromDialog(DialogState* state) {
    if (!state->dialogWindow) {
        state->detached = true;
        return;
    }

    if (state->menuOpen) {
        EndMenu();
    }
    RemoveWindowSubclass(state->dialogWindow, DialogSubclassProc, 0);
    RemovePropW(state->dialogWindow, kButtonProp);
    if (state->button) {
        SetWindowLongPtrW(state->button, GWLP_USERDATA, 0);
        DestroyWindow(state->button);
    }
    state->button = nullptr;
    state->dialogWindow = nullptr;
    state->detached = true;
}

// ---------------------------------------------------------------------------
// Dialog events
// ---------------------------------------------------------------------------

static void OnDialogEvent(DialogState* state, bool folderChanged) {
    AttachToDialog(state);

    if (folderChanged && state->restorePending) {
        state->restorePending = false;
        if (GetTickCount64() <= state->restoreDeadline) {
            RestoreTypedState(state, L"after");
        }
    }
}

class FileDialogEvents : public IFileDialogEvents {
   public:
    explicit FileDialogEvents(DialogState* state) : m_state(state) {}

    IFACEMETHODIMP QueryInterface(REFIID riid, void** ppv) override {
        if (riid == IID_IUnknown || riid == IID_IFileDialogEvents) {
            *ppv = static_cast<IFileDialogEvents*>(this);
            AddRef();
            return S_OK;
        }
        *ppv = nullptr;
        return E_NOINTERFACE;
    }

    IFACEMETHODIMP_(ULONG) AddRef() override {
        return InterlockedIncrement(&m_refCount);
    }

    IFACEMETHODIMP_(ULONG) Release() override {
        LONG count = InterlockedDecrement(&m_refCount);
        if (count == 0) {
            delete this;
        }
        return count;
    }

    IFACEMETHODIMP OnFileOk(IFileDialog*) override { return S_OK; }

    IFACEMETHODIMP OnFolderChanging(IFileDialog*, IShellItem*) override {
        return S_OK;
    }

    IFACEMETHODIMP OnFolderChange(IFileDialog*) override {
        if (!g_unloaded) {
            OnDialogEvent(m_state, true);
        }
        return S_OK;
    }

    IFACEMETHODIMP OnSelectionChange(IFileDialog*) override {
        if (!g_unloaded) {
            OnDialogEvent(m_state, false);
        }
        return S_OK;
    }

    IFACEMETHODIMP OnShareViolation(IFileDialog*,
                                    IShellItem*,
                                    FDE_SHAREVIOLATION_RESPONSE*) override {
        return E_NOTIMPL;
    }

    IFACEMETHODIMP OnTypeChange(IFileDialog*) override {
        if (!g_unloaded) {
            OnDialogEvent(m_state, false);
        }
        return S_OK;
    }

    IFACEMETHODIMP OnOverwrite(IFileDialog*,
                               IShellItem*,
                               FDE_OVERWRITE_RESPONSE*) override {
        return E_NOTIMPL;
    }

   private:
    virtual ~FileDialogEvents() = default;

    LONG m_refCount = 1;
    DialogState* m_state;
};

// ---------------------------------------------------------------------------
// IFileDialog::Show hook (vtable slot 3, right after IUnknown)
// ---------------------------------------------------------------------------

using Show_t = HRESULT(WINAPI*)(IFileDialog*, HWND);

static HRESULT ShowWithFavorites(IFileDialog* self,
                                 HWND owner,
                                 Show_t original) {
    InterlockedIncrement(&g_activeShows);

    DialogState state;
    state.dialog = self;
    FILEOPENDIALOGOPTIONS options = 0;
    if (SUCCEEDED(self->GetOptions(&options))) {
        state.pickFolders = (options & FOS_PICKFOLDERS) != 0;
    }

    auto* events = new FileDialogEvents(&state);
    HRESULT adviseResult = self->Advise(events, &state.adviseCookie);
    if (FAILED(adviseResult)) {
        state.adviseCookie = 0;
    }
    events->Release();
    LOG(L"Show on thread %u (pickFolders=%d, Advise 0x%08X)",
        GetCurrentThreadId(), state.pickFolders, (unsigned)adviseResult);
    RegisterDialog(&state);

    HRESULT result = original(self, owner);

    if (!g_unloaded) {
        UnregisterDialog(&state);
        DetachFromDialog(&state);
        if (state.adviseCookie) {
            self->Unadvise(state.adviseCookie);
        }
    }

    InterlockedDecrement(&g_activeShows);
    return result;
}

template <int N>
struct ShowSlot {
    static Show_t original;
    static HRESULT WINAPI Hook(IFileDialog* self, HWND owner) {
        ActiveCall activeCall;
        if (g_unloading) {
            return original(self, owner);
        }
        return ShowWithFavorites(self, owner, original);
    }
};
template <int N>
Show_t ShowSlot<N>::original;

constexpr int kMaxShowSlots = 4;
static void* const g_showHooks[kMaxShowSlots] = {
    (void*)ShowSlot<0>::Hook, (void*)ShowSlot<1>::Hook,
    (void*)ShowSlot<2>::Hook, (void*)ShowSlot<3>::Hook};
static Show_t* const g_showOriginals[kMaxShowSlots] = {
    &ShowSlot<0>::original, &ShowSlot<1>::original, &ShowSlot<2>::original,
    &ShowSlot<3>::original};
static void* g_showTargets[kMaxShowSlots];
static int g_showSlotCount;
static SRWLOCK g_showLock = SRWLOCK_INIT;

// Hooks the Show implementation of a file dialog object. Returns true if it
// is hooked (now or before).
static bool HookShowOfObject(IUnknown* object) {
    IFileDialog* dialog = nullptr;
    if (!object ||
        FAILED(object->QueryInterface(IID_PPV_ARGS(&dialog))) || !dialog) {
        return false;
    }
    void* show = (*reinterpret_cast<void***>(dialog))[3];
    dialog->Release();

    // Only hook the real implementation in comdlg32. An object created from
    // an MTA thread is a proxy whose slot 3 is a generic COM thunk shared by
    // unrelated interfaces; hooking that would break the process.
    HMODULE comdlg32 = GetModuleHandleW(L"comdlg32.dll");
    HMODULE owner = nullptr;
    if (!comdlg32 ||
        !GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                                GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                            (LPCWSTR)show, &owner) ||
        owner != comdlg32) {
        Wh_Log(L"IFileDialog::Show at %p is not in comdlg32.dll, skipping",
               show);
        return false;
    }

    bool hooked = false;
    AcquireSRWLockExclusive(&g_showLock);
    for (int i = 0; i < g_showSlotCount; i++) {
        if (g_showTargets[i] == show) {
            hooked = true;
            break;
        }
    }
    if (!hooked && !g_unloading && g_showSlotCount < kMaxShowSlots) {
        int slot = g_showSlotCount;
        if (Wh_SetFunctionHook(show, g_showHooks[slot],
                               (void**)g_showOriginals[slot])) {
            g_showTargets[slot] = show;
            g_showSlotCount++;
            Wh_ApplyHookOperations();
            Wh_Log(L"Hooked IFileDialog::Show at %p (slot %d)", show, slot);
            hooked = true;
        }
    }
    ReleaseSRWLockExclusive(&g_showLock);
    return hooked;
}

static bool IsFileDialogClsid(REFCLSID clsid) {
    return IsEqualCLSID(clsid, CLSID_FileOpenDialog) ||
           IsEqualCLSID(clsid, CLSID_FileSaveDialog);
}

// ---------------------------------------------------------------------------
// CoCreateInstance(Ex) hooks: programs that use IFileDialog directly
// ---------------------------------------------------------------------------

using CoCreateInstance_t =
    HRESULT(WINAPI*)(REFCLSID, LPUNKNOWN, DWORD, REFIID, LPVOID*);
static CoCreateInstance_t CoCreateInstance_Original;

static HRESULT WINAPI CoCreateInstance_Hook(REFCLSID clsid,
                                            LPUNKNOWN outer,
                                            DWORD context,
                                            REFIID riid,
                                            LPVOID* ppv) {
    ActiveCall activeCall;
    HRESULT hr = CoCreateInstance_Original(clsid, outer, context, riid, ppv);
    if (SUCCEEDED(hr) && ppv && *ppv && !g_unloading &&
        IsFileDialogClsid(clsid)) {
        HookShowOfObject((IUnknown*)*ppv);
    }
    return hr;
}

using CoCreateInstanceEx_t = HRESULT(
    WINAPI*)(REFCLSID, IUnknown*, DWORD, COSERVERINFO*, DWORD, MULTI_QI*);
static CoCreateInstanceEx_t CoCreateInstanceEx_Original;

static HRESULT WINAPI CoCreateInstanceEx_Hook(REFCLSID clsid,
                                              IUnknown* outer,
                                              DWORD context,
                                              COSERVERINFO* serverInfo,
                                              DWORD count,
                                              MULTI_QI* results) {
    ActiveCall activeCall;
    HRESULT hr = CoCreateInstanceEx_Original(clsid, outer, context, serverInfo,
                                             count, results);
    if (SUCCEEDED(hr) && results && !g_unloading && IsFileDialogClsid(clsid)) {
        for (DWORD i = 0; i < count; i++) {
            if (SUCCEEDED(results[i].hr) && results[i].pItf) {
                HookShowOfObject(results[i].pItf);
                break;
            }
        }
    }
    return hr;
}

// ---------------------------------------------------------------------------
// GetOpenFileName/GetSaveFileName hooks: the legacy API shows the same
// dialog object internally, but never through CoCreateInstance, so a
// throwaway instance is created to find its Show implementation.
// ---------------------------------------------------------------------------

static volatile LONG g_showHookedForLegacyApi;

static void EnsureShowHookedForLegacyApi() {
    if (g_showHookedForLegacyApi || g_unloading) {
        return;
    }

    HRESULT init = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    if (init == RPC_E_CHANGED_MODE) {
        // MTA thread: only a proxy can be created here. Try again on the
        // next call from an STA thread.
        return;
    }

    bool hooked = false;
    for (const CLSID* clsid : {&CLSID_FileOpenDialog, &CLSID_FileSaveDialog}) {
        IUnknown* object = nullptr;
        if (SUCCEEDED(CoCreateInstance(*clsid, nullptr, CLSCTX_INPROC_SERVER,
                                       IID_PPV_ARGS(&object))) &&
            object) {
            hooked = HookShowOfObject(object) || hooked;
            object->Release();
        }
    }

    if (SUCCEEDED(init)) {
        CoUninitialize();
    }
    if (hooked) {
        InterlockedExchange(&g_showHookedForLegacyApi, 1);
    }
}

using GetFileNameW_t = BOOL(WINAPI*)(LPOPENFILENAMEW);
using GetFileNameA_t = BOOL(WINAPI*)(LPOPENFILENAMEA);
static GetFileNameW_t GetOpenFileNameW_Original;
static GetFileNameW_t GetSaveFileNameW_Original;
static GetFileNameA_t GetOpenFileNameA_Original;
static GetFileNameA_t GetSaveFileNameA_Original;

static BOOL WINAPI GetOpenFileNameW_Hook(LPOPENFILENAMEW ofn) {
    ActiveCall activeCall;
    EnsureShowHookedForLegacyApi();
    return GetOpenFileNameW_Original(ofn);
}

static BOOL WINAPI GetSaveFileNameW_Hook(LPOPENFILENAMEW ofn) {
    ActiveCall activeCall;
    EnsureShowHookedForLegacyApi();
    return GetSaveFileNameW_Original(ofn);
}

static BOOL WINAPI GetOpenFileNameA_Hook(LPOPENFILENAMEA ofn) {
    ActiveCall activeCall;
    EnsureShowHookedForLegacyApi();
    return GetOpenFileNameA_Original(ofn);
}

static BOOL WINAPI GetSaveFileNameA_Hook(LPOPENFILENAMEA ofn) {
    ActiveCall activeCall;
    EnsureShowHookedForLegacyApi();
    return GetSaveFileNameA_Original(ofn);
}

// ---------------------------------------------------------------------------
// Hooking combase.dll and comdlg32.dll, also when loaded after startup
// ---------------------------------------------------------------------------

static bool g_combaseHooked;
static bool g_comdlg32Hooked;
// The LoadLibraryExW hook only waits for the two modules above, and it sits
// on a hot path, so it is removed as soon as both are hooked.
static void* g_loadLibraryExWTarget;
static SRWLOCK g_moduleLock = SRWLOCK_INIT;

// applyNow is false only during Wh_ModInit, where Windhawk applies the hook
// operations itself afterwards.
static void HookLoadedModules(bool applyNow) {
    if (g_combaseHooked && g_comdlg32Hooked && !g_loadLibraryExWTarget) {
        return;
    }

    AcquireSRWLockExclusive(&g_moduleLock);
    bool changed = false;

    if (!g_combaseHooked && !g_unloading) {
        if (HMODULE combase = GetModuleHandleW(L"combase.dll")) {
            if (void* p = (void*)GetProcAddress(combase, "CoCreateInstance")) {
                Wh_SetFunctionHook(p, (void*)CoCreateInstance_Hook,
                                   (void**)&CoCreateInstance_Original);
            }
            if (void* p =
                    (void*)GetProcAddress(combase, "CoCreateInstanceEx")) {
                Wh_SetFunctionHook(p, (void*)CoCreateInstanceEx_Hook,
                                   (void**)&CoCreateInstanceEx_Original);
            }
            g_combaseHooked = true;
            changed = true;
        }
    }

    if (!g_comdlg32Hooked && !g_unloading) {
        if (HMODULE comdlg32 = GetModuleHandleW(L"comdlg32.dll")) {
            struct {
                const char* name;
                void* hook;
                void** original;
            } hooks[] = {
                {"GetOpenFileNameW", (void*)GetOpenFileNameW_Hook,
                 (void**)&GetOpenFileNameW_Original},
                {"GetSaveFileNameW", (void*)GetSaveFileNameW_Hook,
                 (void**)&GetSaveFileNameW_Original},
                {"GetOpenFileNameA", (void*)GetOpenFileNameA_Hook,
                 (void**)&GetOpenFileNameA_Original},
                {"GetSaveFileNameA", (void*)GetSaveFileNameA_Hook,
                 (void**)&GetSaveFileNameA_Original},
            };
            for (auto& hook : hooks) {
                if (void* p = (void*)GetProcAddress(comdlg32, hook.name)) {
                    Wh_SetFunctionHook(p, hook.hook, hook.original);
                }
            }
            g_comdlg32Hooked = true;
            changed = true;
        }
    }

    if (applyNow && g_combaseHooked && g_comdlg32Hooked &&
        g_loadLibraryExWTarget && !g_unloading) {
        Wh_RemoveFunctionHook(g_loadLibraryExWTarget);
        g_loadLibraryExWTarget = nullptr;
        changed = true;
        Wh_Log(L"All modules hooked, removing the LoadLibraryExW hook");
    }

    if (changed && applyNow) {
        Wh_ApplyHookOperations();
    }
    ReleaseSRWLockExclusive(&g_moduleLock);
}

using LoadLibraryExW_t = HMODULE(WINAPI*)(LPCWSTR, HANDLE, DWORD);
static LoadLibraryExW_t LoadLibraryExW_Original;

static HMODULE WINAPI LoadLibraryExW_Hook(LPCWSTR fileName,
                                          HANDLE file,
                                          DWORD flags) {
    ActiveCall activeCall;
    HMODULE module = LoadLibraryExW_Original(fileName, file, flags);
    if (module && !g_unloading &&
        !(flags & (LOAD_LIBRARY_AS_DATAFILE |
                   LOAD_LIBRARY_AS_DATAFILE_EXCLUSIVE |
                   LOAD_LIBRARY_AS_IMAGE_RESOURCE))) {
        HookLoadedModules(true);
    }
    return module;
}

// ---------------------------------------------------------------------------
// Mod lifecycle
// ---------------------------------------------------------------------------

BOOL Wh_ModInit() {
    Wh_Log(L">");

    // If a previous copy was pinned (see "Unloading"), Windhawk may load the
    // same module again, with globals left over from the previous run.
    g_unloading = false;
    g_unloaded = false;
    g_showSlotCount = 0;
    g_showHookedForLegacyApi = 0;
    g_combaseHooked = false;
    g_comdlg32Hooked = false;
    g_loadLibraryExWTarget = nullptr;
    g_dialogs.clear();

    LoadSettings();
    HookLoadedModules(false);

    if (!g_combaseHooked || !g_comdlg32Hooked) {
        HMODULE kernelBase = GetModuleHandleW(L"kernelbase.dll");
        void* target = kernelBase
                           ? (void*)GetProcAddress(kernelBase, "LoadLibraryExW")
                           : nullptr;
        if (target && Wh_SetFunctionHook(target, (void*)LoadLibraryExW_Hook,
                                         (void**)&LoadLibraryExW_Original)) {
            g_loadLibraryExWTarget = target;
        }
    }

    return TRUE;
}

void Wh_ModBeforeUninit() {
    Wh_Log(L">");
    g_unloading = true;
}

void Wh_ModUninit() {
    Wh_Log(L">");

    // Remove the buttons and event sinks from open dialogs, each on its own
    // thread. The list is copied so that the lock is not held while a dialog
    // thread may be waiting for it.
    std::vector<HWND> windows;
    AcquireSRWLockShared(&g_dialogsLock);
    for (DialogState* state : g_dialogs) {
        if (state->dialogWindow) {
            windows.push_back(state->dialogWindow);
        }
    }
    ReleaseSRWLockShared(&g_dialogsLock);

    for (HWND window : windows) {
        DWORD_PTR result;
        if (!SendMessageTimeoutW(window, g_detachMessage, 0, 0,
                                 SMTO_ABORTIFHUNG, 2000, &result)) {
            Wh_Log(L"Could not detach from dialog %p", window);
        }
    }

    if (g_activeCalls > 0) {
        HMODULE module;
        GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                               GET_MODULE_HANDLE_EX_FLAG_PIN,
                           (LPCWSTR)&Wh_ModUninit, &module);
        Wh_Log(L"%d call(s) in flight (%d dialog(s) open), keeping the mod "
               L"loaded",
               (int)g_activeCalls, (int)g_activeShows);
    } else {
        if (g_buttonClassName[0]) {
            UnregisterClassW(g_buttonClassName, GetModuleInstance());
            g_buttonClassName[0] = L'\0';
        }
        if (g_folderBitmap) {
            DeleteObject(g_folderBitmap);
            g_folderBitmap = nullptr;
        }
    }

    g_unloaded = true;
}

void Wh_ModSettingsChanged() {
    Wh_Log(L">");
    LoadSettings();
}
