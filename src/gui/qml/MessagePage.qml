import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import NetShare

Rectangle {
    id: root
    color: Theme.backgroundColor

    signal closeMessage()

    Rectangle {
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.margins: 12
        width: 32
        height: 32
        radius: 4
        color: closeMsgBtn.containsMouse ? Theme.hoverColor : "transparent"
        z: 10

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
            id: closeMsgBtn
            anchors.fill: parent
            hoverEnabled: true
            cursorShape: Qt.PointingHandCursor
            onClicked: root.closeMessage()
        }
    }

    property string selectedUserAddress: ""
    property string selectedUserName: ""
    property bool selectedUserIsAnonymous: false

    Timer {
        id: refreshTimer
        interval: 3000
        running: true
        repeat: true
        onTriggered: refreshUserList()
    }

    function refreshUserList() {
        if (typeof chatService === 'undefined') return
        var users = chatService.getUserList()
        var items = []
        for (var i = 0; i < users.length; i++) {
            var u = users[i]
            items.push({
                name: u.name,
                address: u.address,
                port: u.port,
                isOnline: u.isOnline,
                deviceType: u.deviceType,
                unreadCount: u.unreadCount,
                isAnonymous: u.isAnonymous,
                lastMessage: u.lastMessage || ""
            })
        }
        items.sort(function(a, b) {
            if (a.unreadCount > 0 && b.unreadCount === 0) return -1
            if (a.unreadCount === 0 && b.unreadCount > 0) return 1
            if (a.isOnline && !b.isOnline) return -1
            if (!a.isOnline && b.isOnline) return 1
            return 0
        })
        userModel.clear()
        for (var j = 0; j < items.length; j++) {
            userModel.append(items[j])
        }
    }

    function refreshChatHistory() {
        if (typeof chatService === 'undefined' || selectedUserAddress === "") return
        var history = chatService.getChatHistory(selectedUserAddress)
        chatModel.clear()
        for (var i = 0; i < history.length; i++) {
            var m = history[i]
            chatModel.append({
                msgId: m.msgId,
                fromUser: m.fromUser,
                content: m.content,
                timestamp: m.timestamp,
                isSent: m.isSent,
                sendFailed: m.sendFailed
            })
        }
        chatListView.positionViewAtEnd()
    }

    function selectUser(address, name, isAnonymous) {
        selectedUserAddress = address
        selectedUserName = name
        selectedUserIsAnonymous = isAnonymous
        if (typeof chatService !== 'undefined') {
            chatService.clearUnread(address)
        }
        refreshChatHistory()
    }

    function sendMessage() {
        if (typeof chatService === 'undefined' || selectedUserAddress === "") return
        var text = messageInput.text.trim()
        if (text === "") return

        var userModelEntry = null
        for (var i = 0; i < userModel.count; i++) {
            var entry = userModel.get(i)
            if (entry.address === selectedUserAddress) {
                userModelEntry = entry
                break
            }
        }

        var port = userModelEntry ? userModelEntry.port : 0
        chatService.sendMessage(selectedUserAddress, port, text)
        messageInput.text = ""
        refreshChatHistory()
    }

    Connections {
        target: typeof chatService !== 'undefined' ? chatService : null
        function onMessageReceived(fromAddress) {
            refreshUserList()
            if (fromAddress === selectedUserAddress) {
                refreshChatHistory()
            }
        }
        function onUnreadCountChanged() {
            refreshUserList()
        }
        function onUserListChanged() {
            refreshUserList()
        }
        function onMessageSent(toAddress) {
            if (toAddress === selectedUserAddress) {
                refreshChatHistory()
            }
        }
        function onMessageSendFailed(toAddress, errorMsg) {
            if (toAddress === selectedUserAddress) {
                refreshChatHistory()
            }
        }
    }

    Component.onCompleted: {
        refreshUserList()
    }

    RowLayout {
        anchors.fill: parent
        spacing: 0

        Rectangle {
            Layout.preferredWidth: 260
            Layout.fillHeight: true
            color: Theme.sidebarColor

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 0
                spacing: 0

                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 56
                    color: "transparent"

                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: 16
                        anchors.rightMargin: 16

                        Label {
                            text: qsTr("消息")
                            font.pixelSize: 20
                            font.bold: true
                            color: Theme.textColor
                        }

                        Item { Layout.fillWidth: true }
                    }
                }

                ListView {
                    id: userListView
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    clip: true

                    model: ListModel {
                        id: userModel
                    }

                    delegate: Rectangle {
                        width: userListView.width
                        height: 64
                        color: selectedUserAddress === model.address ? Theme.accentColor :
                               (userMouseArea.containsMouse ? Theme.hoverColor : "transparent")

                        MouseArea {
                            id: userMouseArea
                            anchors.fill: parent
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            onClicked: {
                                selectUser(model.address, model.name, model.isAnonymous)
                            }
                        }

                        RowLayout {
                            anchors.fill: parent
                            anchors.leftMargin: 12
                            anchors.rightMargin: 12
                            spacing: 10

                            Label {
                                text: model.deviceType === "mobile" ? "📱" :
                                      model.deviceType === "browser" ? "🌐" : "💻"
                                font.pixelSize: 24
                            }

                            ColumnLayout {
                                Layout.fillWidth: true
                                spacing: 2

                                RowLayout {
                                    Layout.fillWidth: true
                                    Label {
                                        text: model.name
                                        font.pixelSize: 14
                                        color: selectedUserAddress === model.address ? Theme.textOnAccentColor : Theme.textColor
                                        elide: Text.ElideRight
                                        Layout.fillWidth: true
                                    }
                                    Rectangle {
                                        visible: model.unreadCount > 0
                                        width: 20
                                        height: 20
                                        radius: 10
                                        color: Theme.errorColor

                                        Label {
                                            anchors.centerIn: parent
                                            text: model.unreadCount > 99 ? "99+" : model.unreadCount
                                            font.pixelSize: 10
                                            color: Theme.textOnAccentColor
                                        }
                                    }
                                }

                                Label {
                                    text: model.lastMessage || model.address
                                    font.pixelSize: 11
                                    color: selectedUserAddress === model.address ? Theme.textOnAccentSecondaryColor : Theme.textSecondary
                                    elide: Text.ElideRight
                                    Layout.fillWidth: true
                                }
                            }

                            Label {
                                text: model.isOnline ? "●" : "○"
                                font.pixelSize: 10
                                color: model.isOnline ? Theme.successColor : Theme.textSecondary
                            }
                        }
                    }
                }
            }
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.fillHeight: true
            color: Theme.backgroundColor

            ColumnLayout {
                anchors.fill: parent
                spacing: 0

                Rectangle {
                    visible: selectedUserAddress !== ""
                    Layout.fillWidth: true
                    Layout.preferredHeight: 48
                    color: Theme.surfaceColor

                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: 16
                        anchors.rightMargin: 16

                        Label {
                            text: selectedUserName
                            font.pixelSize: 16
                            font.bold: true
                            color: Theme.textColor
                        }

                        Label {
                            text: selectedUserIsAnonymous ? qsTr("(移动端用户)") : ""
                            font.pixelSize: 12
                            color: Theme.textSecondary
                        }

                        Item { Layout.fillWidth: true }
                    }
                }

                Item {
                    visible: selectedUserAddress === ""
                    Layout.fillWidth: true
                    Layout.fillHeight: true

                    Label {
                        anchors.centerIn: parent
                        text: qsTr("选择一个用户开始聊天")
                        font.pixelSize: 16
                        color: Theme.textSecondary
                    }
                }

                ListView {
                    id: chatListView
                    visible: selectedUserAddress !== ""
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    Layout.margins: 12
                    clip: true
                    spacing: 8

                    model: ListModel {
                        id: chatModel
                    }

                    delegate: Item {
                        width: chatListView.width
                        height: msgBubble.height + 16

                        Rectangle {
                            id: msgBubble
                            anchors.right: model.isSent ? parent.right : undefined
                            anchors.left: model.isSent ? undefined : parent.left
                            anchors.rightMargin: model.isSent ? 0 : 0
                            anchors.leftMargin: model.isSent ? 0 : 0
                            width: Math.min(implicitWidth, chatListView.width * 0.7)
                            height: msgText.height + msgTime.height + 16
                            color: model.sendFailed ? Theme.errorColor :
                                   (model.isSent ? Theme.accentColor : Theme.surfaceColor)
                            radius: 12

                            property real implicitWidth: Math.max(msgText.implicitWidth, msgTime.implicitWidth) + 28

                            ColumnLayout {
                                anchors.fill: parent
                                anchors.margins: 8
                                spacing: 4

                                TextEdit {
                                    id: msgText
                                    Layout.fillWidth: true
                                    text: model.content
                                    font.pixelSize: 14
                                    color: model.isSent ? Theme.textOnAccentColor : Theme.textColor
                                    wrapMode: TextEdit.Wrap
                                    readOnly: true
                                    selectByMouse: true
                                    padding: 0
                                }

                                Label {
                                    id: msgTime
                                    Layout.fillWidth: true
                                    text: {
                                        var ts = model.timestamp
                                        if (ts.length > 19) ts = ts.substring(11, 19)
                                        else if (ts.length > 10) ts = ts.substring(11)
                                        if (model.sendFailed) return ts + " ⚠ " + qsTr("Send failed")
                                        return ts
                                    }
                                    font.pixelSize: 10
                                    color: model.isSent ? Theme.textOnAccentSecondaryColor : Theme.textSecondary
                                    horizontalAlignment: Text.Right
                                }
                            }
                        }
                    }
                }

                Rectangle {
                    visible: selectedUserAddress !== ""
                    Layout.fillWidth: true
                    Layout.preferredHeight: 56
                    color: Theme.surfaceColor

                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: 12
                        anchors.rightMargin: 12
                        spacing: 8

                        TextField {
                            id: messageInput
                            Layout.fillWidth: true
                            placeholderText: qsTr("输入消息...")
                            color: Theme.textColor
                            placeholderTextColor: Theme.textSecondary
                            selectionColor: Theme.accentColor
                            selectedTextColor: Theme.textOnAccentColor
                            font.pixelSize: 14

                            background: Rectangle {
                                color: Theme.backgroundColor
                                border.color: messageInput.activeFocus ? Theme.accentColor : Theme.borderColor
                                radius: 8
                            }

                            onAccepted: {
                                sendMessage()
                            }
                        }

                        Button {
                            text: qsTr("发送")
                            enabled: messageInput.text.trim().length > 0
                            onClicked: sendMessage()

                            contentItem: Label {
                                text: parent.text
                                color: parent.enabled ? Theme.textOnAccentColor : Theme.textSecondary
                                horizontalAlignment: Text.AlignHCenter
                                font.pixelSize: 14
                            }

                            background: Rectangle {
                                color: parent.enabled ? Theme.accentColor : Theme.borderColor
                                radius: 8
                            }
                        }
                    }
                }
            }
        }
    }
}
