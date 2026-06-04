import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import NetShare

Rectangle {
    id: root
    color: Theme.backgroundColor

    property var mainWindow: null

    ListModel {
        id: deviceModel
    }

    Timer {
        id: scanTimer
        interval: 3000
        running: true
        repeat: true
        onTriggered: refreshDevices()
    }

    Connections {
        target: chatService
        function onUserListChanged() {
            refreshDevices()
        }
    }

    function refreshDevices() {
        var localIp = typeof shareManager !== 'undefined' ? shareManager.localIp : "127.0.0.1"
        var localPort = 8080
        if (typeof settingsManager !== 'undefined') {
            localPort = Number(settingsManager.value("Network/Port", 8080))
        }

        deviceModel.clear()

        deviceModel.append({
            name: qsTr("This PC"),
            address: localIp,
            port: localPort,
            status: qsTr("This PC"),
            isLocal: true,
            deviceType: "desktop"
        })

        if (typeof chatService !== 'undefined') {
            var users = chatService.getUserList()
            for (var i = 0; i < users.length; i++) {
                var u = users[i]
                if (u.address === localIp) continue
                deviceModel.append({
                    name: u.name || qsTr("Unknown device"),
                    address: u.address || "--",
                    port: Number(u.port) || 0,
                    status: u.isOnline ? qsTr("Online") : qsTr("Connected"),
                    isLocal: false,
                    deviceType: u.deviceType || "mobile"
                })
            }
        }

        if (typeof webSocketHandler !== 'undefined') {
            var clients = webSocketHandler.getConnectedClientsList()
            var existingAddrs = {}
            for (var j = 0; j < deviceModel.count; j++) {
                existingAddrs[deviceModel.get(j).address] = true
            }
            for (var k = 0; k < clients.length; k++) {
                var c = clients[k]
                if (c.address === localIp) continue
                if (existingAddrs[c.address]) continue
                var parts = c.address.split(".")
                var suffix = parts.length > 0 ? parts[parts.length - 1] : c.address
                var customName = (typeof chatService !== 'undefined') ? chatService.getDeviceName(c.address) : ""
                var displayName = customName.length > 0 ? customName : qsTr("Device-%1").arg(suffix)
                deviceModel.append({
                    name: displayName,
                    address: c.address,
                    port: Number(c.port) || localPort,
                    status: qsTr("Connected"),
                    isLocal: false,
                    deviceType: "mobile"
                })
            }
        }

        if (typeof mdnsService !== 'undefined') {
            var services = mdnsService.getDiscoveredServicesList()
            var existingAddrs2 = {}
            for (var m = 0; m < deviceModel.count; m++) {
                existingAddrs2[deviceModel.get(m).address] = true
            }
            for (var n = 0; n < services.length; n++) {
                var s = services[n]
                if (s.address === localIp) continue
                if (existingAddrs2[s.address]) continue
                deviceModel.append({
                    name: s.name || qsTr("Unknown device"),
                    address: s.address || "--",
                    port: Number(s.port) || 0,
                    status: qsTr("Online"),
                    isLocal: false,
                    deviceType: "desktop"
                })
            }
        }

        deviceCountLabel.text = qsTr("Found %1 devices").arg(deviceModel.count)
    }

    Component.onCompleted: {
        refreshDevices()
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 20
        spacing: 16

        RowLayout {
            Layout.fillWidth: true

            Label {
                text: qsTr("Device Discovery")
                font.pixelSize: 24
                font.bold: true
                color: Theme.textColor
            }

            Item { Layout.fillWidth: true }

            Button {
                id: scanButton
                text: scanTimer.running ? qsTr("Stop Scan") : qsTr("Scan Devices")

                contentItem: Label {
                    text: parent.text
                    color: Theme.textOnAccentColor
                    horizontalAlignment: Text.AlignHCenter
                    font.pixelSize: 13
                }

                background: Rectangle {
                    color: scanTimer.running ? Theme.errorColor : Theme.accentColor
                    radius: 4
                }

                onClicked: {
                    if (scanTimer.running) {
                        scanTimer.stop()
                    } else {
                        refreshDevices()
                        scanTimer.start()
                    }
                }
            }
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: 12

            Rectangle {
                Layout.preferredWidth: 80
                Layout.preferredHeight: 28
                color: Theme.accentColor
                radius: 4

                Label {
                    anchors.centerIn: parent
                    text: qsTr("All")
                    color: Theme.textOnAccentColor
                    font.pixelSize: 12
                }
            }

            Item { Layout.fillWidth: true }

            Label {
                id: deviceCountLabel
                text: qsTr("Found 1 device")
                color: Theme.textSecondary
                font.pixelSize: 13
            }
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.fillHeight: true
            color: Theme.surfaceColor
            radius: 8

            ListView {
                id: deviceListView
                anchors.fill: parent
                anchors.margins: 8
                clip: true

                model: deviceModel

                ScrollBar.vertical: ScrollBar {
                    id: deviceListScrollBar
                    contentItem: Rectangle {
                        implicitWidth: 6
                        implicitHeight: 100
                        radius: 3
                        color: deviceListScrollBar.active ? Theme.accentColor : Theme.borderColor
                        opacity: deviceListScrollBar.active ? 1.0 : 0.5
                    }
                }

                Label {
                    anchors.centerIn: parent
                    text: qsTr("No other devices found\nClick \"Scan Devices\" to search LAN")
                    color: Theme.textSecondary
                    font.pixelSize: 14
                    horizontalAlignment: Text.AlignHCenter
                    visible: deviceModel.count <= 1
                }

                delegate: Rectangle {
                    width: parent ? parent.width - 16 : 0
                    height: 72
                    color: deviceMouseArea.containsMouse ? Theme.itemHoverColor : Theme.sidebarColor
                    radius: 4

                    MouseArea {
                        id: deviceMouseArea
                        anchors.fill: parent
                        hoverEnabled: true
                    }

                    RowLayout {
                        anchors.fill: parent
                        anchors.margins: 12
                        spacing: 12

                        Rectangle {
                            Layout.preferredWidth: 44
                            Layout.preferredHeight: 44
                            radius: 22
                            color: model.isLocal ? Theme.accentColor : Theme.successColor

                            Label {
                                anchors.centerIn: parent
                                text: model.isLocal ? "🖥" : (model.deviceType === "desktop" ? "💻" : "📱")
                                font.pixelSize: 20
                            }
                        }

                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 2

                            RowLayout {
                                spacing: 6

                                Label {
                                    id: deviceNameLabel
                                    text: model.name
                                    color: Theme.textColor
                                    font.pixelSize: 14
                                    font.bold: true
                                    visible: !editNameField.visible
                                }

                                TextField {
                                    id: editNameField
                                    Layout.preferredWidth: 140
                                    text: model.name
                                    color: Theme.textColor
                                    placeholderTextColor: Theme.textSecondary
                                    selectionColor: Theme.accentColor
                                    selectedTextColor: Theme.textOnAccentColor
                                    font.pixelSize: 14
                                    font.bold: true
                                    visible: false
                                    selectByMouse: true
                                    property bool saving: false

                                    background: Rectangle {
                                        color: Theme.backgroundColor
                                        radius: 4
                                        border.color: editNameField.activeFocus ? Theme.accentColor : Theme.borderColor
                                    }

                                    function finishEdit(save) {
                                        if (saving) return
                                        saving = true
                                        if (save && text.trim().length > 0 && text.trim() !== model.name && typeof chatService !== 'undefined') {
                                            chatService.renameDevice(model.address, text.trim())
                                        }
                                        editNameField.visible = false
                                        deviceNameLabel.visible = true
                                        saving = false
                                    }

                                    Keys.onReturnPressed: finishEdit(true)
                                    Keys.onEscapePressed: finishEdit(false)
                                    onActiveFocusChanged: {
                                        if (!activeFocus && visible) {
                                            finishEdit(true)
                                        }
                                    }
                                }

                                Button {
                                    id: editNameBtn
                                    visible: !model.isLocal && !editNameField.visible
                                    implicitWidth: 20
                                    implicitHeight: 20
                                    contentItem: Label {
                                        text: "✏️"
                                        horizontalAlignment: Text.AlignHCenter
                                        font.pixelSize: 12
                                    }
                                    background: Rectangle {
                                        color: parent.hovered ? Theme.hoverColor : "transparent"
                                        radius: 4
                                    }
                                    ToolTip.text: qsTr("Rename")
                                    ToolTip.visible: hovered
                                    onClicked: {
                                        editNameField.text = model.name
                                        editNameField.visible = true
                                        deviceNameLabel.visible = false
                                        editNameField.forceActiveFocus()
                                        editNameField.selectAll()
                                    }
                                }
                            }

                            Label {
                                text: model.address + (model.port > 0 ? ":" + model.port : "")
                                color: Theme.textSecondary
                                font.pixelSize: 12
                            }
                        }

                        Rectangle {
                            Layout.preferredHeight: 24
                            Layout.preferredWidth: 48
                            color: model.isLocal ? Theme.accentColor : Theme.successColor
                            radius: 12

                            Label {
                                anchors.centerIn: parent
                                text: model.status
                                color: Theme.textOnAccentColor
                                font.pixelSize: 11
                            }
                        }

                        Button {
                            visible: !model.isLocal
                            implicitWidth: 28
                            implicitHeight: 28
                            contentItem: Label {
                                text: "💬"
                                horizontalAlignment: Text.AlignHCenter
                                font.pixelSize: 14
                            }
                            background: Rectangle {
                                color: parent.hovered ? Theme.hoverColor : "transparent"
                                radius: 4
                            }
                            ToolTip.text: qsTr("Send Message")
                            ToolTip.visible: hovered
                            onClicked: {
                                if (mainWindow !== null) {
                                    mainWindow.openMessagePage(model.address, model.name, false)
                                }
                            }
                        }
                    }
                }
            }
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 48
            color: Theme.surfaceColor
            radius: 4

            RowLayout {
                anchors.fill: parent
                anchors.margins: 12

                Label {
                    text: qsTr("💡 Tip: Make sure devices are on the same LAN and NetShare is running on the other device")
                    color: Theme.textSecondary
                    font.pixelSize: 12
                }

                Item { Layout.fillWidth: true }

                Label {
                    text: scanTimer.running ? qsTr("Scanning...") : qsTr("Ready")
                    color: scanTimer.running ? Theme.accentColor : Theme.textSecondary
                    font.pixelSize: 12
                }
            }
        }
    }

}
