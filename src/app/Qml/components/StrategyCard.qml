// components/StrategyCard.qml
import QtQuick 2.15
import QtQuick.Layouts 1.15
import "../utils/Constants.js" as Constants

Rectangle {
    id: strategyCard
    implicitWidth: 300
    implicitHeight: 280
    radius: Constants.borderRadiusXLarge
    color: Constants.secondaryBg
    border.color: Constants.borderColor
    
    // 属性
    property string strategyName: ""
    property string description: ""
    property string status: "stopped" // running, paused, stopped
    property string returns: "+0.0%"
    property string maxDrawdown: "-0.0%"
    property string sharpeRatio: "0.0"
    property string winRate: "0.0%"
    property var tags: []
    
    // 信号
    signal editClicked()
    signal copyClicked()
    signal runClicked()
    signal cardClicked()
    
    ColumnLayout {
        anchors.fill: parent
        anchors.margins: Constants.spacingXLarge
        
        // 头部
        RowLayout {
            Text {
                text: strategyCard.strategyName
                font.pixelSize: Constants.fontSizeLarge
                font.weight: Font.DemiBold
                color: Constants.textPrimary
                Layout.fillWidth: true
                elide: Text.ElideRight
            }
            
            // 状态标签
            Rectangle {
                width: 60
                height: 24
                radius: Constants.borderRadiusLarge
                color: {
                    if (status === "running") return Qt.rgba(16/255, 185/255, 129/255, 0.15);
                    if (status === "paused") return Qt.rgba(245/255, 158/255, 11/255, 0.15);
                    return Qt.rgba(239/255, 68/255, 68/255, 0.15);
                }
                
                Text {
                    anchors.centerIn: parent
                    text: {
                        if (status === "running") return "运行中";
                        if (status === "paused") return "已暂停";
                        return "已停止";
                    }
                    font.pixelSize: Constants.fontSizeSmall
                    font.weight: Font.DemiBold
                    color: {
                        if (status === "running") return Constants.profitGreen;
                        if (status === "paused") return Constants.warningAmber;
                        return Constants.lossRed;
                    }
                }
            }
        }
        
        // 描述
        Text {
            text: strategyCard.description
            font.pixelSize: Constants.fontSizeNormal
            color: Constants.textSecondary
            wrapMode: Text.WordWrap
            Layout.fillWidth: true
            Layout.topMargin: Constants.spacingMedium
            Layout.bottomMargin: Constants.spacingLarge
            maximumLineCount: 2
            elide: Text.ElideRight
        }
        
        // 指标网格
        GridLayout {
            columns: 2
            columnSpacing: Constants.spacingMedium
            rowSpacing: Constants.spacingMedium
            
            MetricItem {
                label: "收益率"
                value: strategyCard.returns
                isPositive: strategyCard.returns.startsWith("+")
            }
            
            MetricItem {
                label: "最大回撤"
                value: strategyCard.maxDrawdown
                isPositive: false
            }
            
            MetricItem {
                label: "夏普比率"
                value: strategyCard.sharpeRatio
                isPositive: parseFloat(strategyCard.sharpeRatio) > 1.5
            }
            
            MetricItem {
                label: "胜率"
                value: strategyCard.winRate
                isPositive: parseFloat(strategyCard.winRate) > 50
            }
        }
        
        Item { Layout.fillHeight: true }
        
        // 底部操作区域
        RowLayout {
            // 标签
            Flow {
                spacing: Constants.spacingSmall
                Layout.fillWidth: true
                
                Repeater {
                    model: strategyCard.tags
                    
                    delegate: Rectangle {
                        height: 20
                        radius: Constants.borderRadiusSmall
                        color: Constants.tertiaryBg
                        
                        Text {
                            anchors.centerIn: parent
                            text: modelData
                            font.pixelSize: Constants.fontSizeSmall
                            color: Constants.textTertiary
                            leftPadding: Constants.spacingSmall
                            rightPadding: Constants.spacingSmall
                        }
                    }
                }
            }
            
            // 操作按钮
            Row {
                spacing: Constants.spacingSmall
                
                IconButton {
                    icon: "\uf044" // fa-edit
                    size: 32
                    onClicked: strategyCard.editClicked()
                }
                
                IconButton {
                    icon: "\uf0c5" // fa-copy
                    size: 32
                    onClicked: strategyCard.copyClicked()
                }
                
                IconButton {
                    icon: "\uf04b" // fa-play
                    size: 32
                    primary: true
                    onClicked: strategyCard.runClicked()
                }
            }
        }
    }
    
    // 卡片点击区域
    MouseArea {
        anchors.fill: parent
        cursorShape: Qt.PointingHandCursor
        hoverEnabled: true
        
        onEntered: {
            strategyCard.border.color = Constants.accentBlue;
            strategyCard.scale = 1.02;
        }
        
        onExited: {
            strategyCard.border.color = Constants.borderColor;
            strategyCard.scale = 1.0;
        }
        
        onClicked: strategyCard.cardClicked()
    }
}

// 指标项组件
Rectangle {
    id: metricItem
    implicitHeight: 40
    
    property string label: ""
    property string value: ""
    property bool isPositive: true
    
    Column {
        spacing: 4
        
        Text {
            text: metricItem.label
            font.pixelSize: Constants.fontSizeSmall
            color: Constants.textTertiary
        }
        
        Text {
            text: metricItem.value
            font.pixelSize: Constants.fontSizeMedium
            font.weight: Font.DemiBold
            color: metricItem.isPositive ? Constants.profitGreen : Constants.lossRed
        }
    }
}

// 图标按钮组件（简化版）
Rectangle {
    id: iconButton
    width: size
    height: size
    radius: Constants.borderRadiusMedium
    color: primary ? Constants.accentBlue : Constants.tertiaryBg
    
    property string icon: ""
    property int size: 32
    property bool primary: false
    
    signal clicked()
    
    Text {
        anchors.centerIn: parent
        text: iconButton.icon
        font.family: fontAwesome.name
        color: primary ? "white" : Constants.textSecondary
        font.pixelSize: 12
    }
    
    MouseArea {
        anchors.fill: parent
        cursorShape: Qt.PointingHandCursor
        hoverEnabled: true
        
        onEntered: {
            if (!primary) {
                parent.color = Constants.borderLight;
            } else {
                parent.color = Constants.accentBlueDark;
            }
        }
        
        onExited: {
            if (!primary) {
                parent.color = Constants.tertiaryBg;
            } else {
                parent.color = Constants.accentBlue;
            }
        }
        
        onClicked: {
            parent.clicked();
        }
    }
}