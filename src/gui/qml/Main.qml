import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Window
import NetShare

ApplicationWindow {
    id: root
    width: 1024
    height: 768
    minimumWidth: 800
    minimumHeight: 600
    visible: false
    title: qsTr("NetShare - 局域网文件共享")

    // Frameless window - custom title bar
    flags: Qt.Window | Qt.FramelessWindowHint

    color: Theme.backgroundColor

    property bool isSettingsPage: false

    function switchToPage(pageIndex) {
        if (pageIndex >= 0 && pageIndex < pageStack.length) {
            menuList.currentIndex = pageIndex
            root.isSettingsPage = (pageIndex === pageStack.length - 1)
            stackView.replace(pageStack[pageIndex])
        }
        root.show()
        root.raise()
        root.requestActivate()
    }

    onClosing: function(close) {
        if (typeof settingsManager !== 'undefined' && settingsManager.getBool("General/MinimizeToTray", true)) {
            close.accepted = false
            root.hide()
        }
    }

    function addDroppedFiles(urls) {
        if (typeof shareManager === 'undefined') return
        for (var i = 0; i < urls.length; i++) {
            var path = urls[i].toString()
            if (path.startsWith("file:///")) {
                path = path.substring(8)
            } else if (path.startsWith("file://")) {
                path = path.substring(7)
            }
            if (path === "") continue
            shareManager.createShareAuto(path, 24, 0, "")
        }
        switchToPage(0)
    }

    DropArea {
        id: dropArea
        anchors.fill: parent
        z: -1

        property bool containsDragExternal: false

        onEntered: function(drag) {
            if (drag.hasUrls) {
                drag.accepted = true
                containsDragExternal = true
            } else {
                drag.accepted = false
                containsDragExternal = false
            }
        }

        onDropped: function(drop) {
            containsDragExternal = false
            if (drop.hasUrls) {
                root.addDroppedFiles(drop.urls)
            }
        }

        onExited: {
            containsDragExternal = false
        }

        Rectangle {
            anchors.fill: parent
            color: "#80000000"
            visible: dropArea.containsDragExternal
            z: 9999

            Rectangle {
                anchors.centerIn: parent
                width: 300
                height: 120
                radius: 12
                color: Theme.surfaceColor
                border.color: Theme.accentColor
                border.width: 2

                ColumnLayout {
                    anchors.centerIn: parent
                    spacing: 8

                    Label {
                        text: "📁"
                        font.pixelSize: 36
                        Layout.alignment: Qt.AlignHCenter
                    }

                    Label {
                        text: qsTr("拖放文件到发送列表")
                        color: Theme.textColor
                        font.pixelSize: 16
                        Layout.alignment: Qt.AlignHCenter
                    }
                }
            }
        }
    }

    Component.onCompleted: {
        console.log("MainWindow loaded")
    }

    // Custom title bar
    Rectangle {
        id: titleBar
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        height: 32
        color: Theme.backgroundColor

        // Draggable area for moving the window
        MouseArea {
            id: dragArea
            anchors.fill: parent
            anchors.rightMargin: messageBtn.width + settingsBtn.width + minimizeBtn.width + maximizeBtn.width + closeBtn.width
            onPressed: function(mouse) {
                if (mouse.button === Qt.LeftButton) {
                    root.startSystemMove()
                }
            }
            onDoubleClicked: {
                root.visibility === Window.Maximized ? root.showNormal() : root.showMaximized()
            }
        }

        // App title
        Label {
            anchors.left: parent.left
            anchors.leftMargin: 12
            anchors.verticalCenter: parent.verticalCenter
            text: root.title
            color: Theme.textSecondary
            font.pixelSize: 12
        }

        // Message button
        Rectangle {
            id: messageBtn
            anchors.right: settingsBtn.left
            anchors.verticalCenter: parent.verticalCenter
            width: 46
            height: titleBar.height
            color: messageMouseArea.containsMouse ? "#3e3e42" : "transparent"

            Label {
                anchors.centerIn: parent
                text: "\uD83D\uDCAC"
                font.pixelSize: 16
                color: Theme.textColor
            }

            Rectangle {
                visible: typeof chatService !== 'undefined' && chatService.totalUnreadCount > 0
                anchors.right: parent.right
                anchors.top: parent.top
                anchors.rightMargin: 8
                anchors.topMargin: 4
                width: 18
                height: 18
                radius: 9
                color: Theme.errorColor

                Label {
                    anchors.centerIn: parent
                    text: {
                        if (typeof chatService === 'undefined') return ""
                        var c = chatService.totalUnreadCount
                        return c > 99 ? "99+" : c
                    }
                    font.pixelSize: 9
                    color: "#ffffff"
                }
            }

            MouseArea {
                id: messageMouseArea
                anchors.fill: parent
                hoverEnabled: true
                cursorShape: Qt.PointingHandCursor
                onClicked: {
                    menuList.currentIndex = -1
                    root.isSettingsPage = true
                    stackView.replace(messagePage)
                }
            }
        }

        // Settings button
        Rectangle {
            id: settingsBtn
            anchors.right: minimizeBtn.left
            anchors.verticalCenter: parent.verticalCenter
            width: 46
            height: titleBar.height
            color: settingsMouseArea.containsMouse ? "#3e3e42" : "transparent"

            Label {
                anchors.centerIn: parent
                text: "\u2699"
                font.pixelSize: 16
                color: Theme.textColor
            }

            MouseArea {
                id: settingsMouseArea
                anchors.fill: parent
                hoverEnabled: true
                cursorShape: Qt.PointingHandCursor
                onClicked: {
                    menuList.currentIndex = -1
                    root.isSettingsPage = true
                    stackView.replace(settingsPage)
                }
            }
        }

        // Minimize button
        Rectangle {
            id: minimizeBtn
            anchors.right: maximizeBtn.left
            anchors.verticalCenter: parent.verticalCenter
            width: 46
            height: titleBar.height
            color: minMouseArea.containsMouse ? "#3e3e42" : "transparent"

            Rectangle {
                anchors.centerIn: parent
                width: 10
                height: 1
                color: Theme.textColor
            }

            MouseArea {
                id: minMouseArea
                anchors.fill: parent
                hoverEnabled: true
                onClicked: root.showMinimized()
            }
        }

        // Maximize/Restore button
        Rectangle {
            id: maximizeBtn
            anchors.right: closeBtn.left
            anchors.verticalCenter: parent.verticalCenter
            width: 46
            height: titleBar.height
            color: maxMouseArea.containsMouse ? "#3e3e42" : "transparent"

            Rectangle {
                anchors.centerIn: parent
                width: 10
                height: 10
                radius: 0
                color: "transparent"
                border.color: Theme.textColor
                border.width: 1
            }

            MouseArea {
                id: maxMouseArea
                anchors.fill: parent
                hoverEnabled: true
                onClicked: {
                    root.visibility === Window.Maximized ? root.showNormal() : root.showMaximized()
                }
            }
        }

        // Close button
        Rectangle {
            id: closeBtn
            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            width: 46
            height: titleBar.height
            color: closeMouseArea.containsMouse ? "#e81123" : "transparent"

            // X icon
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
                id: closeMouseArea
                anchors.fill: parent
                hoverEnabled: true
                onClicked: root.close()
            }
        }
    }

    // Resize handles for frameless window
    MouseArea {
        anchors.left: parent.left
        anchors.top: titleBar.bottom
        anchors.bottom: parent.bottom
        width: 4
        cursorShape: Qt.SizeHorCursor
        onPressed: root.startSystemResize(Qt.LeftEdge)
    }
    MouseArea {
        anchors.right: parent.right
        anchors.top: titleBar.bottom
        anchors.bottom: parent.bottom
        width: 4
        cursorShape: Qt.SizeHorCursor
        onPressed: root.startSystemResize(Qt.RightEdge)
    }
    MouseArea {
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        height: 4
        cursorShape: Qt.SizeVerCursor
        onPressed: root.startSystemResize(Qt.TopEdge)
    }
    MouseArea {
        anchors.bottom: parent.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        height: 4
        cursorShape: Qt.SizeVerCursor
        onPressed: root.startSystemResize(Qt.BottomEdge)
    }

    footer: Rectangle {
        height: 28
        color: Theme.sidebarColor

        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: 12
            anchors.rightMargin: 12

            Label {
                id: statusBarLeft
                text: typeof shareManager !== 'undefined' ? qsTr("IP：%1").arg(shareManager.localIp) : qsTr("IP：--")
                color: Theme.textSecondary
                font.pixelSize: 11
            }

            Item { Layout.fillWidth: true }

            Label {
                id: statusBarCenter
                text: typeof shareManager !== 'undefined' ? qsTr("分享：%1 个活跃").arg(shareManager.getActiveShareCount()) : qsTr("分享：0 个活跃")
                color: Theme.textSecondary
                font.pixelSize: 11
            }

            Item { Layout.fillWidth: true }

            Label {
                id: statusBarRight
                text: qsTr("就绪")
                color: Theme.textSecondary
                font.pixelSize: 11
            }
        }
    }

    Rectangle {
        id: sidebar
        anchors.left: parent.left
        anchors.top: titleBar.bottom
        anchors.bottom: parent.bottom
        width: root.isSettingsPage ? 0 : 200
        color: Theme.sidebarColor
        clip: true

        Behavior on width {
            NumberAnimation { duration: 200; easing.type: Easing.InOutQuad }
        }

        ListView {
            id: menuList
            anchors.fill: parent
            anchors.topMargin: 8

            model: ListModel {
                ListElement { title: qsTr("发送"); icon: "qrc:/qt/qml/NetShare/qml/icons/send.svg"; isSvg: true }
                ListElement { title: qsTr("接收"); icon: "qrc:/qt/qml/NetShare/qml/icons/receive.svg"; isSvg: true }
                ListElement { title: qsTr("传输"); icon: "⇅"; isSvg: false }
                ListElement { title: qsTr("LAN"); icon: "🌐"; isSvg: false }
            }

            delegate: Item {
                width: parent.width
                height: 48

                Rectangle {
                    id: navBg
                    anchors.fill: parent
                    anchors.leftMargin: 8
                    anchors.rightMargin: 8
                    radius: 4
                    color: {
                        if (ListView.isCurrentItem) return Theme.accentColor
                        if (navMouseArea.containsMouse) return "#3e3e42"
                        return "transparent"
                    }

                    Behavior on color {
                        ColorAnimation { duration: 150 }
                    }

                    MouseArea {
                        id: navMouseArea
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: {
                            menuList.currentIndex = index
                            root.isSettingsPage = false
                            stackView.replace(pageStack[index])
                        }
                    }

                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: 12

                        Loader {
                            sourceComponent: isSvg ? svgIconComponent : emojiIconComponent
                            property string iconSource: icon
                            property bool isSelected: ListView.isCurrentItem

                            Component {
                                id: svgIconComponent
                                Image {
                                    source: iconSource
                                    sourceSize.width: 18
                                    sourceSize.height: 18
                                    opacity: isSelected ? 1.0 : 0.7
                                }
                            }

                            Component {
                                id: emojiIconComponent
                                Label {
                                    text: iconSource
                                    font.pixelSize: 18
                                    color: isSelected ? "#ffffff" : Theme.textColor
                                }
                            }
                        }

                        Label {
                            text: title
                            color: ListView.isCurrentItem ? "#ffffff" : Theme.textColor
                            font.pixelSize: 14
                        }

                        Item { Layout.fillWidth: true }
                    }
                }
            }
        }
    }

    StackView {
        id: stackView
        anchors.left: sidebar.right
        anchors.top: titleBar.bottom
        anchors.right: parent.right
        anchors.bottom: parent.bottom

        replaceEnter: Transition {}
        replaceExit: Transition {}

        initialItem: shareManagementPage
    }

    Component {
        id: messagePage
        MessagePage {
            onCloseMessage: {
                menuList.currentIndex = 0
                root.isSettingsPage = false
                stackView.replace(shareManagementPage)
            }
        }
    }

    Component {
        id: receiveManagementPage
        ReceiveManagement {}
    }

    Component {
        id: shareManagementPage
        ShareManagement {}
    }

    Component {
        id: transferPage
        TransferList {}
    }

    Component {
        id: devicePage
        DeviceDiscovery {}
    }

    Component {
        id: settingsPage
        SettingsPage {
            onCloseSettings: {
                menuList.currentIndex = 0
                root.isSettingsPage = false
                stackView.replace(shareManagementPage)
            }
        }
    }

    property list<QtObject> pageStack: [
        shareManagementPage,
        receiveManagementPage,
        transferPage,
        devicePage,
        settingsPage
    ]
}
