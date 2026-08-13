# Compatibility alias (M14.1): npm run dev:start -> start-all
& (Join-Path $PSScriptRoot 'dev-start-all.ps1')
exit $LASTEXITCODE
