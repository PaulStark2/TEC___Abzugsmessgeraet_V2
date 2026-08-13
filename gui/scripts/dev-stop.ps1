# Compatibility alias (M14.1): npm run dev:stop -> stop-all
& (Join-Path $PSScriptRoot 'dev-stop-all.ps1')
exit $LASTEXITCODE
