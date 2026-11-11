"""Structural live-path/build boundaries only; native/PTY tests supply the behavioural evidence."""
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[1]


class PreferencesLiveArtifactTests(unittest.TestCase):
    def test_prerun_inspection_is_read_only_and_original_epoch_reaches_device_admission(self):
        source = (ROOT / 'src/product/prerun_preferences.h').read_text(encoding='utf8')
        for forbidden in ('PreferencesStore', 'MarketStore', 'DeclarationStore', 'owner->save(', 'getenv(', 'argv[', 'canonical(', 'exists('):
            self.assertNotIn(forbidden, source)
        self.assertIn('owner->load(kinds[phase])', source)
        self.assertIn('owner->takeReply(ticket)', source)
        self.assertLess(source.index('if(stop&&*stop)return'), source.index('selfExecutablePath()'))
        main = (ROOT / 'src/core/miner.cu').read_text(encoding='utf8')
        for synchronous in ('RuntimePreferencesStore', 'GamePreferencesStore', 'PoolSecurityPreferencesStore', 'LanguagePreferencesStore'):
            self.assertNotIn(synchronous, main)
        self.assertIn('preRunScope.emplace(preRunNativeEpoch)', main)
        self.assertIn('NativeProcessScope originalPreRun(inspected.nativeEpoch)', main)
        device = main.index('// ---- devices: identity')
        self.assertLess(main.index('if(preRunNativeEpoch&&!nativeProcessEpochCurrent(preRunNativeEpoch))', device), main.index('if (!queryDevices(&present))', device))

    def test_idle_command_has_only_background_preference_io_and_fixed_module_binding(self):
        source = (ROOT / 'src/product/settings_command.h').read_text(encoding='utf8')
        for forbidden in ('PreferencesStore', 'MarketStore', 'DeclarationStore', '.save(', 'openRuntimeControls(', 'openManualMarketControls(', 'openElectricityControls(', 'openTariffControls(', 'openGameControls(', 'openPoolSecurityControls(', 'openLanguageControls(', 'getenv(', 'exists(', 'canonical('):
            self.assertNotIn(forbidden, source)
        self.assertIn('preferences.load(PreferencesKind::Ui)', source)
        self.assertIn('preferences.key(', source)
        self.assertIn('preferences.poll()', source)
        self.assertIn('selfExecutablePath()', source)
        self.assertLess(source.index('RequiresTerminal'), source.index('selfExecutablePath()'))
        self.assertLess(source.index('owner->requestStop()'), source.index('owner->stop()'))
        self.assertNotIn('runSettingsCommand', (ROOT / 'src/product/settings.h').read_text(encoding='utf8'))

    def test_live_supervisor_has_no_store_callback_or_synchronous_open_fallback(self):
        source = (ROOT / 'src/product/supervisor.h').read_text(encoding='utf8')
        live = source[source.index('for (;;) {', source.index('const auto latchNativeFault=')):source.index('const bool stopRequested = quit')]
        self.assertIn('preferencesSettings_->key(', live)
        self.assertIn('openPreferencesSettings()', live)
        for name in ('uiStore_', 'runtimeStore_', 'manualStore_', 'electricityStore_', 'tariffStore_', 'gameStore_', 'poolSecurityStore_', 'languageStore_', 'openRuntimeControls(', 'openManualMarketControls(', 'openElectricityControls(', 'openTariffControls(', 'openGameControls(', 'openPoolSecurityControls(', 'openLanguageControls('):
            self.assertNotIn(name, source)
        self.assertNotIn('Store', live)

    def test_bootstrap_reads_share_the_owner_without_a_synchronous_initial_store(self):
        source = (ROOT / 'src/product/supervisor.h').read_text(encoding='utf8')
        for store in ('UiPreferencesStore', 'ManualMarketStore', 'ElectricityPreferencesStore'):
            self.assertNotIn(store, source)
        self.assertIn('preferences_->takeReply(bootstrapTicket_)', source)
        self.assertIn('!alive && !pending && !bootstrapPending_', source)
        self.assertIn('if(!sessionActivated_)', source)
        self.assertIn('settingsRequested_=true', source)
        self.assertLess(source.index('const bool stopRequested = quit'), source.index('if(!activateSession(t0,nativeEpoch))'))

    def test_native_optional_join_follows_worker_drain_and_actual_module_only(self):
        source = (ROOT / 'src/product/supervisor.h').read_text(encoding='utf8')
        stopping = source[source.index('void beginStop()'):source.index('void onExit(')]
        self.assertIn('preferences_->requestStop()', stopping)
        self.assertNotIn('preferences_->stop()', stopping)
        self.assertLess(source.index('fleet_->stop();', source.index('if (history_) history_->stop();')), source.index('preferences_->stop();'))
        binding = source[source.index('void startPreferences()'):source.index('void updatePreferences()')]
        self.assertIn('selfExecutablePath()', binding)
        self.assertNotIn('opt_.exe', binding)
        for discovery in ('getenv', 'exists(', 'canonical(', 'argv'):
            self.assertNotIn(discovery, binding)

    def test_parent_build_does_not_link_helper_disk_service(self):
        source = (ROOT / 'cmake/CanaryPreferences.cmake').read_text(encoding='utf8')
        owner = source[source.index('add_library(canary_preferences_owner'):source.index('add_executable(canary_preferences_worker')]
        self.assertNotIn('canary_preferences_service', owner)
        ordinary = (ROOT / 'CMakeLists.txt').read_text(encoding='utf8')
        self.assertIn('canary_economics canary_preferences_owner', ordinary)
        self.assertIn('install(TARGETS canaryminer canary_preferences_worker', ordinary)

    def test_fixture_transport_faults_are_absent_from_ordinary_units(self):
        for name in ('preferences_owner.cpp', 'preferences_protocol.cpp', 'preferences_service.cpp', 'preferences_worker.cpp'):
            source = (ROOT / 'src/product' / name).read_text(encoding='utf8')
            for marker in ('CANARY_PREFERENCES_FIXTURE_MODE', 'fixture-lease-no-ack', 'getenv('):
                self.assertNotIn(marker, source)


if __name__ == '__main__':
    unittest.main()
