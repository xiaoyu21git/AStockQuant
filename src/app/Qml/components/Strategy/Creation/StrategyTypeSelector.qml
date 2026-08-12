import AStock.Bridge 1.0 as Bridge
// StrategyTypeSelector.qml
// 策略类型选择组件 - 用于策略创建向导步骤1

import QtQuick 2.15
import QtQuick.Layouts 1.15
import QtQuick.Controls 2.15


Rectangle {
    id: root
    
    // ============ 属性 ============
    
    // 策略类型一律使用 QML 契约枚举值 (Bridge.StrategyTypes.StrategyType.*), 禁止硬编码数字
    property int selectedStrategyType: Bridge.StrategyTypes.StrategyType.DoubleMovingAverage
    readonly property int selectedStrategyBehaviorKind: Bridge.StrategyBridge.strategyBehaviorKindOfType(selectedStrategyType)
    readonly property int compactSelectorColumns: width >= 280 ? 2 : 1
    readonly property int strategyCardHeight: compactSelectorColumns > 1 ? 44 : 50

    // 信号
    signal strategyTypeChanged(int strategyType)
    
    // ============ 主布局 ============
    
    color: "transparent"
    
    ColumnLayout {
        anchors.fill: parent
        spacing: 12
        
        // 左侧：策略类型选择（1/4宽度）
        ColumnLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.preferredWidth: parent.width * 0.25
            Layout.minimumWidth: 180
            spacing: 12
            
            Text {
                text: Bridge.StrategyBridge.tr('strategyCreation.selectStrategyType')
                font.pixelSize: 16
                font.weight: Font.Medium
                color: "#f1f5f9"
            }
            
            // 类型选择列表 - 使用紧凑卡片
            ScrollView {
                id: strategyScrollView
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.minimumHeight: 200
                Layout.preferredHeight: root.compactSelectorColumns > 1 ? 236 : 360
                Layout.maximumHeight: root.compactSelectorColumns > 1 ? 260 : 420
                clip: true
                ScrollBar.vertical.policy: ScrollBar.AlwaysOff
                ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
                
                GridLayout {
                    id: strategyListColumn
                    width: strategyScrollView.availableWidth
                    columns: root.compactSelectorColumns
                    columnSpacing: 8
                    rowSpacing: 6
                    
                    // 策略类型卡片组件
                    Component {
                        id: strategyTypeCard

                        Rectangle {
                            id: cardRoot
                            property int strategyType: -1
                            property bool isSelected: root.selectedStrategyType === strategyType
                            
                            Layout.fillWidth: true
                            height: root.strategyCardHeight
                            radius: 6
                            color: isSelected ? "#1e40af" : "#1e293b"
                            border.width: isSelected ? 2 : 1
                            border.color: isSelected ? "#3b82f6" : "#475569"
                            
                            RowLayout {
                                anchors.fill: parent
                                anchors.margins: root.compactSelectorColumns > 1 ? 8 : 10
                                spacing: root.compactSelectorColumns > 1 ? 8 : 10
                                
                                // 左侧图标
                                Rectangle {
                                    Layout.preferredWidth: root.compactSelectorColumns > 1 ? 24 : 28
                                    Layout.preferredHeight: root.compactSelectorColumns > 1 ? 24 : 28
                                    radius: 6
                                    color: isSelected ? "#3b82f6" : "#334155"
                                    border.width: 1
                                    border.color: isSelected ? "#60a5fa" : "#475569"
                                    
                                    Text {
                                        anchors.centerIn: parent
                                        text: Bridge.StrategyBridge.strategyTypeIcon(cardRoot.strategyType)
                                        font.pixelSize: root.compactSelectorColumns > 1 ? 12 : 14
                                        color: isSelected ? "white" : "#cbd5e1"
                                    }
                                }
                                
                                // 策略名称和简要描述
                                ColumnLayout {
                                    Layout.fillWidth: true
                                    Layout.fillHeight: true
                                    spacing: root.compactSelectorColumns > 1 ? 0 : 2
                                    
                                    Text {
                                        text: Bridge.StrategyBridge.strategyTypeName(cardRoot.strategyType)
                                        font.pixelSize: root.compactSelectorColumns > 1 ? 12 : 13
                                        font.weight: isSelected ? Font.DemiBold : Font.Medium
                                        color: isSelected ? "white" : "#f1f5f9"
                                        elide: Text.ElideRight
                                        maximumLineCount: 1
                                    }
                                    
                                    Text {
                                        text: Bridge.StrategyBridge.strategyTypeBrief(cardRoot.strategyType)
                                        font.pixelSize: 10
                                        color: isSelected ? "#dbeafe" : "#94a3b8"
                                        elide: Text.ElideRight
                                        maximumLineCount: 1
                                        visible: root.compactSelectorColumns === 1
                                    }
                                }
                                
                                // 选中指示器
                                Rectangle {
                                    visible: isSelected
                                    Layout.preferredWidth: 10
                                    Layout.preferredHeight: 10
                                    radius: 5
                                    color: "#10b981"
                                    border.width: 2
                                    border.color: "white"
                                }
                            }
                        }
                    }

                    Repeater {
                        // 模型由 C++ strategyTypeList() 提供: [{type, id, name, icon, brief}] × 11
                        model: Bridge.StrategyBridge.strategyTypeList()

                        delegate: Loader {
                            required property var modelData

                            readonly property int strategyType: Number(modelData.type)

                            sourceComponent: strategyTypeCard
                            Layout.fillWidth: true
                            Layout.preferredHeight: root.strategyCardHeight

                            onLoaded: {
                                item.strategyType = strategyType
                            }

                            MouseArea {
                                anchors.fill: parent
                                cursorShape: Qt.PointingHandCursor

                                onClicked: {
                                    root.toggleStrategyType(parent.strategyType)
                                }
                            }
                        }
                    }
                }
            }
            
            // 策略类型描述
            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: 100
                Layout.minimumHeight: 80
                radius: 8
                color: "#1e293b"
                border.width: 1
                border.color: "#334155"
                
                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 12
                    spacing: 6
                    
                    Text {
                        id: strategyTypeDesc
                        text: Bridge.StrategyBridge.strategyTypeBrief(root.selectedStrategyType)
                        font.pixelSize: 12
                        color: "#94a3b8"
                        wrapMode: Text.WordWrap
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                    }
                }
            }
        }
    }
    
    // ============ 功能函数 ============
    
    // 重置选择
    function reset() {
        root.selectedStrategyType = Bridge.StrategyTypes.StrategyType.DoubleMovingAverage
    }

    function toggleStrategyType(strategyType) {
        root.selectedStrategyType = root.selectedStrategyType === strategyType ? -1 : strategyType
        root.strategyTypeChanged(root.selectedStrategyType)
    }

    function setSelectedStrategyType(strategyType, emitSignal) {
        root.selectedStrategyType = strategyType
        if (emitSignal === undefined || emitSignal) {
            root.strategyTypeChanged(root.selectedStrategyType)
        }
    }

    // 验证
    function isValid() {
        return root.selectedStrategyType >= 0
    }
}