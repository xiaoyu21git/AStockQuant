// RiskParameterSlider.qml - 风险参数滑块组件
import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15

Rectangle {
    id: riskParameterSlider
    
    // 属性
    property string parameterName: "参数名称"
    property real parameterValue: 50
    property real value: parameterValue  // 别名，兼容旧代码
    property real minValue: 0
    property real maxValue: 100
    property real stepSize: 0.1
    property string unit: "%"
    property string description: "参数描述"
    
    // 外观
    radius: 8
    color: "#121c44"  // darkCard
    border.color: "#2a3560"  // darkBorder
    border.width: 1
    
    implicitHeight: 120
    
    // 内容布局
    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 16
        spacing: 12
        
        // 参数名称和值
        RowLayout {
            Layout.fillWidth: true
            
            // 参数名称
            Text {
                text: parameterName
                font.pixelSize: 16
                font.bold: true
                color: "#e0e0e0"  // darkText
                Layout.fillWidth: true
            }
            
            // 当前值
            Text {
                text: parameterValue.toFixed(1) + unit
                font.pixelSize: 16
                font.bold: true
                color: getValueColor()
            }
        }
        
        // 滑块
        Slider {
            id: slider
            Layout.fillWidth: true
            from: minValue
            to: maxValue
            value: riskParameterSlider.parameterValue
            stepSize: riskParameterSlider.stepSize
            
            background: Rectangle {
                x: slider.leftPadding
                y: slider.topPadding + slider.availableHeight / 2 - height / 2
                implicitWidth: 200
                implicitHeight: 4
                width: slider.availableWidth
                height: implicitHeight
                radius: 2
                color: "#2a3560"  // darkBorder
                
                Rectangle {
                    width: slider.visualPosition * parent.width
                    height: parent.height
                    color: getValueColor()
                    radius: 2
                }
            }
            
            handle: Rectangle {
                x: slider.leftPadding + slider.visualPosition * (slider.availableWidth - width)
                y: slider.topPadding + slider.availableHeight / 2 - height / 2
                implicitWidth: 20
                implicitHeight: 20
                radius: 10
                color: slider.pressed ? Qt.darker(getValueColor(), 1.2) : getValueColor()
                border.color: "#2a3560"  // darkBorder
                border.width: 2
            }
            
            onValueChanged: {
                riskParameterSlider.parameterValue = value
                riskParameterSlider.value = value  // 更新别名
                // Qt会自动为value属性创建valueChanged信号
                riskParameterSlider.parameterValueChanged(value)  // 发出自定义信号
            }
        }
        
        // 范围标签
        RowLayout {
            Layout.fillWidth: true
            
            Text {
                text: minValue + unit
                font.pixelSize: 12
                color: "#a0a0a0"  // darkTextSecondary
            }
            
            Item { Layout.fillWidth: true }
            
            Text {
                text: maxValue + unit
                font.pixelSize: 12
                color: "#a0a0a0"  // darkTextSecondary
            }
        }
        
        // 描述
        Text {
            text: description
            font.pixelSize: 12
            color: "#a0a0a0"  // darkTextSecondary
            wrapMode: Text.WordWrap
            Layout.fillWidth: true
        }
    }
    
    // 根据值获取颜色
    function getValueColor() {
        // 对于负值（如止损、亏损），值越小（越负）风险越高
        if (minValue < 0) {
            // 负值范围：越负风险越高（红色）
            if (parameterValue < minValue * 0.7) return "#f44336"  // dangerColor
            else if (parameterValue < minValue * 0.4) return "#ff9800"  // warningColor
            else return "#4caf50"  // successColor
        } else {
            // 正值范围：值越大风险越高
            var range = maxValue - minValue
            var normalized = (parameterValue - minValue) / range
            
            if (normalized > 0.7) return "#f44336"  // dangerColor
            else if (normalized > 0.4) return "#ff9800"  // warningColor
            else return "#4caf50"  // successColor
        }
    }
}