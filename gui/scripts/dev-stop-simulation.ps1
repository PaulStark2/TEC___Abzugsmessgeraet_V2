# M14.2 - Stop project simulation_protocol only. Does NOT stop GUI.

$ErrorActionPreference = 'Continue'
. (Join-Path $PSScriptRoot 'dev-common.ps1')

$stopped = Stop-ProjectSimProcesses
if ($stopped) {
  Write-Dev 'Simulation stopped.'
} else {
  Write-Dev 'No project simulation_protocol processes found.'
}
Report-PortStatus $script:SimPort
Report-PortStatus $script:PlantPort
exit 0
