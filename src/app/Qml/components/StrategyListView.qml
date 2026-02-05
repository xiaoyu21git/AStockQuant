import QtQuick 2.15
import QtQuick.Layouts 1.15
import QtQuick.Controls 2.15
Rectangle {
    id: strategyListView
    color: "transparent"
    
    // 属性
    property var model
    property int selectedIndex: -1
    property string viewMode: "grid"
    
    // 信号
    signal strategySelected(int index, string strategyName)
    
    // 颜色常量
    readonly property color secondaryBg: "#1E293B"
    readonly property color accentBlue: "#3B82F6"
    readonly property color borderColor: "#475569"
    readonly property color textPrimary: "#F1F5F9"
    readonly property color textSecondary: "#94A3B8"
    readonly property color profitGreen: "#10B981"
    readonly property color lossRed: "#EF4444"
    readonly property color warningAmber: "#F59E0B"
    
    readonly property real borderRadiusXLarge: 16
    readonly property real spacingLarge: 16
    
    // 列表容器
    ScrollView {
        anchors.fill: parent
        clip: true
        
        GridLayout {
            id: strategyGrid
            width: parent.width
            columns: viewMode === "grid" ? 2 : 1
            columnSpacing: spacingLarge
            rowSpacing: spacingLarge
            
            // 策略卡片
            Repeater {
                model: strategyListView.model
                
                delegate: Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 280
                    radius: borderRadiusXLarge
                    color: secondaryBg
                    border.color: index === selectedIndex ? accentBlue : borderColor
                    border.width: 2
                    
                    ColumnLayout {
                        anchors.fill: parent
                        anchors.margins: 20
                        
                        // 头部
                        RowLayout {
                            Text {
                                text: model.name
                                font.pixelSize: 16
                                font.weight: Font.DemiBold
                                color: textPrimary
                                Layout.fillWidth: true
                                elide: Text.ElideRight
                            }
                            
                            // 状态标签
                            Rectangle {
                                width: 60
                                height: 24
                                radius: 8
                                color: {
                                    if (model.status === "running") return Qt.rgba(16/255, 185/255, 129/255, 0.15);
                                    if (model.status === "paused") return Qt.rgba(245/255, 158/255, 11/255, 0.15);
                                    return Qt.rgba(239/255, 68/255, 68/255, 0.15);
                                }
                                
                                Text {
                                    anchors.centerIn: parent
                                    text: {
                                        if (model.status === "running") return "运行中";
                                        if (model.status === "paused") return "已暂停";
                                        return "已停止";
                                    }
                                    font.pixelSize: 12
                                    font.weight: Font.DemiBold
                                    color: {
                                        if (model.status === "running") return profitGreen;
                                        if (model.status === "paused") return warningAmber;
                                        return lossRed;
                                    }
                                }
                            }
                        }
                        
                        // 描述
                        Text {
                            text: model.description
                            font.pixelSize: 14
                            color: textSecondary
                            wrapMode: Text.WordWrap
                            Layout.fillWidth: true
                            Layout.topMargin: 8
                            Layout.bottomMargin: 16
                            maximumLineCount: 2
                            elide: Text.ElideRight
                        }
                        
                        // 指标网格
                        GridLayout {
                            columns: 2
                            columnSpacing: 8
                            rowSpacing: 8
                            
                            // 收益率
                            Rectangle {
                                Layout.fillWidth: true
                                height: 60
                                radius: 6
                                color: Qt.rgba(255, 255, 255, 0.05)
                                
                                Column {
                                    anchors.centerIn: parent
                                    spacing: 2
                                    
                                    Text {
                                        text: "收益率"
                                        font.pixelSize: 12
                                        color: textSecondary
                                        anchors.horizontalCenter: parent.horizontalCenter
                                    }
                                    
                                    Text {
                                        text: model.returns
                                        font.pixelSize: 16
                                        font.bold: true
                                        color: model.returns.startsWith("+") ? profitGreen : lossRed
                                        anchors.horizontalCenter: parent.horizontalCenter
                                    }
                                }
                            }
                            
                            // 最大回撤
                            Rectangle {
                                Layout.fillWidth: true
                                height: 60
                                radius: 6
                                color: Qt.rgba(255, 255, 255, 0.05)
                                
                                Column {
                                    anchors.centerIn: parent
                                    spacing: 2
                                    
                                    Text {
                                        text: "最大回撤"
                                        font.pixelSize: 12
                                        color: textSecondary
                                        anchors.horizontalCenter: parent.horizontalCenter
                                    }
                                    
                                    Text {
                                        text: model.maxDrawdown
                                        font.pixelSize: 16
                                        font.bold: true
                                        color: lossRed
                                        anchors.horizontalCenter: parent.horizontalCenter
                                    }
                                }
                            }
                            
                            // 夏普比率
                            Rectangle {
                                Layout.fillWidth: true
                                height: 60
                                radius: 6
                                color: Qt.rgba(255, 255, 255, 0.05)
                                
                                Column {
                                    anchors.centerIn: parent
                                    spacing: 2
                                    
                                    Text {
                                        text: "夏普比率"
                                        font.pixelSize: 12
                                        color: textSecondary
                                        anchors.horizontalCenter: parent.horizontalCenter
                                    }
                                    
                                    Text {
                                        text: model.sharpeRatio
                                        font.pixelSize: 16
                                        font.bold: true
                                        color: parseFloat(model.sharpeRatio) > 1.5 ? profitGreen : textPrimary
                                        anchors.horizontalCenter: parent.horizontalCenter
                                    }
                                }
                            }
                            
                            // 胜率
                            Rectangle {
                                Layout.fillWidth: true
                                height: 60
                                radius: 6
                                color: Qt.rgba(255, 255, 255, 0.05)
                                
                                Column {
                                    anchors.centerIn: parent
                                    spacing: 2
                                    
                                    Text {
                                        text: "胜率"
                                        font.pixelSize: 12
                                        color: textSecondary
                                        anchors.horizontalCenter: parent.horizontalCenter
                                    }
                                    
                                    Text {
                                        text: model.winRate
                                        font.pixelSize: 16
                                        font.bold: true
                                        color: parseFloat(model.winRate) > 50 ? profitGreen : textPrimary
                                        anchors.horizontalCenter: parent.horizontalCenter
                                    }
                                }
                            }
                        }
                        
                        Item { Layout.fillHeight: true }
                        
                        // 底部标签
                        Flow {
                            spacing: 4
                            Layout.fillWidth: true
                            
                            Repeater {
                                model: model.tags
                                
                                delegate: Rectangle {
                                    height: 20
                                    radius: 4
                                    color: "#334155"
                                    
                                    Text {
                                        anchors.centerIn: parent
                                        text: modelData
                                        font.pixelSize: 12
                                        color: textSecondary
                                        leftPadding: 4
                                        rightPadding: 4
                                    }
                                }
                            }
                        }
                    }
                    
                    // 点击区域
                    MouseArea {
                        anchors.fill: parent
                        cursorShape: Qt.PointingHandCursor
                        hoverEnabled: true
                        
                        onEntered: {
                            if (index !== selectedIndex) {
                                parent.border.color = accentBlue;
                            }
                            parent.scale = 1.02;
                        }
                        
                        onExited: {
                            if (index !== selectedIndex) {
                                parent.border.color = borderColor;
                            }
                            parent.scale = 1.0;
                        }
                        
                        onClicked: {
                            strategyListView.strategySelected(index, model.name);
                        }
                    }
                }
            }
        }
    }
}