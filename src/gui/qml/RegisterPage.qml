import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import NetShare

Popup {
    id: registerPopup

    modal: true
    closePolicy: Popup.NoAutoClose
    anchors.centerIn: parent
    width: 480
    height: 620
    padding: 0

    background: Rectangle {
        color: Theme.backgroundColor
        border.color: Theme.borderColor
        border.width: 1
        radius: 8
    }

    property bool emailSending: false

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 24
        spacing: 12

        // 标题
        Label {
            text: qsTr("Welcome to NetShare")
            font.pixelSize: 20
            font.bold: true
            color: Theme.textColor
            Layout.alignment: Qt.AlignHCenter
        }

        Label {
            text: qsTr("Please register or try the software")
            font.pixelSize: 13
            color: Theme.textSecondary
            Layout.alignment: Qt.AlignHCenter
        }

        // 机器码展示区
        GroupBox {
            Layout.fillWidth: true
            title: qsTr("Machine Code")

            background: Rectangle {
                color: Theme.surfaceColor
                border.color: Theme.borderColor
                radius: 4
                y: parent.topPadding - parent.bottomPadding
                width: parent.width
                height: parent.height - parent.topPadding + parent.bottomPadding
            }

            label: Label {
                text: parent.title
                color: Theme.textSecondary
                font.pixelSize: 12
            }

            RowLayout {
                width: parent.width
                spacing: 8

                TextField {
                    id: machineIdField
                    Layout.fillWidth: true
                    text: authService ? authService.currentMachineId() : ""
                    readOnly: true
                    selectByMouse: true
                    font.pixelSize: 11
                    color: Theme.textColor
                    background: Rectangle {
                        color: Theme.backgroundColor
                        border.color: Theme.borderColor
                        radius: 3
                    }
                }

                Button {
                    text: qsTr("Copy")
                    palette.buttonText: Theme.textColor
                    background: Rectangle {
                        color: Theme.surfaceColor
                        border.color: Theme.borderColor
                        radius: 4
                        implicitWidth: 60
                        implicitHeight: 28
                    }
                    onClicked: {
                        machineIdField.selectAll()
                        machineIdField.copy()
                    }
                }
            }
        }

        // 注册信息输入区
        GroupBox {
            Layout.fillWidth: true
            title: qsTr("Registration Information")

            background: Rectangle {
                color: Theme.surfaceColor
                border.color: Theme.borderColor
                radius: 4
                y: parent.topPadding - parent.bottomPadding
                width: parent.width
                height: parent.height - parent.topPadding + parent.bottomPadding
            }

            label: Label {
                text: parent.title
                color: Theme.textSecondary
                font.pixelSize: 12
            }

            ColumnLayout {
                width: parent.width
                spacing: 8

                // 用户名
                RowLayout {
                    Layout.fillWidth: true
                    Label {
                        text: qsTr("Username:")
                        color: Theme.textColor
                        Layout.preferredWidth: 80
                    }
                    TextField {
                        id: usernameField
                        Layout.fillWidth: true
                        placeholderText: qsTr("2-20 chars (Chinese, letters, numbers)")
                        color: Theme.textColor
                        background: Rectangle {
                            color: Theme.backgroundColor
                            border.color: usernameField.text.length > 0 && !authService.validateUsername(usernameField.text)
                                ? "#e53935" : Theme.borderColor
                            radius: 3
                        }
                    }
                    Label {
                        visible: usernameField.text.length > 0
                        text: authService && authService.validateUsername(usernameField.text) ? "✓" : "✗"
                        color: authService && authService.validateUsername(usernameField.text) ? "#4caf50" : "#e53935"
                        font.pixelSize: 16
                    }
                }

                // 邮箱
                RowLayout {
                    Layout.fillWidth: true
                    Label {
                        text: qsTr("Email:")
                        color: Theme.textColor
                        Layout.preferredWidth: 80
                    }
                    TextField {
                        id: emailField
                        Layout.fillWidth: true
                        placeholderText: qsTr("your@email.com")
                        color: Theme.textColor
                        background: Rectangle {
                            color: Theme.backgroundColor
                            border.color: emailField.text.length > 0 && !authService.validateEmail(emailField.text)
                                ? "#e53935" : Theme.borderColor
                            radius: 3
                        }
                    }
                    Label {
                        visible: emailField.text.length > 0
                        text: authService && authService.validateEmail(emailField.text) ? "✓" : "✗"
                        color: authService && authService.validateEmail(emailField.text) ? "#4caf50" : "#e53935"
                        font.pixelSize: 16
                    }
                }

                // 手机号
                RowLayout {
                    Layout.fillWidth: true
                    Label {
                        text: qsTr("Phone:")
                        color: Theme.textColor
                        Layout.preferredWidth: 80
                    }
                    TextField {
                        id: phoneField
                        Layout.fillWidth: true
                        placeholderText: qsTr("11-digit phone number")
                        color: Theme.textColor
                        background: Rectangle {
                            color: Theme.backgroundColor
                            border.color: phoneField.text.length > 0 && !authService.validatePhone(phoneField.text)
                                ? "#e53935" : Theme.borderColor
                            radius: 3
                        }
                    }
                    Label {
                        visible: phoneField.text.length > 0
                        text: authService && authService.validatePhone(phoneField.text) ? "✓" : "✗"
                        color: authService && authService.validatePhone(phoneField.text) ? "#4caf50" : "#e53935"
                        font.pixelSize: 16
                    }
                }
            }
        }

        // 注册码输入区（始终显示）
        GroupBox {
            Layout.fillWidth: true
            title: qsTr("Registration Code")

            background: Rectangle {
                color: Theme.surfaceColor
                border.color: Theme.borderColor
                radius: 4
                y: parent.topPadding - parent.bottomPadding
                width: parent.width
                height: parent.height - parent.topPadding + parent.bottomPadding
            }

            label: Label {
                text: parent.title
                color: Theme.textSecondary
                font.pixelSize: 12
            }

            TextField {
                id: regCodeField
                width: parent.width
                placeholderText: qsTr("Paste registration code here")
                color: Theme.textColor
                wrapMode: TextArea.Wrap
                background: Rectangle {
                    color: Theme.backgroundColor
                    border.color: Theme.borderColor
                    radius: 3
                    implicitHeight: 60
                }
            }
        }

        // 状态提示
        Label {
            id: statusLabel
            Layout.fillWidth: true
            wrapMode: Text.WordWrap
            font.pixelSize: 12
            color: "#e53935"
            visible: text.length > 0
        }

        // 操作按钮区
        RowLayout {
            Layout.fillWidth: true
            spacing: 12
            Layout.alignment: Qt.AlignHCenter

            Button {
                text: qsTr("Trial")
                highlighted: true
                palette.buttonText: "#ffffff"
                background: Rectangle {
                    color: Theme.accentColor
                    border.color: Theme.accentColor
                    radius: 4
                    implicitWidth: 70
                    implicitHeight: 32
                }
                onClicked: {
                    if (authService && authService.enterTrialMode()) {
                        authService.registrationCompleted()
                        registerPopup.close()
                    } else {
                        statusLabel.text = qsTr("Failed to enter trial mode")
                    }
                }
            }

            Button {
                text: qsTr("Register")
                palette.buttonText: Theme.textColor
                enabled: !emailSending && authService
                         && authService.validateUsername(usernameField.text)
                         && authService.validateEmail(emailField.text)
                         && authService.validatePhone(phoneField.text)
                background: Rectangle {
                    color: Theme.surfaceColor
                    border.color: Theme.borderColor
                    radius: 4
                    implicitWidth: 90
                    implicitHeight: 32
                }
                onClicked: {
                    emailSending = true
                    statusLabel.text = qsTr("Sending registration info...")
                    statusLabel.color = Theme.textSecondary
                    authService.sendRegistrationEmail(
                        usernameField.text, emailField.text, phoneField.text)
                }
            }

            Button {
                text: qsTr("Verify Code")
                highlighted: true
                palette.buttonText: "#ffffff"
                enabled: authService
                         && authService.validateUsername(usernameField.text)
                         && regCodeField.text.length > 0
                background: Rectangle {
                    color: Theme.accentColor
                    border.color: Theme.accentColor
                    radius: 4
                    implicitWidth: 100
                    implicitHeight: 32
                }
                onClicked: {
                    if (authService.registerUser(
                            usernameField.text, emailField.text,
                            phoneField.text, regCodeField.text)) {
                        authService.registrationCompleted()
                        registerPopup.close()
                    } else {
                        statusLabel.text = qsTr("Invalid registration code")
                        statusLabel.color = "#e53935"
                    }
                }
            }
        }
    }

    // 监听邮件发送结果
    Connections {
        target: authService
        function onEmailSent() {
            emailSending = false
            statusLabel.text = qsTr("Registration info sent. Please wait for registration code.")
            statusLabel.color = "#4caf50"
        }
        function onEmailSendFailed(reason) {
            emailSending = false
            statusLabel.text = qsTr("Failed to send: %1").arg(reason)
            statusLabel.color = "#e53935"
            copyFallback.visible = true
        }
        function onRegistrationFailed(reason) {
            statusLabel.text = qsTr("Registration failed: %1").arg(reason)
            statusLabel.color = "#e53935"
        }
    }

    // 邮件发送失败时的备选方案：复制注册信息到剪贴板
    RowLayout {
        id: copyFallback
        Layout.fillWidth: true
        visible: false
        spacing: 8

        Label {
            text: qsTr("Or copy info to send manually:")
            font.pixelSize: 12
            color: Theme.textSecondary
        }

        Button {
            text: qsTr("Copy Registration Info")
            font.pixelSize: 12
            onClicked: {
                var info = "Username: " + usernameField.text +
                           "\nEmail: " + emailField.text +
                           "\nPhone: " + phoneField.text +
                           "\nMachine ID: " + (authService ? authService.currentMachineId() : "")
                var prevText = machineIdField.text
                machineIdField.text = info
                machineIdField.selectAll()
                machineIdField.copy()
                machineIdField.text = prevText
                statusLabel.text = qsTr("Registration info copied to clipboard")
                statusLabel.color = "#4caf50"
            }
        }
    }
}
