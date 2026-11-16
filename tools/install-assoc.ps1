param(
    [string]$Exe = "",
    [switch]$Remove
)

$ErrorActionPreference = "Stop"
$Ext = "HKCU:\Software\Classes\.kbl"
$Prog = "HKCU:\Software\Classes\Kbl.Script"

if ($Remove) {
    Remove-Item -Path $Prog -Recurse -Force -ErrorAction SilentlyContinue
    Remove-Item -Path $Ext -Force -ErrorAction SilentlyContinue
    Write-Host "Связь расширения .kbl с kbl.exe удалена."
    exit 0
}

if ([string]::IsNullOrWhiteSpace($Exe)) {
    $Exe = Join-Path (Split-Path -Parent $PSScriptRoot) "build\kbl.exe"
}
if (-not (Test-Path -LiteralPath $Exe)) {
    Write-Host "Не найден $Exe"
    Write-Host "Сначала соберите проект: build.bat"
    exit 1
}
$Exe = (Resolve-Path -LiteralPath $Exe).Path

New-Item -Path $Ext -Force | Out-Null
Set-Item -Path $Ext -Value "Kbl.Script"

New-Item -Path $Prog -Force | Out-Null
Set-Item -Path $Prog -Value "kbl script"
New-ItemProperty -Path $Prog -Name "FriendlyTypeName" -Value "kbl script" -PropertyType String -Force | Out-Null

New-Item -Path "$Prog\DefaultIcon" -Force | Out-Null
Set-Item -Path "$Prog\DefaultIcon" -Value "`"$Exe`",0"

New-Item -Path "$Prog\shell\open\command" -Force | Out-Null
Set-Item -Path "$Prog\shell\open\command" -Value "`"$Exe`" `"%1`""

Write-Host "Готово: файлы .kbl открываются через $Exe"
Write-Host "Если Проводник ещё не подхватил связь, перезапустите его или войдите заново."
