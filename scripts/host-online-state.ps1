# Dot-sourced by host-online.ps1: the state file that records every mapping -Start asks for, and the
# scheduled task that runs -Stop when the lease ends. Uses the constants host-online.ps1 defines first.

function Read-HostState {
    if (-not (Test-Path -LiteralPath $StatePath)) { return }
    $text = [IO.File]::ReadAllText($StatePath)
    if (-not $text.Trim()) { return }
    foreach ($entry in @(ConvertFrom-Json $text)) { $entry }
}

function Save-HostState($Entries) {
    $list = @($Entries)
    if (-not $list) { Remove-Item -LiteralPath $StatePath -Force -ErrorAction SilentlyContinue; return }
    New-Item -ItemType Directory -Force -Path $StateDir | Out-Null
    [IO.File]::WriteAllText($StatePath, (ConvertTo-Json -InputObject $list -Depth 3))
}

function Get-StateKey($Entry) { '{0}|{1}' -f $Entry.ControlUrl, $Entry.Port }

function New-StateEntry($Router, [int]$Port) {
    [pscustomobject]@{ Router = $Router.Address; ServiceType = $Router.Igd.ServiceType; ControlUrl = $Router.Igd.ControlUrl; Port = $Port; InternalClient = $Router.InternalClient }
}

function Add-StateEntry($Entry) {
    $others = @(Read-HostState | Where-Object { (Get-StateKey $_) -ne (Get-StateKey $Entry) })
    Save-HostState (@($others) + $Entry)
}

function Remove-StateEntry($Entry) {
    Save-HostState @(Read-HostState | Where-Object { (Get-StateKey $_) -ne (Get-StateKey $Entry) })
}

function ConvertTo-XmlText([string]$Text) { [Security.SecurityElement]::Escape($Text) }

function New-StopTaskXml([datetime]$At) {
    $sid = [Security.Principal.WindowsIdentity]::GetCurrent().User.Value
    $arguments = "--headless powershell.exe -NoProfile -ExecutionPolicy Bypass -File `"$ScriptPath`" -Stop -LogFile `"$StopLogPath`""
    @"
<?xml version="1.0" encoding="UTF-16"?>
<Task version="1.2" xmlns="http://schemas.microsoft.com/windows/2004/02/mit/task">
  <RegistrationInfo><Description>Closes the gow2 System Link port mappings at the end of their lease (scripts\host-online.ps1 -Stop).</Description></RegistrationInfo>
  <Triggers><TimeTrigger><StartBoundary>$($At.ToString('s'))</StartBoundary><EndBoundary>$($At.AddDays($StopTaskExpiryDays).ToString('s'))</EndBoundary><Enabled>true</Enabled></TimeTrigger></Triggers>
  <Principals><Principal id="Author"><UserId>$sid</UserId><LogonType>InteractiveToken</LogonType><RunLevel>LeastPrivilege</RunLevel></Principal></Principals>
  <Settings>
    <MultipleInstancesPolicy>IgnoreNew</MultipleInstancesPolicy>
    <DisallowStartIfOnBatteries>false</DisallowStartIfOnBatteries>
    <StopIfGoingOnBatteries>false</StopIfGoingOnBatteries>
    <StartWhenAvailable>true</StartWhenAvailable>
    <ExecutionTimeLimit>PT10M</ExecutionTimeLimit>
    <DeleteExpiredTaskAfter>PT0S</DeleteExpiredTaskAfter>
    <Enabled>true</Enabled>
  </Settings>
  <Actions Context="Author"><Exec><Command>conhost.exe</Command><Arguments>$(ConvertTo-XmlText $arguments)</Arguments></Exec></Actions>
</Task>
"@
}

function Invoke-Schtasks([string[]]$Arguments) {
    $ErrorActionPreference = 'Continue'  # 5.1 turns a native command's stderr into terminating errors under 'Stop'
    $output = & schtasks.exe @Arguments 2>&1 | ForEach-Object { "$_" }
    [pscustomobject]@{ ExitCode = $LASTEXITCODE; Output = ($output -join ' ').Trim() }
}

function Register-StopTask([datetime]$At) {
    New-Item -ItemType Directory -Force -Path $StateDir | Out-Null
    $xmlPath = Join-Path $StateDir 'host-online-stop-task.xml'
    [IO.File]::WriteAllText($xmlPath, (New-StopTaskXml $At), [Text.Encoding]::Unicode)
    try { $result = Invoke-Schtasks @('/Create', '/XML', $xmlPath, '/TN', $StopTaskName, '/F') }
    finally { Remove-Item -LiteralPath $xmlPath -Force -ErrorAction SilentlyContinue }
    if ($result.ExitCode -ne 0) { throw "schtasks /Create answered $($result.ExitCode): $($result.Output)" }
}

function Register-StopTaskOrWarn([datetime]$At) {
    try { Register-StopTask $At; $true }
    catch { Write-Warn "Could not schedule the automatic -Stop: $($_.Exception.Message) Run -Stop yourself when the session ends."; $false }
}

function Test-StopTask { (Invoke-Schtasks @('/Query', '/TN', $StopTaskName)).ExitCode -eq 0 }

function Unregister-StopTask {
    if (-not (Test-StopTask)) { return $false }
    $result = Invoke-Schtasks @('/Delete', '/TN', $StopTaskName, '/F')
    if ($result.ExitCode -ne 0) { Write-Warn "Could not delete the scheduled task '$StopTaskName': $($result.Output)"; return $false }
    Write-Note "deleted  the scheduled task '$StopTaskName'"
    $true
}

function Get-StopTaskTime {
    $result = Invoke-Schtasks @('/Query', '/TN', $StopTaskName, '/XML')
    if ($result.ExitCode -ne 0) { return $null }
    $match = [regex]::Match($result.Output, '<StartBoundary>([^<]+)</StartBoundary>')
    if ($match.Success) { return $match.Groups[1].Value }
    $null
}
