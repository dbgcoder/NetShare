import QtQuick
import QtQuick.Controls
import NetShare

Switch {
    id: root

    indicator: Rectangle {
        x: root.leftPadding
        y: parent.height / 2 - height / 2
        width: 40
        height: 22
        radius: 11
        color: root.checked ? Theme.accentColor : Theme.switchTrackColor

        Behavior on color {
            ColorAnimation { duration: 150 }
        }

        Rectangle {
            x: root.checked ? parent.width - width - 3 : 3
            y: parent.height / 2 - height / 2
            width: 16
            height: 16
            radius: 8
            color: Theme.switchThumbColor

            Behavior on x {
                NumberAnimation { duration: 150; easing.type: Easing.InOutQuad }
            }
        }
    }
}
