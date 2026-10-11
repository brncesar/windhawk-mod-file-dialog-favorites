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

- [ ] The Favorites Button appears once, immediately left of the caption buttons, matching their height.
- [ ] The button follows move, resize, maximize/restore, minimize/restore, DPI change and theme change.
- [ ] The button looks dimmed when the dialog is inactive and disabled while a child dialog (e.g. overwrite confirmation) is open.
- [ ] Clicking the button opens the Favorites Menu below it; Esc and clicking elsewhere close it.
- [ ] Picking a Favorite Navigates: the Current Folder changes, the dialog does not Commit, and the Typed File Name and File Type Filter are unchanged.
- [ ] A Favorite that does not exist leaves the dialog where it was (beep for now).
- [ ] Dragging the dialog by its title bar works everywhere except on the button.
- [ ] Disabling the mod with a dialog open removes the button without crashing; the dialog keeps working.
- [ ] Opening and cancelling many dialogs in a row leaves no duplicate or orphan buttons.

## Results

| Date | Windows build | Windhawk | Program | Arch | Dialog / API | Result |
| --- | --- | --- | --- | --- | --- | --- |
