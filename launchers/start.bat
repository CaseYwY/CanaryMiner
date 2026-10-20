@echo off
rem CanaryMiner, basic launcher. Edit YOUR_PRL_ADDRESS to your public PRL payout address, then double-click.
rem ".rig1" names this rig on the pool; change it or leave it. The window stays open after the miner stops.
rem GPU clocks, memory, power limit and fan: set them with MSI Afterburner (or a similar tool) before starting.
rem CanaryMiner does not change GPU clocks on Windows; it mines with whatever your tuning tool has applied.
@cd /d "%~dp0"
"%~dp0canary-miner.exe" --pool stratum+tcp://prl-us.kryptex.network:7048 --wallet YOUR_PRL_ADDRESS.rig1
pause
