// Settings → General: language, driver side, name, input.
SettingsSection {
    SettingDropdown {
        // Translations::languages(): every language with a translation, named in itself
        title: qsTrId("settings.general.language")
        description: qsTrId("settings.general.language_openhu_onscreen_keyboard")
        key: "general.language"
        options: settings.info.languages
    }
    SettingChoice {
        title: qsTrId("settings.general.steering_wheel")
        description: qsTrId("settings.general.android_auto_arranges_controls")
        key: "general.driverSide"
        options: [{ value: "left", label: qsTrId("settings.general.left") }, { value: "right", label: qsTrId("settings.general.right") }]
    }
    SettingText {
        title: qsTrId("settings.general.name_suffix")
        description: qsTrId("settings.general.phone_shows_openhu_different")
                     .arg(settings.values["general.nameSuffix"] || qsTrId("settings.general.suffix"))
        key: "general.nameSuffix"
        placeholder: qsTrId("settings.general.e_g_golf")
    }
    SettingSwitch {
        title: qsTrId("settings.general.show_mouse_cursor")
        description: qsTrId("settings.general.using_mouse_instead_touchscreen")
        key: "general.showCursor"
    }
    SettingSwitch {
        title: qsTrId("settings.general.hide_status_bar")
        description: qsTrId("settings.general.hide_status_bar_description")
        key: "general.hideStatusBar"
    }
    SettingSwitch {
        visible: onScreenKeyboardAvailable
        title: qsTrId("settings.general.onscreen_keyboard")
        description: qsTrId("settings.general.appears_text_fields_switch")
        key: "general.onScreenKeyboard"
    }
}
