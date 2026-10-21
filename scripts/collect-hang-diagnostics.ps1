param(
    [Parameter(Mandatory = $true)]
    [ValidateRange(1, 2147483647)]
    [int]$ProcessId,
    [string]$JdkHome = $env:JAVA_HOME,
    [string]$OutputDirectory = (Join-Path (Get-Location) 'hang-diagnostics')
)

$ErrorActionPreference = 'Stop'
$game = Get-Process -Id $ProcessId
if ($game.ProcessName -notin @('java', 'javaw')) {
    throw 'The selected process is not Java. Use the Minecraft process ID from its launcher console.'
}

$jcmd = if ($JdkHome) { Join-Path $JdkHome 'bin/jcmd.exe' }
if (-not $jcmd -or -not (Test-Path -LiteralPath $jcmd)) {
    $jcmd = (Get-Command jcmd.exe -ErrorAction SilentlyContinue).Source
}
if (-not $jcmd) {
    throw 'A JDK with jcmd.exe is required. Specify -JdkHome with its installation directory.'
}

New-Item -ItemType Directory -Path $OutputDirectory -Force | Out-Null
$prefix = "minecraft-$ProcessId-$(Get-Date -Format yyyyMMdd-HHmmss)"
$game | Select-Object Id, ProcessName, Responding, CPU, WorkingSet64, StartTime |
    ConvertTo-Json | Set-Content -LiteralPath (Join-Path $OutputDirectory "$prefix-process.json") -Encoding utf8
for ($sample = 1; $sample -le 3; ++$sample) {
    & $jcmd $ProcessId Thread.print -l 2>&1 |
        Set-Content -LiteralPath (Join-Path $OutputDirectory "$prefix-threads-$sample.txt") -Encoding utf8
    if ($LASTEXITCODE -ne 0) {
        throw 'Thread dump failed. Keep the game open and use a JDK matching its Java version.'
    }
    if ($sample -lt 3) { Start-Sleep -Seconds 2 }
}
Write-Output 'Saved process information and three thread dumps to the diagnostics directory.'
