import QtQuick
import QtQuick.Controls
import NetShare

ComboBox {
    id: root

    contentItem: Label {
        text: root.displayText
        color: Theme.textColor
        font.pixelSize: 13
        verticalAlignment: Text.AlignVCenter
        leftPadding: 8
    }

    background: Rectangle {
        color: Theme.surfaceColor
        radius: 4
        border.color: root.activeFocus ? Theme.accentColor : Theme.borderColor
    }

    indicator: Canvas {
        x: root.width - width - 8
        y: root.topPadding + (root.availableHeight - height) / 2
        width: 12
        height: 8
        contextType: "2d"

        onPaint: {
            context.reset()
            context.moveTo(0, 0)
            context.lineTo(width / 2, height)
            context.lineTo(width, 0)
            context.strokeStyle = Theme.textSecondary
            context.lineWidth = 1.5
            context.stroke()
        }
    }

    popup: Popup {
        y: root.height
        width: root.width
        implicitHeight: contentItem.implicitHeight
        padding: 1

        contentItem: ListView {
            clip: true
            implicitHeight: contentHeight
            model: root.popup.visible ? root.delegateModel : null
            currentIndex: root.highlightedIndex
        }

        background: Rectangle {
            color: Theme.surfaceColor
            radius: 4
            border.color: Theme.borderColor
        }
    }

    delegate: ItemDelegate {
        width: root.width
        contentItem: Label {
            text: modelData
            color: highlighted ? "#ffffff" : Theme.textColor
            font.pixelSize: 13
            verticalAlignment: Text.AlignVCenter
        }
        background: Rectangle {
            color: highlighted ? Theme.accentColor : Theme.surfaceColor
        }
        highlighted: root.highlightedIndex === index
    }
}
