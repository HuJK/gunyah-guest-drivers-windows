$ErrorActionPreference='Continue'
$base = $PSScriptRoot
Write-Host "DroidVM ARM64 driver installer" -ForegroundColor Cyan
# NetKVM LAST: on a protected VM, touching a live NIC can bugcheck; do the others first.
$order = @('rdmapool','pvmpower','viostor','vioscsi','vioinput','NetKVM')

# pvmpower binds to a root-enumerated software device (ROOT\PVMPOWER) so it sits in
# the PnP tree and receives system power IRPs. Create the devnode once (persists
# across boots); pnputil /install then binds the driver to it.
function Ensure-PvmPowerDevNode {
  $have = Get-PnpDevice -ErrorAction SilentlyContinue | Where-Object { $_.InstanceId -like 'ROOT\PVMPOWER\*' }
  if ($have) { Write-Host "  ROOT\PVMPOWER devnode already present"; return }
  Add-Type -ErrorAction SilentlyContinue @"
using System;
using System.Runtime.InteropServices;
public static class PvmDevNode {
    [StructLayout(LayoutKind.Sequential)]
    public struct SP_DEVINFO_DATA { public uint cbSize; public Guid ClassGuid; public uint DevInst; public IntPtr Reserved; }
    [DllImport("setupapi.dll", SetLastError=true)]
    public static extern IntPtr SetupDiCreateDeviceInfoList(ref Guid ClassGuid, IntPtr hwndParent);
    [DllImport("setupapi.dll", CharSet=CharSet.Unicode, SetLastError=true)]
    public static extern bool SetupDiCreateDeviceInfoW(IntPtr DeviceInfoSet, string DeviceName, ref Guid ClassGuid, string DeviceDescription, IntPtr hwndParent, uint CreationFlags, ref SP_DEVINFO_DATA DeviceInfoData);
    [DllImport("setupapi.dll", CharSet=CharSet.Unicode, SetLastError=true)]
    public static extern bool SetupDiSetDeviceRegistryPropertyW(IntPtr DeviceInfoSet, ref SP_DEVINFO_DATA DeviceInfoData, uint Property, byte[] PropertyBuffer, uint PropertyBufferSize);
    [DllImport("setupapi.dll", SetLastError=true)]
    public static extern bool SetupDiCallClassInstaller(uint InstallFunction, IntPtr DeviceInfoSet, ref SP_DEVINFO_DATA DeviceInfoData);
    [DllImport("setupapi.dll", SetLastError=true)]
    public static extern bool SetupDiDestroyDeviceInfoList(IntPtr DeviceInfoSet);
}
"@
  $classSystem = [Guid]'4d36e97d-e325-11ce-bfc1-08002be10318'
  $DICD_GENERATE_ID = 1; $SPDRP_HARDWAREID = 1; $DIF_REGISTERDEVICE = 0x19
  $set = [PvmDevNode]::SetupDiCreateDeviceInfoList([ref]$classSystem, [IntPtr]::Zero)
  if ($set -eq [IntPtr]::Zero -or $set -eq [IntPtr](-1)) { Write-Host "  devnode: SetupDiCreateDeviceInfoList failed" -ForegroundColor Red; return }
  try {
    $dev = New-Object PvmDevNode+SP_DEVINFO_DATA
    $dev.cbSize = [uint32][Runtime.InteropServices.Marshal]::SizeOf([type][PvmDevNode+SP_DEVINFO_DATA])
    if (-not [PvmDevNode]::SetupDiCreateDeviceInfoW($set, 'PVMPOWER', [ref]$classSystem, 'Gunyah pVM Power Manager', [IntPtr]::Zero, $DICD_GENERATE_ID, [ref]$dev)) {
      Write-Host "  devnode: SetupDiCreateDeviceInfo failed ($([Runtime.InteropServices.Marshal]::GetLastWin32Error()))" -ForegroundColor Red; return
    }
    # REG_MULTI_SZ "ROOT\PVMPOWER" + double NUL, UTF-16LE
    $hwid = [Text.Encoding]::Unicode.GetBytes("ROOT\PVMPOWER`0`0")
    if (-not [PvmDevNode]::SetupDiSetDeviceRegistryPropertyW($set, [ref]$dev, $SPDRP_HARDWAREID, $hwid, [uint32]$hwid.Length)) {
      Write-Host "  devnode: set HardwareID failed ($([Runtime.InteropServices.Marshal]::GetLastWin32Error()))" -ForegroundColor Red; return
    }
    if (-not [PvmDevNode]::SetupDiCallClassInstaller($DIF_REGISTERDEVICE, $set, [ref]$dev)) {
      Write-Host "  devnode: DIF_REGISTERDEVICE failed ($([Runtime.InteropServices.Marshal]::GetLastWin32Error()))" -ForegroundColor Red; return
    }
    Write-Host "  created ROOT\PVMPOWER devnode"
  } finally {
    [void][PvmDevNode]::SetupDiDestroyDeviceInfoList($set)
  }
}

function Get-Pkgs {
  $r=@(); $pub=$null
  foreach($l in (pnputil /enum-drivers)){
    if($l -match 'Published Name\s*:\s*(\S+)'){ $pub=$matches[1] }
    elseif(($l -match 'Original Name\s*:\s*(\S+)') -and $pub){ $r += [pscustomobject]@{Pub=$pub;Orig=$matches[1].ToLower()}; $pub=$null }
  }
  ,$r
}
foreach($d in $order){
  $inf = Get-ChildItem (Join-Path $base "drivers\$d") -Filter *.inf -ErrorAction SilentlyContinue | Select-Object -First 1
  if(-not $inf){ Write-Host "skip $d (no inf found)" -ForegroundColor Yellow; continue }
  $orig = $inf.Name.ToLower()
  Write-Host ""
  Write-Host ("== install " + $d + " : " + $inf.Name + " ==") -ForegroundColor Cyan
  if($d -eq 'pvmpower'){ Ensure-PvmPowerDevNode }
  $before = @(Get-Pkgs | Where-Object { $_.Orig -eq $orig } | ForEach-Object Pub)
  pnputil /add-driver $inf.FullName /install | Write-Host
  $after = @(Get-Pkgs | Where-Object { $_.Orig -eq $orig } | ForEach-Object Pub)
  $new = @($after | Where-Object { $_ -notin $before }) | Select-Object -First 1
  if(-not $new){ $new = $after | Select-Object -Last 1 }
  foreach($p in $after){ if($p -ne $new){ Write-Host ("  remove old " + $p); pnputil /delete-driver $p /uninstall | Out-Null } }
}
Write-Host ""
Write-Host "DONE. Reboot so the boot drivers (viostor/rdmapool) load the new version." -ForegroundColor Green
