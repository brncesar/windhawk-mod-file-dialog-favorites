# File Dialog Favorites

> **Status: experimental — not usable yet.** Nothing here has been implemented or tested. Do not install it expecting a working mod.

A [Windhawk](https://windhawk.net/) mod for Windows 11 that adds a favorites menu to file dialogs, in the spirit of the old [FileBox eXtender](https://www.hyperionics.com/files/).

## What it will do

- Adds a folder-icon button to the title bar of Open, Save and folder-picker dialogs, just left of the standard caption buttons.
- Clicking the button opens a menu of your favorite folders.
- Picking a favorite moves the dialog to that folder. It does not open or save anything, and it keeps the file name you typed and the file type you selected.

Favorites are edited in the mod's settings in Windhawk.

## Project documents

- [PLAN.md](PLAN.md) — implementation plan and recorded decisions (in Portuguese).
- [CONTEXT.md](CONTEXT.md) — domain glossary.
- [docs/adr/](docs/adr/) — architecture decision records.

## Naming

The Windhawk mod ID is `file-dialog-favorites`. The repository is named `windhawk-mod-file-dialog-favorites`.

## Credits and license

The behavior is inspired by FileBox eXtender by Hyperionics. No FileBox eXtender code is used (see [ADR 0001](docs/adr/0001-mit-license-no-fbx-code.md)).

Licensed under the [MIT License](LICENSE).
