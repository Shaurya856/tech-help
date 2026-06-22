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
# lang\, assets\docx-guide\, src\mcp-server\, and src\office\.

param(
    [string]$SourceDir = ".",
    [string]$ConfigPath = ".\config.json",
    [string]$InstallDir = "$env:LOCALAPPDATA\TechHelp"
)

$ErrorActionPreference = "Stop"

if (-not (Test-Path $ConfigPath)) {
    throw "config.json not found at '$ConfigPath'. Copy install/config.example.json, fill it in, and pass -ConfigPath."
}

# ── Read config to extract source folder paths for ACL setup ──────────────
$config = Get-Content $ConfigPath -Raw | ConvertFrom-Json
$grantedFolders = @()
if ($config.PSObject.Properties.Name -contains "granted_folders") {
    $grantedFolders = $config.granted_folders
}
$indexDir = "$env:LOCALAPPDATA\FatherTechAssist\index"
$logsDir  = "$env:LOCALAPPDATA\FatherTechAssist\logs"

# ── Create directories ─────────────────────────────────────────────────────
Write-Host "Installing to $InstallDir"
foreach ($dir in @(
    $InstallDir,
    "$InstallDir\mcp-server",
    "$InstallDir\office",
    "$InstallDir\lang",
    "$InstallDir\assets\docx-guide",
    $indexDir,
    $logsDir
)) {
    New-Item -ItemType Directory -Force -Path $dir | Out-Null
}

# ── Copy executables ───────────────────────────────────────────────────────
foreach ($exe in @("ui.exe", "email-watcher.exe", "core-cli.exe")) {
    $src = Join-Path $SourceDir $exe
    if (Test-Path $src) {
        Copy-Item $src -Destination $InstallDir -Force
    } else {
        Write-Warning "$exe not found in $SourceDir - skipping"
    }
}

Copy-Item (Join-Path $SourceDir "lang\*")                   -Destination "$InstallDir\lang"              -Recurse -Force -ErrorAction SilentlyContinue
Copy-Item (Join-Path $SourceDir "assets\docx-guide\*")      -Destination "$InstallDir\assets\docx-guide" -Recurse -Force -ErrorAction SilentlyContinue

$mcpServerSrc = Join-Path $SourceDir "mcp-server"
if (Test-Path $mcpServerSrc) {
    Copy-Item "$mcpServerSrc\*" -Destination "$InstallDir\mcp-server" -Recurse -Force
}

$officeSrc = Join-Path $SourceDir "office"
if (Test-Path $officeSrc) {
    Copy-Item "$officeSrc\*" -Destination "$InstallDir\office" -Recurse -Force
}

Copy-Item $ConfigPath -Destination "$InstallDir\config.json"             -Force
Copy-Item $ConfigPath -Destination "$InstallDir\mcp-server\config.json"  -Force

# ── Python virtual environments ────────────────────────────────────────────
Write-Host "Setting up MCP server Python environment..."
Push-Location "$InstallDir\mcp-server"
python -m venv venv
& ".\venv\Scripts\pip.exe" install -r requirements.txt
Pop-Location

Write-Host "Setting up Office conversion Python environment..."
Push-Location "$InstallDir\office"
python -m venv venv
& ".\venv\Scripts\pip.exe" install -r requirements.txt
Pop-Location

# ── Patch config.json with resolved python_exe and office_convert_script ──
$pyExe    = "$InstallDir\office\venv\Scripts\python.exe"
$convScript = "$InstallDir\office\convert.py"
$cfgObj = Get-Content "$InstallDir\config.json" -Raw | ConvertFrom-Json
$cfgObj | Add-Member -Force -NotePropertyName "python_exe"              -NotePropertyValue $pyExe
$cfgObj | Add-Member -Force -NotePropertyName "office_convert_script"   -NotePropertyValue $convScript
$cfgObj | ConvertTo-Json -Depth 10 | Set-Content "$InstallDir\config.json"
# MCP server config doesn't need office paths — only the email watcher does.

# ── Scheduled task: email watcher every 30 minutes ────────────────────────
Write-Host "Registering the email watcher scheduled task (every 30 minutes)..."
$action   = New-ScheduledTaskAction -Execute "$InstallDir\email-watcher.exe" -WorkingDirectory $InstallDir
$trigger  = New-ScheduledTaskTrigger -Once -At (Get-Date) -RepetitionInterval (New-TimeSpan -Minutes 30) -RepetitionDuration ([TimeSpan]::MaxValue)
$settings = New-ScheduledTaskSettingsSet -AllowStartIfOnBatteries -DontStopIfGoingOnBatteries -StartWhenAvailable
Register-ScheduledTask -TaskName "TechHelpEmailWatcher" -Action $action -Trigger $trigger -Settings $settings -Force | Out-Null

# ── Register MCP server with Claude Desktop ────────────────────────────────
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
        TECH_HELP_CONFIG   = "$InstallDir\mcp-server\config.json"
        TECH_HELP_CORE_CLI = "$InstallDir\core-cli.exe"
    }
}
New-Item -ItemType Directory -Force -Path (Split-Path $claudeConfigPath) | Out-Null
$claudeConfig | ConvertTo-Json -Depth 10 | Set-Content $claudeConfigPath

# ── OS-level ACLs for Hermes Agent ────────────────────────────────────────
# Hermes Agent runs as the current user but we restrict it to read-only on
# source folders and write-only on the index output folder using icacls.
# These ACLs apply to the folder paths stored in config.json.
Write-Host "Setting OS-level ACLs for Hermes Agent file boundaries..."

# Index output folder: current user full control (Hermes writes here).
# This is already the default, but make it explicit and remove inheritance
# from any broader deny rules.
if (Test-Path $indexDir) {
    & icacls $indexDir /grant "${env:USERNAME}:(OI)(CI)F" /T /Q 2>$null
}

# Source folders: mark with a deny-write ACL entry so accidental writes by
# Hermes are blocked at the OS level.
foreach ($folder in $grantedFolders) {
    if (Test-Path $folder) {
        # Deny write and delete for "Hermes" (running as current user).
        # In practice this removes write from the folder itself; sub-items
        # inherit. This is best-effort for a single-user machine where
        # running Hermes under a separate restricted account isn't practical.
        & icacls $folder /deny "${env:USERNAME}:(W,D,DC,WDAC,WO)" /T /Q 2>$null
        Write-Host "  Read-only ACL applied to: $folder"
    }
}

# Log directory: full control for the watcher process.
if (Test-Path $logsDir) {
    & icacls $logsDir /grant "${env:USERNAME}:(OI)(CI)F" /T /Q 2>$null
}

Write-Host ""
Write-Host "Install complete."
Write-Host "Remaining manual steps:"
Write-Host "  - In Claude Desktop > Settings > Cowork, grant access to Desktop, Downloads, and Documents only."
Write-Host "  - Set the Cowork model to Haiku with extended thinking off."
Write-Host "  - Upload skills/conversion-skill/SKILL.md and skills/daily-index-skill/SKILL.md via Customize > Skills."
Write-Host "  - Schedule the daily-sync-skill pass at 9:00 AM via Cowork's /schedule feature."
Write-Host "  - Restart Claude Desktop so it picks up the new MCP server."
Write-Host "  - NOTE: The deny-write ACL on source folders also blocks your own writes from Explorer."
Write-Host "    If you need to save files there normally, remove the deny rule:"
Write-Host "    icacls <folder> /remove:d %USERNAME% /T"
