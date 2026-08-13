# M14.2 - Start GUI only (MACHINE_CLIENT=protocol). Does NOT start simulation.
# GUI may run offline if simulation_protocol is not listening on :8766.

$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'dev-common.ps1')

$script:StartedGuiPid = $null
$script:CleanupDone = $false

function Invoke-GuiCleanup {
  if ($script:CleanupDone) { return }
  $script:CleanupDone = $true
  Write-Host ''
  Write-Dev 'Cleaning up owned GUI...'
  if ($null -ne $script:StartedGuiPid -and (Test-IsProjectGuiProcess $script:StartedGuiPid)) {
    Stop-ProjectProcess $script:StartedGuiPid 'GUI'
    [void](Wait-ProcessGone $script:StartedGuiPid)
  }
  $guiFile = Read-PidFile $script:GuiPidFile
  if ($null -ne $guiFile -and $null -ne $script:StartedGuiPid -and $guiFile -eq $script:StartedGuiPid) {
    Clear-PidFile $script:GuiPidFile
  }
}

Write-Dev 'Cleaning previous project GUI...'
[void](Stop-ProjectGuiProcesses)
Start-Sleep -Milliseconds 300

if (-not (Test-PortFree $script:GuiPort)) {
  Write-Dev "ERROR: Port $($script:GuiPort) is not free."
  exit 1
}
Write-Dev "Port $($script:GuiPort) is free."

if (Test-PortReachable '127.0.0.1' $script:SimPort) {
  Write-Dev "simulation_protocol appears reachable on $($script:SimPort) (optional)."
} else {
  Write-Dev "WARNING: Port $($script:SimPort) not reachable - GUI will start offline until simulation starts."
}

try {
  $pidGui = Start-ProjectGuiProcess
  if ($null -eq $pidGui) { exit 1 }
  $script:StartedGuiPid = $pidGui

  Write-Host ''
  Write-Dev 'GUI ready (independent of simulation lifecycle).'
  Write-Dev 'GUI: http://localhost:3000'
  Write-Dev 'Press Ctrl+C to stop GUI only (simulation is left running).'
  Write-Host ''

  while ($true) {
    $guiAlive = Get-Process -Id $script:StartedGuiPid -ErrorAction SilentlyContinue
    if ($null -eq $guiAlive) {
      Write-Dev 'GUI process exited.'
      break
    }
    Start-Sleep -Seconds 1
  }
} finally {
  Invoke-GuiCleanup
}

exit 0
