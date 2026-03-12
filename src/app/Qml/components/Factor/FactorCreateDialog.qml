// FactorCreateDialog.qml
// 因子创建对话框

import QtQuick 2.15
import QtQuick.Layouts 1.15
import QtQuick.Controls 2.15
import QtQuick.Dialogs 1.3

Dialog {
    id: dialog
    
    property string factorType: ""
    property string factorTypeName: ""
    
    title: "创建 " + factorTypeName + " 因子"
    width: 600
    height: 500
    modality: Qt.ApplicationModal
    
    // 因子数据
    property string factorName: ""
    property string factorDescription: ""
    property var factorParameters: ({})
    
    signal factorCreated(string factorName, string factorType, string description, var parameters)
    
    background: Rectangle {
        anchors.fill: parent
        radius: 8
        color: "#0F172A"
        border.width: 1
        border.color: "#334155"
    }
    
    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 20
        spacing: 16
        
        // 对话框标题
        Text {
            text: "创建 " + factorTypeName + " 因子"
            font.pixelSize: 20
            font.weight: Font.DemiBold
            color: "#F1F5F9"
        }
        
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 1
            color: "#334155"
        }
        
        // 因子名称输入
        ColumnLayout {
            spacing: 4
            
            Text {
                text: "因子名称"
                font.pixelSize: 14
                color: "#F1F5F9"
            }
            
            TextField {
                id: nameField
                Layout.fillWidth: true
                Layout.preferredHeight: 40
                placeholderText: "请输入因子名称"
                font.pixelSize: 14
                color: "#F1F5F9"
                
                background: Rectangle {
                    radius: 6
                    color: "#1E293B"
                    border.width: 1
                    border.color: "#334155"
                }
                
                onTextChanged: factorName = text
            }
        }
        
        // 因子描述输入
        ColumnLayout {
            spacing: 4
            
            Text {
                text: "因子描述"
                font.pixelSize: 14
                color: "#F1F5F9"
            }
            
            TextArea {
                id: descriptionField
                Layout.fillWidth: true
                Layout.preferredHeight: 80
                placeholderText: "请输入因子描述"
                font.pixelSize: 14
                color: "#F1F5F9"
                wrapMode: Text.WordWrap
                
                background: Rectangle {
                    radius: 6
                    color: "#1E293B"
                    border.width: 1
                    border.color: "#334155"
                }
                
                onTextChanged: factorDescription = text
            }
        }
        
        // 参数配置区域
        ColumnLayout {
            spacing: 4
            
            Text {
                text: "参数配置"
                font.pixelSize: 14
                color: "#F1F5F9"
            }
            
            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: 200
                radius: 8
                color: "#1E293B"
                border.width: 1
                border.color: "#334155"
                
                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 12
                    spacing: 12
                    
                    // 根据因子类型显示不同参数
                    Loader {
                        id: parameterLoader
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        sourceComponent: getParameterComponent()
                        
                        function getParameterComponent() {
                            switch(factorType) {
                                case "momentum":
                                    return momentumParameters
                                case "value":
                                    return valueParameters
                                case "quality":
                                    return qualityParameters
                                case "technical":
                                    return technicalParameters
                                case "sentiment":
                                    return sentimentParameters
                                case "macro":
                                    return macroParameters
                                case "risk":
                                    return riskParameters
                                case "custom":
                                    return customParameters
                                default:
                                    return genericParameters
                            }
                        }
                    }
                }
            }
        }
        
        Item { Layout.fillHeight: true }
        
        // 操作按钮
        RowLayout {
            Layout.fillWidth: true
            spacing: 12
            
            Button {
                text: "取消"
                Layout.preferredWidth: 100
                
                background: Rectangle {
                    radius: 6
                    color: "#334155"
                }
                
                contentItem: Text {
                    text: parent.text
                    font.pixelSize: 14
                    color: "#F1F5F9"
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                }
                
                onClicked: dialog.close()
            }
            
            Item { Layout.fillWidth: true }
            
            Button {
                id: createButton
                text: "创建"
                Layout.preferredWidth: 100
                enabled: nameField.text !== "" && descriptionField.text !== ""
                
                background: Rectangle {
                    radius: 6
                    color: enabled ? "#3B82F6" : "#334155"
                }
                
                contentItem: Text {
                    text: parent.text
                    font.pixelSize: 14
                    color: enabled ? "white" : "#94A3B8"
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                }
                
                onClicked: {
                    var parameters = collectParameters()
                    factorParameters = parameters
                    
                    console.log("创建因子:", {
                        name: factorName,
                        type: factorType,
                        description: factorDescription,
                        parameters: parameters
                    })
                    
                    // 发送信号
                    factorCreated(factorName, factorType, factorDescription, parameters)
                    
                    // 显示成功消息
                    showSuccessToast()
                    
                    // 关闭对话框
                    dialog.close()
                }
            }
        }
    }
    
    // ============ 参数组件 ============
    
    // 动量因子参数
    Component {
        id: momentumParameters
        
        ColumnLayout {
            spacing: 8
            
            Text {
                text: "动量因子参数"
                font.pixelSize: 12
                color: "#94A3B8"
            }
            
            // 窗口期
            RowLayout {
                spacing: 8
                
                Text {
                    text: "窗口期:"
                    font.pixelSize: 12
                    color: "#F1F5F9"
                    Layout.preferredWidth: 80
                }
                
                TextField {
                    id: momentumWindow
                    Layout.fillWidth: true
                    placeholderText: "20"
                    font.pixelSize: 12
                    color: "#F1F5F9"
                    
                    background: Rectangle {
                        radius: 4
                        color: "#0F172A"
                        border.width: 1
                        border.color: "#334155"
                    }
                }
            }
            
            // 计算方法
            RowLayout {
                spacing: 8
                
                Text {
                    text: "计算方法:"
                    font.pixelSize: 12
                    color: "#F1F5F9"
                    Layout.preferredWidth: 80
                }
                
                ComboBox {
                    id: momentumMethod
                    Layout.fillWidth: true
                    model: ["简单收益率", "对数收益率", "价格动量", "相对强弱"]
                    
                    background: Rectangle {
                        radius: 4
                        color: "#0F172A"
                        border.width: 1
                        border.color: "#334155"
                    }
                    
                    contentItem: Text {
                        text: parent.displayText
                        font.pixelSize: 12
                        color: "#F1F5F9"
                        horizontalAlignment: Text.AlignLeft
                        verticalAlignment: Text.AlignVCenter
                    }
                }
            }
        }
    }
    
    // 价值因子参数
    Component {
        id: valueParameters
        
        ColumnLayout {
            spacing: 8
            
            Text {
                text: "价值因子参数"
                font.pixelSize: 12
                color: "#94A3B8"
            }
            
            // 估值指标
            RowLayout {
                spacing: 8
                
                Text {
                    text: "估值指标:"
                    font.pixelSize: 12
                    color: "#F1F5F9"
                    Layout.preferredWidth: 80
                }
                
                ComboBox {
                    id: valueIndicator
                    Layout.fillWidth: true
                    model: ["市盈率(PE)", "市净率(PB)", "市销率(PS)", "企业价值倍数(EV/EBITDA)"]
                    
                    background: Rectangle {
                        radius: 4
                        color: "#0F172A"
                        border.width: 1
                        border.color: "#334155"
                    }
                    
                    contentItem: Text {
                        text: parent.displayText
                        font.pixelSize: 12
                        color: "#F1F5F9"
                        horizontalAlignment: Text.AlignLeft
                        verticalAlignment: Text.AlignVCenter
                    }
                }
            }
            
            // 行业中性化
            RowLayout {
                spacing: 8
                
                Text {
                    text: "行业中性化:"
                    font.pixelSize: 12
                    color: "#F1F5F9"
                    Layout.preferredWidth: 80
                }
                
                Switch {
                    id: industryNeutral
                    checked: true
                }
            }
        }
    }
    
    // 通用参数组件
    Component {
        id: genericParameters
        
        ColumnLayout {
            spacing: 8
            
            Text {
                text: "通用参数"
                font.pixelSize: 12
                color: "#94A3B8"
            }
            
            // 参数1
            RowLayout {
                spacing: 8
                
                Text {
                    text: "参数1:"
                    font.pixelSize: 12
                    color: "#F1F5F9"
                    Layout.preferredWidth: 80
                }
                
                TextField {
                    id: genericParam1
                    Layout.fillWidth: true
                    placeholderText: "值1"
                    font.pixelSize: 12
                    color: "#F1F5F9"
                    
                    background: Rectangle {
                        radius: 4
                        color: "#0F172A"
                        border.width: 1
                        border.color: "#334155"
                    }
                }
            }
            
            // 参数2
            RowLayout {
                spacing: 8
                
                Text {
                    text: "参数2:"
                    font.pixelSize: 12
                    color: "#F1F5F9"
                    Layout.preferredWidth: 80
                }
                
                TextField {
                    id: genericParam2
                    Layout.fillWidth: true
                    placeholderText: "值2"
                    font.pixelSize: 12
                    color: "#F1F5F9"
                    
                    background: Rectangle {
                        radius: 4
                        color: "#0F172A"
                        border.width: 1
                        border.color: "#334155"
                    }
                }
            }
        }
    }
    
    // 技术指标参数
    Component {
        id: technicalParameters
        
        ColumnLayout {
            spacing: 8
            
            Text {
                text: "技术指标参数"
                font.pixelSize: 12
                color: "#94A3B8"
            }
            
            // 指标类型
            RowLayout {
                spacing: 8
                
                Text {
                    text: "指标类型:"
                    font.pixelSize: 12
                    color: "#F1F5F9"
                    Layout.preferredWidth: 80
                }
                
                ComboBox {
                    id: technicalType
                    Layout.fillWidth: true
                    model: ["移动平均线", "RSI", "MACD", "布林带", "KDJ"]
                    
                    background: Rectangle {
                        radius: 4
                        color: "#0F172A"
                        border.width: 1
                        border.color: "#334155"
                    }
                    
                    contentItem: Text {
                        text: parent.displayText
                        font.pixelSize: 12
                        color: "#F1F5F9"
                        horizontalAlignment: Text.AlignLeft
                        verticalAlignment: Text.AlignVCenter
                    }
                }
            }
            
            // 周期
            RowLayout {
                spacing: 8
                
                Text {
                    text: "周期:"
                    font.pixelSize: 12
                    color: "#F1F5F9"
                    Layout.preferredWidth: 80
                }
                
                TextField {
                    id: technicalPeriod
                    Layout.fillWidth: true
                    placeholderText: "14"
                    font.pixelSize: 12
                    color: "#F1F5F9"
                    
                    background: Rectangle {
                        radius: 4
                        color: "#0F172A"
                        border.width: 1
                        border.color: "#334155"
                    }
                }
            }
        }
    }
    
    // 其他因子类型的参数组件（简略）
    Component {
        id: qualityParameters
        ColumnLayout {
            spacing: 8
            Text { text: "质量因子参数"; font.pixelSize: 12; color: "#94A3B8" }
        }
    }
    
    Component {
        id: sentimentParameters
        ColumnLayout {
            spacing: 8
            Text { text: "情绪因子参数"; font.pixelSize: 12; color: "#94A3B8" }
        }
    }
    
    Component {
        id: macroParameters
        ColumnLayout {
            spacing: 8
            Text { text: "宏观因子参数"; font.pixelSize: 12; color: "#94A3B8" }
        }
    }
    
    Component {
        id: riskParameters
        ColumnLayout {
            spacing: 8
            Text { text: "风险因子参数"; font.pixelSize: 12; color: "#94A3B8" }
        }
    }
    
    Component {
        id: customParameters
        ColumnLayout {
            spacing: 8
            Text { text: "自定义因子参数"; font.pixelSize: 12; color: "#94A3B8" }
        }
    }
    
    // ============ 辅助函数 ============
    
    // 收集参数
    function collectParameters() {
        var parameters = {}
        
        switch(factorType) {
            case "momentum":
                parameters.window = momentumWindow.text || "20"
                parameters.method = momentumMethod.currentText || "简单收益率"
                break
            case "value":
                parameters.indicator = valueIndicator.currentText || "市盈率(PE)"
                parameters.industryNeutral = industryNeutral.checked
                break
            case "technical":
                parameters.type = technicalType.currentText || "移动平均线"
                parameters.period = technicalPeriod.text || "14"
                break
            case "quality":
            case "sentiment":
            case "macro":
            case "risk":
            case "custom":
            default:
                parameters.param1 = genericParam1.text || ""
                parameters.param2 = genericParam2.text || ""
                break
        }
        
        return parameters
    }
    
    // 显示成功提示
    function showSuccessToast() {
        console.log("因子创建成功:", factorName)
        // 在实际应用中，这里可以显示一个toast提示
    }
    
    // 对话框打开时的初始化
    onOpened: {
        nameField.text = ""
        descriptionField.text = ""
        factorName = ""
        factorDescription = ""
        factorParameters = {}
    }
    
    // 快捷键支持
    Keys.onPressed: {
        if (event.key === Qt.Key_Escape) {
            dialog.close()
            event.accepted = true
        } else if (event.key === Qt.Key_Return && event.modifiers === Qt.ControlModifier) {
            if (createButton.enabled) {
                createButton.clicked()
                event.accepted = true
            }
        }
    }
    
    // 设置焦点
    Component.onCompleted: {
        nameField.forceActiveFocus()
    }
}