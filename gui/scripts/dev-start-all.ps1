# M14.2 - Start simulation then GUI (convenience). Lifecycles remain independent:
# stopping one component via stop-gui / stop-simulation does not stop the other.
# Ctrl+C on THIS script stops only the processes THIS script started.

$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'dev-common.ps1')

$script:StartedSimPid = $null
$script:StartedGuiPid = $null
$script:CleanupDone = $false

function Invoke-OwnedCleanup {
  if ($script:CleanupDone) { return }
  $script:CleanupDone = $true
  Write-Host ''
  Write-Dev 'Cleaning up processes owned by this start-all session...'
  if ($null -ne $script:StartedGuiPid -and (Test-IsProjectGuiProcess $script:StartedGuiPid)) {
    Stop-ProjectProcess $script:StartedGuiPid 'GUI'
    [void](Wait-ProcessGone $script:StartedGuiPid)
  }
  if ($null -ne $script:StartedSimPid -and (Test-IsProjectSimProcess $script:StartedSimPid)) {
    Stop-ProjectProcess $script:StartedSimPid 'simulation_protocol'
    [void](Wait-ProcessGone $script:StartedSimPid)
  }
  $guiFile = Read-PidFile $script:GuiPidFile
  if ($null -ne $guiFile -and $null -ne $script:StartedGuiPid -and $guiFile -eq $script:StartedGuiPid) {
    Clear-PidFile $script:GuiPidFile
  }
  $simFile = Read-PidFile $script:SimPidFile
  if ($null -ne $simFile -and $null -ne $script:StartedSimPid -and $simFile -eq $script:StartedSimPid) {
    Clear-PidFile $script:SimPidFile
  }
}

Write-Dev 'Cleaning previous project GUI + simulation...'
[void](Stop-ProjectGuiProcesses)
[void](Stop-ProjectSimProcesses)
Start-Sleep -Milliseconds 400

try {
  $pidSim = Start-ProjectSimulationProcess
  if ($null -eq $pidSim) { exit 1 }
  $script:StartedSimPid = $pidSim
  Write-Host ''

  $pidGui = Start-ProjectGuiProcess
  if ($null -eq $pidGui) {
    Invoke-OwnedCleanup
    exit 1
  }
  $script:StartedGuiPid = $pidGui

  Write-Host ''
  Write-Dev 'Development environment ready (GUI + simulation started independently).'
  Write-Dev 'GUI: http://localhost:3000'
  Write-Dev 'Simulation UI: http://localhost:3000/simulation'
  Write-Dev 'Prefer: npm run dev:stop-gui / dev:stop-simulation for independent stop.'
  Write-Dev 'Ctrl+C stops only this session''s owned PIDs.'
  Write-Host ''

  $simExitWarned = $false
  $guiExitWarned = $false
  while ($true) {
    # PIDs are cleared once a component exited; Get-Process must not see $null.
    $guiAlive = $null
    if ($null -ne $script:StartedGuiPid) {
      $guiAlive = Get-Process -Id $script:StartedGuiPid -ErrorAction SilentlyContinue
    }
    $simAlive = $null
    if ($null -ne $script:StartedSimPid) {
      $simAlive = Get-Process -Id $script:StartedSimPid -ErrorAction SilentlyContinue
    }

    if ($null -eq $guiAlive -and -not $guiExitWarned) {
      Write-Dev 'WARNING: GUI exited. Simulation left running (independent lifecycle).'
      $guiExitWarned = $true
      $script:StartedGuiPid = $null
    }
    if ($null -eq $simAlive -and -not $simExitWarned) {
      Write-Dev 'WARNING: simulation_protocol exited. GUI left running (offline).'
      $simExitWarned = $true
      $script:StartedSimPid = $null
    }

    # Keep waiting until operator Ctrl+C; do not tear down the other side.
    if ($null -eq $guiAlive -and $null -eq $simAlive) {
      Write-Dev 'Both owned processes exited.'
      break
    }
    Start-Sleep -Seconds 1
  }
} finally {
  Invoke-OwnedCleanup
}

exit 0
