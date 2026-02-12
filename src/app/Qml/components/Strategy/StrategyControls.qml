import QtQuick 2.15
import QtQuick.Layouts 1.15

Rectangle {
    id: controlsPanel
    radius: 16  // borderRadiusXLarge
    color: "#1E293B"  // secondaryBg
    border.color: "#475569"  // borderColor
    
    // 属性
    property string currentStatus: "stopped"  // running, paused, stopped
    
    // 信号
    signal startClicked()
    signal pauseClicked()
    signal stopClicked()
    signal optimizeClicked()
    
    // 颜色常量
    readonly property color textPrimary: "#F1F5F9"
    readonly property color accentBlue: "#3B82F6"
    readonly property color warningAmber: "#F59E0B"
    readonly property color dangerRed: "#EF4444"
    readonly property color accentPurple: "#8B5CF6"
    readonly property color successGreen: "#10B981"
    
    readonly property int fontSizeNormal: 14
    readonly property int fontSizeMedium: 16
    
    readonly property real spacingMedium: 8
    readonly property real spacingLarge: 16
    
    readonly property real borderRadiusMedium: 8
    
    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 20
        spacing: spacingMedium
        
        Text {
            text: "策略控制"
            font.pixelSize: fontSizeMedium
            font.weight: Font.DemiBold
            color: textPrimary
            Layout.alignment: Qt.AlignLeft
        }
        
        // 控制按钮
        RowLayout {
            spacing: spacingMedium
            Layout.alignment: Qt.AlignHCenter
            Layout.fillWidth: true
            
            ControlButton {
                Layout.preferredWidth: 80
                Layout.preferredHeight: 40
                icon: "▶"
                label: "启动"
                gradientStart: accentBlue
                gradientEnd: "#1D4ED8"
                enabled: currentStatus !== "running"
                onClicked: startClicked()
            }
            
            ControlButton {
                Layout.preferredWidth: 80
                Layout.preferredHeight: 40
                icon: "■"
                label: "停止"
                gradientStart: dangerRed
                gradientEnd: "#DC2626"
                enabled: currentStatus === "running" || currentStatus === "paused"
                onClicked: stopClicked()
            }
            
            ControlButton {
                Layout.preferredWidth: 80
                Layout.preferredHeight: 40
                icon: "⚙"
                label: "优化"
                gradientStart: accentPurple
                gradientEnd: "#7C3AED"
                onClicked: optimizeClicked()
            }
        }
        
        // 状态显示 - 修复高度问题
        Rectangle {
            Layout.alignment: Qt.AlignHCenter
            Layout.topMargin: spacingMedium
            width: Math.min(parent.width - 40, 220)  // 限制最大宽度
            height: 36  // 增加高度，确保内容不溢出
            radius: borderRadiusMedium
            color: "#33415580"
            
            Row {
                anchors.centerIn: parent
                spacing: spacingMedium
                
                // 状态指示灯
                Rectangle {
                    width: 10
                    height: 10
                    radius: 5
                    anchors.verticalCenter: parent.verticalCenter
                    color: {
                        if (currentStatus === "running") return successGreen;
                        if (currentStatus === "paused") return warningAmber;
                        return dangerRed;
                    }
                }
                
                // 状态文本
                Text {
                    text: {
                        if (currentStatus === "running") return "策略运行中";
                        if (currentStatus === "paused") return "策略已暂停";
                        return "策略已停止";
                    }
                    font.pixelSize: fontSizeNormal
                    color: textPrimary
                    anchors.verticalCenter: parent.verticalCenter
                }
            }
        }
    }
    
    // 控制按钮组件
    component ControlButton: Rectangle {
        property string icon: ""
        property string label: ""
        property color gradientStart: "#3B82F6"
        property color gradientEnd: "#1D4ED8"
        property bool enabled: true
        
        signal clicked()
        
        radius: borderRadiusMedium
        opacity: enabled ? 1.0 : 0.5
        
        gradient: Gradient {
            GradientStop { position: 0.0; color: gradientStart }
            GradientStop { position: 1.0; color: gradientEnd }
        }
        
        Column {
            anchors.centerIn: parent
            spacing: 2
            
            Text {
                text: parent.parent.icon
                font.pixelSize: 14
                font.family: "Segoe UI Symbol, Arial"
                color: "white"
                anchors.horizontalCenter: parent.horizontalCenter
            }
            
            Text {
                text: parent.parent.label
                font.pixelSize: 12
                color: "white"
                anchors.horizontalCenter: parent.horizontalCenter
            }
        }
        
        MouseArea {
            anchors.fill: parent
            cursorShape: Qt.PointingHandCursor
            enabled: parent.enabled
            onClicked: parent.clicked()
            
            onPressed: parent.scale = 0.95
            onReleased: parent.scale = 1.0
            onCanceled: parent.scale = 1.0
        }
    }
}