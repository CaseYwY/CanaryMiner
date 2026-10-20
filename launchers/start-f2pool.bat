@echo off
rem CanaryMiner on F2Pool. Edit YOUR_F2POOL_ACCOUNT to your F2Pool account name, then double-click.
rem F2Pool logs in with your account name, not a prl1 address. Set your PRL payout address in your F2Pool account settings.
rem GPU clocks, memory, power limit and fan: set them with MSI Afterburner (or a similar tool) before starting.
rem CanaryMiner does not change GPU clocks on Windows; it mines with whatever your tuning tool has applied.
@cd /d "%~dp0"
"%~dp0canary-miner.exe" --pool f2pool --wallet YOUR_F2POOL_ACCOUNT.rig1
pause
