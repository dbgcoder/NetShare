import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import NetShare

Rectangle {
    id: root
    color: Theme.backgroundColor

    ListModel {
        id: deviceModel
    }

    Timer {
        id: scanTimer
        interval: 3000
        running: false
        repeat: true
        onTriggered: refreshDevices()
    }

    function refreshDevices() {
        if (typeof mdnsService === 'undefined') return
        var services = mdnsService.getDiscoveredServicesList()
        deviceModel.clear()
        for (var i = 0; i < services.length; i++) {
            var s = services[i]
            deviceModel.append({
                name: s.name || qsTr("Unknown device"),
                address: s.address || "--",
                port: s.port || 0,
                status: qsTr("Online"),
                isLocal: false
            })
        }
        deviceCountLabel.text = qsTr("Found %1 devices").arg(deviceModel.count)
    }

    Component.onCompleted: {
        addLocalDevice()
    }

    function addLocalDevice() {
        var ip = typeof shareManager !== 'undefined' ? shareManager.localIp : "127.0.0.1"
        deviceModel.append({
                name: qsTr("This PC"),
                address: ip,
                port: 8080,
                status: qsTr("This PC"),
                isLocal: true
        })
        deviceCountLabel.text = qsTr("Found %1 devices").arg(deviceModel.count)
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
                    color: "#ffffff"
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
                    color: "#ffffff"
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
                    parent: deviceListView.parent
                    anchors.top: parent.top
                    anchors.bottom: parent.bottom
                    anchors.right: parent.right
                    anchors.rightMargin: -8
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
                    color: deviceMouseArea.containsMouse ? "#333336" : Theme.sidebarColor
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
                                text: model.isLocal ? "🖥" : "💻"
                                font.pixelSize: 20
                            }
                        }

                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 2

                            Label {
                                text: model.name
                                color: Theme.textColor
                                font.pixelSize: 14
                                font.bold: true
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
                                color: "#ffffff"
                                font.pixelSize: 11
                            }
                        }

                        Button {
                            visible: !model.isLocal
                            implicitWidth: 28
                            implicitHeight: 28
                            contentItem: Label {
                                text: "🔗"
                                horizontalAlignment: Text.AlignHCenter
                                font.pixelSize: 14
                            }
                            background: Rectangle {
                                color: parent.hovered ? "#3e3e42" : "transparent"
                                radius: 4
                            }
                            ToolTip.text: qsTr("Open in browser")
                            ToolTip.visible: hovered
                            onClicked: {
                                Qt.openUrlExternally("http://" + model.address + ":" + model.port)
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
