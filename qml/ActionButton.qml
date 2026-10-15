import QtQuick
import QtQuick.Controls

Button {
    id: control
    property bool primary: false
    property bool danger: false
    implicitHeight: 40
    leftPadding: 16
    rightPadding: 16
    font.pixelSize: 14
    hoverEnabled: true
    contentItem: Text {
        text: control.text
        font: control.font
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
        color: !control.enabled ? "#98a2b3" : control.primary ? "white" : control.danger ? "#b42318" : "#344054"
    }
    background: Rectangle {
        radius: 8
        color: !control.enabled ? "#f2f4f7" : control.primary ? (control.down ? "#2446bd" : control.hovered ? "#345ade" : "#4169ed") : control.hovered ? "#f2f4f7" : "white"
        border.color: control.primary && control.enabled ? "transparent" : "#d0d5dd"
        Behavior on color { ColorAnimation { duration: 110 } }
    }
}
