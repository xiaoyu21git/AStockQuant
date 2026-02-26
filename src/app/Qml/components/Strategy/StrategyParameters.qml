import QtQuick 2.15
import QtQuick.Layouts 1.15

Rectangle {
    id: parametersPanel
    radius: 16  // borderRadiusXLarge
    color: "#1E293B"  // secondaryBg
    border.color: "#475569"  // borderColor
    
    // 属性
    property var parameters: [
        {name: "短期均线周期", value: 20, min: 5, max: 200, unit: "", color: accentBlue},
        {name: "长期均线周期", value: 60, min: 10, max: 500, unit: "", color: accentBlue},
        {name: "止损比例", value: 5, min: 1, max: 20, unit: "%", color: warningAmber},
        {name: "止盈比例", value: 10, min: 1, max: 30, unit: "%", color: successGreen}
    ]
    
    // 信号
    signal parameterChanged(int index, real value)
    signal resetClicked()
    
    // 颜色常量
    readonly property color textPrimary: "#F1F5F9"
    readonly property color textSecondary: "#94A3B8"
    readonly property color accentBlue: "#3B82F6"
    readonly property color warningAmber: "#F59E0B"
    readonly property color successGreen: "#10B981"
    readonly property color tertiaryBg: "#334155"
    readonly property color borderLight: "#64748B"
    
    readonly property int fontSizeNormal: 14
    readonly property int fontSizeMedium: 16
    
    readonly property real spacingMedium: 8
    readonly property real spacingLarge: 16
    
    readonly property real borderRadiusMedium: 8
    
    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 20
        
        Text {
            text: "策略参数"
            font.pixelSize: fontSizeMedium
            font.weight: Font.DemiBold
            color: textPrimary
        }
        
        GridLayout {
            columns: 2
            columnSpacing: spacingLarge
            rowSpacing: spacingMedium
            Layout.topMargin: spacingMedium
            
            Repeater {
                model: parameters
                
                delegate: ColumnLayout {
                    spacing: 4
                    
                    RowLayout {
                        Text {
                            text: modelData.name
                            font.pixelSize: fontSizeNormal
                            color: textSecondary
                        }
                        
                        Item { Layout.fillWidth: true }
                        
                        Text {
                            text: modelData.value + modelData.unit
                            font.pixelSize: fontSizeNormal
                            font.weight: Font.DemiBold
                            color: {
                                if (modelData.color === "warning") return warningAmber;
                                if (modelData.color === "success") return successGreen;
                                if (modelData.color === "blue") return accentBlue;
                                return textPrimary;
                            }
                        }
                    }
                    
                    // 滑块背景
                    Rectangle {
                        Layout.fillWidth: true
                        height: 6
                        radius: 3
                        color: tertiaryBg
                        
                        // 滑块值
                        Rectangle {
                            width: parent.width * ((modelData.value - modelData.min) / (modelData.max - modelData.min))
                            height: parent.height
                            radius: 3
                            color: accentBlue
                        }
                        
                        MouseArea {
                            anchors.fill: parent
                            cursorShape: Qt.PointingHandCursor
                            onClicked: {
                                // 点击调节参数
                                var newValue = Math.min(modelData.max, Math.max(modelData.min, 
                                    modelData.value + (modelData.max - modelData.min) * 0.1));
                                parameters[index].value = newValue;
                                parameterChanged(index, newValue);
                            }
                        }
                    }
                }
            }
        }
        
        Item { Layout.fillHeight: true }
        
        // 重置按钮
        Rectangle {
            Layout.fillWidth: true
            height: 36
            radius: borderRadiusMedium
            color: tertiaryBg
            border.color: borderLight
            
            Text {
                anchors.centerIn: parent
                text: "重置参数"
                font.pixelSize: fontSizeNormal
                color: textSecondary
            }
            
            MouseArea {
                anchors.fill: parent
                cursorShape: Qt.PointingHandCursor
                onClicked: resetClicked()
            }
        }
    }
}