# M14.2 - Stop GUI + simulation_protocol (independent processes, both cleaned).
# Alias target for npm run dev:stop / dev:stop-all

$ErrorActionPreference = 'Continue'
. (Join-Path $PSScriptRoot 'dev-common.ps1')

$guiStopped = Stop-ProjectGuiProcesses
$simStopped = Stop-ProjectSimProcesses

if (-not $guiStopped -and -not $simStopped) {
  Write-Dev 'No project processes found.'
} else {
  Write-Dev 'Development environment stopped.'
}

Report-PortStatus $script:GuiPort
Report-PortStatus $script:SimPort
Report-PortStatus $script:PlantPort
exit 0
