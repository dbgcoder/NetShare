import QtQuick
import QtQuick.Controls
import NetShare

Button {
    id: root

    property bool primary: false

    contentItem: Label {
        text: root.text
        color: root.primary ? "#ffffff" : Theme.textColor
        font.pixelSize: 13
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
    }

    background: Rectangle {
        color: {
            if (root.primary) return root.pressed ? "#005a9e" : Theme.accentColor
            if (root.hovered) return "#3e3e42"
            return Theme.surfaceColor
        }
        radius: 4
        border.color: root.primary ? "transparent" : Theme.borderColor

        Behavior on color {
            ColorAnimation { duration: 100 }
        }
    }
}
