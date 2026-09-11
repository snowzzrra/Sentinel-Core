param(
    [Parameter(Mandatory=$true)][ValidateSet('RUN','PREPARE','EXPORT')][string]$Stage,
    [Parameter(Mandatory=$true)][string]$Config,
    [ValidateSet('not_observed','unchanged','changed')][string]$Catalog = 'not_observed',
    [ValidateSet('not_observed','unchanged','changed')][string]$Selection = 'not_observed',
    [ValidateSet('not_performed','restored_pair','removed_test_pair')][string]$Rollback = 'not_performed',
    [string]$Note = '',
    [switch]$ComparisonCancelled
)
$ErrorActionPreference = 'Stop'
if ($PSVersionTable.PSVersion.Major -lt 7) { throw 'Use the configured PowerShell 7 executable.' }
$resolved = Get-Content -LiteralPath $Config -Raw | ConvertFrom-Json
$arguments = @('-B', (Join-Path $PSScriptRoot 'retest_milestone_a.py'), '--config', $Config, $Stage,
    '--catalog', $Catalog, '--selection', $Selection, '--rollback', $Rollback, '--note', $Note)
if ($ComparisonCancelled) { $arguments += '--comparison-cancelled' }
& $resolved.Python @arguments
exit $LASTEXITCODE
