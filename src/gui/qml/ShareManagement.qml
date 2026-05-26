import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Window
import QtQuick.Dialogs
import NetShare

Rectangle {
    id: root
    color: Theme.backgroundColor

    property int currentIndex: 0
    property string copiedText: ""
    property bool shareIsFolder: false

    onCurrentIndexChanged: refreshShares()

    Component.onCompleted: {
        if (typeof shareManager !== 'undefined') {
            refreshShares()
        }
    }

    Connections {
        target: typeof shareManager !== 'undefined' ? shareManager : null
        enabled: typeof shareManager !== 'undefined'
        function onShareCreated(token) {
            refreshShares()
        }
        function onShareCancelled(token) {
            refreshShares()
        }
        function onShareExpired(token) {
            refreshShares()
        }
    }

    function refreshShares() {
        if (typeof shareManager === 'undefined') return
        shareListModel.clear()
        var shares = shareManager.getAllShares()
        for (var i = 0; i < shares.length; i++) {
            var share = shares[i]
            var isExpired = share.isExpired()
            // Filter by currentIndex: 0=all, 1=active, 2=expired
            if (currentIndex === 1 && isExpired) continue
            if (currentIndex === 2 && !isExpired) continue
            var fileName = share.filePath.split("/").pop().split("\\").pop()
            var fileSize = formatFileSize(share.fileSize)
            var expireTime = getExpireTime(share.expiresAt)
            shareListModel.append({
                fileName: fileName,
                fileSize: fileSize,
                expireTime: expireTime,
                status: isExpired ? "已过期" : "进行中",
                statusColor: isExpired ? Theme.errorColor : Theme.successColor,
                visitCount: share.downloadCount,
                shareUrl: "http://" + shareManager.localIp + ":8080/s/" + share.token,
                token: share.token,
                filePath: share.filePath,
                isFolder: share.isFolder,
                passwordRequired: share.passwordRequired,
                maxDownloads: share.maxDownloads,
                downloadCount: share.downloadCount,
                expiresAt: share.expiresAt.toString()
            })
        }
        shareCountLabel.text = "共 " + shareListModel.count + " 个分享"
    }

    function formatFileSize(bytes) {
        if (bytes < 1024) return bytes + " B"
        if (bytes < 1024 * 1024) return (bytes / 1024).toFixed(1) + " KB"
        if (bytes < 1024 * 1024 * 1024) return (bytes / 1024 / 1024).toFixed(1) + " MB"
        return (bytes / 1024 / 1024 / 1024).toFixed(1) + " GB"
    }

    function getExpireTime(expiresAt) {
        if (!expiresAt || expiresAt === "" || expiresAt === undefined)
            return "永不过期"
        var now = new Date()
        var expire = new Date(expiresAt)
        if (isNaN(expire.getTime()))
            return "永不过期"
        var diff = expire - now
        if (diff <= 0) return "已过期"
        var hours = Math.floor(diff / 3600000)
        if (hours < 24) return hours + "小时后过期"
        var days = Math.floor(hours / 24)
        return days + "天后过期"
    }

    function copyToClipboard(text) {
        if (typeof shareManager !== 'undefined') {
            shareManager.copyToClipboard(text)
        }
        copiedText = text
        copyToast.show()
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 20
        spacing: 16

        RowLayout {
            Layout.fillWidth: true

            Label {
                text: "分享管理"
                font.pixelSize: 24
                font.bold: true
                color: Theme.textColor
            }

            Item { Layout.fillWidth: true }

            Button {
                text: "新建分享"
                contentItem: Label {
                    text: parent.text
                    color: "#ffffff"
                    horizontalAlignment: Text.AlignHCenter
                }
                background: Rectangle {
                    color: Theme.accentColor
                    radius: 4
                }
                onClicked: shareDialog.open()
            }
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: 8

            Repeater {
                model: ["全部", "进行中", "已过期"]
                delegate: Rectangle {
                    property bool isSelected: currentIndex === index
                    Layout.preferredHeight: 32
                    Layout.minimumWidth: 80
                    color: isSelected ? Theme.accentColor : Theme.surfaceColor
                    radius: 4

                    Label {
                        anchors.centerIn: parent
                        text: modelData
                        color: isSelected ? "#ffffff" : Theme.textColor
                        font.pixelSize: 13
                    }

                    MouseArea {
                        anchors.fill: parent
                        cursorShape: Qt.PointingHandCursor
                        onClicked: currentIndex = index
                    }
                }
            }

            Item { Layout.fillWidth: true }

            Label {
                id: shareCountLabel
                text: "共 0 个分享"
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
                id: shareListView
                anchors.fill: parent
                anchors.margins: 8
                clip: true

                model: ListModel {
                    id: shareListModel
                }

                ScrollBar.vertical: ScrollBar {
                    parent: shareListView.parent
                    anchors.top: parent.top
                    anchors.bottom: parent.bottom
                    anchors.right: parent.right
                    anchors.rightMargin: -8
                }

                Label {
                    anchors.centerIn: parent
                    text: "暂无分享\n点击右上角「新建分享」开始"
                    color: Theme.textSecondary
                    font.pixelSize: 16
                    horizontalAlignment: Text.AlignHCenter
                    visible: shareListModel.count === 0
                }

                delegate: shareListDelegate
            }
        }
    }

    Component {
        id: shareListDelegate
        Rectangle {
            width: parent ? parent.width - 16 : 0
            height: 80
            color: mouseArea.containsMouse ? "#333336" : Theme.sidebarColor
            radius: 4

            MouseArea {
                id: mouseArea
                anchors.fill: parent
                hoverEnabled: true
                cursorShape: Qt.PointingHandCursor
                onClicked: {
                    detailDialog.fileName = model.fileName
                    detailDialog.fileSize = model.fileSize
                    detailDialog.filePath = model.filePath
                    detailDialog.shareUrl = model.shareUrl
                    detailDialog.token = model.token
                    detailDialog.expireTime = model.expireTime
                    detailDialog.status = model.status
                    detailDialog.statusColor = model.statusColor
                    detailDialog.visitCount = model.visitCount
                    detailDialog.isFolder = model.isFolder
                    detailDialog.passwordRequired = model.passwordRequired
                    detailDialog.maxDownloads = model.maxDownloads
                    detailDialog.downloadCount = model.downloadCount
                    detailDialog.expiresAt = model.expiresAt
                    detailDialog.open()
                }
            }

            RowLayout {
                anchors.fill: parent
                anchors.margins: 12

                Rectangle {
                    width: 48
                    height: 48
                    color: Theme.accentColor
                    radius: 4

                    Label {
                        anchors.centerIn: parent
                        text: model.isFolder ? "📁" : "📄"
                        font.pixelSize: 24
                    }
                }

                ColumnLayout {
                    Layout.leftMargin: 12
                    Layout.fillWidth: true

                    Label {
                        text: model.fileName
                        color: Theme.textColor
                        font.pixelSize: 14
                        font.bold: true
                        elide: Text.ElideRight
                        Layout.fillWidth: true
                    }

                    Label {
                        text: model.fileSize + " • " + model.expireTime
                        color: Theme.textSecondary
                        font.pixelSize: 12
                    }

                    Label {
                        text: model.status
                        color: model.statusColor
                        font.pixelSize: 12
                    }
                }

                ColumnLayout {
                    spacing: 8

                    Label {
                        text: "访问 " + model.visitCount + " 次"
                        color: Theme.textSecondary
                        font.pixelSize: 11
                    }

                    RowLayout {
                        spacing: 4

                        Button {
                            implicitWidth: 32
                            implicitHeight: 32
                            contentItem: Label {
                                text: "📋"
                                horizontalAlignment: Text.AlignHCenter
                                font.pixelSize: 16
                            }
                            background: Rectangle {
                                color: parent.hovered ? "#3e3e42" : "transparent"
                                radius: 4
                            }
                            ToolTip.text: "复制链接"
                            ToolTip.visible: hovered
                            onClicked: copyToClipboard(model.shareUrl)
                        }

                        Button {
                            implicitWidth: 32
                            implicitHeight: 32
                            contentItem: Label {
                                text: "📱"
                                horizontalAlignment: Text.AlignHCenter
                                font.pixelSize: 16
                            }
                            background: Rectangle {
                                color: parent.hovered ? "#3e3e42" : "transparent"
                                radius: 4
                            }
                            ToolTip.text: "二维码"
                            ToolTip.visible: hovered
                            onClicked: {
                                qrCodeText.text = model.shareUrl
                                qrCodeImage.source = qrCodeHelper.generateDataUrl(model.shareUrl, 600)
                                qrCodeDialog.open()
                            }
                        }

                        Button {
                            implicitWidth: 32
                            implicitHeight: 32
                            contentItem: Label {
                                text: "❌"
                                horizontalAlignment: Text.AlignHCenter
                                font.pixelSize: 16
                            }
                            background: Rectangle {
                                color: parent.hovered ? "#3e3e42" : "transparent"
                                radius: 4
                            }
                            ToolTip.text: "取消分享"
                            ToolTip.visible: hovered
                            onClicked: {
                                shareManager.cancelShare(model.token)
                            }
                        }
                    }
                }
            }
        }
    }

    Dialog {
        id: detailDialog

        property string fileName: ""
        property string fileSize: ""
        property string filePath: ""
        property string shareUrl: ""
        property string token: ""
        property string expireTime: ""
        property string status: ""
        property color statusColor: Theme.successColor
        property int visitCount: 0
        property bool isFolder: false
        property bool passwordRequired: false
        property int maxDownloads: 0
        property int downloadCount: 0
        property string expiresAt: ""

        title: "分享详情"
        modal: true
        width: 480
        height: 420
        parent: Overlay.overlay
        anchors.centerIn: parent
        standardButtons: Dialog.Close

        background: Rectangle {
            color: Theme.surfaceColor
            radius: 8
            border.color: Theme.sidebarColor
        }

        contentItem: Item {
            ColumnLayout {
                anchors.fill: parent
                spacing: 12

                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 60
                    color: Theme.sidebarColor
                    radius: 4

                    RowLayout {
                        anchors.fill: parent
                        anchors.margins: 12

                        Rectangle {
                            width: 40
                            height: 40
                            color: Theme.accentColor
                            radius: 4

                            Label {
                                anchors.centerIn: parent
                                text: detailDialog.isFolder ? "📁" : "📄"
                                font.pixelSize: 20
                            }
                        }

                        ColumnLayout {
                            Layout.leftMargin: 8

                            Label {
                                text: detailDialog.fileName
                                color: Theme.textColor
                                font.pixelSize: 16
                                font.bold: true
                                elide: Text.ElideRight
                                Layout.fillWidth: true
                            }

                            Label {
                                text: detailDialog.fileSize + (detailDialog.isFolder ? " (文件夹)" : "")
                                color: Theme.textSecondary
                                font.pixelSize: 12
                            }
                        }

                        Rectangle {
                            Layout.preferredHeight: 24
                            Layout.preferredWidth: 60
                            color: detailDialog.statusColor
                            radius: 12

                            Label {
                                anchors.centerIn: parent
                                text: detailDialog.status
                                color: "#ffffff"
                                font.pixelSize: 11
                            }
                        }
                    }
                }

                GridLayout {
                    Layout.fillWidth: true
                    columns: 2
                    rowSpacing: 8
                    columnSpacing: 12

                    Label {
                        text: "分享路径"
                        color: Theme.textSecondary
                        font.pixelSize: 13
                    }
                    Label {
                        text: detailDialog.filePath
                        color: Theme.textColor
                        font.pixelSize: 13
                        elide: Text.ElideMiddle
                        Layout.fillWidth: true
                    }

                    Label {
                        text: "过期时间"
                        color: Theme.textSecondary
                        font.pixelSize: 13
                    }
                    Label {
                        text: detailDialog.expiresAt
                        color: Theme.textColor
                        font.pixelSize: 13
                        Layout.fillWidth: true
                        wrapMode: Text.WordWrap
                    }

                    Label {
                        text: "访问次数"
                        color: Theme.textSecondary
                        font.pixelSize: 13
                    }
                    Label {
                        text: detailDialog.downloadCount + " 次"
                        color: Theme.textColor
                        font.pixelSize: 13
                    }

                    Label {
                        text: "下载限制"
                        color: Theme.textSecondary
                        font.pixelSize: 13
                    }
                    Label {
                        text: detailDialog.maxDownloads > 0 ? detailDialog.maxDownloads + " 次" : "无限制"
                        color: Theme.textColor
                        font.pixelSize: 13
                    }

                    Label {
                        text: "访问密码"
                        color: Theme.textSecondary
                        font.pixelSize: 13
                    }
                    Label {
                        text: detailDialog.passwordRequired ? "已设置" : "无"
                        color: detailDialog.passwordRequired ? Theme.warningColor : Theme.textColor
                        font.pixelSize: 13
                    }

                    Label {
                        text: "分享令牌"
                        color: Theme.textSecondary
                        font.pixelSize: 13
                    }
                    Label {
                        text: detailDialog.token
                        color: Theme.textColor
                        font.pixelSize: 13
                        font.family: "Consolas"
                    }
                }

                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 1
                    color: Theme.borderColor
                }

                Label {
                    text: "分享链接"
                    color: Theme.textSecondary
                    font.pixelSize: 13
                }

                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 36
                    color: Theme.backgroundColor
                    radius: 4

                    Label {
                        anchors.fill: parent
                        anchors.leftMargin: 8
                        anchors.rightMargin: 70
                        text: detailDialog.shareUrl
                        color: Theme.accentColor
                        font.pixelSize: 13
                        font.family: "Consolas"
                        verticalAlignment: Text.AlignVCenter
                        elide: Text.ElideMiddle
                    }

                    RowLayout {
                        anchors.right: parent.right
                        anchors.verticalCenter: parent.verticalCenter
                        anchors.rightMargin: 4
                        spacing: 4

                        Button {
                            implicitHeight: 28
                            implicitWidth: 28
                            contentItem: Label {
                                text: "📋"
                                horizontalAlignment: Text.AlignHCenter
                                font.pixelSize: 14
                            }
                            background: Rectangle {
                                color: parent.hovered ? "#3e3e42" : "transparent"
                                radius: 4
                            }
                            ToolTip.text: "复制链接"
                            ToolTip.visible: hovered
                            onClicked: copyToClipboard(detailDialog.shareUrl)
                        }

                        Button {
                            implicitHeight: 28
                            implicitWidth: 28
                            contentItem: Label {
                                text: "📱"
                                horizontalAlignment: Text.AlignHCenter
                                font.pixelSize: 14
                            }
                            background: Rectangle {
                                color: parent.hovered ? "#3e3e42" : "transparent"
                                radius: 4
                            }
                            ToolTip.text: "二维码"
                            ToolTip.visible: hovered
                            onClicked: {
                                qrCodeText.text = detailDialog.shareUrl
                                qrCodeImage.source = qrCodeHelper.generateDataUrl(detailDialog.shareUrl, 600)
                                qrCodeDialog.open()
                            }
                        }
                    }
                }

                Item { Layout.fillHeight: true }
            }
        }
    }

    Dialog {
        id: shareDialog
        modal: true
        width: 450
        height: 320
        parent: Overlay.overlay
        anchors.centerIn: parent
        closePolicy: Popup.NoAutoClose

        background: Rectangle {
            color: Theme.surfaceColor
            radius: 8
            border.color: Theme.sidebarColor
        }

        contentItem: ColumnLayout {
            spacing: 0

            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: 40
                color: Theme.sidebarColor
                radius: 8
                clip: true

                Label {
                    anchors.left: parent.left
                    anchors.leftMargin: 16
                    anchors.verticalCenter: parent.verticalCenter
                    text: "新建分享"
                    color: Theme.textColor
                    font.pixelSize: 16
                    font.bold: true
                }
            }

            ColumnLayout {
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.margins: 16
                spacing: 16

                Label {
                    text: "选择文件或文件夹"
                    color: Theme.textColor
                }

                RowLayout {
                    Layout.fillWidth: true

                    TextField {
                        id: filePathInput
                        Layout.fillWidth: true
                        placeholderText: "请选择文件或文件夹"
                        readOnly: true
                        color: Theme.textColor
                        background: Rectangle {
                            color: Theme.backgroundColor
                            radius: 4
                            border.color: Theme.borderColor
                        }
                    }

                    ThemedButton {
                        text: "文件"
                        onClicked: fileDialog.open()
                    }

                    ThemedButton {
                        text: "文件夹"
                        onClicked: folderDialog.open()
                    }
                }

                RowLayout {
                    Layout.fillWidth: true

                    Label { text: "有效期"; color: Theme.textColor }
                    Item { Layout.fillWidth: true }
                    ThemedComboBox {
                        id: expireCombo
                        model: ["24小时", "7天", "30天", "永不过期"]
                        currentIndex: 0
                    }
                }

                RowLayout {
                    Layout.fillWidth: true

                    Label { text: "访问密码"; color: Theme.textColor }
                    Item { Layout.fillWidth: true }
                    TextField {
                        id: passwordInput
                        placeholderText: "留空则无需密码"
                        echoMode: TextInput.Password
                        color: Theme.textColor
                        background: Rectangle {
                            color: Theme.backgroundColor
                            radius: 4
                            border.color: Theme.borderColor
                        }
                    }
                }

                Item { Layout.fillHeight: true }

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 8

                    Item { Layout.fillWidth: true }

                    ThemedButton {
                        text: "确定"
                        primary: true
                        onClicked: {
                            if (filePathInput.text.trim() !== "") {
                                var expireHours = 24
                                if (expireCombo.currentIndex === 1) expireHours = 168
                                else if (expireCombo.currentIndex === 2) expireHours = 720
                                else if (expireCombo.currentIndex === 3) expireHours = 0

                                shareManager.createShare(
                                    filePathInput.text.trim(),
                                    shareIsFolder,
                                    expireHours,
                                    0,
                                    passwordInput.text
                                )

                                filePathInput.text = ""
                                passwordInput.text = ""
                                expireCombo.currentIndex = 0
                                shareIsFolder = false
                                shareDialog.close()
                            }
                        }
                    }

                    ThemedButton {
                        text: "取消"
                        onClicked: shareDialog.close()
                    }
                }
            }
        }
    }

    FileDialog {
        id: fileDialog
        title: "选择文件"
        onAccepted: {
            var path = selectedFile.toString()
            if (path.startsWith("file:///")) {
                path = path.substring(8)
            }
            filePathInput.text = path
            shareIsFolder = false
        }
    }

    FolderDialog {
        id: folderDialog
        title: "选择文件夹"
        onAccepted: {
            var path = selectedFolder.toString()
            if (path.startsWith("file:///")) {
                path = path.substring(8)
            }
            filePathInput.text = path
            shareIsFolder = true
        }
    }

    Dialog {
        id: qrCodeDialog
        title: "分享二维码"
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
                id: qrCodeText
                Layout.fillWidth: true
                text: ""
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
                    id: qrCodeImage
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
                text: "扫描二维码访问分享"
                color: Theme.textSecondary
                font.pixelSize: 12
                horizontalAlignment: Text.AlignHCenter
            }

            Button {
                Layout.alignment: Qt.AlignHCenter
                text: "复制链接"
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
                onClicked: copyToClipboard(qrCodeText.text)
            }
        }
    }

    Rectangle {
        id: copyToast
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 20
        anchors.horizontalCenter: parent.horizontalCenter
        width: 160
        height: 36
        color: "#80000000"
        radius: 18
        visible: false

        function show() {
            opacity = 1
            visible = true
            copyToastTimer.restart()
        }

        Label {
            anchors.centerIn: parent
            text: "✓ 已复制到剪贴板"
            color: "#ffffff"
            font.pixelSize: 13
        }

        Timer {
            id: copyToastTimer
            interval: 1500
            onTriggered: {
                copyToast.opacity = 0
                copyToast.visible = false
            }
        }

        Behavior on opacity {
            NumberAnimation { duration: 300 }
        }
    }
}
