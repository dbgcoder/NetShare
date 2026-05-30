import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import NetShare

Rectangle {
    id: root
    color: Theme.backgroundColor

    signal closeSettings()

    property int currentSection: 0
    property int logDownloadCount: 0
    property int logUploadCount: 0
    property int logTotalBytes: 0

    function formatBytes(bytes) {
        if (bytes <= 0) return "0 B"
        var units = ["B", "KB", "MB", "GB", "TB"]
        var i = Math.floor(Math.log(bytes) / Math.log(1024))
        if (i >= units.length) i = units.length - 1
        return (bytes / Math.pow(1024, i)).toFixed(i === 0 ? 0 : 1) + " " + units[i]
    }

    function refreshLogs() {
        if (typeof transferLogService === 'undefined') return
        var logs
        if (logSearchField.text.length > 0) {
            logs = transferLogService.searchLogs(logSearchField.text)
        } else if (logTypeFilter.currentIndex === 1) {
            logs = transferLogService.queryByType(0)
        } else if (logTypeFilter.currentIndex === 2) {
            logs = transferLogService.queryByType(1)
        } else {
            logs = transferLogService.queryLogs(200, 0)
        }

        logModel.clear()
        for (var i = 0; i < logs.length; i++) {
            var entry = logs[i]
            logModel.append({
                "fileName": entry.fileName || "",
                "fileSize": entry.fileSize || 0,
                "peerAddress": entry.peerAddress || "",
                "status": entry.status,
                "type": entry.type,
                "timeStr": entry.timestamp ? Qt.formatDateTime(entry.timestamp, "yyyy-MM-dd HH:mm:ss") : ""
            })
        }

        logDownloadCount = transferLogService.countByType(0)
        logUploadCount = transferLogService.countByType(1)
        logTotalBytes = transferLogService.totalBytesTransferred()
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
        generalAutoStart.checked = sm().getBool("General/AutoStart", false)
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

        uploadPathField.text = sm().getString("Paths/UploadDir", sm().getDefaultUploadPath())

        securityAuth.checked = sm().getBool("Security/RequireAuth", false)
        securityPasswordField.text = sm().getString("Security/Password", "")
        securityAllowUpload.checked = sm().getBool("Security/AllowUpload", true)
        securityAllowDelete.checked = sm().getBool("Security/AllowDelete", false)
        securityLogAccess.checked = sm().getBool("Security/LogAccess", true)

        tlsEnabled.checked = sm().getBool("server/tlsEnabled", false)
        tlsPortField.text = sm().getString("server/httpsPort", "8443")
        tlsCertField.text = sm().getString("server/tlsCertPath", "")
        tlsKeyField.text = sm().getString("server/tlsKeyPath", "")
    }

    Component.onCompleted: loadSettings()
    onCurrentSectionChanged: {
        if (currentSection === 3) refreshLogs()
    }

    Timer {
        interval: 1000
        running: currentSection === 4 && typeof bandwidthManager !== 'undefined'
        repeat: true
        onTriggered: {
            var speed = bandwidthManager.globalCurrentSpeed()
            bandwidthSpeedLabel.text = formatBytes(speed) + "/s"
        }
    }

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
                        { title: qsTr("General"), icon: "⚙️" },
                        { title: qsTr("Network"), icon: "🌐" },
                        { title: qsTr("Security"), icon: "🔒" },
                        { title: qsTr("Transfer Log"), icon: "📋" },
                        { title: qsTr("Bandwidth"), icon: "📊" },
                        { title: qsTr("TLS/HTTPS"), icon: "🔐" }
                    ]
                    delegate: Rectangle {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 40
                        radius: 4
                        color: currentSection === index ? Theme.accentColor : (sectionMouse.containsMouse ? "#3e3e42" : "transparent")

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
                                color: currentSection === index ? "#ffffff" : Theme.textColor
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
                color: closeSettingsMouse.containsMouse ? "#3e3e42" : "transparent"

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
                            font.pixelSize: 14

                            background: Rectangle {
                                color: Theme.surfaceColor
                                radius: 4
                                border.color: generalNameField.activeFocus ? Theme.accentColor : Theme.borderColor
                            }

                            onTextChanged: saveSetting("General/DeviceName", text)
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
                            onCheckedChanged: saveSetting("General/AutoStart", checked)
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
                                color: parent.hovered ? "#3e3e42" : Theme.surfaceColor
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

                // ---- 传输日志 ----
                ColumnLayout {
                    spacing: 16

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 12

                        Label {
                            text: qsTr("Transfer Log")
                            font.pixelSize: 20
                            font.bold: true
                            color: Theme.textColor
                        }

                        Item { Layout.fillWidth: true }

                        Label {
                            text: qsTr("Total %1 entries").arg(logListView.count)
                            color: Theme.textSecondary
                            font.pixelSize: 13
                        }

                        ThemedButton {
                            text: qsTr("Refresh")
                            onClicked: refreshLogs()
                        }

                        ThemedButton {
                            text: qsTr("Export")
                            onClicked: {
                                if (typeof transferLogService !== 'undefined')
                                    transferLogService.exportLogs("NetShare_transfer_logs.json")
                            }
                        }

                        ThemedButton {
                            text: qsTr("Clear")
                            onClicked: {
                                if (typeof transferLogService !== 'undefined')
                                    transferLogService.clearLogs(0)
                                refreshLogs()
                            }
                        }
                    }

                    // Filter bar
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 8

                        ComboBox {
                            id: logTypeFilter
                            model: [qsTr("All"), qsTr("Download"), qsTr("Upload")]
                            font.pixelSize: 13
                            Layout.preferredWidth: 100
                            onCurrentIndexChanged: refreshLogs()

                            background: Rectangle {
                                color: Theme.surfaceColor
                                radius: 4
                                border.color: Theme.borderColor
                            }
                            contentItem: Label {
                                text: logTypeFilter.displayText
                                color: Theme.textColor
                                font.pixelSize: 13
                                verticalAlignment: Text.AlignVCenter
                                leftPadding: 8
                            }
                            delegate: ItemDelegate {
                                width: logTypeFilter.width
                                contentItem: Label {
                                    text: modelData
                                    color: Theme.textColor
                                    font.pixelSize: 13
                                }
                                background: Rectangle {
                                    color: highlighted ? Theme.accentColor : Theme.surfaceColor
                                }
                                highlighted: logTypeFilter.highlightedIndex === index
                            }
                        }

                        TextField {
                            id: logSearchField
                            Layout.fillWidth: true
                            placeholderText: qsTr("Search file name or address...")
                            color: Theme.textColor
                            font.pixelSize: 13
                            onTextChanged: refreshLogs()

                            background: Rectangle {
                                color: Theme.surfaceColor
                                radius: 4
                                border.color: logSearchField.activeFocus ? Theme.accentColor : Theme.borderColor
                            }
                        }
                    }

                    // Statistics
                    Rectangle {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 50
                        color: Theme.surfaceColor
                        radius: 4

                        RowLayout {
                            anchors.fill: parent
                            anchors.leftMargin: 16
                            anchors.rightMargin: 16

                            Label {
                                text: qsTr("📊 Statistics:")
                                color: Theme.textSecondary
                                font.pixelSize: 13
                            }
                            Label {
                                text: qsTr("Download %1 times").arg(logDownloadCount)
                                color: Theme.accentColor
                                font.pixelSize: 13
                            }
                            Label {
                                text: qsTr("Upload %1 times").arg(logUploadCount)
                                color: Theme.successColor
                                font.pixelSize: 13
                            }
                            Label {
                                text: qsTr("Total transfer %1").arg(formatBytes(logTotalBytes))
                                color: Theme.warningColor
                                font.pixelSize: 13
                            }
                            Item { Layout.fillWidth: true }
                        }
                    }

                    // Log list
                    ListView {
                        id: logListView
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        clip: true
                        spacing: 2

                        model: ListModel { id: logModel }

                        delegate: Rectangle {
                            width: logListView.width
                            height: 44
                            color: index % 2 === 0 ? "transparent" : "#252528"
                            radius: 2

                            RowLayout {
                                anchors.fill: parent
                                anchors.leftMargin: 12
                                anchors.rightMargin: 12

                                Label {
                                    text: model.type === 0 ? "⬇️" : "⬆️"
                                    font.pixelSize: 14
                                    Layout.preferredWidth: 24
                                }
                                Label {
                                    text: model.fileName
                                    color: Theme.textColor
                                    font.pixelSize: 13
                                    elide: Text.ElideMiddle
                                    Layout.fillWidth: true
                                    Layout.minimumWidth: 120
                                }
                                Label {
                                    text: formatBytes(model.fileSize)
                                    color: Theme.textSecondary
                                    font.pixelSize: 12
                                    Layout.preferredWidth: 80
                                }
                                Label {
                                    text: model.peerAddress
                                    color: Theme.textSecondary
                                    font.pixelSize: 12
                                    Layout.preferredWidth: 100
                                }
                                Label {
                                    text: {
                                        switch (model.status) {
                                        case 0: return qsTr("Started")
                                        case 1: return qsTr("Completed")
                                        case 2: return qsTr("Failed")
                                        case 3: return qsTr("Cancelled")
                                        default: return qsTr("Unknown")
                                        }
                                    }
                                    color: {
                                        switch (model.status) {
                                        case 1: return Theme.successColor
                                        case 2: return Theme.errorColor
                                        case 3: return Theme.warningColor
                                        default: return Theme.textSecondary
                                        }
                                    }
                                    font.pixelSize: 12
                                    Layout.preferredWidth: 50
                                }
                                Label {
                                    text: model.timeStr
                                    color: Theme.textSecondary
                                    font.pixelSize: 12
                                    Layout.preferredWidth: 130
                                }
                            }
                        }
                    }
                }

                // ---- 带宽控制 ----
                ColumnLayout {
                    spacing: 20

                    Label {
                        text: qsTr("Bandwidth Control")
                        font.pixelSize: 20
                        font.bold: true
                        color: Theme.textColor
                    }

                    // Real-time speed display
                    Rectangle {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 80
                        color: Theme.surfaceColor
                        radius: 8

                        RowLayout {
                            anchors.fill: parent
                            anchors.margins: 16

                            ColumnLayout {
                                spacing: 4
                                Label {
                                    text: qsTr("Current Global Speed")
                                    color: Theme.textSecondary
                                    font.pixelSize: 13
                                }
                                Label {
                                    id: bandwidthSpeedLabel
                                    text: "0 KB/s"
                                    color: Theme.accentColor
                                    font.pixelSize: 24
                                    font.bold: true
                                }
                            }

                            Item { Layout.fillWidth: true }

                            ColumnLayout {
                                spacing: 4
                                Label {
                                    text: qsTr("Bandwidth Limit")
                                    color: Theme.textSecondary
                                    font.pixelSize: 13
                                }
                                Label {
                                    id: bandwidthLimitLabel
                                    text: qsTr("0 = Unlimited")
                                    color: Theme.warningColor
                                    font.pixelSize: 16
                                }
                            }
                        }
                    }

                    GridLayout {
                        Layout.fillWidth: true
                        columns: 2
                        rowSpacing: 16
                        columnSpacing: 16

                        Label {
                            text: qsTr("Global Speed Limit (KB/s)")
                            color: Theme.textColor
                            font.pixelSize: 14
                            Layout.alignment: Qt.AlignVCenter
                        }

                        TextField {
                            id: bandwidthGlobalLimitField
                            Layout.fillWidth: true
                            placeholderText: qsTr("0 = Unlimited")
                            color: Theme.textColor
                            font.pixelSize: 14
                            validator: IntValidator { bottom: 0; top: 999999 }

                            background: Rectangle {
                                color: Theme.surfaceColor
                                radius: 4
                                border.color: bandwidthGlobalLimitField.activeFocus ? Theme.accentColor : Theme.borderColor
                            }

                            onTextChanged: {
                                saveSetting("Network/MaxBandwidth", text)
                                bandwidthLimitLabel.text = text + " KB/s" + (parseInt(text) === 0 ? qsTr(" (Unlimited)") : "")
                                if (typeof bandwidthManager !== 'undefined')
                                    bandwidthManager.setGlobalLimit(parseInt(text) * 1024)
                            }
                        }

                        Label {
                            text: qsTr("Transfer Statistics")
                            color: Theme.textColor
                            font.pixelSize: 14
                            Layout.alignment: Qt.AlignVCenter
                        }

                        Label {
                            text: qsTr("Total: %1 | Records: %2").arg(formatBytes(typeof transferLogService !== 'undefined' ? transferLogService.totalBytesTransferred() : 0)).arg(typeof transferLogService !== 'undefined' ? transferLogService.totalCount() : 0)
                            color: Theme.textSecondary
                            font.pixelSize: 13
                            wrapMode: Text.WordWrap
                        }
                    }

                    Item { Layout.fillHeight: true }
                }

                // ---- Upload Path ----
                ColumnLayout {
                    spacing: 20

                    Label {
                        text: qsTr("Upload Path")
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
                            text: qsTr("Upload Directory")
                            color: Theme.textColor
                            font.pixelSize: 14
                            Layout.alignment: Qt.AlignVCenter
                        }

                        TextField {
                            id: uploadPathField
                            Layout.fillWidth: true
                            placeholderText: sm().getDefaultUploadPath()
                            color: Theme.textColor
                            selectByMouse: true
                            background: Rectangle {
                                implicitHeight: 36
                                radius: 4
                                border.color: uploadPathField.activeFocus ? Theme.accentColor : Theme.borderColor
                            }

                            onTextChanged: saveSetting("Paths/UploadDir", text)
                        }
                    }
                }

                // ---- TLS/HTTPS ----
                ColumnLayout {
                    spacing: 20

                    Label {
                        text: qsTr("TLS / HTTPS Settings")
                        font.pixelSize: 20
                        font.bold: true
                        color: Theme.textColor
                    }

                    Rectangle {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 48
                        color: "#3e2a1a"
                        radius: 4
                        visible: !tlsEnabled.checked

                        RowLayout {
                            anchors.fill: parent
                            anchors.leftMargin: 12

                            Label {
                                text: "⚠️"
                                font.pixelSize: 16
                            }
                            Label {
                                text: qsTr("TLS encryption is not enabled, all transfers are in plaintext")
                                color: Theme.warningColor
                                font.pixelSize: 13
                            }
                        }
                    }

                    Rectangle {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 48
                        color: "#1a3e2a"
                        radius: 4
                        visible: tlsEnabled.checked

                        RowLayout {
                            anchors.fill: parent
                            anchors.leftMargin: 12

                            Label {
                                text: "🔒"
                                font.pixelSize: 16
                            }
                            Label {
                                text: qsTr("TLS encryption is enabled, transfer data is protected")
                                color: Theme.successColor
                                font.pixelSize: 13
                            }
                        }
                    }

                    GridLayout {
                        Layout.fillWidth: true
                        columns: 2
                        rowSpacing: 16
                        columnSpacing: 16

                        Label {
                            text: qsTr("Enable TLS")
                            color: Theme.textColor
                            font.pixelSize: 14
                            Layout.alignment: Qt.AlignVCenter
                        }

                        ThemedSwitch {
                            id: tlsEnabled
                            onCheckedChanged: saveSetting("server/tlsEnabled", checked)
                        }

                        Label {
                            text: qsTr("HTTPS Port")
                            color: Theme.textColor
                            font.pixelSize: 14
                            Layout.alignment: Qt.AlignVCenter
                            enabled: tlsEnabled.checked
                            opacity: enabled ? 1.0 : 0.5
                        }

                        TextField {
                            id: tlsPortField
                            Layout.fillWidth: true
                            placeholderText: "8443"
                            color: Theme.textColor
                            font.pixelSize: 14
                            validator: IntValidator { bottom: 1; top: 65535 }
                            enabled: tlsEnabled.checked
                            opacity: enabled ? 1.0 : 0.5

                            background: Rectangle {
                                color: Theme.surfaceColor
                                radius: 4
                                border.color: tlsPortField.activeFocus ? Theme.accentColor : Theme.borderColor
                            }

                            onTextChanged: saveSetting("server/httpsPort", text)
                        }

                        Label {
                            text: qsTr("Certificate File")
                            color: Theme.textColor
                            font.pixelSize: 14
                            Layout.alignment: Qt.AlignVCenter
                            enabled: tlsEnabled.checked
                            opacity: enabled ? 1.0 : 0.5
                        }

                        TextField {
                            id: tlsCertField
                            Layout.fillWidth: true
                            placeholderText: qsTr("Select .pem or .crt certificate file")
                            color: Theme.textColor
                            font.pixelSize: 14
                            enabled: tlsEnabled.checked
                            opacity: enabled ? 1.0 : 0.5

                            background: Rectangle {
                                color: Theme.surfaceColor
                                radius: 4
                                border.color: tlsCertField.activeFocus ? Theme.accentColor : Theme.borderColor
                            }

                            onTextChanged: saveSetting("server/tlsCertPath", text)
                        }

                        Label {
                            text: qsTr("Private Key File")
                            color: Theme.textColor
                            font.pixelSize: 14
                            Layout.alignment: Qt.AlignVCenter
                            enabled: tlsEnabled.checked
                            opacity: enabled ? 1.0 : 0.5
                        }

                        TextField {
                            id: tlsKeyField
                            Layout.fillWidth: true
                            placeholderText: qsTr("Select .pem or .key private key file")
                            color: Theme.textColor
                            font.pixelSize: 14
                            enabled: tlsEnabled.checked
                            opacity: enabled ? 1.0 : 0.5

                            background: Rectangle {
                                color: Theme.surfaceColor
                                radius: 4
                                border.color: tlsKeyField.activeFocus ? Theme.accentColor : Theme.borderColor
                            }

                            onTextChanged: saveSetting("server/tlsKeyPath", text)
                        }
                    }

                    Label {
                        text: qsTr("Note: TLS settings require service restart to take effect")
                        color: Theme.textSecondary
                        font.pixelSize: 12
                        Layout.topMargin: 8
                    }

                    Item { Layout.fillHeight: true }
                }
            }
        }
    }

    Dialog {
        id: restartHintDialog
        title: qsTr("语言设置")
        standardButtons: Dialog.Ok
        modal: true
        x: (parent.width - width) / 2
        y: (parent.height - height) / 2
        width: 320
        contentItem: Label {
            text: qsTr("语言设置将在重启后生效")
            wrapMode: Text.WordWrap
        }
    }
}
