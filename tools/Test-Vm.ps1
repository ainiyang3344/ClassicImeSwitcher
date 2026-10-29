#requires -version 7.0
[CmdletBinding()]
param(
    [ValidateSet('All','Smoke','Management','Switch','External')][string]$Mode = 'All',
    [string]$ComputerName = '10.0.0.6',
    [string]$CredentialPath = 'C:\Users\yang\.codex\vm-10.0.0.5.credential.xml'
)
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path $PSScriptRoot -Parent
$applicationDirectory = Join-Path $projectRoot 'out\app'
if (-not (Test-Path -LiteralPath (Join-Path $applicationDirectory 'bin\ClassicImeSwitcher.exe'))) { throw 'Build and deploy the app first.' }
$runId = Get-Date -Format 'yyyyMMdd-HHmmss'
$localResults = Join-Path $projectRoot "out\checks\vm-$runId"
[void][IO.Directory]::CreateDirectory($localResults)
$package = Join-Path $projectRoot "out\vm-package-$runId.zip"
Compress-Archive -Path "$applicationDirectory\*" -DestinationPath $package -CompressionLevel Fastest
$vmCredential = Import-Clixml -LiteralPath $CredentialPath
$vmOption = New-PSSessionOption -OpenTimeout 6000 -OperationTimeout 30000
$vmSession = New-PSSession -ComputerName $ComputerName -Credential $vmCredential -SessionOption $vmOption
$activeTasks = [Collections.Generic.List[string]]::new()
try {
    $target = Invoke-Command -Session $vmSession -ArgumentList $runId -ScriptBlock {
        param($runId)
        $identity = [Security.Principal.WindowsIdentity]::GetCurrent()
        $principal = [Security.Principal.WindowsPrincipal]::new($identity)
        if ($env:COMPUTERNAME -ne 'DESKTOP-KUOPJ45' -or $identity.Name -ne 'DESKTOP-KUOPJ45\codexvm' -or -not $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) { throw 'VM management identity mismatch.' }
        $account = (Get-CimInstance Win32_ComputerSystem).UserName
        if (-not $account) { throw 'No desktop user is logged on.' }
        $sid = ([Security.Principal.NTAccount]::new($account)).Translate([Security.Principal.SecurityIdentifier]).Value
        $profile = Get-CimInstance Win32_UserProfile | Where-Object SID -eq $sid
        $folder = Join-Path $profile.LocalPath "AppData\Local\ClassicImeSwitcher-Validation\$runId"
        [void][IO.Directory]::CreateDirectory($folder)
        [pscustomobject]@{ Account=$account; Sid=$sid; Folder=$folder }
    }
    Copy-Item -LiteralPath $package -Destination (Join-Path $target.Folder 'app.zip') -ToSession $vmSession
    Invoke-Command -Session $vmSession -ArgumentList $target.Folder -ScriptBlock {
        param($folder)
        Expand-Archive -LiteralPath (Join-Path $folder 'app.zip') -DestinationPath (Join-Path $folder 'app')
    }
    $modes = if ($Mode -eq 'All') { @('Smoke','Management','Switch','External') } else { @($Mode) }
    foreach ($testMode in $modes) {
        $taskName = 'LYLab-ClassicIme-Test-' + [guid]::NewGuid().ToString('N')
        $activeTasks.Add($taskName)
        $remoteResults = Join-Path $target.Folder $testMode
        $flag = switch ($testMode) { 'Smoke' { '--smoke-test' }; 'Management' { '--verify-management' }; 'Switch' { '--verify-switch' }; 'External' { '--verify-external' } }
        Write-Output "VM $testMode test started ($($target.Account))."
        Invoke-Command -Session $vmSession -ArgumentList $taskName,$target.Account,$target.Folder,$remoteResults,$flag -ScriptBlock {
            param($taskName,$account,$folder,$output,$flag)
            [void][IO.Directory]::CreateDirectory($output)
            $executable = Join-Path $folder 'app\bin\ClassicImeSwitcher.exe'
            $action = New-ScheduledTaskAction -Execute $executable -Argument ($flag+' "'+$output+'"') -WorkingDirectory (Split-Path $executable -Parent)
            $principal = New-ScheduledTaskPrincipal -UserId $account -LogonType Interactive -RunLevel Highest
            $settings = New-ScheduledTaskSettingsSet -ExecutionTimeLimit (New-TimeSpan -Minutes 1) -AllowStartIfOnBatteries -DontStopIfGoingOnBatteries
            Register-ScheduledTask -TaskName $taskName -Action $action -Principal $principal -Settings $settings | Out-Null
            Start-ScheduledTask -TaskName $taskName
        }
        $watch = [Diagnostics.Stopwatch]::StartNew()
        do {
            Start-Sleep -Milliseconds 500
            $status = Invoke-Command -Session $vmSession -ArgumentList $taskName,$remoteResults -ScriptBlock {
                param($taskName,$output)
                [pscustomobject]@{ State=[string](Get-ScheduledTask -TaskName $taskName).State; Result=(Get-ScheduledTaskInfo -TaskName $taskName).LastTaskResult; HasReport=(Test-Path -LiteralPath (Join-Path $output 'report.json')) }
            }
            if ($status.State -ne 'Running' -and ($status.HasReport -or ($status.Result -ne 267011 -and $watch.Elapsed.TotalSeconds -gt 4))) { break }
        } while ($watch.Elapsed.TotalSeconds -lt 40)
        $destination = Join-Path $localResults $testMode
        Copy-Item -LiteralPath $remoteResults -Destination $destination -Recurse -FromSession $vmSession
        $reportPath = Join-Path $destination 'report.json'
        if (-not (Test-Path -LiteralPath $reportPath)) { throw "VM $testMode did not write a report. Task result: $($status.Result). See $destination" }
        $report = Get-Content -LiteralPath $reportPath -Raw -Encoding utf8 | ConvertFrom-Json
        [pscustomobject]@{ Mode=$testMode; Passed=$report.passed; TaskResult=$status.Result; UserSid=$report.userSid; Error=$report.error; Report=$reportPath } | ConvertTo-Json -Compress
        if (-not $report.passed -or $status.Result -ne 0) { throw "VM $testMode failed. See $reportPath" }
    }
    [pscustomobject]@{ ComputerName=$ComputerName; User=$target.Account; RemoteFolder=$target.Folder; LocalResults=$localResults } | ConvertTo-Json
} finally {
    try {
        Invoke-Command -Session $vmSession -ArgumentList (,$activeTasks.ToArray()) -ScriptBlock {
            param($taskNames)
            foreach ($taskName in $taskNames) {
                $task = Get-ScheduledTask -TaskName $taskName -ErrorAction SilentlyContinue
                if ($task) {
                    if ($task.State -eq 'Running') { Stop-ScheduledTask -TaskName $taskName }
                    Unregister-ScheduledTask -TaskName $taskName -Confirm:$false
                }
            }
        }
    } finally { Remove-PSSession $vmSession }
}
