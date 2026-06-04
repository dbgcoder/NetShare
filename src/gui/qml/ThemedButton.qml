import QtQuick
import QtQuick.Controls
import NetShare

Button {
    id: root

    property bool primary: false

    contentItem: Label {
        text: root.text
        color: root.primary ? Theme.textOnAccentColor : Theme.textColor
        font.pixelSize: 13
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
    }

    background: Rectangle {
        color: {
            if (root.primary) return root.pressed ? Theme.accentPressedColor : Theme.accentColor
            if (root.hovered) return Theme.hoverColor
            return Theme.surfaceColor
        }
        radius: 4
        border.color: root.primary ? "transparent" : Theme.borderColor

        Behavior on color {
            ColorAnimation { duration: 100 }
        }
    }
}
