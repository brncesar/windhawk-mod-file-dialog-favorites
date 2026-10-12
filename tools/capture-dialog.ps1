<#
.SYNOPSIS
Opens a File Dialog from the test harness and saves a screenshot of it.

.DESCRIPTION
Starts build\test-dialogs-<Arch>.exe (unless already running), clicks one of
its buttons, waits for the dialog and captures the dialog's window area plus a
margin to a PNG. Used to check the Favorites Button's placement and look
without manual screenshots. The dialog is left open.

.EXAMPLE
pwsh tools\capture-dialog.ps1 -Dialog SaveModern -Output build\save.png
#>
param(
    [ValidateSet('OpenModern', 'SaveModern', 'PickFolderModern', 'OpenLegacy', 'SaveLegacy', 'SaveModernThread')]
    [string]$Dialog = 'SaveModern',
    [ValidateSet('x64', 'x86')]
    [string]$Arch = 'x64',
    [string]$Output,
    [int]$Margin = 40,
    [switch]$Full
)

$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent $PSScriptRoot
if (-not $Output) { $Output = Join-Path $repo "build\$Dialog-$Arch.png" }

Add-Type -AssemblyName System.Drawing
Add-Type @'
using System;
using System.Runtime.InteropServices;
public static class CaptureNative {
    [DllImport("user32.dll", CharSet = CharSet.Unicode)]
    public static extern IntPtr FindWindowW(string cls, string title);
    [DllImport("user32.dll")]
    public static extern IntPtr GetDlgItem(IntPtr dlg, int id);
    [DllImport("user32.dll")]
    public static extern bool PostMessageW(IntPtr hwnd, uint msg, IntPtr w, IntPtr l);
    [DllImport("user32.dll", CharSet = CharSet.Unicode)]
    public static extern IntPtr FindWindowExW(IntPtr parent, IntPtr after, string cls, string title);
    [DllImport("user32.dll")]
    public static extern bool IsWindowVisible(IntPtr hwnd);
    [DllImport("user32.dll")]
    public static extern bool SetForegroundWindow(IntPtr hwnd);
    [DllImport("user32.dll")]
    public static extern void keybd_event(byte vk, byte scan, uint flags, UIntPtr extra);
    // Windows only lets the process that received the last input event take
    // the foreground; a synthetic Alt tap satisfies that.
    public static bool ForceForeground(IntPtr hwnd) {
        keybd_event(0x12, 0, 0, UIntPtr.Zero);
        keybd_event(0x12, 0, 2, UIntPtr.Zero);
        return SetForegroundWindow(hwnd);
    }
    [DllImport("user32.dll")]
    public static extern uint GetWindowThreadProcessId(IntPtr hwnd, out uint pid);
    [DllImport("user32.dll")]
    public static extern bool SetProcessDpiAwarenessContext(IntPtr value);
    [DllImport("dwmapi.dll")]
    public static extern int DwmGetWindowAttribute(IntPtr hwnd, int attr, out RECT rect, int size);
    [StructLayout(LayoutKind.Sequential)]
    public struct RECT { public int Left, Top, Right, Bottom; }
}
'@
# Physical pixels, so the capture matches what is on screen at any scale.
[CaptureNative]::SetProcessDpiAwarenessContext([IntPtr]-4) | Out-Null

$commandIds = @{ OpenModern = 100; SaveModern = 101; PickFolderModern = 102; OpenLegacy = 103; SaveLegacy = 104; SaveModernThread = 105 }
$title = "File Dialog Favorites test ($Arch)"

$main = [CaptureNative]::FindWindowW('FileDialogFavoritesTestDialogs', $title)
if ($main -eq [IntPtr]::Zero) {
    Start-Process (Join-Path $repo "build\test-dialogs-$Arch.exe")
    for ($i = 0; $i -lt 50 -and $main -eq [IntPtr]::Zero; $i++) {
        Start-Sleep -Milliseconds 100
        $main = [CaptureNative]::FindWindowW('FileDialogFavoritesTestDialogs', $title)
    }
    if ($main -eq [IntPtr]::Zero) { throw 'Test harness window not found' }
}

[uint32]$harnessPid = 0
[CaptureNative]::GetWindowThreadProcessId($main, [ref]$harnessPid) | Out-Null
[CaptureNative]::ForceForeground($main) | Out-Null

$button = [CaptureNative]::GetDlgItem($main, $commandIds[$Dialog])
$BM_CLICK = 0x00F5
[CaptureNative]::PostMessageW($button, $BM_CLICK, [IntPtr]::Zero, [IntPtr]::Zero) | Out-Null

# Wait for a visible top-level #32770 window from the harness process.
$dialogWindow = [IntPtr]::Zero
for ($i = 0; $i -lt 300 -and $dialogWindow -eq [IntPtr]::Zero; $i++) {
    Start-Sleep -Milliseconds 100
    $candidate = [IntPtr]::Zero
    while ($true) {
        $candidate = [CaptureNative]::FindWindowExW([IntPtr]::Zero, $candidate, '#32770', [NullString]::Value)
        if ($candidate -eq [IntPtr]::Zero) { break }
        [uint32]$candidatePid = 0
        [CaptureNative]::GetWindowThreadProcessId($candidate, [ref]$candidatePid) | Out-Null
        if ($candidatePid -eq $harnessPid -and [CaptureNative]::IsWindowVisible($candidate)) {
            $dialogWindow = $candidate
            break
        }
    }
}
if ($dialogWindow -eq [IntPtr]::Zero) { throw 'Dialog window not found' }
[CaptureNative]::ForceForeground($dialogWindow) | Out-Null
Start-Sleep -Milliseconds 800  # let the dialog finish painting

$rect = New-Object CaptureNative+RECT
$DWMWA_EXTENDED_FRAME_BOUNDS = 9
[CaptureNative]::DwmGetWindowAttribute($dialogWindow, $DWMWA_EXTENDED_FRAME_BOUNDS, [ref]$rect, 16) | Out-Null

$left = $rect.Left - $Margin
$top = $rect.Top - $Margin
$width = $rect.Right - $rect.Left + 2 * $Margin
$height = $(if ($Full) { $rect.Bottom - $rect.Top } else { [Math]::Min($rect.Bottom - $rect.Top, 200) }) + 2 * $Margin

$bitmap = New-Object System.Drawing.Bitmap $width, $height
$graphics = [System.Drawing.Graphics]::FromImage($bitmap)
$graphics.CopyFromScreen($left, $top, 0, 0, $bitmap.Size)
New-Item -ItemType Directory -Force (Split-Path -Parent $Output) | Out-Null
$bitmap.Save($Output, [System.Drawing.Imaging.ImageFormat]::Png)
$graphics.Dispose()
$bitmap.Dispose()

Write-Host ("Dialog 0x{0:X} frame ({1},{2})-({3},{4}); saved {5}" -f $dialogWindow.ToInt64(), $rect.Left, $rect.Top, $rect.Right, $rect.Bottom, $Output)
