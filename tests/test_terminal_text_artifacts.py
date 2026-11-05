"""Default-off/source/import boundaries only; not dependency authentication or terminal qualification."""
import argparse
import hashlib
from pathlib import Path
import re


def main(args):
    root = Path(__file__).resolve().parents[1]
    cmake = (root / 'cmake/CanaryTerminalText.cmake').read_text(encoding='utf-8')
    assert re.search(r'option\(CANARY_ENABLE_TERMINAL_ICU\s+"[^"]+"\s+OFF\)', cmake)
    assert all(marker not in cmake for marker in ('FetchContent', 'file(DOWNLOAD', 'ExternalProject', 'execute_process'))
    main_cmake = (root / 'CMakeLists.txt').read_text(encoding='utf-8')
    assert 'target_link_libraries(canaryminer PRIVATE canary_terminal_text)' not in main_cmake
    assert 'canary_localized_format' not in main_cmake
    assert 'canary_terminal_number' not in main_cmake
    assert 'canary_terminal_message' not in main_cmake
    assert 'canary_terminal_count' not in main_cmake
    assert 'canary_terminal_time' not in main_cmake
    assert 'canary_terminal_settings_message' not in main_cmake
    for source in ('src/core/miner.cu', 'src/product/dashboard.h', 'src/product/supervisor.h', 'src/product/settings.h', 'src/product/language_settings.h', 'src/product/runtime_settings.h', 'src/product/game_settings.h', 'src/product/pool_security_settings.h', 'src/product/manual_market_settings.h', 'src/product/electricity_settings.h', 'src/product/language_preferences.h', 'src/product/runtime_preferences.h', 'src/product/game_preferences.h', 'src/product/pool_security_preferences.h', 'src/product/manual_market_preferences.h', 'src/product/manual_market.cpp', 'src/product/electricity_preferences.h', 'src/product/electricity.cpp', 'src/product/energy_owner.cpp'):
        text = (root / source).read_text(encoding='utf-8')
        assert all(marker not in text for marker in ('layoutTerminalElectricityTariffMessage', 'wrapTerminalElectricityTariffMessage', 'renderTerminalElectricityTariffMessage', 'renderWrappedTerminalElectricityTariffMessage'))
        assert all(marker not in text for marker in ('layoutTerminalElectricityContextMessage', 'wrapTerminalElectricityContextMessage', 'renderTerminalElectricityContextMessage', 'renderWrappedTerminalElectricityContextMessage'))
        assert all(marker not in text for marker in ('layoutTerminalElectricityValueMessage', 'wrapTerminalElectricityValueMessage', 'renderTerminalElectricityValueMessage', 'renderWrappedTerminalElectricityValueMessage'))
        assert all(marker not in text for marker in ('layoutTerminalElectricityPanelMessage', 'wrapTerminalElectricityPanelMessage', 'renderTerminalElectricityPanelMessage', 'renderWrappedTerminalElectricityPanelMessage'))
        assert all(marker not in text for marker in ('layoutTerminalElectricityNoticeMessage', 'wrapTerminalElectricityNoticeMessage', 'renderTerminalElectricityNoticeMessage', 'renderWrappedTerminalElectricityNoticeMessage'))
        assert all(marker not in text for marker in ('layoutTerminalElectricityFieldMessage', 'wrapTerminalElectricityFieldMessage', 'renderTerminalElectricityFieldMessage', 'renderWrappedTerminalElectricityFieldMessage'))
        assert all(marker not in text for marker in ('layoutTerminalManualMarketContextMessage', 'wrapTerminalManualMarketContextMessage', 'renderTerminalManualMarketContextMessage', 'renderWrappedTerminalManualMarketContextMessage'))
        assert all(marker not in text for marker in ('layoutTerminalManualMarketValueMessage', 'wrapTerminalManualMarketValueMessage', 'renderTerminalManualMarketValueMessage', 'renderWrappedTerminalManualMarketValueMessage'))
        assert all(marker not in text for marker in ('layoutTerminalManualMarketPanelMessage', 'wrapTerminalManualMarketPanelMessage', 'renderTerminalManualMarketPanelMessage', 'renderWrappedTerminalManualMarketPanelMessage'))
        assert all(marker not in text for marker in ('layoutTerminalManualMarketNoticeMessage', 'wrapTerminalManualMarketNoticeMessage', 'renderTerminalManualMarketNoticeMessage', 'renderWrappedTerminalManualMarketNoticeMessage'))
        assert all(marker not in text for marker in ('layoutTerminalManualMarketFieldMessage', 'wrapTerminalManualMarketFieldMessage', 'renderTerminalManualMarketFieldMessage', 'renderWrappedTerminalManualMarketFieldMessage'))
        assert all(marker not in text for marker in ('layoutTerminalPoolRuleMessage', 'wrapTerminalPoolRuleMessage', 'renderTerminalPoolRuleMessage', 'renderWrappedTerminalPoolRuleMessage'))
        assert all(marker not in text for marker in ('layoutTerminalPoolValueMessage', 'wrapTerminalPoolValueMessage', 'renderTerminalPoolValueMessage', 'renderWrappedTerminalPoolValueMessage'))
        assert all(marker not in text for marker in ('layoutTerminalPoolPanelMessage', 'wrapTerminalPoolPanelMessage', 'renderTerminalPoolPanelMessage', 'renderWrappedTerminalPoolPanelMessage'))
        assert all(marker not in text for marker in ('layoutTerminalPoolNoticeMessage', 'wrapTerminalPoolNoticeMessage', 'renderTerminalPoolNoticeMessage', 'renderWrappedTerminalPoolNoticeMessage'))
        assert all(marker not in text for marker in ('layoutTerminalPoolFieldMessage', 'wrapTerminalPoolFieldMessage', 'renderTerminalPoolFieldMessage', 'renderWrappedTerminalPoolFieldMessage'))
        assert all(marker not in text for marker in ('layoutTerminalGameValueMessage', 'wrapTerminalGameValueMessage', 'renderTerminalGameValueMessage', 'renderWrappedTerminalGameValueMessage'))
        assert all(marker not in text for marker in ('layoutTerminalGamePanelMessage', 'wrapTerminalGamePanelMessage', 'renderTerminalGamePanelMessage', 'renderWrappedTerminalGamePanelMessage'))
        assert all(marker not in text for marker in ('layoutTerminalGameFieldMessage', 'wrapTerminalGameFieldMessage', 'renderTerminalGameFieldMessage', 'renderWrappedTerminalGameFieldMessage'))
        assert all(marker not in text for marker in ('layoutTerminalActivationMessage', 'wrapTerminalActivationMessage', 'renderTerminalActivationMessage', 'renderWrappedTerminalActivationMessage'))
        assert all(marker not in text for marker in ('layoutTerminalRuntimePanelMessage', 'wrapTerminalRuntimePanelMessage', 'renderTerminalRuntimePanelMessage', 'renderWrappedTerminalRuntimePanelMessage'))
        assert all(marker not in text for marker in ('layoutTerminalRuntimeFieldMessage', 'wrapTerminalRuntimeFieldMessage', 'renderTerminalRuntimeFieldMessage', 'renderWrappedTerminalRuntimeFieldMessage'))
        assert all(marker not in text for marker in ('layoutTerminalLanguagePanelMessage', 'wrapTerminalLanguagePanelMessage', 'renderTerminalLanguagePanelMessage', 'renderWrappedTerminalLanguagePanelMessage'))
        assert all(marker not in text for marker in ('layoutTerminalLanguageFieldMessage', 'wrapTerminalLanguageFieldMessage', 'renderTerminalLanguageFieldMessage', 'renderWrappedTerminalLanguageFieldMessage'))
        assert all(marker not in text for marker in ('layoutTerminalSettingsDiagnosticMessage', 'wrapTerminalSettingsDiagnosticMessage', 'renderTerminalSettingsDiagnosticMessage', 'renderWrappedTerminalSettingsDiagnosticMessage'))
        assert all(marker not in text for marker in ('layoutTerminalSettingsActionMessage', 'wrapTerminalSettingsActionMessage', 'renderTerminalSettingsActionMessage', 'renderWrappedTerminalSettingsActionMessage'))
        assert all(marker not in text for marker in ('layoutTerminalSettingsChangeMessage', 'wrapTerminalSettingsChangeMessage', 'renderTerminalSettingsChangeMessage', 'renderWrappedTerminalSettingsChangeMessage'))
        assert all(marker not in text for marker in ('layoutTerminalUiFieldMessage', 'wrapTerminalUiFieldMessage', 'renderTerminalUiFieldMessage', 'renderWrappedTerminalUiFieldMessage', 'layoutTerminalSettingsMessage', 'wrapTerminalSettingsMessage', 'renderTerminalSettingsMessage', 'renderWrappedTerminalSettingsMessage', 'formatTerminalTime', 'layoutTerminalCount', 'wrapTerminalCount', 'renderTerminalCount', 'renderWrappedTerminalCount', 'layoutTerminalMessage', 'wrapTerminalMessage', 'renderTerminalMessage', 'renderWrappedTerminalMessage', 'wrapTerminalTextRuns', 'padTerminalTextRuns', 'wrapTerminalSegments', 'padTerminalSegments', 'renderWrappedTerminalSegments', 'renderPaddedTerminalSegments', 'formatTerminalQuantity', 'layoutTerminalTextRuns', 'layoutTerminalSegments', 'renderTerminalSegments', 'formatPresentationNumber', 'layoutTerminalText', 'wrapTerminalText', 'padTerminalText', 'terminalGraphemeBoundaries', 'ubidi_', 'ubrk_'))
    time = (root / 'src/product/terminal_time.cpp').read_text(encoding='utf-8')
    assert all(marker not in time for marker in ('ucal_getNow', 'localtime', 'gmtime', 'getenv', 'setlocale', 'uloc_setDefault', 'configureCivilTimeData', 'udat_parse'))
    assert 'civilAt(request.clock,*request.utcMs)' in time and 'calendar=gregorian' in time
    style = (root / 'src/product/terminal_style.h').read_text(encoding='utf-8')
    source = (root / 'src/product/activation_source.h').read_bytes().decode('ascii')
    assert '#include "' not in source and 'activationEnglishText' in source
    preferences = (root / 'src/product/runtime_preferences.h').read_text(encoding='utf-8')
    assert '#include "activation_source.h"' in preferences
    assert all(marker not in preferences for marker in ('activation_messages.h', 'localization.h', 'activationLocalePacks', 'activationMessage('))
    pool_preferences = (root / 'src/product/pool_security_preferences.h').read_text(encoding='utf-8')
    assert all(marker not in pool_preferences for marker in ('pool_field_messages.h', 'pool_notice_messages.h', 'pool_panel_messages.h', 'pool_value_messages.h', 'pool_rule_messages.h', 'localization.h', 'poolFieldMessage(', 'poolNoticeMessage(', 'poolPanelMessage(', 'poolValueMessage(', 'poolRuleMessage('))
    for source in ('src/product/manual_market_preferences.h', 'src/product/manual_market.cpp'):
        text = (root / source).read_text(encoding='utf-8')
        assert all(marker not in text for marker in ('manual_market_field_messages.h', 'manual_market_notice_messages.h', 'manual_market_panel_messages.h', 'manual_market_value_messages.h', 'manual_market_context_messages.h', 'localization.h', 'manualMarketFieldMessage(', 'manualMarketNoticeMessage(', 'manualMarketPanelMessage(', 'manualMarketValueMessage(', 'manualMarketContextMessage('))
    for source in ('src/product/electricity_preferences.h', 'src/product/electricity.cpp', 'src/product/energy_owner.cpp'):
        text = (root / source).read_text(encoding='utf-8')
        assert all(marker not in text for marker in ('electricity_field_messages.h', 'electricity_notice_messages.h', 'electricity_panel_messages.h', 'electricity_value_messages.h', 'electricity_context_messages.h', 'electricity_tariff_messages.h', 'localization.h', 'electricityFieldMessage(', 'electricityNoticeMessage(', 'electricityPanelMessage(', 'electricityValueMessage(', 'electricityContextMessage(', 'electricityTariffMessage('))
    for source in ('src/product/energy_authorization_settings.h', 'src/product/energy_authorization.h', 'src/product/energy_authorization.cpp', 'src/product/energy_authorization_owner.cpp', 'src/product/energy_capture.cpp', 'src/product/settings.h', 'src/product/supervisor.h', 'src/core/miner.cu'):
        text = (root / source).read_text(encoding='utf-8')
        assert all(marker not in text for marker in ('layoutTerminalEnergyAuthorizationFieldMessage', 'wrapTerminalEnergyAuthorizationFieldMessage', 'renderTerminalEnergyAuthorizationFieldMessage', 'renderWrappedTerminalEnergyAuthorizationFieldMessage'))
        assert all(marker not in text for marker in ('layoutTerminalEnergyAuthorizationNoticeMessage', 'wrapTerminalEnergyAuthorizationNoticeMessage', 'renderTerminalEnergyAuthorizationNoticeMessage', 'renderWrappedTerminalEnergyAuthorizationNoticeMessage'))
        assert all(marker not in text for marker in ('layoutTerminalEnergyAuthorizationPanelMessage', 'wrapTerminalEnergyAuthorizationPanelMessage', 'renderTerminalEnergyAuthorizationPanelMessage', 'renderWrappedTerminalEnergyAuthorizationPanelMessage'))
        assert all(marker not in text for marker in ('layoutTerminalEnergyAuthorizationValueMessage', 'wrapTerminalEnergyAuthorizationValueMessage', 'renderTerminalEnergyAuthorizationValueMessage', 'renderWrappedTerminalEnergyAuthorizationValueMessage'))
        assert all(marker not in text for marker in ('layoutTerminalEnergyAuthorizationPrivateMessage', 'wrapTerminalEnergyAuthorizationPrivateMessage', 'renderTerminalEnergyAuthorizationPrivateMessage', 'renderWrappedTerminalEnergyAuthorizationPrivateMessage'))
        if source not in ('src/product/energy_authorization_settings.h', 'src/product/settings.h', 'src/product/supervisor.h', 'src/core/miner.cu'):
            assert all(marker not in text for marker in ('energy_authorization_field_messages.h', 'energy_authorization_notice_messages.h', 'energy_authorization_panel_messages.h', 'energy_authorization_value_messages.h', 'energy_authorization_private_messages.h', 'localization.h', 'energyAuthorizationFieldMessage(', 'energyAuthorizationNoticeMessage(', 'energyAuthorizationPanelMessage(', 'energyAuthorizationValueMessage(', 'energyAuthorizationPrivateMessage('))
    for source in ('src/product/tariff_settings.h', 'src/product/tariff_declaration.h', 'src/product/tariff_declaration.cpp', 'src/product/tariff_store.h', 'src/product/electricity.cpp', 'src/product/civil_time.cpp', 'src/product/settings.h', 'src/product/supervisor.h', 'src/core/miner.cu'):
        text = (root / source).read_text(encoding='utf-8')
        assert all(marker not in text for marker in ('layoutTerminalTariffNoticeMessage', 'wrapTerminalTariffNoticeMessage', 'renderTerminalTariffNoticeMessage', 'renderWrappedTerminalTariffNoticeMessage'))
        assert all(marker not in text for marker in ('layoutTerminalTariffPanelMessage', 'wrapTerminalTariffPanelMessage', 'renderTerminalTariffPanelMessage', 'renderWrappedTerminalTariffPanelMessage'))
        assert all(marker not in text for marker in ('layoutTerminalTariffHintMessage', 'wrapTerminalTariffHintMessage', 'renderTerminalTariffHintMessage', 'renderWrappedTerminalTariffHintMessage'))
        if source not in ('src/product/tariff_settings.h', 'src/product/settings.h', 'src/product/supervisor.h', 'src/core/miner.cu'):
            assert all(marker not in text for marker in ('tariff_notice_messages.h', 'tariff_panel_messages.h', 'tariff_hint_messages.h', 'localization.h', 'tariffNoticeMessage(', 'tariffPanelMessage(', 'tariffHintMessage('))
    assert all(marker not in style for marker in ('dashboard.h', 'platform.h', 'getenv', 'ubidi_', 'ubrk_', 'filesystem'))
    assert hashlib.sha256((root / 'LICENSES/ICU-79.1.txt').read_bytes()).hexdigest() == 'e09827667e0bb3c808b2a463548ba814a5e6249b67353234d70c8dfc2bcc417d'
    if args.disabled_library:
        data = args.disabled_library.read_bytes()
        assert all(marker not in data for marker in (b'ubrk_open', b'ubidi_setPara', b'u_charMirror', b'u_stringHasBinaryProperty', b'icuuc79.dll'))
    if args.disabled_format:
        data = args.disabled_format.read_bytes()
        assert all(marker not in data for marker in (b'unumf_', b'uplrules_', b'icuuc79.dll', b'icuin79.dll'))
    if args.disabled_time:
        data = args.disabled_time.read_bytes()
        assert all(marker not in data for marker in (b'udat_open', b'udat_format', b'udatpg_', b'icuuc79.dll', b'icuin79.dll'))
    print('terminal text artifacts: ICU opt-in OFF, no automatic downloads or ordinary activation, exact notice retained')


if __name__ == '__main__':
    if not __debug__:
        raise RuntimeError('assertions require non-optimized Python')
    parser = argparse.ArgumentParser()
    parser.add_argument('--disabled-library', type=Path)
    parser.add_argument('--disabled-format', type=Path)
    parser.add_argument('--disabled-time', type=Path)
    main(parser.parse_args())
