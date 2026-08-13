# M14.2 - Start simulation_protocol only. Does NOT start GUI.

$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'dev-common.ps1')

$script:StartedSimPid = $null
$script:CleanupDone = $false

function Invoke-SimCleanup {
  if ($script:CleanupDone) { return }
  $script:CleanupDone = $true
  Write-Host ''
  Write-Dev 'Cleaning up owned simulation_protocol...'
  if ($null -ne $script:StartedSimPid -and (Test-IsProjectSimProcess $script:StartedSimPid)) {
    Stop-ProjectProcess $script:StartedSimPid 'simulation_protocol'
    [void](Wait-ProcessGone $script:StartedSimPid)
  }
  $simFile = Read-PidFile $script:SimPidFile
  if ($null -ne $simFile -and $null -ne $script:StartedSimPid -and $simFile -eq $script:StartedSimPid) {
    Clear-PidFile $script:SimPidFile
  }
}

Write-Dev 'Cleaning previous project simulation_protocol...'
[void](Stop-ProjectSimProcesses)
Start-Sleep -Milliseconds 300

foreach ($port in @($script:SimPort, $script:PlantPort)) {
  if (-not (Test-PortFree $port)) {
    Write-Dev "ERROR: Port $port is not free."
    exit 1
  }
  Write-Dev "Port $port is free."
}

try {
  $pidSim = Start-ProjectSimulationProcess
  if ($null -eq $pidSim) { exit 1 }
  $script:StartedSimPid = $pidSim

  Write-Host ''
  Write-Dev 'Simulation/controller host ready (no GUI required).'
  Write-Dev "M12 endpoint: 127.0.0.1:$($script:SimPort)"
  Write-Dev "Plant channel (simulation only): 127.0.0.1:$($script:PlantPort)"
  Write-Dev 'Press Ctrl+C to stop simulation only (GUI is left running).'
  Write-Host ''

  while ($true) {
    $simAlive = Get-Process -Id $script:StartedSimPid -ErrorAction SilentlyContinue
    if ($null -eq $simAlive) {
      Write-Dev 'simulation_protocol exited.'
      break
    }
    Start-Sleep -Seconds 1
  }
} finally {
  Invoke-SimCleanup
}

exit 0
