import QtQuick
import QtQuick.Layouts

Rectangle {
    id: panel
    default property alias content: body.data
    implicitHeight: body.implicitHeight + 40
    color: "white"
    radius: 14
    border.color: "#e4e7ec"
    ColumnLayout {
        id: body
        x: 20
        y: 20
        width: panel.width - 40
        spacing: 16
    }
}
