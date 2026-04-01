// StrategyTypeSelector.qml
// 策略类型选择组件 - 用于策略创建向导步骤1

import QtQuick 2.15
import QtQuick.Layouts 1.15
import QtQuick.Controls 2.15
import "../../../utils/StrategyCreationUtils.js" as Utils

Rectangle {
    id: root
    
    // ============ 属性 ============
    
    property string selectedStrategyType: ""
    property string selectedStrategyName: Utils.StrategyCreationUtils.getStrategyTypeName(selectedStrategyType)
    
    // 信号
    signal strategyTypeChanged(string strategyType)
    
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
                text: Utils.StrategyCreationUtils.tr('strategyCreation.selectStrategyType')
                font.pixelSize: 16
                font.weight: Font.Medium
                color: "#f1f5f9"
            }
            
            // 类型选择列表 - 使用紧凑卡片
            ScrollView {
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.minimumHeight: 200
                Layout.preferredHeight: 300
                Layout.maximumHeight: 350
                clip: true
                ScrollBar.vertical.policy: ScrollBar.AlwaysOff
                ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
                
                ColumnLayout {
                    id: strategyListColumn
                    width: parent.width
                    spacing: 2
                    
                    // 策略类型卡片组件
                    Component {
                        id: strategyTypeCard
                        
                        Rectangle {
                            id: cardRoot
                            property string typeId: ""
                            property string displayName: ""
                            property string description: ""
                            property bool isSelected: root.selectedStrategyType === typeId
                            
                            width: parent.width
                            height: 48
                            radius: 6
                            color: isSelected ? "#1e40af" : "#1e293b"
                            border.width: isSelected ? 2 : 1
                            border.color: isSelected ? "#3b82f6" : "#475569"
                            
                            RowLayout {
                                anchors.fill: parent
                                anchors.margins: 10
                                spacing: 12
                                
                                // 左侧图标
                                Rectangle {
                                    Layout.preferredWidth: 28
                                    Layout.preferredHeight: 28
                                    radius: 6
                                    color: isSelected ? "#3b82f6" : "#334155"
                                    border.width: 1
                                    border.color: isSelected ? "#60a5fa" : "#475569"
                                    
                                    Text {
                                        anchors.centerIn: parent
                                        text: Utils.StrategyCreationUtils.getStrategyIcon(cardRoot.typeId)
                                        font.pixelSize: 14
                                        color: isSelected ? "white" : "#cbd5e1"
                                    }
                                }
                                
                                // 策略名称和简要描述
                                ColumnLayout {
                                    Layout.fillWidth: true
                                    Layout.fillHeight: true
                                    spacing: 2
                                    
                                    Text {
                                        text: cardRoot.displayName
                                        font.pixelSize: 13
                                        font.weight: isSelected ? Font.DemiBold : Font.Medium
                                        color: isSelected ? "white" : "#f1f5f9"
                                        elide: Text.ElideRight
                                    }
                                    
                                    Text {
                                        text: Utils.StrategyCreationUtils.getBriefDescription(cardRoot.typeId)
                                        font.pixelSize: 10
                                        color: isSelected ? "#dbeafe" : "#94a3b8"
                                        elide: Text.ElideRight
                                        maximumLineCount: 1
                                    }
                                }
                                
                                // 选中指示器
                                Rectangle {
                                    visible: isSelected
                                    Layout.preferredWidth: 12
                                    Layout.preferredHeight: 12
                                    radius: 6
                                    color: "#10b981"
                                    border.width: 2
                                    border.color: "white"
                                }
                            }
                        }
                    }
                    
                    // 趋势跟踪策略
                    Loader {
                        id: trendCard
                        sourceComponent: strategyTypeCard
                        Layout.fillWidth: true
                        Layout.preferredHeight: 48
                        onLoaded: {
                            item.typeId = "trend_following"
                            item.displayName = Utils.StrategyCreationUtils.getStrategyTypeName("trend_following")
                            item.description = Utils.StrategyCreationUtils.getBriefDescription("trend_following")
                        }
                        
                        MouseArea {
                            anchors.fill: parent
                            cursorShape: Qt.PointingHandCursor
                            onClicked: {
                                if (root.selectedStrategyType === "trend_following") {
                                    // 如果已经选中，取消选择
                                    root.selectedStrategyType = ""
                                } else {
                                    // 否则选择该类型
                                    root.selectedStrategyType = "trend_following"
                                }
                                root.strategyTypeChanged(root.selectedStrategyType)
                            }
                        }
                    }
                    
                    // 均值回归策略
                    Loader {
                        id: meanReversionCard
                        sourceComponent: strategyTypeCard
                        Layout.fillWidth: true
                        Layout.preferredHeight: 48
                        onLoaded: {
                            item.typeId = "mean_reversion"
                            item.displayName = Utils.StrategyCreationUtils.getStrategyTypeName("mean_reversion")
                            item.description = Utils.StrategyCreationUtils.getBriefDescription("mean_reversion")
                        }
                        
                        MouseArea {
                            anchors.fill: parent
                            cursorShape: Qt.PointingHandCursor
                            onClicked: {
                                if (root.selectedStrategyType === "mean_reversion") {
                                    root.selectedStrategyType = ""
                                } else {
                                    root.selectedStrategyType = "mean_reversion"
                                }
                                root.strategyTypeChanged(root.selectedStrategyType)
                            }
                        }
                    }
                    
                    // 动量策略
                    Loader {
                        id: momentumCard
                        sourceComponent: strategyTypeCard
                        Layout.fillWidth: true
                        Layout.preferredHeight: 48
                        onLoaded: {
                            item.typeId = "momentum"
                            item.displayName = Utils.StrategyCreationUtils.getStrategyTypeName("momentum")
                            item.description = Utils.StrategyCreationUtils.getBriefDescription("momentum")
                        }
                        
                        MouseArea {
                            anchors.fill: parent
                            cursorShape: Qt.PointingHandCursor
                            onClicked: {
                                if (root.selectedStrategyType === "momentum") {
                                    root.selectedStrategyType = ""
                                } else {
                                    root.selectedStrategyType = "momentum"
                                }
                                root.strategyTypeChanged(root.selectedStrategyType)
                            }
                        }
                    }
                    
                    // 套利策略
                    Loader {
                        id: arbitrageCard
                        sourceComponent: strategyTypeCard
                        Layout.fillWidth: true
                        Layout.preferredHeight: 48
                        onLoaded: {
                            item.typeId = "arbitrage"
                            item.displayName = Utils.StrategyCreationUtils.getStrategyTypeName("arbitrage")
                            item.description = Utils.StrategyCreationUtils.getBriefDescription("arbitrage")
                        }
                        
                        MouseArea {
                            anchors.fill: parent
                            cursorShape: Qt.PointingHandCursor
                            onClicked: {
                                if (root.selectedStrategyType === "arbitrage") {
                                    root.selectedStrategyType = ""
                                } else {
                                    root.selectedStrategyType = "arbitrage"
                                }
                                root.strategyTypeChanged(root.selectedStrategyType)
                            }
                        }
                    }
                    
                    // 机器学习策略
                    Loader {
                        id: mlCard
                        sourceComponent: strategyTypeCard
                        Layout.fillWidth: true
                        Layout.preferredHeight: 48
                        onLoaded: {
                            item.typeId = "machine_learning"
                            item.displayName = Utils.StrategyCreationUtils.getStrategyTypeName("machine_learning")
                            item.description = Utils.StrategyCreationUtils.getBriefDescription("machine_learning")
                        }
                        
                        MouseArea {
                            anchors.fill: parent
                            cursorShape: Qt.PointingHandCursor
                            onClicked: {
                                if (root.selectedStrategyType === "machine_learning") {
                                    root.selectedStrategyType = ""
                                } else {
                                    root.selectedStrategyType = "machine_learning"
                                }
                                root.strategyTypeChanged(root.selectedStrategyType)
                            }
                        }
                    }
                    
                    // 多因子策略
                    Loader {
                        id: multiFactorCard
                        sourceComponent: strategyTypeCard
                        Layout.fillWidth: true
                        Layout.preferredHeight: 48
                        onLoaded: {
                            item.typeId = "multi_factor"
                            item.displayName = Utils.StrategyCreationUtils.getStrategyTypeName("multi_factor")
                            item.description = Utils.StrategyCreationUtils.getBriefDescription("multi_factor")
                        }
                        
                        MouseArea {
                            anchors.fill: parent
                            cursorShape: Qt.PointingHandCursor
                            onClicked: {
                                if (root.selectedStrategyType === "multi_factor") {
                                    root.selectedStrategyType = ""
                                } else {
                                    root.selectedStrategyType = "multi_factor"
                                }
                                root.strategyTypeChanged(root.selectedStrategyType)
                            }
                        }
                    }
                    
                    // 高频策略
                    Loader {
                        id: hfCard
                        sourceComponent: strategyTypeCard
                        Layout.fillWidth: true
                        Layout.preferredHeight: 48
                        onLoaded: {
                            item.typeId = "high_frequency"
                            item.displayName = Utils.StrategyCreationUtils.getStrategyTypeName("high_frequency")
                            item.description = Utils.StrategyCreationUtils.getBriefDescription("high_frequency")
                        }
                        
                        MouseArea {
                            anchors.fill: parent
                            cursorShape: Qt.PointingHandCursor
                            onClicked: {
                                if (root.selectedStrategyType === "high_frequency") {
                                    root.selectedStrategyType = ""
                                } else {
                                    root.selectedStrategyType = "high_frequency"
                                }
                                root.strategyTypeChanged(root.selectedStrategyType)
                            }
                        }
                    }
                    
                    // 事件驱动策略
                    Loader {
                        id: eventCard
                        sourceComponent: strategyTypeCard
                        Layout.fillWidth: true
                        Layout.preferredHeight: 48
                        onLoaded: {
                            item.typeId = "event_driven"
                            item.displayName = Utils.StrategyCreationUtils.getStrategyTypeName("event_driven")
                            item.description = Utils.StrategyCreationUtils.getBriefDescription("event_driven")
                        }
                        
                        MouseArea {
                            anchors.fill: parent
                            cursorShape: Qt.PointingHandCursor
                            onClicked: {
                                if (root.selectedStrategyType === "event_driven") {
                                    root.selectedStrategyType = ""
                                } else {
                                    root.selectedStrategyType = "event_driven"
                                }
                                root.strategyTypeChanged(root.selectedStrategyType)
                            }
                        }
                    }
                    
                    // 自定义策略
                    Loader {
                        id: customCard
                        sourceComponent: strategyTypeCard
                        Layout.fillWidth: true
                        Layout.preferredHeight: 48
                        onLoaded: {
                            item.typeId = "custom"
                            item.displayName = Utils.StrategyCreationUtils.getStrategyTypeName("custom")
                            item.description = Utils.StrategyCreationUtils.getBriefDescription("custom")
                        }
                        
                        MouseArea {
                            anchors.fill: parent
                            cursorShape: Qt.PointingHandCursor
                            onClicked: {
                                if (root.selectedStrategyType === "custom") {
                                    root.selectedStrategyType = ""
                                } else {
                                    root.selectedStrategyType = "custom"
                                }
                                root.strategyTypeChanged(root.selectedStrategyType)
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
                color: "#0f172a"
                border.width: 1
                border.color: "#334155"
                
                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 12
                    spacing: 6
                    
                    Text {
                        id: strategyTypeDesc
                        text: Utils.StrategyCreationUtils.getStrategyTypeDescription(root.selectedStrategyType)
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
        root.selectedStrategyType = "trend_following"
    }

    function setSelectedStrategyType(strategyType, emitSignal) {
        root.selectedStrategyType = strategyType || "trend_following"
        if (emitSignal === undefined || emitSignal) {
            root.strategyTypeChanged(root.selectedStrategyType)
        }
    }
    
    // 验证
    function isValid() {
        return root.selectedStrategyType !== ""
    }
}