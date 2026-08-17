import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Window
import QtQuick.Effects
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

    Overlay.modal: Rectangle {
        color: Theme.overlayColor
    }

    palette.windowText: Theme.textColor
    palette.window: Theme.surfaceColor
    palette.toolTipBase: Theme.surfaceColor
    palette.toolTipText: Theme.textColor

    property bool isSettingsPage: false

    property bool isRegistered: authService ? authService.isRegistered() : false
    property bool isTrialMode: authService ? authService.isTrialMode() : false

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

    function openMessagePage(address, name, isAnonymous) {
        menuList.currentIndex = -1
        root.isSettingsPage = true
        stackView.replace(messagePage)
        var msgPage = stackView.currentItem
        if (msgPage && typeof msgPage.selectUser === 'function') {
            msgPage.selectUser(address, name, isAnonymous)
        }
        root.show()
        root.raise()
        root.requestActivate()
    }

    onClosing: function(close) {
        var minimizeToTray = typeof settingsManager !== 'undefined' && settingsManager.getBool("General/MinimizeToTray", true)
        if (minimizeToTray) {
            close.accepted = false
            root.hide()
        } else {
            Qt.quit()
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
            color: Theme.overlayColor
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
        // Initialize navigation selection to first tab (Send)
        menuList.currentIndex = 0
        if (!authService) return

        if (authService.isRegistered()) {
            // 已注册：校验机器码匹配度，<60分需重新注册
            if (!authService.verifyMachineMatch()) {
                console.warn("Machine mismatch detected, requiring re-registration")
                registerPopup.open()
            }
            // 已注册且机器码匹配：不弹窗，直接使用
        } else {
            // 未注册（含试用用户）：弹窗注册页面
            registerPopup.open()
        }
    }

    // 注册弹窗
    RegisterPage {
        id: registerPopup
        parent: Overlay.overlay
    }

    // Root layout: column-based to guarantee all sections visible
    ColumnLayout {
        id: mainLayout
        anchors.fill: parent
        spacing: 0

        // Custom title bar
        Rectangle {
            id: titleBar
            Layout.fillWidth: true
            Layout.preferredHeight: 32
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

        // App title with icon
        RowLayout {
            anchors.left: parent.left
            anchors.leftMargin: 12
            anchors.verticalCenter: parent.verticalCenter
            spacing: 6

            Image {
                source: "qrc:/icons/netshare.png"
                width: 16
                height: 16
                sourceSize.width: 16
                sourceSize.height: 16
                fillMode: Image.PreserveAspectFit
                smooth: true
            }

            Label {
                text: root.title + (root.isTrialMode ? qsTr(" (Trial)") : "")
                color: Theme.textSecondary
                font.pixelSize: 12
            }
        }

        // Message button
        Rectangle {
            id: messageBtn
            anchors.right: settingsBtn.left
            anchors.verticalCenter: parent.verticalCenter
            width: 46
            height: titleBar.height
            color: messageMouseArea.containsMouse ? Theme.hoverColor : "transparent"

            Label {
                anchors.centerIn: parent
                text: "\uD83D\uDCAC"
                font.pixelSize: 16
                color: Theme.textColor
            }

            Rectangle {
                visible: chatService && chatService.totalUnreadCount > 0
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
                        if (!chatService) return ""
                        var c = chatService.totalUnreadCount
                        return c > 99 ? "99+" : c
                    }
                    font.pixelSize: 9
                    color: Theme.textOnAccentColor
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
            color: settingsMouseArea.containsMouse ? Theme.hoverColor : "transparent"

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
            color: minMouseArea.containsMouse ? Theme.hoverColor : "transparent"

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
            color: maxMouseArea.containsMouse ? Theme.hoverColor : "transparent"

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
            color: closeMouseArea.containsMouse ? Theme.closeHoverColor : "transparent"

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

        // Main content area
        Item {
            id: contentArea
            Layout.fillWidth: true
            Layout.fillHeight: true

            Rectangle {
                id: sidebar
                anchors.left: parent.left
                anchors.top: parent.top
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

                        // Explicit binding to currentIndex - avoids unreliable ListView.isCurrentItem
                        readonly property bool isCurrent: menuList.currentIndex === index

                        // Selection indicator - left accent bar
                        Rectangle {
                            anchors.left: parent.left
                            anchors.top: parent.top
                            anchors.bottom: parent.bottom
                            anchors.topMargin: 10
                            anchors.bottomMargin: 10
                            width: 3
                            radius: 2
                            color: isCurrent ? Theme.accentColor : "transparent"
                            visible: isCurrent
                        }

                        Rectangle {
                            id: navBg
                            anchors.fill: parent
                            anchors.leftMargin: 8
                            anchors.rightMargin: 8
                            radius: 4
                            // Selection background takes priority over hover
                            color: isCurrent ? Theme.accentColor
                                 : (navMouseArea.containsMouse ? Theme.hoverColor : "transparent")

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
                                spacing: 8

                                Loader {
                                    sourceComponent: isSvg ? svgIconComponent : emojiIconComponent
                                    Layout.preferredWidth: 24
                                    Layout.preferredHeight: 24
                                    Layout.alignment: Qt.AlignVCenter
                                    property string iconSource: icon

                                    Component {
                                        id: svgIconComponent
                                        Item {
                                            width: 20; height: 20
                                            Image {
                                                id: svgIcon
                                                source: iconSource
                                                sourceSize.width: 20
                                                sourceSize.height: 20
                                                visible: false
                                            }
                                            MultiEffect {
                                                anchors.fill: svgIcon
                                                source: svgIcon
                                                colorization: 1.0
                                                colorizationColor: isCurrent ? Theme.textOnAccentColor : Theme.textColor
                                            }
                                        }
                                    }

                                    Component {
                                        id: emojiIconComponent
                                        Label {
                                            text: iconSource
                                            font.pixelSize: 18
                                            color: isCurrent ? Theme.textOnAccentColor : Theme.textColor
                                            verticalAlignment: Text.AlignVCenter
                                            horizontalAlignment: Text.AlignHCenter
                                        }
                                    }
                                }

                                Label {
                                    text: title
                                    color: isCurrent ? Theme.textOnAccentColor : Theme.textColor
                                    font.pixelSize: 14
                                    Layout.alignment: Qt.AlignVCenter
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
                anchors.top: parent.top
                anchors.right: parent.right
                anchors.bottom: parent.bottom

                replaceEnter: Transition {}
                replaceExit: Transition {}

                initialItem: shareManagementPage
            }
        }

        // Status bar
        Rectangle {
            id: statusBar
            Layout.fillWidth: true
            Layout.preferredHeight: 28
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
    }

    // Resize handles for frameless window (outside mainLayout)
    MouseArea {
        anchors.left: parent.left
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        width: 4
        cursorShape: Qt.SizeHorCursor
        onPressed: root.startSystemResize(Qt.LeftEdge)
    }
    MouseArea {
        anchors.right: parent.right
        anchors.top: parent.top
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
        DeviceDiscovery {
            mainWindow: root
        }
    }

    Component {
        id: settingsPage
        SettingsPage {
            onCloseSettings: {
                menuList.currentIndex = 0
                root.isSettingsPage = false
                stackView.replace(shareManagementPage)
            }
            onOpenRegister: {
                registerPopup.open()
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
