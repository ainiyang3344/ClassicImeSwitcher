pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import ClassicIme

ApplicationWindow {
    id: window
    objectName: "mainWindow"
    required property AppController backend
    property bool trayAvailable: false
    property bool startHidden: false
    property int currentPage: 0
    readonly property var pageTitles: ["经典切换", "输入法管理", "常规设置"]
    readonly property var pageDescriptions: ["让中文输入法与美式键盘切换，回到熟悉的操作。", "管理当前 Windows 用户的输入法，设置后立即检查结果。", "按你的习惯设置启动、快捷键和窗口行为。"]
    width: 1080
    height: 760
    minimumWidth: 960
    minimumHeight: 680
    visible: !startHidden || !trayAvailable
    title: "经典输入法切换"
    flags: Qt.Window | Qt.FramelessWindowHint | Qt.WindowMinMaxButtonsHint
    color: "#f4f6fa"
    font.family: "Microsoft YaHei UI"
    font.pixelSize: 14
    onClosing: function(close) {
        if (window.backend.closeToTray && trayAvailable) { close.accepted = false; window.hide() }
        else window.backend.quit()
    }

    function toggleMaximized() {
        if (window.visibility === Window.Maximized) window.showNormal()
        else window.showMaximized()
    }

    header: Item {
        id: titleBar
        objectName: "titleBar"
        height: 40

        Rectangle { width: sidebar.width; height: parent.height; color: sidebar.color }

        Item {
            anchors.left: parent.left
            anchors.right: windowButtons.left
            height: parent.height
            Label {
                anchors.left: parent.left
                anchors.leftMargin: sidebar.width + 28
                anchors.verticalCenter: parent.verticalCenter
                text: window.title
                color: window.active ? "#667085" : "#98a2b3"
                font.pixelSize: 12
            }
            DragHandler {
                target: null
                acceptedButtons: Qt.LeftButton
                onActiveChanged: if (active) window.startSystemMove()
            }
            TapHandler {
                acceptedButtons: Qt.LeftButton
                onDoubleTapped: window.toggleMaximized()
            }
        }

        Row {
            id: windowButtons
            anchors.right: parent.right
            anchors.top: parent.top
            anchors.topMargin: 5
            anchors.rightMargin: 5

            Repeater {
                model: ["minimize", "maximize", "close"]
                delegate: Button {
                    id: windowButton
                    required property string modelData
                    readonly property bool isClose: modelData === "close"
                    readonly property bool isMaximized: window.visibility === Window.Maximized
                    readonly property color iconColor: isClose && hovered ? "white" : "#475467"
                    objectName: modelData + "WindowButton"
                    width: 44
                    height: 30
                    padding: 0
                    hoverEnabled: true
                    text: modelData === "minimize" ? "最小化" : isClose ? "关闭" : isMaximized ? "还原" : "最大化"
                    Accessible.name: text
                    ToolTip.visible: hovered
                    ToolTip.delay: 700
                    ToolTip.text: text
                    onClicked: {
                        if (modelData === "minimize") window.showMinimized()
                        else if (isClose) window.close()
                        else window.toggleMaximized()
                    }
                    background: Rectangle {
                        radius: 5
                        color: windowButton.isClose && windowButton.hovered ? (windowButton.down ? "#b42318" : "#d92d20")
                            : windowButton.down ? "#dce2ec" : windowButton.hovered ? "#e7ebf2" : "transparent"
                        border.width: windowButton.visualFocus ? 1 : 0
                        border.color: "#4169ed"
                    }
                    contentItem: Item {
                        Canvas {
                            id: buttonIcon
                            anchors.centerIn: parent
                            width: 16
                            height: 16
                            onPaint: {
                                const ctx = getContext("2d")
                                ctx.reset()
                                ctx.strokeStyle = windowButton.iconColor
                                ctx.lineWidth = 1
                                ctx.beginPath()
                                if (windowButton.modelData === "minimize") {
                                    ctx.moveTo(3, 8.5); ctx.lineTo(13, 8.5)
                                } else if (windowButton.isClose) {
                                    ctx.moveTo(3.5, 3.5); ctx.lineTo(12.5, 12.5)
                                    ctx.moveTo(12.5, 3.5); ctx.lineTo(3.5, 12.5)
                                } else if (windowButton.isMaximized) {
                                    ctx.rect(3.5, 5.5, 7, 7)
                                    ctx.moveTo(5.5, 5.5); ctx.lineTo(5.5, 3.5)
                                    ctx.lineTo(12.5, 3.5); ctx.lineTo(12.5, 10.5); ctx.lineTo(10.5, 10.5)
                                } else {
                                    ctx.rect(3.5, 3.5, 9, 9)
                                }
                                ctx.stroke()
                            }
                            Connections {
                                target: windowButton
                                function onIconColorChanged() { buttonIcon.requestPaint() }
                                function onIsMaximizedChanged() { buttonIcon.requestPaint() }
                            }
                        }
                    }
                }
            }
        }
    }

    // Parent the resize handles to the whole window, including its QML header.
    Item {
        id: resizeFrame
        parent: titleBar.parent
        anchors.fill: parent
        z: 100
        visible: window.visibility !== Window.Maximized && window.visibility !== Window.FullScreen
        readonly property int grip: 6
        Repeater {
            model: [Qt.LeftEdge, Qt.RightEdge, Qt.TopEdge, Qt.BottomEdge,
                Qt.TopEdge | Qt.LeftEdge, Qt.TopEdge | Qt.RightEdge,
                Qt.BottomEdge | Qt.LeftEdge, Qt.BottomEdge | Qt.RightEdge]
            delegate: MouseArea {
                required property int modelData
                readonly property bool leftEdge: (modelData & Qt.LeftEdge) !== 0
                readonly property bool rightEdge: (modelData & Qt.RightEdge) !== 0
                readonly property bool topEdge: (modelData & Qt.TopEdge) !== 0
                readonly property bool bottomEdge: (modelData & Qt.BottomEdge) !== 0
                readonly property bool corner: (leftEdge || rightEdge) && (topEdge || bottomEdge)
                x: leftEdge ? 0 : rightEdge ? resizeFrame.width - width : resizeFrame.grip
                y: topEdge ? 0 : bottomEdge ? resizeFrame.height - height : resizeFrame.grip
                width: leftEdge || rightEdge ? resizeFrame.grip : resizeFrame.width - 2 * resizeFrame.grip
                height: topEdge || bottomEdge ? resizeFrame.grip : resizeFrame.height - 2 * resizeFrame.grip
                acceptedButtons: Qt.LeftButton
                cursorShape: corner ? (leftEdge === topEdge ? Qt.SizeFDiagCursor : Qt.SizeBDiagCursor)
                    : leftEdge || rightEdge ? Qt.SizeHorCursor : Qt.SizeVerCursor
                onPressed: window.startSystemResize(modelData)
            }
        }
    }

    Rectangle {
        id: sidebar
        width: 210
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        color: "#142039"
        ColumnLayout {
            anchors.fill: parent
            anchors.margins: 18
            spacing: 10
            Item { Layout.preferredHeight: 16 }
            Rectangle {
                Layout.leftMargin: 6
                width: 44; height: 44; radius: 12; color: "#4169ed"
                Text { anchors.centerIn: parent; text: "A"; color: "white"; font.pixelSize: 27; font.bold: true }
            }
            Label { text: "经典输入法切换"; color: "white"; font.pixelSize: 19; font.bold: true; Layout.topMargin: 6; Layout.leftMargin: 6 }
            Label { text: "CLASSIC IME"; color: "#9daac4"; font.pixelSize: 10; font.letterSpacing: 2; Layout.leftMargin: 6 }
            Item { Layout.preferredHeight: 30 }
            Repeater {
                model: window.pageTitles
                delegate: Button {
                    required property int index
                    required property string modelData
                    Layout.fillWidth: true
                    implicitHeight: 46
                    onClicked: window.currentPage = index
                    contentItem: Text {
                        text: modelData
                        color: window.currentPage === index ? "white" : "#b9c4d8"
                        font.pixelSize: 14
                        font.bold: window.currentPage === index
                        leftPadding: 15
                        verticalAlignment: Text.AlignVCenter
                    }
                    background: Rectangle { radius: 9; color: window.currentPage === index ? "#2a3c60" : parent.hovered ? "#1e2f4d" : "transparent" }
                }
            }
            Item { Layout.fillHeight: true }
            Rectangle { Layout.fillWidth: true; height: 1; color: "#2a3953" }
            RowLayout {
                Layout.topMargin: 10
                Rectangle { width: 7; height: 7; radius: 4; color: window.backend.enabled ? "#6ce9a6" : "#98a2b3" }
                Label { text: window.backend.enabled ? "经典切换已开启" : "经典切换未开启"; color: "#c5cee0"; font.pixelSize: 12 }
            }
            Label { text: "v0.1.0 · 个人工具"; color: "#7f8eac"; font.pixelSize: 11; Layout.bottomMargin: 6 }
        }
    }

    ColumnLayout {
        anchors.left: sidebar.right
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.bottom: noticeBar.top
        anchors.margins: 28
        spacing: 20
        RowLayout {
            Layout.fillWidth: true
            ColumnLayout {
                spacing: 7
                Label { text: window.pageTitles[window.currentPage]; font.pixelSize: 27; font.bold: true; color: "#101828" }
                Label { text: window.pageDescriptions[window.currentPage]; font.pixelSize: 13; color: "#667085"; Layout.fillWidth: true; wrapMode: Text.Wrap }
            }
            Item { Layout.fillWidth: true }
            ActionButton { text: "刷新列表"; onClicked: window.backend.refresh() }
        }

        StackLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            currentIndex: window.currentPage
            ScrollView {
                id: switchScroll
                objectName: "switchPage"
                contentWidth: availableWidth
                clip: true
                ColumnLayout {
                    width: switchScroll.availableWidth
                    spacing: 18
                    Panel {
                        Layout.fillWidth: true
                        RowLayout {
                            Layout.fillWidth: true
                            ColumnLayout {
                                spacing: 9
                                Label { text: "经典的切换方式"; font.pixelSize: 20; font.bold: true; color: "#101828" }
                                Label { text: "一组快捷键，在中文与独立美式键盘之间切换。"; color: "#667085"; font.pixelSize: 13 }
                            }
                            Item { Layout.fillWidth: true }
                            Switch { id: enableSwitch; objectName: "enableSwitch"; checked: window.backend.enabled; onToggled: window.backend.enabled = checked }
                        }
                        Rectangle {
                            Layout.fillWidth: true
                            implicitHeight: 115
                            radius: 10
                            color: "#eef2ff"
                            Column {
                                anchors.centerIn: parent
                                spacing: 9
                                Text { anchors.horizontalCenter: parent.horizontalCenter; text: window.backend.shortcutText; color: "#3656c8"; font.pixelSize: 28; font.bold: true }
                                Text { anchors.horizontalCenter: parent.horizontalCenter; text: "中文输入法  ↔  美式键盘"; color: "#6678b8"; font.pixelSize: 13 }
                            }
                        }
                        RowLayout {
                            Layout.fillWidth: true
                            Label { text: window.backend.enabled ? "●  切换正在运行" : "○  开启上方开关后生效"; color: window.backend.enabled ? "#027a48" : "#667085" }
                            Item { Layout.fillWidth: true }
                            Label { text: "当前：" + window.backend.currentInput; color: "#667085"; font.pixelSize: 12 }
                        }
                    }
                    Panel {
                        Layout.fillWidth: true
                        Label { text: "切换目标"; font.pixelSize: 16; font.bold: true; color: "#101828" }
                        RowLayout {
                            Layout.fillWidth: true
                            ColumnLayout {
                                Label { text: "美式键盘"; color: "#344054" }
                                Label { text: window.backend.hasUS ? "已添加，可以直接输入英文" : "尚未添加，点击右侧按钮完成配置"; color: "#667085"; font.pixelSize: 12 }
                            }
                            Item { Layout.fillWidth: true }
                            ActionButton { text: window.backend.hasUS ? "已就绪" : "添加美式键盘"; primary: !window.backend.hasUS; enabled: !window.backend.hasUS; onClicked: window.backend.addUS() }
                        }
                        Rectangle { Layout.fillWidth: true; height: 1; color: "#eaecf0" }
                        RowLayout {
                            Layout.fillWidth: true
                            Label { text: "中文输入法"; color: "#344054" }
                            Item { Layout.fillWidth: true }
                            ComboBox {
                                id: chineseCombo
                                objectName: "chineseCombo"
                                Layout.preferredWidth: 280
                                model: window.backend.chineseChoices
                                textRole: "displayName"
                                valueRole: "profileId"
                                currentIndex: {
                                    const choices = window.backend.chineseChoices
                                    for (let i = 0; i < choices.length; ++i) if (choices[i].profileId === window.backend.chineseId) return i
                                    return -1
                                }
                                displayText: currentIndex < 0 ? "请选择已启用的中文输入法" : currentText
                                onActivated: window.backend.chineseId = currentValue
                            }
                        }
                        Label { visible: !window.backend.ready; text: window.backend.preparationHint; color: "#b54708"; font.pixelSize: 12; Layout.fillWidth: true; wrapMode: Text.Wrap }
                    }
                    Panel {
                        Layout.fillWidth: true
                        Label { text: "在这里试一试"; font.pixelSize: 16; font.bold: true; color: "#101828" }
                        TextField { objectName: "testInput"; Layout.fillWidth: true; placeholderText: "开启切换后，在此输入文字并按快捷键测试"; selectByMouse: true }
                    }
                    Item { Layout.preferredHeight: 4 }
                }
            }
            ScrollView {
                id: managerScroll
                objectName: "managerPage"
                contentWidth: availableWidth
                clip: true
                ColumnLayout {
                    width: managerScroll.availableWidth
                    spacing: 18
                    Panel {
                        Layout.fillWidth: true
                        RowLayout {
                            Layout.fillWidth: true
                            ColumnLayout {
                                Label { text: "为当前用户管理输入法"; font.pixelSize: 17; font.bold: true; color: "#101828" }
                                Label { text: "修改前自动保存配置快照，可重新启用被禁用的输入法。"; font.pixelSize: 12; color: "#667085" }
                            }
                            Item { Layout.fillWidth: true }
                            ActionButton { text: window.backend.hasUS ? "美式键盘已添加" : "添加美式键盘"; primary: !window.backend.hasUS; enabled: !window.backend.hasUS; onClicked: window.backend.addUS() }
                        }
                    }
                    Label { text: "已启用  ·  " + window.backend.enabledProfiles.length; font.pixelSize: 16; font.bold: true; color: "#344054" }
                    Repeater {
                        model: window.backend.enabledProfiles
                        delegate: Rectangle {
                            id: enabledProfileItem
                            required property var modelData
                            Layout.fillWidth: true
                            implicitHeight: 85
                            radius: 11; color: "white"; border.color: "#e4e7ec"
                            RowLayout {
                                anchors.fill: parent; anchors.margins: 17; spacing: 13
                                Rectangle {
                                    width: 38; height: 38; radius: 10; color: enabledProfileItem.modelData.isUS ? "#eef2ff" : "#ecfdf3"
                                    Text { anchors.centerIn: parent; text: enabledProfileItem.modelData.isUS ? "A" : "中"; font.pixelSize: 19; color: enabledProfileItem.modelData.isUS ? "#4169ed" : "#039855" }
                                }
                                ColumnLayout {
                                    Layout.fillWidth: true
                                    Label { text: enabledProfileItem.modelData.displayName + (enabledProfileItem.modelData.systemDefault ? "  ·  默认" : ""); color: "#101828"; font.bold: true; elide: Text.ElideRight; Layout.fillWidth: true }
                                    Label { text: enabledProfileItem.modelData.languageName + " · " + enabledProfileItem.modelData.kind; color: "#667085"; font.pixelSize: 11; elide: Text.ElideRight; Layout.fillWidth: true }
                                }
                                ActionButton { text: "设为默认"; enabled: !enabledProfileItem.modelData.systemDefault; onClicked: window.backend.setDefaultProfile(enabledProfileItem.modelData.profileId) }
                                ActionButton { text: "禁用"; onClicked: { disableDialog.profileId = enabledProfileItem.modelData.profileId; disableDialog.profileName = enabledProfileItem.modelData.displayName; disableDialog.open() } }
                            }
                        }
                    }
                    Label { text: "未启用  ·  " + window.backend.disabledProfiles.length; font.pixelSize: 16; font.bold: true; color: "#344054"; Layout.topMargin: 7 }
                    Repeater {
                        model: window.backend.disabledProfiles
                        delegate: Rectangle {
                            id: disabledProfileItem
                            required property var modelData
                            Layout.fillWidth: true
                            implicitHeight: 69
                            radius: 11; color: "#fafbfc"; border.color: "#e4e7ec"
                            RowLayout {
                                anchors.fill: parent; anchors.margins: 16
                                ColumnLayout {
                                    Layout.fillWidth: true
                                    Label { text: disabledProfileItem.modelData.displayName; color: "#475467"; Layout.fillWidth: true; elide: Text.ElideRight }
                                    Label { text: disabledProfileItem.modelData.languageName; color: "#98a2b3"; font.pixelSize: 11 }
                                }
                                ActionButton { text: "启用"; onClicked: window.backend.setProfileEnabled(disabledProfileItem.modelData.profileId, true) }
                            }
                        }
                    }
                    ActionButton { text: "打开 Windows 语言设置"; onClicked: window.backend.openWindowsLanguageSettings() }
                    Item { Layout.preferredHeight: 4 }
                }
            }
            ScrollView {
                id: settingsScroll
                objectName: "settingsPage"
                contentWidth: availableWidth
                clip: true
                ColumnLayout {
                    width: settingsScroll.availableWidth
                    spacing: 18
                    Panel {
                        Layout.fillWidth: true
                        Label { text: "操作习惯"; color: "#101828"; font.pixelSize: 17; font.bold: true }
                        RowLayout {
                            Layout.fillWidth: true
                            ColumnLayout {
                                Label { text: "切换快捷键"; color: "#344054" }
                                Label { text: "松开空格完成切换，长按只触发一次"; color: "#667085"; font.pixelSize: 12 }
                            }
                            Item { Layout.fillWidth: true }
                            ComboBox { model: ["Ctrl + Space", "Ctrl + Shift + Space", "Alt + Space"]; currentIndex: window.backend.shortcut; onActivated: window.backend.shortcut = currentIndex; Layout.preferredWidth: 260 }
                        }
                        Rectangle { Layout.fillWidth: true; height: 1; color: "#eaecf0" }
                        RowLayout {
                            Layout.fillWidth: true
                            ColumnLayout {
                                Label { text: "登录 Windows 时启动"; color: "#344054" }
                                Label { text: "以管理员权限启动后留在托盘，沿用上次设置"; color: "#667085"; font.pixelSize: 12 }
                            }
                            Item { Layout.fillWidth: true }
                            Switch { checked: window.backend.autoStart; onToggled: window.backend.autoStart = checked }
                        }
                        Rectangle { Layout.fillWidth: true; height: 1; color: "#eaecf0" }
                        RowLayout {
                            Layout.fillWidth: true
                            ColumnLayout {
                                Label { text: "关闭窗口时留在托盘"; color: "#344054" }
                                Label { text: "从托盘菜单可以完全退出"; color: "#667085"; font.pixelSize: 12 }
                            }
                            Item { Layout.fillWidth: true }
                            Switch { checked: window.backend.closeToTray; onToggled: window.backend.closeToTray = checked }
                        }
                    }
                    Panel {
                        Layout.fillWidth: true
                        Label { text: "运行与诊断"; color: "#101828"; font.pixelSize: 17; font.bold: true }
                        Label { text: "Qt " + window.backend.qtVersion + "  ·  QML + C++  ·  64 位"; color: "#667085" }
                        Label { text: "配置、操作日志和修改前快照保存在："; color: "#667085"; font.pixelSize: 12 }
                        Text { text: window.backend.dataDirectory; Layout.fillWidth: true; wrapMode: Text.WrapAnywhere; color: "#475467"; font.pixelSize: 12 }
                        RowLayout {
                            ActionButton { text: "打开数据目录"; onClicked: window.backend.openDataDirectory() }
                            ActionButton { text: "退出应用"; onClicked: window.backend.quit() }
                        }
                    }
                    Item { Layout.fillHeight: true }
                }
            }
        }
    }
    Rectangle {
        id: noticeBar
        anchors.left: sidebar.right; anchors.right: parent.right; anchors.bottom: parent.bottom
        height: 44
        color: window.backend.noticeError ? "#fff4ed" : "#edf2fa"
        Label { anchors.fill: parent; anchors.leftMargin: 28; anchors.rightMargin: 20; verticalAlignment: Text.AlignVCenter; text: window.backend.notice; color: window.backend.noticeError ? "#b54708" : "#5a6b85"; font.pixelSize: 12; elide: Text.ElideRight }
    }
    Dialog {
        id: disableDialog
        property string profileId: ""
        property string profileName: ""
        title: "禁用输入法"
        width: 420
        modal: true
        anchors.centerIn: parent
        footer: RowLayout {
            spacing: 10
            Item { Layout.fillWidth: true }
            ActionButton { text: "取消"; onClicked: disableDialog.reject() }
            ActionButton { text: "禁用"; danger: true; onClicked: disableDialog.accept() }
        }
        contentItem: Item {
            implicitWidth: 380
            implicitHeight: disableMessage.implicitHeight + 24
            Label {
                id: disableMessage
                x: 12; y: 12; width: parent.width - 24
                text: "禁用“" + disableDialog.profileName + "”？\n之后可以在未启用列表中恢复。"
                wrapMode: Text.Wrap
            }
        }
        onAccepted: window.backend.setProfileEnabled(profileId, false)
    }
}
