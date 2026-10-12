# Testing

## Build

All builds use the compiler bundled with Windhawk; no other toolchain is needed.

```powershell
pwsh tools\build.ps1                  # test harness (x86, x64) + mod compile check
pwsh tools\build.ps1 -Target Mod      # mod compile check only
```

The mod compile check uses the Windhawk editor's flags plus `-Wall -Wextra` and writes throwaway DLLs to `build\`. It does not install anything.

## Install the mod locally

1. In Windhawk, choose **Create a New Mod**.
2. Replace the editor contents with `src\file-dialog-favorites.wh.cpp`.
3. Click **Compile Mod**, then **Exit Editing Mode**.
4. To see diagnostics, enable logging in the mod's **Advanced** tab; the log never contains file names.

To update, open the local mod in the editor, paste the new source and compile again.

## Test harness

`build\test-dialogs-x64.exe` and `build\test-dialogs-x86.exe` open each File Dialog variant through `IFileDialog` and through the legacy `GetOpenFileName`/`GetSaveFileName` API. The Save Dialogs start with the Typed File Name `typed-name-must-survive.csv` and the second File Type Filter (CSV) selected, and the log shows what the dialog returned.

`tools\capture-dialog.ps1` opens a dialog from the harness and saves a screenshot of its title bar area to `build\`. Screenshots in `build\` can show personal folder names; don't publish them as they are.

## Proof-of-concept checklist (PLAN.md, step 3)

For each dialog (Open, Save, Folder Picker) and each architecture:

- [x] The Favorites Button appears once, immediately left of the caption buttons, matching their height.
- [ ] The button follows move, resize, maximize/restore, minimize/restore, DPI change and theme change. *(All but theme change verified.)*
- [ ] The button looks dimmed when the dialog is inactive and disabled while a child dialog (e.g. overwrite confirmation) is open. *(Disabled during overwrite confirmation verified manually; inactive look not yet checked.)*
- [x] Clicking the button opens the Favorites Menu below it; Esc closes it. *(Closing by clicking elsewhere not yet checked.)*
- [x] Picking a Favorite Navigates: the Current Folder changes, the dialog does not Commit, and the Typed File Name and File Type Filter are unchanged.
- [ ] A Favorite that does not exist leaves the dialog where it was (beep for now).
- [x] Dragging the dialog by its title bar works everywhere except on the button.
- [ ] Disabling the mod with a dialog open removes the button without crashing; the dialog keeps working. *(Passes in Notepad; VS Code crashed with 0.1.0, see below.)*
- [ ] Opening and cancelling many dialogs in a row leaves no duplicate or orphan buttons. *(Two dialogs open at once each had exactly one button.)*

## Results

Automated runs drove the harness with synthetic mouse and keyboard input and checked screenshots and window geometry.

| Date | Windows build | Windhawk | Program | Arch | Dialog / API | Result |
| --- | --- | --- | --- | --- | --- | --- |
| 2026-10-11 | 26300.9550 | 1.7.3 | test-dialogs | x64 | Save / IFileDialog | Pass: Navigated to Downloads; Typed File Name and CSV filter kept; not Committed |
| 2026-10-11 | 26300.9550 | 1.7.3 | test-dialogs | x64 | Open / IFileDialog | Pass: Navigated to Temp; CSV filter kept |
| 2026-10-11 | 26300.9550 | 1.7.3 | test-dialogs | x64 | Folder Picker / IFileDialog | Pass: Navigated to Home |
| 2026-10-11 | 26300.9550 | 1.7.3 | test-dialogs | x64 | Open / GetOpenFileName | Pass: button present; Navigated to Temp; CSV filter kept |
| 2026-10-11 | 26300.9550 | 1.7.3 | test-dialogs | x64 | Save / GetSaveFileName | Pass: Navigated to Downloads; Typed File Name and CSV filter kept |
| 2026-10-11 | 26300.9550 | 1.7.3 | test-dialogs | x86 | Save / IFileDialog | Pass: same as x64 |
| 2026-10-11 | 26300.9550 | 1.7.3 | test-dialogs | x86 | Save / GetSaveFileName | Pass: same as x64; harness logged "Cancelled" after Esc |
| 2026-10-11 | 26300.9550 | 1.7.3 | test-dialogs | x64 | Save / IFileDialog | Pass: button stays aligned after move, resize, maximize, restore and moving between monitors at 100% and ~175% scale; hidden while minimized |

| 2026-10-12 | 26300.9550 | 1.7.3 | Notepad | x64 | Save | Pass (manual): button present; Navigate works; disabling the mod with the dialog open removes the button, no crash |
| 2026-10-12 | 26300.9550 | 1.7.3 | Paint | x64 | Save | Pass (manual): button present |
| 2026-10-12 | 26300.9550 | 1.7.3 | Notepad | x64 | Save + overwrite prompt | Pass (manual): button disabled while the prompt is open |
| 2026-10-12 | 26300.9550 | 1.7.3 | VS Code (custom inclusion) | x64 | Save As | **Fail (0.1.0)**: button only on the first dialog; disabling the mod crashed VS Code |

Known issues:

- The Favorites Menu uses the light theme even when the dialog is dark.
- Re-enabling the mod while a dialog is open does not add a button to that dialog (by design: the mod only reaches dialogs shown after it loads).
- VS Code, second dialog without button (0.1.0): not reproduced in the harness, including with each dialog on a new thread as Electron does. 0.1.1 logs every Show call and silent attach failures to find the cause.
- VS Code crash on disable (0.1.0): the crash dumps show threads still inside the `LoadLibraryExW` and `CoCreateInstance` hooks after Windhawk freed the mod DLL. Fixed in 0.1.1 by counting in-flight calls in every hook (pinning the DLL if any) and removing the `LoadLibraryExW` hook once it is no longer needed.
