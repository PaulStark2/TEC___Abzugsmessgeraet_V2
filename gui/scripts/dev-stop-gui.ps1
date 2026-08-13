# M14.2 - Stop project GUI only. Does NOT stop simulation_protocol.

$ErrorActionPreference = 'Continue'
. (Join-Path $PSScriptRoot 'dev-common.ps1')

$stopped = Stop-ProjectGuiProcesses
if ($stopped) {
  Write-Dev 'GUI stopped.'
} else {
  Write-Dev 'No project GUI processes found.'
}
Report-PortStatus $script:GuiPort
exit 0
