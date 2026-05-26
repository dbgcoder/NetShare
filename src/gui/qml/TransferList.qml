import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import NetShare

Rectangle {
    id: root
    color: Theme.backgroundColor

    property int currentFilter: 0

    onCurrentFilterChanged: refreshTasks()

    Component.onCompleted: {
        if (typeof transferEngine !== 'undefined') {
            refreshTasks()
        }
    }

    Connections {
        target: typeof transferEngine !== 'undefined' ? transferEngine : null
        enabled: typeof transferEngine !== 'undefined'
        function onTaskStarted(taskId) { refreshTasks() }
        function onTaskProgress(taskId, progress, speed) { updateTask(taskId, progress, speed) }
        function onTaskCompleted(taskId) { refreshTasks() }
        function onTaskFailed(taskId, error) { refreshTasks() }
        function onTaskCancelled(taskId) { refreshTasks() }
        function onTaskPaused(taskId) { refreshTasks() }
        function onTaskResumed(taskId) { refreshTasks() }
    }

    function refreshTasks() {
        taskListModel.clear()

        // Collect active task IDs to avoid duplicates with log entries
        var activeIds = {}
        var activeFileNames = {}  // Track by type:fileName for dedup with log entries

        // 1. Active tasks from transfer engine (in-progress/live)
        if (typeof transferEngine !== 'undefined') {
            var tasks = transferEngine.getAllTasks()
            for (var i = 0; i < tasks.length; i++) {
                var task = tasks[i]
                activeIds[task.taskId] = true
                activeFileNames[task.type + ":" + task.fileName] = true

                // Apply filter
                if (currentFilter === 1 && task.type !== 0) continue
                if (currentFilter === 2 && task.type !== 1) continue
                if (currentFilter === 3 && task.status > 4) continue
                if (currentFilter === 4 && task.status !== 5) continue
                if (currentFilter === 5 && task.status !== 6 && task.status !== 7) continue

                var statusText = getStatusText(task.status)
                var statusColor = getStatusColor(task.status)
                var typeText = task.type === 0 ? "下载" : "上传"
                var typeIcon = task.type === 0 ? "⬇" : "⬆"
                var speedText = formatSpeed(task.speed)
                var sizeText = formatFileSize(task.transferredSize) + " / " + formatFileSize(task.fileSize)
                taskListModel.append({
                    taskId: task.taskId,
                    fileName: task.fileName || "未知文件",
                    typeText: typeText,
                    typeIcon: typeIcon,
                    status: task.status,
                    statusText: statusText,
                    statusColor: statusColor,
                    progress: task.progress,
                    speed: task.speed,
                    speedText: speedText,
                    sizeText: sizeText,
                    filePath: task.filePath,
                    savePath: task.savePath,
                    threads: task.threads,
                    error: task.error,
                    startedAt: task.startedAt.toString(),
                    fromLog: false
                })
            }
        }

        // 2. Historical tasks from transfer log service (persisted to DB)
        if (typeof transferLogService !== 'undefined') {
            var logs = transferLogService.queryLogs(200, 0)
            for (var j = 0; j < logs.length; j++) {
                var log = logs[j]

                // Map TransferLogEntry status to TransferTask status
                // TransferLogEntry.Status: Started=0, Completed=2, Failed=3, Cancelled=4
                // TransferTask.Status: Pending=0, Preparing=1, Downloading=2, Uploading=3, Paused=4, Completed=5, Failed=6, Cancelled=7
                var mappedStatus = 5 // default to Completed
                if (log.status === 0) mappedStatus = 5 // Started->Completed (log entries are final states)
                else if (log.status === 2) mappedStatus = 5 // Completed
                else if (log.status === 3) mappedStatus = 6 // Failed
                else if (log.status === 4) mappedStatus = 7 // Cancelled

                // Skip if already shown as an active engine task (dedup by type:fileName)
                var logKey = log.type + ":" + log.fileName
                if (activeFileNames[logKey]) continue

                // Apply filter
                // TransferLogEntry.Type: DownloadLog=0, UploadLog=1
                if (currentFilter === 1 && log.type !== 0) continue
                if (currentFilter === 2 && log.type !== 1) continue
                if (currentFilter === 3 && mappedStatus > 4) continue  // Active filter: skip completed/failed/cancelled
                if (currentFilter === 4 && mappedStatus !== 5) continue
                if (currentFilter === 5 && mappedStatus !== 6 && mappedStatus !== 7) continue

                var logStatusText = getStatusText(mappedStatus)
                var logStatusColor = getStatusColor(mappedStatus)
                var logTypeText = log.type === 0 ? "下载" : "上传"
                var logTypeIcon = log.type === 0 ? "⬇" : "⬆"
                var logSizeText = formatFileSize(log.fileSize)

                taskListModel.append({
                    taskId: log.id,
                    fileName: log.fileName || "未知文件",
                    typeText: logTypeText,
                    typeIcon: logTypeIcon,
                    status: mappedStatus,
                    statusText: logStatusText,
                    statusColor: logStatusColor,
                    progress: mappedStatus === 5 ? 100 : 0,
                    speed: -1,
                    speedText: "-- /s",
                    sizeText: logSizeText,
                    filePath: log.filePath,
                    savePath: "",
                    threads: 1,
                    error: log.detail || "",
                    startedAt: log.timestamp ? log.timestamp.toString() : "",
                    fromLog: true
                })
            }
        }

        taskCountLabel.text = "共 " + taskListModel.count + " 个任务"
    }

    function updateTask(taskId, progress, speed) {
        for (var i = 0; i < taskListModel.count; i++) {
            if (taskListModel.get(i).taskId === taskId) {
                taskListModel.setProperty(i, "progress", progress)
                taskListModel.setProperty(i, "speed", speed)
                taskListModel.setProperty(i, "speedText", formatSpeed(speed))
                if (typeof transferEngine !== 'undefined') {
                    var task = transferEngine.getTaskInfo(taskId)
                    if (task && task.taskId) {
                        taskListModel.setProperty(i, "sizeText", formatFileSize(task.transferredSize) + " / " + formatFileSize(task.fileSize))
                    }
                }
                break
            }
        }
    }

    function getStatusText(status) {
        switch (status) {
            case 0: return "等待中"
            case 1: return "准备中"
            case 2: return "下载中"
            case 3: return "上传中"
            case 4: return "已暂停"
            case 5: return "已完成"
            case 6: return "失败"
            case 7: return "已取消"
            default: return "未知"
        }
    }

    function getStatusColor(status) {
        switch (status) {
            case 0: return Theme.textSecondary
            case 1: return Theme.warningColor
            case 2: return Theme.accentColor
            case 3: return Theme.accentColor
            case 4: return Theme.warningColor
            case 5: return Theme.successColor
            case 6: return Theme.errorColor
            case 7: return Theme.textSecondary
            default: return Theme.textSecondary
        }
    }

    function formatSpeed(bytesPerSecond) {
        if (bytesPerSecond < 0) return "-- /s"
        if (bytesPerSecond === 0) return "0 B/s"
        if (bytesPerSecond < 1024) return bytesPerSecond + " B/s"
        if (bytesPerSecond < 1024 * 1024) return (bytesPerSecond / 1024).toFixed(1) + " KB/s"
        return (bytesPerSecond / 1024 / 1024).toFixed(1) + " MB/s"
    }

    function formatFileSize(bytes) {
        if (bytes <= 0) return "0 B"
        if (bytes < 1024) return bytes + " B"
        if (bytes < 1024 * 1024) return (bytes / 1024).toFixed(1) + " KB"
        if (bytes < 1024 * 1024 * 1024) return (bytes / 1024 / 1024).toFixed(1) + " MB"
        return (bytes / 1024 / 1024 / 1024).toFixed(1) + " GB"
    }

    Timer {
        id: refreshTimer
        interval: 1000
        running: typeof transferEngine !== 'undefined'
        repeat: true
        onTriggered: refreshTasks()
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 20
        spacing: 16

        RowLayout {
            Layout.fillWidth: true

            Label {
                text: "传输列表"
                font.pixelSize: 24
                font.bold: true
                color: Theme.textColor
            }

            Item { Layout.fillWidth: true }

            Button {
                text: "全部暂停"
                visible: taskListModel.count > 0
                contentItem: Label {
                    text: parent.text
                    color: Theme.textColor
                    horizontalAlignment: Text.AlignHCenter
                }
                background: Rectangle {
                    color: parent.hovered ? "#3e3e42" : Theme.surfaceColor
                    radius: 4
                    border.color: Theme.borderColor
                }
                onClicked: {
                    for (var i = 0; i < taskListModel.count; i++) {
                        var task = taskListModel.get(i)
                        if (task.status === 2 || task.status === 3) {
                            transferEngine.pauseTask(task.taskId)
                        }
                    }
                }
            }

            Button {
                text: "清空已完成"
                visible: taskListModel.count > 0
                contentItem: Label {
                    text: parent.text
                    color: Theme.textColor
                    horizontalAlignment: Text.AlignHCenter
                }
                background: Rectangle {
                    color: parent.hovered ? "#3e3e42" : Theme.surfaceColor
                    radius: 4
                    border.color: Theme.borderColor
                }
                onClicked: {
                    var toRemove = []
                    for (var i = 0; i < taskListModel.count; i++) {
                        var task = taskListModel.get(i)
                        if (task.status === 5 || task.status === 7) {
                            toRemove.push(task.taskId)
                        }
                    }
                    for (var j = 0; j < toRemove.length; j++) {
                        transferEngine.cancelTask(toRemove[j])
                    }
                    // Also clear log entries from database
                    if (typeof transferLogService !== 'undefined') {
                        transferLogService.clearLogs(0)
                    }
                }
            }
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: 8

            Repeater {
                model: ["全部", "下载", "上传", "进行中", "已完成", "失败"]
                delegate: Rectangle {
                    property bool isSelected: currentFilter === index
                    Layout.preferredHeight: 32
                    Layout.minimumWidth: 70
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
                        onClicked: currentFilter = index
                    }
                }
            }

            Item { Layout.fillWidth: true }

            Label {
                id: taskCountLabel
                text: "共 0 个任务"
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
                id: taskListView
                anchors.fill: parent
                anchors.margins: 8
                clip: true

                model: ListModel {
                    id: taskListModel
                }

                ScrollBar.vertical: ScrollBar {
                    parent: taskListView.parent
                    anchors.top: parent.top
                    anchors.bottom: parent.bottom
                    anchors.right: parent.right
                    anchors.rightMargin: -8
                }

                Label {
                    anchors.centerIn: parent
                    text: "暂无传输任务"
                    color: Theme.textSecondary
                    font.pixelSize: 16
                    horizontalAlignment: Text.AlignHCenter
                    visible: taskListModel.count === 0
                }

                delegate: taskListDelegate
            }
        }
    }

    Component {
        id: taskListDelegate
        Rectangle {
            width: parent ? parent.width - 16 : 0
            height: 88
            color: taskMouseArea.containsMouse ? "#333336" : Theme.sidebarColor
            radius: 4

            MouseArea {
                id: taskMouseArea
                anchors.fill: parent
                hoverEnabled: true
            }

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 12
                spacing: 6

                RowLayout {
                    Layout.fillWidth: true

                    Label {
                        text: model.typeIcon
                        font.pixelSize: 16
                    }

                    Label {
                        text: model.fileName
                        color: Theme.textColor
                        font.pixelSize: 14
                        font.bold: true
                        elide: Text.ElideRight
                        Layout.fillWidth: true
                    }

                    Rectangle {
                        Layout.preferredHeight: 20
                        Layout.preferredWidth: 56
                        color: model.statusColor
                        radius: 10

                        Label {
                            anchors.centerIn: parent
                            text: model.statusText
                            color: "#ffffff"
                            font.pixelSize: 10
                        }
                    }

                    RowLayout {
                        spacing: 4

                        Button {
                            visible: !model.fromLog && (model.status === 2 || model.status === 3)
                            implicitWidth: 28
                            implicitHeight: 28
                            contentItem: Label {
                                text: "⏸"
                                horizontalAlignment: Text.AlignHCenter
                                font.pixelSize: 14
                            }
                            background: Rectangle {
                                color: parent.hovered ? "#3e3e42" : "transparent"
                                radius: 4
                            }
                            ToolTip.text: "暂停"
                            ToolTip.visible: hovered
                            onClicked: transferEngine.pauseTask(model.taskId)
                        }

                        Button {
                            visible: !model.fromLog && model.status === 4
                            implicitWidth: 28
                            implicitHeight: 28
                            contentItem: Label {
                                text: "▶"
                                horizontalAlignment: Text.AlignHCenter
                                font.pixelSize: 14
                            }
                            background: Rectangle {
                                color: parent.hovered ? "#3e3e42" : "transparent"
                                radius: 4
                            }
                            ToolTip.text: "继续"
                            ToolTip.visible: hovered
                            onClicked: transferEngine.resumeTask(model.taskId)
                        }

                        Button {
                            visible: !model.fromLog && model.status !== 5 && model.status !== 7
                            implicitWidth: 28
                            implicitHeight: 28
                            contentItem: Label {
                                text: "✕"
                                horizontalAlignment: Text.AlignHCenter
                                font.pixelSize: 14
                                color: Theme.errorColor
                            }
                            background: Rectangle {
                                color: parent.hovered ? "#3e3e42" : "transparent"
                                radius: 4
                            }
                            ToolTip.text: "取消"
                            ToolTip.visible: hovered
                            onClicked: transferEngine.cancelTask(model.taskId)
                        }
                    }
                }

                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 6
                    color: Theme.backgroundColor
                    radius: 3

                    Rectangle {
                        width: parent.width * (model.progress / 100)
                        height: parent.height
                        radius: 3
                        color: {
                            if (model.status === 5) return Theme.successColor
                            if (model.status === 6) return Theme.errorColor
                            if (model.status === 4) return Theme.warningColor
                            return Theme.accentColor
                        }

                        Behavior on width {
                            NumberAnimation { duration: 300 }
                        }
                    }
                }

                RowLayout {
                    Layout.fillWidth: true

                    Label {
                        text: model.progress + "%"
                        color: Theme.textSecondary
                        font.pixelSize: 11
                    }

                    Label {
                        text: model.sizeText
                        color: Theme.textSecondary
                        font.pixelSize: 11
                    }

                    Item { Layout.fillWidth: true }

                    Label {
                        text: model.speedText
                        color: model.status === 2 || model.status === 3 ? Theme.accentColor : Theme.textSecondary
                        font.pixelSize: 11
                    }

                    Label {
                        text: model.typeText
                        color: Theme.textSecondary
                        font.pixelSize: 11
                    }
                }
            }
        }
    }
}
