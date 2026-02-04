// components/ControlPanel.qml
import QtQuick 2.15
import QtQuick.Layouts 1.15
import "../utils/Constants.js" as Constants

Rectangle {
    id: controlPanel
    implicitWidth: 320
    implicitHeight: 250
    radius: Constants.borderRadiusXLarge
    color: Constants.secondaryBg
    border.color: Constants.borderColor
    
    // 信号
    signal startClicked()
    signal pauseClicked()
    signal stopClicked()
    signal optimizeClicked()
    
    ColumnLayout {
        anchors.fill: parent
        anchors.margins: Constants.spacingXLarge
        
        Text {
            text: "策略控制"
            font.pixelSize: Constants.fontSizeLarge
            font.weight: Font.DemiBold
            color: Constants.textPrimary
        }
        
        // 控制按钮
        ColumnLayout {
            Layout.topMargin: Constants.spacingLarge
            spacing: Constants.spacingMedium
            
            ControlButton {
                text: "\uf04b 启动策略"
                gradientType: "primary"
                Layout.fillWidth: true
                onClicked: controlPanel.startClicked()
            }
            
            ControlButton {
                text: "\uf04c 暂停策略"
                gradientType: "warning"
                Layout.fillWidth: true
                onClicked: controlPanel.pauseClicked()
            }
            
            ControlButton {
                text: "\uf04d 停止策略"
                gradientType: "danger"
                Layout.fillWidth: true
                onClicked: controlPanel.stopClicked()
            }
            
            ControlButton {
                text: "\uf085 优化参数"
                gradientType: "purple"
                Layout.fillWidth: true
                onClicked: controlPanel.optimizeClicked()
            }
        }
    }
}

// 控制按钮组件
Rectangle {
    id: controlButton
    implicitHeight: 44
    radius: Constants.borderRadiusLarge
    
    property string text: ""
    property string gradientType: "primary" // primary, success, danger, warning, purple
    
    signal clicked()
    
    gradient: {
        switch(gradientType) {
        case "primary": return Constants.primaryGradient();
        case "success": return Constants.successGradient();
        case "danger": return Constants.dangerGradient();
        case "warning": return Constants.warningGradient();
        case "purple": return Constants.purpleGradient();
        default: return Constants.primaryGradient();
        }
    }
    
    Text {
        anchors.centerIn: parent
        text: controlButton.text
        color: "white"
        font.pixelSize: Constants.fontSizeMedium
        font.weight: Font.DemiBold
    }
    
    MouseArea {
        anchors.fill: parent
        cursorShape: Qt.PointingHandCursor
        hoverEnabled: true
        
        onEntered: {
            controlButton.scale = 1.02;
        }
        
        onExited: {
            controlButton.scale = 1.0;
        }
        
        onClicked: {
            controlButton.clicked();
        }
    }
}