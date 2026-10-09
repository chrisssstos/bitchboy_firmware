<# : BitchBoy Windows MIDI fix. Batch launcher first, PowerShell body after the comment-close line.
@echo off
setlocal
set "BBFIX_SELF=%~f0"
set "BBFIX_MODE=%~1"
set "BBFIX_PS=%SystemRoot%\System32\WindowsPowerShell\v1.0\powershell.exe"
rem A 32-bit cmd would get 32-bit PowerShell, which can't change drivers.
if exist "%SystemRoot%\Sysnative\WindowsPowerShell\v1.0\powershell.exe" set "BBFIX_PS=%SystemRoot%\Sysnative\WindowsPowerShell\v1.0\powershell.exe"
"%BBFIX_PS%" -NoProfile -ExecutionPolicy Bypass -Command "Invoke-Expression ([IO.File]::ReadAllText($env:BBFIX_SELF))"
rem Exit code 10 = relaunched as admin in a new window; don't hold this one open.
if not "%errorlevel%"=="10" pause
exit /b
#>

# Windows 11's MIDI 2.0 update (Windows MIDI Services) leaves USB MIDI 1.0
# devices on the old "USB Audio Device" driver (usbaudio.sys). On that driver
# the BitchBoy's MIDI input works but MIDI *to* the device (LED feedback) does
# not. Switching the device to the new "USBMidi2-ACX" driver fixes it, see
# https://resolume.com/support/en/midi-troubles-on-windows-with-midi-2-0-update
#
# This does that switch automatically, only for plugged-in BitchBoys that are
# still on the old driver, and only on PCs that have Windows MIDI Services.
# Windows remembers the choice per unit (the USB serial is per unit), so it's
# needed once per BitchBoy per PC.
#
# Run with "undo" as the first argument to switch back to the old driver.

$ErrorActionPreference = 'Stop'
$undo = ($env:BBFIX_MODE -eq 'undo')

Write-Host ''
Write-Host '  BitchBoy - Windows MIDI fix' -ForegroundColor Magenta
Write-Host '  ---------------------------'
Write-Host ''

$principal = New-Object Security.Principal.WindowsPrincipal([Security.Principal.WindowsIdentity]::GetCurrent())
if (-not $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
    Write-Host '  Changing a driver needs administrator rights - asking Windows...'
    try {
        if ($env:BBFIX_MODE) {
            Start-Process -FilePath $env:BBFIX_SELF -ArgumentList $env:BBFIX_MODE -Verb RunAs
        } else {
            Start-Process -FilePath $env:BBFIX_SELF -Verb RunAs
        }
        exit 10
    } catch {
        Write-Host '  Administrator rights were declined. Nothing was changed.' -ForegroundColor Yellow
        exit 1
    }
}

Add-Type -TypeDefinition @'
using System;
using System.ComponentModel;
using System.Runtime.InteropServices;

public static class BitchBoyDriver {
    [StructLayout(LayoutKind.Sequential)]
    struct SP_DEVINFO_DATA {
        public uint cbSize;
        public Guid ClassGuid;
        public uint DevInst;
        public IntPtr Reserved;
    }

    [StructLayout(LayoutKind.Sequential, CharSet = CharSet.Unicode)]
    struct SP_DRVINFO_DATA_V2 {
        public uint cbSize;
        public uint DriverType;
        public IntPtr Reserved;
        [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 256)] public string Description;
        [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 256)] public string MfgName;
        [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 256)] public string ProviderName;
        public System.Runtime.InteropServices.ComTypes.FILETIME DriverDate;
        public ulong DriverVersion;
    }

    const uint SPDIT_CLASSDRIVER = 1;
    const uint SPDIT_COMPATDRIVER = 2;
    const int ERROR_NO_MORE_ITEMS = 259;

    [DllImport("setupapi.dll", SetLastError = true)]
    static extern IntPtr SetupDiCreateDeviceInfoList(IntPtr classGuid, IntPtr hwnd);

    [DllImport("setupapi.dll", SetLastError = true, CharSet = CharSet.Unicode)]
    static extern bool SetupDiOpenDeviceInfo(IntPtr set, string instanceId, IntPtr hwnd, uint flags, ref SP_DEVINFO_DATA data);

    [DllImport("setupapi.dll", SetLastError = true)]
    static extern bool SetupDiBuildDriverInfoList(IntPtr set, ref SP_DEVINFO_DATA data, uint driverType);

    [DllImport("setupapi.dll", SetLastError = true, CharSet = CharSet.Unicode)]
    static extern bool SetupDiEnumDriverInfo(IntPtr set, ref SP_DEVINFO_DATA data, uint driverType, uint index, ref SP_DRVINFO_DATA_V2 drv);

    [DllImport("setupapi.dll", SetLastError = true)]
    static extern bool SetupDiDestroyDriverInfoList(IntPtr set, ref SP_DEVINFO_DATA data, uint driverType);

    [DllImport("setupapi.dll", SetLastError = true)]
    static extern bool SetupDiDestroyDeviceInfoList(IntPtr set);

    [DllImport("newdev.dll", SetLastError = true)]
    static extern bool DiInstallDevice(IntPtr hwnd, IntPtr set, ref SP_DEVINFO_DATA data, ref SP_DRVINFO_DATA_V2 drv, uint flags, [MarshalAs(UnmanagedType.Bool)] out bool needReboot);

    // Installs the first available driver whose description contains `match`
    // (case-insensitive) on the device - the same thing Device Manager's
    // "Let me pick from a list of available drivers" does. Returns the driver
    // description, or null if no such driver exists on this PC.
    public static string Install(string instanceId, string match, out bool needReboot) {
        needReboot = false;
        IntPtr set = SetupDiCreateDeviceInfoList(IntPtr.Zero, IntPtr.Zero);
        if (set == new IntPtr(-1)) throw new Win32Exception(Marshal.GetLastWin32Error());
        try {
            SP_DEVINFO_DATA dev = new SP_DEVINFO_DATA();
            dev.cbSize = (uint)Marshal.SizeOf(typeof(SP_DEVINFO_DATA));
            if (!SetupDiOpenDeviceInfo(set, instanceId, IntPtr.Zero, 0, ref dev))
                throw new Win32Exception(Marshal.GetLastWin32Error());

            // Compatible drivers first; fall back to every driver of the
            // device's class (Device Manager's "Show compatible hardware" off).
            foreach (uint type in new uint[] { SPDIT_COMPATDRIVER, SPDIT_CLASSDRIVER }) {
                if (!SetupDiBuildDriverInfoList(set, ref dev, type)) continue;
                try {
                    for (uint i = 0; ; i++) {
                        SP_DRVINFO_DATA_V2 drv = new SP_DRVINFO_DATA_V2();
                        drv.cbSize = (uint)Marshal.SizeOf(typeof(SP_DRVINFO_DATA_V2));
                        if (!SetupDiEnumDriverInfo(set, ref dev, type, i, ref drv)) {
                            int err = Marshal.GetLastWin32Error();
                            if (err == ERROR_NO_MORE_ITEMS) break;
                            throw new Win32Exception(err);
                        }
                        if (drv.Description == null ||
                            drv.Description.IndexOf(match, StringComparison.OrdinalIgnoreCase) < 0) continue;
                        if (!DiInstallDevice(IntPtr.Zero, set, ref dev, ref drv, 0, out needReboot))
                            throw new Win32Exception(Marshal.GetLastWin32Error());
                        return drv.Description;
                    }
                } finally {
                    SetupDiDestroyDriverInfoList(set, ref dev, type);
                }
            }
            return null;
        } finally {
            SetupDiDestroyDeviceInfoList(set);
        }
    }
}
'@

function Get-DevProp([string]$id, [string]$key) {
    try { (Get-PnpDeviceProperty -InstanceId $id -KeyName $key -ErrorAction Stop).Data } catch { $null }
}

# VID/PID are TinyUSB defaults shared with other hobby devices, so confirm the
# product string ("BitchBoy", reported on the interface or its parent device).
function Test-BitchBoy([string]$id) {
    $names = @(Get-DevProp $id 'DEVPKEY_Device_BusReportedDeviceDesc')
    $parent = Get-DevProp $id 'DEVPKEY_Device_Parent'
    if ($parent) { $names += Get-DevProp $parent 'DEVPKEY_Device_BusReportedDeviceDesc' }
    return [bool]($names | Where-Object { $_ -like '*bitchboy*' })
}

if (-not $undo -and -not (Get-Service -Name midisrv -ErrorAction SilentlyContinue)) {
    Write-Host '  This PC does not have the Windows MIDI 2.0 update (Windows MIDI Services),'
    Write-Host '  so it is not affected. Nothing to do.' -ForegroundColor Green
    exit 0
}

$devices = @(Get-PnpDevice -PresentOnly -ErrorAction SilentlyContinue | Where-Object {
    $_.InstanceId -like 'USB\VID_239A&PID_CAFE&MI_*' -and $_.Class -eq 'MEDIA' -and (Test-BitchBoy $_.InstanceId)
})

if ($devices.Count -eq 0) {
    Write-Host '  No BitchBoy found. Plug it in (directly, not through a hub if possible)' -ForegroundColor Yellow
    Write-Host '  and run this again.'
    exit 1
}

if ($undo) {
    $wantService = 'usbaudio'; $wantDriver = 'USB Audio Device'; $label = 'old driver (USB Audio Device)'
} else {
    $wantService = 'usbmidi2'; $wantDriver = 'USBMidi2';         $label = 'new driver (USBMidi2-ACX)'
}

Write-Host "  Found $($devices.Count) BitchBoy MIDI device(s)."
Write-Host '  Close Resolume, Ableton and any other MIDI software before continuing.' -ForegroundColor Yellow
[void](Read-Host '  Press Enter to continue')
Write-Host ''

$failed = 0
$reboot = $false
foreach ($dev in $devices) {
    $service = Get-DevProp $dev.InstanceId 'DEVPKEY_Device_Service'
    if ($service -eq $wantService) {
        Write-Host "  [ok]   Already on the $label." -ForegroundColor Green
        continue
    }
    try {
        $needReboot = $false
        $installed = [BitchBoyDriver]::Install($dev.InstanceId, $wantDriver, [ref]$needReboot)
        if (-not $installed) {
            Write-Host "  [fail] The $label is not available on this PC. Run Windows Update and try again." -ForegroundColor Red
            $failed++
            continue
        }
        Write-Host "  [done] Switched to the $label." -ForegroundColor Green
        if ($needReboot) { $reboot = $true } else { & pnputil.exe /restart-device "$($dev.InstanceId)" *> $null }
    } catch {
        Write-Host "  [fail] Could not change the driver: $($_.Exception.Message)" -ForegroundColor Red
        $failed++
    }
}

Write-Host ''
if ($failed -gt 0) {
    Write-Host '  Some devices could not be switched. You can still do it by hand, see:'
    Write-Host '  https://resolume.com/support/en/midi-troubles-on-windows-with-midi-2-0-update'
    exit 1
}
if ($reboot) {
    Write-Host '  Done. Windows needs a restart to finish - restart the PC, then open Resolume.' -ForegroundColor Yellow
} else {
    Write-Host '  Done. Unplug the BitchBoy, plug it back in, then open Resolume.' -ForegroundColor Green
}
exit 0
