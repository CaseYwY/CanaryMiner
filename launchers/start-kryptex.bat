@echo off
rem CanaryMiner on Kryptex. Edit YOUR_PRL_ADDRESS to your public PRL payout address, then double-click.
rem GPU clocks, memory, power limit and fan: set them with MSI Afterburner (or a similar tool) before starting.
rem CanaryMiner does not change GPU clocks on Windows; it mines with whatever your tuning tool has applied.
@cd /d "%~dp0"
"%~dp0canary-miner.exe" --pool kryptex --wallet YOUR_PRL_ADDRESS.rig1
pause
