# File Dialog Favorites

A Windhawk mod that brings FileBox eXtender-style favorites to Windows 11 file dialogs: a button in the dialog's title bar opens a menu of favorite folders, and picking one moves the dialog there without completing the open or save.

## Language

### Dialogs

**File Dialog**:
The standard Windows 11 window an application shows to choose a file or folder, in any of its three variants: Open Dialog, Save Dialog or Folder Picker.
_Avoid_: File window, file box, common dialog, picker

**Open Dialog**:
A File Dialog for choosing one or more existing files to open.

**Save Dialog**:
A File Dialog for choosing the folder and name under which to save a file.

**Folder Picker**:
A File Dialog for choosing a folder rather than a file. It has no Typed File Name or File Type Filter.
_Avoid_: Browse for folder dialog

**Legacy File Dialog**:
The pre-Vista style file dialog that some applications still show when they customize it with hooks or templates. Out of scope until a separate adapter exists.
_Avoid_: Old dialog, classic dialog

**Host Application**:
The application whose process owns the File Dialog.
_Avoid_: Target app, client

**Current Folder**:
The folder whose contents the File Dialog is showing.

**Typed File Name**:
Whatever text is in the File Dialog's file name box at a given moment, whether typed by the user or prefilled by the Host Application.
_Avoid_: File name (ambiguous with the selected file)

**File Type Filter**:
The entry selected in the File Dialog's file type list.
_Avoid_: Extension, filter

**Commit**:
Completing the File Dialog with a result (Open, Save or Select Folder), which closes it and hands the choice to the Host Application.
_Avoid_: Confirm, accept, OK

### Favorites

**Favorite**:
A user-defined entry made of a display name and a folder path; the path may be local or a network share and may contain environment variables.
_Avoid_: Bookmark, favorite location, favorite folder, place

**Favorites Button**:
The folder-icon button the mod adds to a File Dialog's title bar, immediately left of the standard caption buttons.
_Avoid_: FBX button, toolbar button

**Favorites Menu**:
The menu of Favorites that the Favorites Button opens.

**Navigate**:
Changing a File Dialog's Current Folder to a Favorite's folder without Committing, leaving the Typed File Name and File Type Filter as they were.
_Avoid_: Open, jump, switch
