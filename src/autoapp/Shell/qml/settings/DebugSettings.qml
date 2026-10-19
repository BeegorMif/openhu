import QtQuick
import QtQuick.Controls
import OpenHU 1.0

// Settings → Debug (#120): for development and tests, only while the debug mode is on (Settings → Info). The
// simulated driving state, the log level and the video overlay are not saved: off after every start.
SettingsSection {
    SettingChoice {
        title: qsTrId("settings.debug.simulate")
        description: qsTrId("settings.debug.simulate_description")
        key: "debug.simulate"
        options: [{ value: "off", label: qsTrId("settings.debug.off") },
                  { value: "park", label: qsTrId("settings.debug.park") },
                  { value: "drive", label: qsTrId("settings.debug.drive") },
                  { value: "reverse", label: qsTrId("settings.debug.reverse") }]
    }
    SettingDropdown {
        title: qsTrId("settings.debug.log_level")
        description: qsTrId("settings.debug.log_level_description")
        key: "debug.logLevel"
        // untranslated: the names of the levels
        options: ["trace", "debug", "info", "warning", "error", "fatal"].map(level => ({ value: level, label: level }))
    }
    SettingSwitch {
        title: qsTrId("settings.debug.video_overlay")
        description: qsTrId("settings.debug.video_overlay_description")
        key: "debug.videoOverlay"
    }
    SettingDropdown {
        title: qsTrId("settings.debug.video_decoder")
        description: qsTrId("settings.debug.video_decoder_description")
        key: "debug.videoDecoder"
        options: [{ value: "auto", label: qsTrId("settings.debug.auto") },
                  { value: "software", label: qsTrId("settings.debug.software") },
                  { value: "h264_v4l2m2m", label: "h264_v4l2m2m" }]
    }
    SettingChoice {
        title: qsTrId("settings.debug.video_codec")
        description: qsTrId("settings.debug.video_codec_description")
        key: "debug.videoCodec"
        options: [{ value: "auto", label: qsTrId("settings.debug.auto") }, { value: "h264", label: "H.264" }]
    }
}
