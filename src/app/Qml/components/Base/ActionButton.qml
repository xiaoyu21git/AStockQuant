import QtQuick 2.15

Rectangle {
    id: root

    property string label: ""
    property string tone: "neutral"
    property int buttonWidth: 104
    property int buttonHeight: 36
    property int labelSize: 14
    property bool buttonEnabled: true

    signal clicked()

    width: buttonWidth
    height: buttonHeight
    radius: Math.min(14, Math.round(buttonHeight / 2))
    color: !buttonEnabled ? "#334155"
        : (tone === "primary" ? "#3B82F6"
        : tone === "secondary" ? "#1D4ED8"
        : tone === "warning" ? "#9A3412"
        : tone === "success" ? "#10B981"
        : tone === "danger" ? "#7F1D1D"
        : tone === "muted" ? "#1E293B"
        : "#334155")
    border.width: 1
    border.color: !buttonEnabled ? "#475569"
        : (tone === "primary" ? "#60A5FA"
        : tone === "secondary" ? "#60A5FA"
        : tone === "warning" ? "#FDBA74"
        : tone === "success" ? "#6EE7B7"
        : tone === "danger" ? "#F87171"
        : "#475569")
    opacity: buttonEnabled ? 1.0 : 0.7

    Text {
        anchors.centerIn: parent
        text: root.label
        font.pixelSize: root.labelSize
        font.weight: Font.Medium
        color: "#F8FAFC"
    }

    MouseArea {
        anchors.fill: parent
        enabled: root.buttonEnabled
        cursorShape: enabled ? Qt.PointingHandCursor : Qt.ArrowCursor
        onClicked: root.clicked()
    }
}