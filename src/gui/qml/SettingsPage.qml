import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import NetShare

Rectangle {
    id: root
    color: Theme.backgroundColor

    signal closeSettings()

    property int currentSection: 0

    function formatBytes(bytes) {
        if (bytes <= 0) return "0 B"
        var units = ["B", "KB", "MB", "GB", "TB"]
        var i = Math.floor(Math.log(bytes) / Math.log(1024))
        if (i >= units.length) i = units.length - 1
        return (bytes / Math.pow(1024, i)).toFixed(i === 0 ? 0 : 1) + " " + units[i]
    }

    function sm() {
        return typeof settingsManager !== 'undefined' ? settingsManager : null
    }

    function saveSetting(key, value) {
        if (sm()) sm().setValue(key, value)
    }

    function loadSettings() {
        if (!sm()) return
        generalNameField.text = sm().getString("General/DeviceName", "NetShare-PC")
        generalAutoStart.checked = sm().isAutoStartEnabled()
        generalMinimizeTray.checked = sm().getBool("General/MinimizeToTray", true)
        generalNotify.checked = sm().getBool("General/ShowNotifications", true)
        generalLangCombo.currentIndex = sm().getInt("General/Language", 0)
        generalLangCombo.originalLangIndex = generalLangCombo.currentIndex
        generalLangCombo.ready = true

        networkPortField.text = sm().getString("Network/Port", "8080")
        networkMaxConnField.text = sm().getString("Network/MaxConnections", "10")
        networkBandwidthField.text = sm().getString("Network/MaxBandwidth", "0")
        networkAutoDetect.checked = sm().getBool("Network/AutoDetectIP", true)
        networkManualIp.text = sm().getString("Network/ManualIP", "")

        securityAuth.checked = sm().getBool("Security/RequireAuth", false)
        securityPasswordField.text = sm().getString("Security/Password", "")
        securityAllowUpload.checked = sm().getBool("Security/AllowUpload", true)
        securityAllowDelete.checked = sm().getBool("Security/AllowDelete", false)
        securityLogAccess.checked = sm().getBool("Security/LogAccess", true)
    }

    Component.onCompleted: loadSettings()

    RowLayout {
        anchors.fill: parent
        spacing: 0

        Rectangle {
            Layout.preferredWidth: 180
            Layout.fillHeight: true
            color: Theme.sidebarColor

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 12
                spacing: 4

                Label {
                    text: qsTr("Settings")
                    font.pixelSize: 20
                    font.bold: true
                    color: Theme.textColor
                    Layout.bottomMargin: 16
                }

                Repeater {
                    model: [
                        { title: qsTr("General"), icon: "\u2699\uFE0F" },
                        { title: qsTr("Network"), icon: "\uD83C\uDF10" },
                        { title: qsTr("Security"), icon: "\uD83D\uDD12" },
                        { title: qsTr("About"), icon: "\u2139\uFE0F" }
                    ]
                    delegate: Rectangle {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 40
                        radius: 4
                        color: currentSection === index ? Theme.accentColor : (sectionMouse.containsMouse ? Theme.hoverColor : "transparent")

                        Behavior on color {
                            ColorAnimation { duration: 150 }
                        }

                        RowLayout {
                            anchors.fill: parent
                            anchors.leftMargin: 10

                            Label {
                                text: modelData.icon
                                font.pixelSize: 16
                            }

                            Label {
                                text: modelData.title
                                color: currentSection === index ? Theme.textOnAccentColor : Theme.textColor
                                font.pixelSize: 14
                            }
                        }

                        MouseArea {
                            id: sectionMouse
                            anchors.fill: parent
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            onClicked: currentSection = index
                        }
                    }
                }

                Item { Layout.fillHeight: true }

                Label {
                    text: "NetShare v1.0"
                    color: Theme.textSecondary
                    font.pixelSize: 11
                    Layout.alignment: Qt.AlignHCenter
                }
            }
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.fillHeight: true
            color: Theme.backgroundColor

            // Close button at top right
            Rectangle {
                id: closeSettingsBtn
                anchors.right: parent.right
                anchors.top: parent.top
                anchors.margins: 12
                width: 32
                height: 32
                radius: 4
                color: closeSettingsMouse.containsMouse ? Theme.hoverColor : "transparent"

                Rectangle {
                    anchors.centerIn: parent
                    width: 12
                    height: 1
                    color: Theme.textColor
                    rotation: 45
                }
                Rectangle {
                    anchors.centerIn: parent
                    width: 12
                    height: 1
                    color: Theme.textColor
                    rotation: -45
                }

                MouseArea {
                    id: closeSettingsMouse
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: root.closeSettings()
                }
            }

            StackLayout {
                id: settingsStack
                anchors.fill: parent
                anchors.margins: 24
                currentIndex: currentSection

                ColumnLayout {
                    spacing: 20

                    Label {
                        text: qsTr("General Settings")
                        font.pixelSize: 20
                        font.bold: true
                        color: Theme.textColor
                    }

                    GridLayout {
                        Layout.fillWidth: true
                        columns: 2
                        rowSpacing: 16
                        columnSpacing: 16

                        Label {
                            text: qsTr("Device Name")
                            color: Theme.textColor
                            font.pixelSize: 14
                            Layout.alignment: Qt.AlignVCenter
                        }

                        TextField {
                            id: generalNameField
                            Layout.fillWidth: true
                            placeholderText: qsTr("Enter device name")
                            color: Theme.textColor
                            placeholderTextColor: Theme.textSecondary
                            selectionColor: Theme.accentColor
                            selectedTextColor: Theme.textOnAccentColor
                            font.pixelSize: 14

                            background: Rectangle {
                                color: Theme.surfaceColor
                                radius: 4
                                border.color: generalNameField.activeFocus ? Theme.accentColor : Theme.borderColor
                            }

                            onTextChanged: saveSetting("General/DeviceName", text)
                        }

                        Label {
                            text: qsTr("Theme")
                            color: Theme.textColor
                            font.pixelSize: 14
                            Layout.alignment: Qt.AlignVCenter
                        }

                        ThemedComboBox {
                            id: generalThemeCombo
                            Layout.fillWidth: true
                            property bool initialized: false
                            model: Theme.availableThemes.map(function(id) {
                                if (id === "dark") return qsTr("Dark")
                                if (id === "light") return qsTr("Light")
                                return id
                            })

                            Component.onCompleted: {
                                var currentId = Theme.currentTheme
                                var themes = Theme.availableThemes
                                for (var i = 0; i < themes.length; i++) {
                                    if (themes[i] === currentId) {
                                        currentIndex = i
                                        break
                                    }
                                }
                                initialized = true
                            }

                            onCurrentIndexChanged: {
                                if (!initialized) return
                                var themes = Theme.availableThemes
                                if (currentIndex >= 0 && currentIndex < themes.length) {
                                    Theme.switchTheme(themes[currentIndex])
                                }
                            }
                        }

                        Label {
                            text: qsTr("Language")
                            color: Theme.textColor
                            font.pixelSize: 14
                            Layout.alignment: Qt.AlignVCenter
                        }

                        ThemedComboBox {
                            id: generalLangCombo
                            Layout.fillWidth: true
                            property int originalLangIndex: 0
                            property bool ready: false
                            model: ["简体中文", "English"]

                            onCurrentIndexChanged: {
                                if (!ready) return
                                saveSetting("General/Language", currentIndex)
                                if (currentIndex !== originalLangIndex) {
                                    restartHintDialog.open()
                                }
                            }
                        }

                        Label {
                            text: qsTr("Auto Start")
                            color: Theme.textColor
                            font.pixelSize: 14
                            Layout.alignment: Qt.AlignVCenter
                        }

                        ThemedSwitch {
                            id: generalAutoStart
                            onCheckedChanged: {
                                saveSetting("General/AutoStart", checked)
                                if (typeof settingsManager !== 'undefined') {
                                    settingsManager.setAutoStartEnabled(checked)
                                }
                            }
                        }

                        Label {
                            text: qsTr("Minimize to Tray")
                            color: Theme.textColor
                            font.pixelSize: 14
                            Layout.alignment: Qt.AlignVCenter
                        }

                        ThemedSwitch {
                            id: generalMinimizeTray
                            checked: true
                            onCheckedChanged: saveSetting("General/MinimizeToTray", checked)
                        }

                        Label {
                            text: qsTr("Show Notifications")
                            color: Theme.textColor
                            font.pixelSize: 14
                            Layout.alignment: Qt.AlignVCenter
                        }

                        ThemedSwitch {
                            id: generalNotify
                            checked: true
                            onCheckedChanged: saveSetting("General/ShowNotifications", checked)
                        }
                    }

                    Item { Layout.fillHeight: true }
                }

                ColumnLayout {
                    spacing: 20

                    Label {
                        text: qsTr("Network Settings")
                        font.pixelSize: 20
                        font.bold: true
                        color: Theme.textColor
                    }

                    GridLayout {
                        Layout.fillWidth: true
                        columns: 2
                        rowSpacing: 16
                        columnSpacing: 16

                        Label {
                            text: qsTr("Service Port")
                            color: Theme.textColor
                            font.pixelSize: 14
                            Layout.alignment: Qt.AlignVCenter
                        }

                        TextField {
                            id: networkPortField
                            Layout.fillWidth: true
                            placeholderText: "8080"
                            color: Theme.textColor
                            placeholderTextColor: Theme.textSecondary
                            selectionColor: Theme.accentColor
                            selectedTextColor: Theme.textOnAccentColor
                            font.pixelSize: 14
                            validator: IntValidator { bottom: 1; top: 65535 }

                            background: Rectangle {
                                color: Theme.surfaceColor
                                radius: 4
                                border.color: networkPortField.activeFocus ? Theme.accentColor : Theme.borderColor
                            }

                            onTextChanged: saveSetting("Network/Port", text)
                        }

                        Label {
                            text: qsTr("Max Connections")
                            color: Theme.textColor
                            font.pixelSize: 14
                            Layout.alignment: Qt.AlignVCenter
                        }

                        TextField {
                            id: networkMaxConnField
                            Layout.fillWidth: true
                            placeholderText: "10"
                            color: Theme.textColor
                            placeholderTextColor: Theme.textSecondary
                            selectionColor: Theme.accentColor
                            selectedTextColor: Theme.textOnAccentColor
                            font.pixelSize: 14
                            validator: IntValidator { bottom: 1; top: 100 }

                            background: Rectangle {
                                color: Theme.surfaceColor
                                radius: 4
                                border.color: networkMaxConnField.activeFocus ? Theme.accentColor : Theme.borderColor
                            }

                            onTextChanged: saveSetting("Network/MaxConnections", text)
                        }

                        Label {
                            text: qsTr("Bandwidth Limit (KB/s)")
                            color: Theme.textColor
                            font.pixelSize: 14
                            Layout.alignment: Qt.AlignVCenter
                        }

                        TextField {
                            id: networkBandwidthField
                            Layout.fillWidth: true
                            placeholderText: qsTr("0 = Unlimited")
                            color: Theme.textColor
                            placeholderTextColor: Theme.textSecondary
                            selectionColor: Theme.accentColor
                            selectedTextColor: Theme.textOnAccentColor
                            font.pixelSize: 14
                            validator: IntValidator { bottom: 0; top: 999999 }

                            background: Rectangle {
                                color: Theme.surfaceColor
                                radius: 4
                                border.color: networkBandwidthField.activeFocus ? Theme.accentColor : Theme.borderColor
                            }

                            onTextChanged: saveSetting("Network/MaxBandwidth", text)
                        }

                        Label {
                            text: qsTr("Auto Detect IP")
                            color: Theme.textColor
                            font.pixelSize: 14
                            Layout.alignment: Qt.AlignVCenter
                        }

                        ThemedSwitch {
                            id: networkAutoDetect
                            checked: true
                            onCheckedChanged: {
                                saveSetting("Network/AutoDetectIP", checked)
                                networkManualIp.enabled = !checked
                            }
                        }

                        Label {
                            text: qsTr("Manual IP Address")
                            color: Theme.textColor
                            font.pixelSize: 14
                            Layout.alignment: Qt.AlignVCenter
                            enabled: !networkAutoDetect.checked
                            opacity: enabled ? 1.0 : 0.5
                        }

                        TextField {
                            id: networkManualIp
                            Layout.fillWidth: true
                            placeholderText: "192.168.1.100"
                            color: Theme.textColor
                            placeholderTextColor: Theme.textSecondary
                            selectionColor: Theme.accentColor
                            selectedTextColor: Theme.textOnAccentColor
                            font.pixelSize: 14
                            enabled: !networkAutoDetect.checked
                            opacity: enabled ? 1.0 : 0.5

                            background: Rectangle {
                                color: Theme.surfaceColor
                                radius: 4
                                border.color: networkManualIp.activeFocus ? Theme.accentColor : Theme.borderColor
                            }

                            onTextChanged: saveSetting("Network/ManualIP", text)
                        }
                    }

                    Item { Layout.fillHeight: true }
                }

                ColumnLayout {
                    spacing: 20

                    Label {
                        text: qsTr("Security Settings")
                        font.pixelSize: 20
                        font.bold: true
                        color: Theme.textColor
                    }

                    GridLayout {
                        Layout.fillWidth: true
                        columns: 2
                        rowSpacing: 16
                        columnSpacing: 16

                        Label {
                            text: qsTr("Access Password")
                            color: Theme.textColor
                            font.pixelSize: 14
                            Layout.alignment: Qt.AlignVCenter
                        }

                        ThemedSwitch {
                            id: securityAuth
                            onCheckedChanged: {
                                saveSetting("Security/RequireAuth", checked)
                                securityPasswordField.enabled = checked
                            }
                        }

                        Label {
                            text: qsTr("Password")
                            color: Theme.textColor
                            font.pixelSize: 14
                            Layout.alignment: Qt.AlignVCenter
                            enabled: securityAuth.checked
                            opacity: enabled ? 1.0 : 0.5
                        }

                        TextField {
                            id: securityPasswordField
                            Layout.fillWidth: true
                            placeholderText: qsTr("Set access password")
                            echoMode: TextInput.Password
                            color: Theme.textColor
                            placeholderTextColor: Theme.textSecondary
                            selectionColor: Theme.accentColor
                            selectedTextColor: Theme.textOnAccentColor
                            font.pixelSize: 14
                            enabled: securityAuth.checked
                            opacity: enabled ? 1.0 : 0.5

                            background: Rectangle {
                                color: Theme.surfaceColor
                                radius: 4
                                border.color: securityPasswordField.activeFocus ? Theme.accentColor : Theme.borderColor
                            }

                            onTextChanged: saveSetting("Security/Password", text)
                        }

                        Label {
                            text: qsTr("Allow Upload")
                            color: Theme.textColor
                            font.pixelSize: 14
                            Layout.alignment: Qt.AlignVCenter
                        }

                        ThemedSwitch {
                            id: securityAllowUpload
                            checked: true
                            onCheckedChanged: saveSetting("Security/AllowUpload", checked)
                        }

                        Label {
                            text: qsTr("Allow Delete")
                            color: Theme.textColor
                            font.pixelSize: 14
                            Layout.alignment: Qt.AlignVCenter
                        }

                        ThemedSwitch {
                            id: securityAllowDelete
                            onCheckedChanged: saveSetting("Security/AllowDelete", checked)
                        }

                        Label {
                            text: qsTr("Access Log")
                            color: Theme.textColor
                            font.pixelSize: 14
                            Layout.alignment: Qt.AlignVCenter
                        }

                        ThemedSwitch {
                            id: securityLogAccess
                            checked: true
                            onCheckedChanged: saveSetting("Security/LogAccess", checked)
                        }
                    }

                    Item { Layout.fillHeight: true }

                    RowLayout {
                        Layout.fillWidth: true

                        Button {
                            text: qsTr("Reset to Defaults")
                            contentItem: Label {
                                text: parent.text
                                color: Theme.errorColor
                                horizontalAlignment: Text.AlignHCenter
                            }
                            background: Rectangle {
                                color: parent.hovered ? Theme.hoverColor : Theme.surfaceColor
                                radius: 4
                                border.color: Theme.borderColor
                            }
                            onClicked: {
                                if (sm()) sm().clear()
                                loadSettings()
                            }
                        }

                        Item { Layout.fillWidth: true }
                    }
                }

                // About Page
                ColumnLayout {
                    spacing: 24

                    Label {
                        text: qsTr("About NetShare")
                        font.pixelSize: 20
                        font.bold: true
                        color: Theme.textColor
                    }

                    // App icon + name + version
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 16

                        Image {
                            source: "qrc:/icons/netshare.png"
                            width: 64
                            height: 64
                            sourceSize.width: 64
                            sourceSize.height: 64
                            fillMode: Image.PreserveAspectFit
                            smooth: true
                        }

                        ColumnLayout {
                            spacing: 4

                            Label {
                                text: "NetShare"
                                font.pixelSize: 22
                                font.bold: true
                                color: Theme.textColor
                            }

                            Label {
                                text: qsTr("LAN File Sharing Tool")
                                font.pixelSize: 13
                                color: Theme.textSecondary
                            }

                            Label {
                                text: "v1.0.0"
                                font.pixelSize: 13
                                color: Theme.textSecondary
                                font.family: "Consolas"
                            }
                        }

                        Item { Layout.fillWidth: true }
                    }

                    Rectangle {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 1
                        color: Theme.borderColor
                    }

                    // Info items
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 12

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 12

                            Label {
                                text: qsTr("Version")
                                color: Theme.textSecondary
                                font.pixelSize: 14
                                Layout.preferredWidth: 100
                            }

                            Label {
                                text: "1.0.0"
                                color: Theme.textColor
                                font.pixelSize: 14
                                font.family: "Consolas"
                            }

                            Item { Layout.fillWidth: true }
                        }

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 12

                            Label {
                                text: qsTr("Build")
                                color: Theme.textSecondary
                                font.pixelSize: 14
                                Layout.preferredWidth: 100
                            }

                            Label {
                                text: Qt.platform.os === "windows" ? "Windows (MSVC 2022)" : Qt.platform.os === "osx" ? "macOS" : "Linux"
                                color: Theme.textColor
                                font.pixelSize: 14
                            }

                            Item { Layout.fillWidth: true }
                        }

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 12

                            Label {
                                text: qsTr("Qt Version")
                                color: Theme.textSecondary
                                font.pixelSize: 14
                                Layout.preferredWidth: 100
                            }

                            Label {
                                text: "6.8.3"
                                color: Theme.textColor
                                font.pixelSize: 14
                                font.family: "Consolas"
                            }

                            Item { Layout.fillWidth: true }
                        }

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 12

                            Label {
                                text: qsTr("Contact")
                                color: Theme.textSecondary
                                font.pixelSize: 14
                                Layout.preferredWidth: 100
                            }

                            Label {
                                text: "HD5080@outlook.com"
                                color: Theme.accentColor
                                font.pixelSize: 14
                                MouseArea {
                                    anchors.fill: parent
                                    cursorShape: Qt.PointingHandCursor
                                    onClicked: Qt.openUrlExternally("mailto:HD5080@outlook.com")
                                }
                            }

                            Item { Layout.fillWidth: true }
                        }
                    }

                    Rectangle {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 1
                        color: Theme.borderColor
                    }

                    // Check for updates button
                    Button {
                        id: checkUpdateBtn
                        text: qsTr("Check for Updates")
                        Layout.preferredHeight: 40
                        Layout.preferredWidth: 200
                        enabled: !updateChecking

                        property bool updateChecking: false

                        contentItem: Label {
                            text: parent.text
                            color: parent.enabled ? (parent.down ? Theme.textOnAccentColor : Theme.accentColor) : Theme.textSecondary
                            horizontalAlignment: Text.AlignHCenter
                            verticalAlignment: Text.AlignVCenter
                        }

                        background: Rectangle {
                            color: parent.down ? Theme.accentColor : (parent.hovered ? Theme.hoverColor : Theme.surfaceColor)
                            radius: 4
                            border.color: Theme.accentColor
                        }

                        onClicked: {
                            checkUpdateBtn.updateChecking = true
                            updateStatusText.text = qsTr("Checking for updates...")
                            updateTimer.start()
                        }
                    }

                    Label {
                        id: updateStatusText
                        color: Theme.textSecondary
                        font.pixelSize: 13
                        visible: text !== ""
                    }

                    Timer {
                        id: updateTimer
                        interval: 1500
                        onTriggered: {
                            checkUpdateBtn.updateChecking = false
                            updateStatusText.text = qsTr("You are using the latest version.")
                        }
                    }

                    Item { Layout.fillHeight: true }

                    // Copyright
                    Label {
                        text: "\u00A9 2025 NetShare. " + qsTr("All rights reserved.")
                        color: Theme.textSecondary
                        font.pixelSize: 11
                        Layout.alignment: Qt.AlignHCenter
                    }
                }
            }
        }
    }

    Dialog {
        id: restartHintDialog
        title: qsTr("语言设置")
        modal: true
        x: (parent.width - width) / 2
        y: (parent.height - height) / 2
        width: 320
        contentItem: Label {
            text: qsTr("语言设置将在重启后生效")
            wrapMode: Text.WordWrap
        }
        footer: DialogButtonBox {
            Button {
                text: qsTr("OK")
                DialogButtonBox.buttonRole: DialogButtonBox.AcceptRole
                background: Rectangle {
                    color: parent.down ? Theme.accentColor : Theme.surfaceColor
                    radius: 4
                    border.color: Theme.borderColor
                }
                contentItem: Label {
                    text: parent.text
                    color: parent.down ? Theme.textOnAccentColor : Theme.textColor
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                }
            }
        }
        background: Rectangle {
            color: Theme.surfaceColor
            radius: 8
            border.color: Theme.borderColor
        }
    }
}
