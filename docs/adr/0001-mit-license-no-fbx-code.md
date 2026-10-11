# MIT license, no FileBox eXtender code

FileBox eXtender v2.01 is open source under GPLv3-or-later, and this mod reproduces its favorites feature. We license the mod under MIT and do not copy FBX code: we study its behavior and approach only. FBX targets 2008-era dialogs through global message hooks, the dialog's internal window tree and a "type the path into the file name box and press OK" trick, none of which suits a mod that drives the modern File Dialog through its own interface. Copying even small parts would force the whole mod to GPLv3 for little benefit.

## Consequences

- When a technique is borrowed from FBX (for example, an owned popup window placed beside the caption buttons), it is reimplemented from scratch and the idea is credited in the docs, not the code.
- If FBX code later proves worth reusing, this decision must be superseded and the mod relicensed under GPLv3 before any FBX code is merged.
