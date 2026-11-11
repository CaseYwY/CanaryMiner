# Windows controller heartbeat repair

This patch repairs the optional local Windows automatic controller used alongside CanaryMiner. The base repository does not contain that controller. Apply this patch to an installation that already has `auto_controller.py`, `gpu-clock-helper.ps1`, `auto_economics.py`, and `safe_mining.py`.

The helper previously published `clock-status.json` every second with Windows PowerShell 5.1 `Move-Item -Force`. A concurrent controller read could fail with `FileNotFoundError` or `PermissionError`, ending the mining session. The patch uses the .NET Framework Visual Basic `MoveFile` overwrite operation, which calls Windows `MoveFileEx`, and retries transient sharing errors for up to about 0.5 seconds. The controller retries only missing-file and permission errors for the same bounded period.

An unreadable status file after the retry window still raises an error. Malformed JSON, another session, a failed acknowledgement, and a timestamp outside the existing 15-second validity window still fail the heartbeat check. The controller's existing shutdown path stops the worker and closes the dashboard.

The native replacement behavior is documented in Microsoft's implementation. [FileSystem source](https://github.com/microsoft/referencesource/blob/main/Microsoft.VisualBasic/runtime/msvbalib/FileIO/FileSystem.vb)

## Apply to an existing installation

End the current mining session, then run these commands from the repository root in an elevated PowerShell window.

```powershell
$install = Join-Path $env:ProgramFiles 'CanaryMiner'
$patch = (Resolve-Path '.\contrib\windows-auto-controller\clock-status-race.patch').Path
$backup = Join-Path $install ('backups\clock-status-race-' + (Get-Date -Format 'yyyyMMdd-HHmmss'))
git -c core.autocrlf=false -C $install apply --check $patch
if ($LASTEXITCODE) { throw 'The installed scripts do not match this patch.' }
New-Item -ItemType Directory -Path $backup -ErrorAction Stop | Out-Null
Copy-Item -LiteralPath (Join-Path $install 'auto_controller.py'), (Join-Path $install 'gpu-clock-helper.ps1') -Destination $backup -ErrorAction Stop
git -c core.autocrlf=false -C $install apply $patch
if ($LASTEXITCODE) { throw 'Patch application failed. Restore the saved scripts.' }
python -I -B .\contrib\windows-auto-controller\test_clock_status.py --install-dir $install
```

The tests load the installed controller and exercise its heartbeat check and the helper's actual `Write-Status` function against temporary files. They require Windows PowerShell 5.1 and Python 3.10 or later. They also run concurrent publications to detect intermittent failures.

To restore the saved scripts, end the mining session and copy `auto_controller.py` and `gpu-clock-helper.ps1` from the backup directory into the installation directory.
