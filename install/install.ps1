# Installs the file assistant on a Windows machine: copies the built
# executables, the MCP server, and a config.json you supply alongside this
# script, then registers the email-watcher scheduled task and the MCP
# server entry in Claude Desktop. Re-running this script with the same
# config.json on a new machine fully reproduces the setup.
#
# Usage:
#   .\install.ps1 -SourceDir .\dist -ConfigPath .\config.json
#
# -SourceDir must contain: ui.exe, email-watcher.exe, core-cli.exe,
# lang\, assets\docx-guide\, and the src\mcp-server contents.

param(
    [string]$SourceDir = ".",
    [string]$ConfigPath = ".\config.json",
    [string]$InstallDir = "$env:LOCALAPPDATA\TechHelp"
)

$ErrorActionPreference = "Stop"

if (-not (Test-Path $ConfigPath)) {
    throw "config.json not found at '$ConfigPath'. Copy install/config.example.json, fill it in, and pass -ConfigPath."
}

Write-Host "Installing to $InstallDir"
New-Item -ItemType Directory -Force -Path $InstallDir | Out-Null
New-Item -ItemType Directory -Force -Path "$InstallDir\mcp-server" | Out-Null
New-Item -ItemType Directory -Force -Path "$InstallDir\lang" | Out-Null
New-Item -ItemType Directory -Force -Path "$InstallDir\assets\docx-guide" | Out-Null

foreach ($exe in @("ui.exe", "email-watcher.exe", "core-cli.exe")) {
    $src = Join-Path $SourceDir $exe
    if (Test-Path $src) {
        Copy-Item $src -Destination $InstallDir -Force
    } else {
        Write-Warning "$exe not found in $SourceDir - skipping"
    }
}

Copy-Item (Join-Path $SourceDir "lang\*") -Destination "$InstallDir\lang" -Recurse -Force -ErrorAction SilentlyContinue
Copy-Item (Join-Path $SourceDir "assets\docx-guide\*") -Destination "$InstallDir\assets\docx-guide" -Recurse -Force -ErrorAction SilentlyContinue

$mcpServerSrc = Join-Path $SourceDir "mcp-server"
if (Test-Path $mcpServerSrc) {
    Copy-Item "$mcpServerSrc\*" -Destination "$InstallDir\mcp-server" -Recurse -Force
}

Copy-Item $ConfigPath -Destination "$InstallDir\config.json" -Force
Copy-Item $ConfigPath -Destination "$InstallDir\mcp-server\config.json" -Force

Write-Host "Setting up the MCP server's Python virtual environment..."
Push-Location "$InstallDir\mcp-server"
python -m venv venv
& ".\venv\Scripts\pip.exe" install -r requirements.txt
Pop-Location

Write-Host "Registering the email watcher scheduled task (every 30 minutes)..."
$action = New-ScheduledTaskAction -Execute "$InstallDir\email-watcher.exe" -WorkingDirectory $InstallDir
$trigger = New-ScheduledTaskTrigger -Once -At (Get-Date) -RepetitionInterval (New-TimeSpan -Minutes 30) -RepetitionDuration ([TimeSpan]::MaxValue)
$settings = New-ScheduledTaskSettingsSet -AllowStartIfOnBatteries -DontStopIfGoingOnBatteries -StartWhenAvailable
Register-ScheduledTask -TaskName "TechHelpEmailWatcher" -Action $action -Trigger $trigger -Settings $settings -Force | Out-Null

Write-Host "Registering the MCP server with Claude Desktop..."
$claudeConfigPath = "$env:APPDATA\Claude\claude_desktop_config.json"
$claudeConfig = @{}
if (Test-Path $claudeConfigPath) {
    $claudeConfig = Get-Content $claudeConfigPath -Raw | ConvertFrom-Json -AsHashtable
}
if (-not $claudeConfig.ContainsKey("mcpServers")) {
    $claudeConfig["mcpServers"] = @{}
}
$claudeConfig["mcpServers"]["tech-help"] = @{
    command = "$InstallDir\mcp-server\venv\Scripts\python.exe"
    args    = @("$InstallDir\mcp-server\server.py")
    env     = @{
        TECH_HELP_CONFIG  = "$InstallDir\mcp-server\config.json"
        TECH_HELP_CORE_CLI = "$InstallDir\core-cli.exe"
    }
}
New-Item -ItemType Directory -Force -Path (Split-Path $claudeConfigPath) | Out-Null
$claudeConfig | ConvertTo-Json -Depth 10 | Set-Content $claudeConfigPath

Write-Host ""
Write-Host "Install complete."
Write-Host "Remaining manual steps:"
Write-Host "  - In Claude Desktop > Settings > Cowork, grant access to Desktop, Downloads, and Documents only."
Write-Host "  - Set the Cowork model to Haiku with extended thinking off."
Write-Host "  - Upload skills/conversion-skill/SKILL.md and skills/daily-sync-skill/SKILL.md via Customize > Skills."
Write-Host "  - Schedule the daily-sync-skill pass at 9:00 AM via Cowork's /schedule feature."
Write-Host "  - If summarize_file will be used on images, install the Tesseract OCR engine separately (see src/mcp-server/README.md)."
Write-Host "  - Restart Claude Desktop so it picks up the new MCP server."
