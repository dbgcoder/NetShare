import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs
import NetShare

Rectangle {
    id: root
    color: Theme.backgroundColor

    property var receivedFiles: []
    property int filterIndex: 0
    property string storagePath: typeof settingsManager !== 'undefined'
        ? settingsManager.getString("Receive/StoragePath", "")
        : ""
    property string effectiveStoragePath: storagePath.length > 0
        ? storagePath
        : (typeof settingsManager !== 'undefined'
            ? settingsManager.getDefaultUploadPath()
            : "")

    function formatBytes(bytes) {
        if (bytes <= 0) return "0 B"
        var units = ["B", "KB", "MB", "GB", "TB"]
        var i = Math.floor(Math.log(bytes) / Math.log(1024))
        if (i >= units.length) i = units.length - 1
        return (bytes / Math.pow(1024, i)).toFixed(i === 0 ? 0 : 1) + " " + units[i]
    }

    function refreshFiles() {
        if (typeof shareManager === 'undefined') return
        receivedFiles = shareManager.getReceivedFiles()
    }

    function fileIcon(fileName, isFolder) {
        if (isFolder) return "📁"
        var ext = fileName.split('.').pop().toLowerCase()
        if (["jpg", "jpeg", "png", "gif", "bmp", "webp", "svg"].indexOf(ext) >= 0) return "🖼"
        if (["mp4", "avi", "mkv", "mov", "wmv", "flv"].indexOf(ext) >= 0) return "🎬"
        if (["mp3", "wav", "flac", "aac", "ogg", "wma"].indexOf(ext) >= 0) return "🎵"
        if (["pdf"].indexOf(ext) >= 0) return "📕"
        if (["doc", "docx"].indexOf(ext) >= 0) return "📄"
        if (["xls", "xlsx"].indexOf(ext) >= 0) return "📊"
        if (["zip", "rar", "7z", "tar", "gz"].indexOf(ext) >= 0) return "📦"
        if (["txt", "log", "md", "json", "xml", "csv"].indexOf(ext) >= 0) return "📝"
        return "📎"
    }

    function getFilteredFiles() {
        var filtered = []
        var now = new Date()
        var todayStart = new Date(now.getFullYear(), now.getMonth(), now.getDate())
        var weekStart = new Date(todayStart)
        weekStart.setDate(weekStart.getDate() - weekStart.getDay())

        for (var i = 0; i < receivedFiles.length; i++) {
            var file = receivedFiles[i]
            // Date filter
            if (root.filterIndex === 1) {
                // Today
                var created = file.createdAt
                if (!created || created < todayStart) continue
            } else if (root.filterIndex === 2) {
                // This week
                var created = file.createdAt
                if (!created || created < weekStart) continue
            }
            // Search filter
            if (searchField.text.length > 0) {
                var fn = file.filePath.split('/').pop().split('\\').pop().toLowerCase()
                if (fn.indexOf(searchField.text.toLowerCase()) < 0) continue
            }
            filtered.push(file)
        }
        return filtered
    }

    function copyToClipboard(text) {
        shareManager.copyToClipboard(text)
        showToast(qsTr("已复制到剪贴板"))
    }

    function showToast(msg) {
        toast.show(msg)
    }

    function shareAndShowQr(token) {
        var newToken = shareManager.shareReceivedFile(token)
        if (newToken.length > 0) {
            var url = "http://" + shareManager.localIp + ":8080/s/" + newToken
            forwardQrDialog.shareUrl = url
            forwardQrImage.source = qrCodeHelper.generateDataUrl(url, 600)
            forwardQrDialog.title = qsTr("分享成功 - 扫描二维码下载文件")
            forwardQrDialog.open()
        }
    }

    function copyFileLink(token) {
        var url = "http://" + shareManager.localIp + ":8080/s/" + token
        copyToClipboard(url)
    }

    Component.onCompleted: refreshFiles()

    FolderDialog {
        id: folderDialog
        currentFolder: "file:///" + root.effectiveStoragePath
        onAccepted: {
            var path = folderDialog.selectedFolder.toString()
            // Remove file:/// prefix
            path = path.replace(/^file:\/\/\//, "")
            root.storagePath = path
            if (typeof settingsManager !== 'undefined') {
                settingsManager.setValue("Receive/StoragePath", path)
            }
        }
    }

    Connections {
        target: typeof shareManager !== 'undefined' ? shareManager : null
        function onShareCreated(token) { refreshFiles() }
        function onReceivedFileDeleted(token) { refreshFiles() }
    }

    // Receive QR Code Dialog
    Dialog {
        id: receiveQrDialog
        title: qsTr("接收二维码")
        standardButtons: Dialog.Close
        modal: true
        width: 340
        height: 480
        parent: Overlay.overlay
        anchors.centerIn: parent
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside

        background: Rectangle {
            color: Theme.surfaceColor
            radius: 8
            border.color: Theme.sidebarColor
        }

        ColumnLayout {
            anchors.fill: parent
            spacing: 16

            Label {
                id: receiveQrUrl
                Layout.fillWidth: true
                text: "http://" + shareManager.localIp + ":8080/receive"
                color: Theme.textColor
                font.pixelSize: 12
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.WordWrap
            }

            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: 300
                color: "#ffffff"
                radius: 4

                Image {
                    id: receiveQrImage
                    anchors.centerIn: parent
                    width: 280
                    height: 280
                    smooth: false
                    fillMode: Image.PreserveAspectFit
                    source: ""
                }
            }

            Label {
                Layout.fillWidth: true
                text: qsTr("扫描二维码发送文件")
                color: Theme.textSecondary
                font.pixelSize: 12
                horizontalAlignment: Text.AlignHCenter
            }

            Button {
                Layout.alignment: Qt.AlignHCenter
                text: qsTr("复制链接")
                contentItem: Label {
                    text: parent.text
                    color: "#ffffff"
                    horizontalAlignment: Text.AlignHCenter
                }
                background: Rectangle {
                    color: Theme.accentColor
                    radius: 4
                    implicitWidth: 100
                    implicitHeight: 32
                }
                onClicked: copyToClipboard(receiveQrUrl.text)
            }
        }

        onOpened: {
            var url = "http://" + shareManager.localIp + ":8080/receive?t=" + Date.now()
            receiveQrImage.source = qrCodeHelper.generateDataUrl(url, 600)
        }
    }

    // Forward Share QR Code Dialog
    Dialog {
        id: forwardQrDialog
        title: qsTr("分享二维码")
        standardButtons: Dialog.Close
        modal: true
        width: 340
        height: 480
        parent: Overlay.overlay
        anchors.centerIn: parent
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside

        background: Rectangle {
            color: Theme.surfaceColor
            radius: 8
            border.color: Theme.sidebarColor
        }

        property string shareUrl: ""

        ColumnLayout {
            anchors.fill: parent
            spacing: 16

            Label {
                id: forwardQrUrl
                Layout.fillWidth: true
                text: forwardQrDialog.shareUrl
                color: Theme.textColor
                font.pixelSize: 12
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.WordWrap
            }

            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: 300
                color: "#ffffff"
                radius: 4

                Image {
                    id: forwardQrImage
                    anchors.centerIn: parent
                    width: 280
                    height: 280
                    smooth: false
                    fillMode: Image.PreserveAspectFit
                    source: ""
                }
            }

            Label {
                Layout.fillWidth: true
                text: qsTr("扫描二维码下载文件")
                color: Theme.textSecondary
                font.pixelSize: 12
                horizontalAlignment: Text.AlignHCenter
            }

            Button {
                Layout.alignment: Qt.AlignHCenter
                text: qsTr("复制链接")
                contentItem: Label {
                    text: parent.text
                    color: "#ffffff"
                    horizontalAlignment: Text.AlignHCenter
                }
                background: Rectangle {
                    color: Theme.accentColor
                    radius: 4
                    implicitWidth: 100
                    implicitHeight: 32
                }
                onClicked: copyToClipboard(forwardQrDialog.shareUrl)
            }
        }
    }

    // Toast notification
    Rectangle {
        id: toast
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 60
        anchors.horizontalCenter: parent.horizontalCenter
        width: toastLabel.width + 24
        height: 36
        radius: 18
        color: Theme.surfaceColor
        border.color: Theme.borderColor
        opacity: 0

        property string message: ""

        Label {
            id: toastLabel
            anchors.centerIn: parent
            text: toast.message
            color: Theme.textColor
            font.pixelSize: 12
        }

        function show(msg) {
            message = msg
            opacity = 1
            toastTimer.restart()
        }

        Behavior on opacity { NumberAnimation { duration: 200 } }

        Timer {
            id: toastTimer
            interval: 1500
            onTriggered: toast.opacity = 0
        }
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        // Header
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 56
            color: Theme.backgroundColor

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 24
                anchors.rightMargin: 24

                Label {
                    text: qsTr("接收管理")
                    font.pixelSize: 22
                    font.bold: true
                    color: Theme.textColor
                }

                Item { Layout.fillWidth: true }

                // Receive QR Code button
                Rectangle {
                    Layout.preferredHeight: 28
                    Layout.preferredWidth: receiveQrLabel.width + 20
                    radius: 4
                    color: receiveQrMouse.containsMouse ? Theme.accentColor : "transparent"
                    border.color: Theme.accentColor
                    border.width: 1

                    RowLayout {
                        anchors.centerIn: parent
                        spacing: 4

                        Label {
                            text: "📱"
                            font.pixelSize: 12
                        }

                        Label {
                            id: receiveQrLabel
                            text: qsTr("接收二维码")
                            color: receiveQrMouse.containsMouse ? "#ffffff" : Theme.accentColor
                            font.pixelSize: 12
                        }
                    }

                    MouseArea {
                        id: receiveQrMouse
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: receiveQrDialog.open()
                    }
                }

                Rectangle {
                    Layout.preferredHeight: 28
                    Layout.preferredWidth: pathLabel.width + 20
                    radius: 4
                    color: pathMouse.containsMouse ? "#3e3e42" : "transparent"
                    border.color: Theme.borderColor
                    border.width: 1

                    RowLayout {
                        anchors.centerIn: parent
                        spacing: 4

                        Label {
                            id: pathLabel
                            text: qsTr("存储路径：%1").arg(effectiveStoragePath.length > 0 ? effectiveStoragePath : "~/NetShare/Uploads")
                            color: Theme.textSecondary
                            font.pixelSize: 12
                        }
                    }

                    MouseArea {
                        id: pathMouse
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: folderDialog.open()
                    }
                }
            }
        }

        // Filter bar
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 44
            color: Theme.surfaceColor

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 24
                anchors.rightMargin: 24
                spacing: 8

                Repeater {
                    model: [qsTr("全部"), qsTr("今日"), qsTr("本周")]

                    Rectangle {
                        Layout.preferredHeight: 28
                        Layout.preferredWidth: filterLabel.width + 20
                        radius: 4
                        color: root.filterIndex === index ? Theme.accentColor : "transparent"
                        border.color: root.filterIndex === index ? Theme.accentColor : Theme.borderColor
                        border.width: 1

                        Label {
                            id: filterLabel
                            anchors.centerIn: parent
                            text: modelData
                            color: root.filterIndex === index ? "#ffffff" : Theme.textColor
                            font.pixelSize: 12
                        }

                        MouseArea {
                            anchors.fill: parent
                            cursorShape: Qt.PointingHandCursor
                            onClicked: root.filterIndex = index
                        }
                    }
                }

                Item { Layout.fillWidth: true }

                TextField {
                    id: searchField
                    Layout.preferredWidth: 200
                    Layout.preferredHeight: 28
                    placeholderText: qsTr("搜索文件...")
                    color: Theme.textColor
                    font.pixelSize: 12

                    background: Rectangle {
                        radius: 4
                        color: Theme.backgroundColor
                        border.color: Theme.borderColor
                    }
                }
            }
        }

        // File list
        ListView {
            id: fileListView
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.topMargin: 8
            clip: true

            model: getFilteredFiles()

            delegate: Rectangle {
                width: fileListView.width
                height: 64
                color: index % 2 === 0 ? Theme.backgroundColor : Theme.surfaceColor

                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: 24
                    anchors.rightMargin: 24
                    spacing: 12

                    Label {
                        text: fileIcon(modelData.filePath, modelData.isFolder)
                        font.pixelSize: 24
                    }

                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 2

                        Label {
                            text: {
                                var path = modelData.filePath
                                return path.split('/').pop().split('\\').pop()
                            }
                            color: Theme.textColor
                            font.pixelSize: 14
                            elide: Text.ElideRight
                            Layout.fillWidth: true
                        }

                        RowLayout {
                            spacing: 12

                            Label {
                                text: formatBytes(modelData.fileSize)
                                color: Theme.textSecondary
                                font.pixelSize: 12
                            }

                            Label {
                                text: modelData.downloadCount > 0 ? qsTr("已下载 %1 次").arg(modelData.downloadCount) : qsTr("未下载")
                                color: Theme.textSecondary
                                font.pixelSize: 12
                            }
                        }
                    }

                    // Action buttons
                    RowLayout {
                        spacing: 4

                        Rectangle {
                            width: 32; height: 32; radius: 4
                            color: openMouse.containsMouse ? "#3e3e42" : "transparent"
                            ToolTip.visible: openMouse.containsMouse
                            ToolTip.text: qsTr("打开文件")
                            Label { anchors.centerIn: parent; text: "📂"; font.pixelSize: 14 }
                            MouseArea {
                                id: openMouse
                                anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor
                                onClicked: shareManager.openReceivedFile(modelData.token)
                            }
                        }

                        Rectangle {
                            width: 32; height: 32; radius: 4
                            color: folderMouse.containsMouse ? "#3e3e42" : "transparent"
                            ToolTip.visible: folderMouse.containsMouse
                            ToolTip.text: qsTr("打开文件夹")
                            Label { anchors.centerIn: parent; text: "📁"; font.pixelSize: 14 }
                            MouseArea {
                                id: folderMouse
                                anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor
                                onClicked: shareManager.openReceivedFileFolder(modelData.token)
                            }
                        }

                        Rectangle {
                            width: 32; height: 32; radius: 4
                            color: linkMouse.containsMouse ? "#3e3e42" : "transparent"
                            ToolTip.visible: linkMouse.containsMouse
                            ToolTip.text: qsTr("复制链接")
                            Label { anchors.centerIn: parent; text: "🔗"; font.pixelSize: 14 }
                            MouseArea {
                                id: linkMouse
                                anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor
                                onClicked: {
                                    copyFileLink(modelData.token)
                                }
                            }
                        }

                        Rectangle {
                            width: 32; height: 32; radius: 4
                            color: qrMouse.containsMouse ? "#3e3e42" : "transparent"
                            ToolTip.visible: qrMouse.containsMouse
                            ToolTip.text: qsTr("分享二维码")
                            Label { anchors.centerIn: parent; text: "📱"; font.pixelSize: 14 }
                            MouseArea {
                                id: qrMouse
                                anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor
                                onClicked: {
                                    shareAndShowQr(modelData.token)
                                }
                            }
                        }

                        Rectangle {
                            width: 32; height: 32; radius: 4
                            color: deleteMouse.containsMouse ? "#e81123" : "transparent"
                            ToolTip.visible: deleteMouse.containsMouse
                            ToolTip.text: qsTr("删除")
                            Label { anchors.centerIn: parent; text: "🗑"; font.pixelSize: 14 }
                            MouseArea {
                                id: deleteMouse
                                anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor
                                onClicked: shareManager.deleteReceivedFile(modelData.token)
                            }
                        }
                    }
                }

                Rectangle {
                    anchors.bottom: parent.bottom
                    anchors.left: parent.left; anchors.right: parent.right
                    height: 1; color: Theme.borderColor
                }
            }

            Label {
                anchors.centerIn: parent
                text: qsTr("还没有接收到文件")
                color: Theme.textSecondary
                font.pixelSize: 16
                visible: fileListView.model.length === 0
            }
        }

        // Statistics bar
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 36
            color: Theme.surfaceColor

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 24
                anchors.rightMargin: 24

                Label {
                    text: qsTr("共 %1 个文件").arg(receivedFiles.length)
                    color: Theme.textSecondary; font.pixelSize: 12
                }

                Item { Layout.fillWidth: true }

                Label {
                    text: {
                        var total = 0
                        for (var i = 0; i < receivedFiles.length; i++)
                            total += receivedFiles[i].fileSize
                        return qsTr("总计 %1").arg(formatBytes(total))
                    }
                    color: Theme.textSecondary; font.pixelSize: 12
                }
            }
        }
    }
}
