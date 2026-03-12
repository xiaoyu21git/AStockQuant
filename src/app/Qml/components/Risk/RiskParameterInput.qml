// RiskParameterInput.qml - 风险参数输入组件
import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15

Rectangle {
    id: riskParameterInput
    
    // 属性
    property string parameterName: "参数名称"
    property int value: 10
    property int minValue: 0
    property int maxValue: 100
    property string unit: "个"
    property string description: "参数描述"
    
    // 信号
    signal valueChanged(int newValue)
    
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
                text: value + unit
                font.pixelSize: 16
                font.bold: true
                color: getValueColor()
            }
        }
        
        // 输入控制
        RowLayout {
            Layout.fillWidth: true
            spacing: 8
            
            // 减少按钮
            Button {
                text: "-"
                font.pixelSize: 18
                font.bold: true
                Layout.preferredWidth: 40
                Layout.preferredHeight: 40
                
                background: Rectangle {
                    radius: 4
                    color: parent.pressed ? Qt.darker("#2a3560", 1.2) : "#2a3560"  // darkBorder
                }
                contentItem: Text {
                    text: parent.text
                    color: "#e0e0e0"  // darkText
                    font: parent.font
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                }
                
                onClicked: {
                    if (riskParameterInput.value > riskParameterInput.minValue) {
                        riskParameterInput.value--
                        riskParameterInput.valueChanged(riskParameterInput.value)
                    }
                }
            }
            
            // 输入框
            TextField {
                id: valueInput
                Layout.fillWidth: true
                Layout.preferredHeight: 40
                text: riskParameterInput.value.toString()
                font.pixelSize: 16
                horizontalAlignment: Text.AlignHCenter
                validator: IntValidator {
                    bottom: riskParameterInput.minValue
                    top: riskParameterInput.maxValue
                }
                
                background: Rectangle {
                    radius: 4
                    color: "#121c44"  // darkCard
                    border.color: "#2a3560"  // darkBorder
                    border.width: 1
                }
                
                onEditingFinished: {
                    var newValue = parseInt(text)
                    if (!isNaN(newValue) && newValue >= minValue && newValue <= maxValue) {
                        riskParameterInput.value = newValue
                        riskParameterInput.valueChanged(newValue)
                    } else {
                        text = riskParameterInput.value.toString()
                    }
                }
            }
            
            // 增加按钮
            Button {
                text: "+"
                font.pixelSize: 18
                font.bold: true
                Layout.preferredWidth: 40
                Layout.preferredHeight: 40
                
                background: Rectangle {
                    radius: 4
                    color: parent.pressed ? Qt.darker("#2a3560", 1.2) : "#2a3560"  // darkBorder
                }
                contentItem: Text {
                    text: parent.text
                    color: "#e0e0e0"  // darkText
                    font: parent.font
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                }
                
                onClicked: {
                    if (riskParameterInput.value < riskParameterInput.maxValue) {
                        riskParameterInput.value++
                        riskParameterInput.valueChanged(riskParameterInput.value)
                    }
                }
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
        var range = maxValue - minValue
        var normalized = (value - minValue) / range
        
        if (normalized > 0.7) return "#f44336"  // dangerColor
        else if (normalized > 0.4) return "#ff9800"  // warningColor
        else return "#4caf50"  // successColor
    }
}