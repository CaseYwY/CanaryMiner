# Register one current-user logon task for the miner installed beside deploy\.
[CmdletBinding()]
param(
    [Parameter(Mandatory)][ValidatePattern('\A[A-Za-z0-9][A-Za-z0-9-]{0,39}\z')][string]$Name,
    [Parameter(Mandatory)][string]$Seed,
    [ValidateRange(1, 86400)][int]$Seconds = 300,
    [switch]$ConfirmElectricityImport,
    [switch]$AllowPublicRates,
    [switch]$WatchInvoices,
    [string]$AccountCapturePython
)

$ErrorActionPreference = 'Stop'
$archive = Get-Item -LiteralPath $Seed -Force
$miner = Get-Item -LiteralPath (Join-Path $PSScriptRoot '..\bin\canary-miner.exe') -Force
$inputs = @($archive, $miner)
if ($AccountCapturePython) {
    if ($ConfirmElectricityImport -or $AllowPublicRates -or $WatchInvoices -or $Seconds -lt 60) {
        throw 'Account capture requires a 60 to 86400 second interval and its own task.'
    }
    $python = Get-Item -LiteralPath $AccountCapturePython -Force
    if ($python.Name -ine 'pythonw.exe') { throw 'Choose the installed pythonw.exe interpreter.' }
    $capture = Get-Item -LiteralPath (Join-Path $PSScriptRoot '..\tools\capture_octopus_consumption.py') -Force
    $credential = Get-Item -LiteralPath (Join-Path $archive.DirectoryName '.canary-octopus-capture.cred') -Force
    $inputs += @($python, $capture, $credential)
}
foreach ($file in $inputs) {
    if ($file -isnot [System.IO.FileInfo] -or
        ($file.Attributes -band [System.IO.FileAttributes]::ReparsePoint)) {
        throw 'The seed and installed miner must be regular files.'
    }
    # Task Scheduler expands %variables% in an Exec action.
    if ($file.FullName.Contains('%')) {
        throw 'Task Scheduler paths must not contain a percent sign.'
    }
    $root = $file.Directory.Root.FullName
    if ($root -notmatch '\A[A-Za-z]:\\\z' -or $file.FullName.Substring(2).Contains(':')) {
        throw 'The billing workspace and installation must be on local drives.'
    }
    $drive = [System.IO.DriveInfo]::new($root)
    if ($drive.DriveType -notin @('Fixed', 'Removable', 'Ram')) {
        throw 'The billing workspace and installation must be on local drives.'
    }
    for ($directory = $file.Directory; $directory.Parent; $directory = $directory.Parent) {
        if ($directory.Attributes -band [System.IO.FileAttributes]::ReparsePoint) {
            throw 'The billing workspace and installation must use paths without junctions or symbolic links.'
        }
    }
}

$account = [System.Security.Principal.WindowsIdentity]::GetCurrent()
$sid = $account.User.Value
$taskType = if ($AccountCapturePython) { 'OctopusCapture' } else { 'Billing' }
$taskName = "CanaryMiner-$taskType-$Name-$sid"
$scheduler = New-Object -ComObject 'Schedule.Service'
$scheduler.Connect()
$task = $scheduler.NewTask(0)
$task.RegistrationInfo.Description = if ($AccountCapturePython) {
    'CanaryMiner scheduled Octopus import readings'
} else { 'CanaryMiner recurring Octopus billing review' }
$task.Principal.UserId = $sid
$task.Principal.LogonType = 3 # TASK_LOGON_INTERACTIVE_TOKEN
$task.Principal.RunLevel = 0  # TASK_RUNLEVEL_LUA
$task.Settings.MultipleInstances = 2 # TASK_INSTANCES_IGNORE_NEW
$task.Settings.ExecutionTimeLimit = 'PT0S'
$task.Settings.DisallowStartIfOnBatteries = $false
$task.Settings.StopIfGoingOnBatteries = $false

$trigger = $task.Triggers.Create(9) # TASK_TRIGGER_LOGON
$trigger.UserId = $sid
$action = $task.Actions.Create(0) # TASK_ACTION_EXEC
$action.WorkingDirectory = $archive.DirectoryName
$interval = $Seconds.ToString([Globalization.CultureInfo]::InvariantCulture)
if ($AccountCapturePython) {
    $action.Path = $python.FullName
    $action.Arguments = '-I -B "{0}" --confirm-electricity-import --run-scheduled "{1}" --watch-every-seconds {2}' -f $capture.FullName, $archive.FullName, $interval
} else {
    $action.Path = $miner.FullName
    $action.Arguments = 'meter-bill --roll-forward "{0}" --watch-every-seconds {1} --watch-quiet' -f $archive.FullName, $interval
    if ($ConfirmElectricityImport) { $action.Arguments += ' --confirm-electricity-import' }
    if ($AllowPublicRates) { $action.Arguments += ' --allow-public-rates' }
    if ($WatchInvoices) { $action.Arguments += ' --watch-invoices' }
}

# TASK_CREATE refuses a collision even if another process registers it first.
$folder = $scheduler.GetFolder('\')
$null = $folder.RegisterTaskDefinition($taskName, $task, 2, $sid, $null, 3, $null)
$taskName
