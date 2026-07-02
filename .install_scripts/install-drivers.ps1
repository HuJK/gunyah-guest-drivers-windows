$ErrorActionPreference='Continue'
$base = $PSScriptRoot
Write-Host "DroidVM ARM64 driver installer" -ForegroundColor Cyan
# NetKVM LAST: on a protected VM, touching a live NIC can bugcheck; do the others first.
# pvmpower.sys ships inside the rdmapool package (upper filter on ACPI\RDMA0000).
$order = @('rdmapool','viostor','vioscsi','vioinput','NetKVM')

function Get-Pkgs {
  $r=@(); $pub=$null
  foreach($l in (pnputil /enum-drivers)){
    if($l -match 'Published Name\s*:\s*(\S+)'){ $pub=$matches[1] }
    elseif(($l -match 'Original Name\s*:\s*(\S+)') -and $pub){ $r += [pscustomobject]@{Pub=$pub;Orig=$matches[1].ToLower()}; $pub=$null }
  }
  ,$r
}

# Clean up the short-lived standalone pvmpower packaging (root-enumerated
# ROOT\PVMPOWER devnode + its own pvmpower.inf). Both are no-ops if never installed.
Get-PnpDevice -ErrorAction SilentlyContinue | Where-Object { $_.InstanceId -like 'ROOT\PVMPOWER\*' } | ForEach-Object {
  Write-Host ("remove legacy pvmpower devnode " + $_.InstanceId)
  pnputil /remove-device $_.InstanceId | Out-Null
}
Get-Pkgs | Where-Object { $_.Orig -eq 'pvmpower.inf' } | ForEach-Object {
  Write-Host ("remove legacy pvmpower package " + $_.Pub)
  pnputil /delete-driver $_.Pub /uninstall | Out-Null
}

foreach($d in $order){
  $inf = Get-ChildItem (Join-Path $base "drivers\$d") -Filter *.inf -ErrorAction SilentlyContinue | Select-Object -First 1
  if(-not $inf){ Write-Host "skip $d (no inf found)" -ForegroundColor Yellow; continue }
  $orig = $inf.Name.ToLower()
  Write-Host ""
  Write-Host ("== install " + $d + " : " + $inf.Name + " ==") -ForegroundColor Cyan
  $before = @(Get-Pkgs | Where-Object { $_.Orig -eq $orig } | ForEach-Object Pub)
  pnputil /add-driver $inf.FullName /install | Write-Host
  $after = @(Get-Pkgs | Where-Object { $_.Orig -eq $orig } | ForEach-Object Pub)
  $new = @($after | Where-Object { $_ -notin $before }) | Select-Object -First 1
  if(-not $new){ $new = $after | Select-Object -Last 1 }
  foreach($p in $after){ if($p -ne $new){ Write-Host ("  remove old " + $p); pnputil /delete-driver $p /uninstall | Out-Null } }
}
Write-Host ""
Write-Host "DONE. Reboot so the boot drivers (viostor/rdmapool) load the new version." -ForegroundColor Green
