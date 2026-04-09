// FactorListRow.qml
// 因子列表行组件，用于列表视图
import QtQuick 2.15
import QtQuick.Layouts 1.15
import QtQuick.Controls 2.15

/**
 * 因子列表行组件
 * 用于列表视图显示，简洁高效
 */
Rectangle {
    id: root
    
    // ============ 公共属性 ============
    
    // 基本数据
    property string factorId: ""
    property string displayName: ""
    property string majorCategory: "动量类"
    
    // 性能指标
    property real icValue: 0.0
    property real irValue: 0.0
    property real turnoverRate: 32  // %/年
    property bool isFavorite: false
    property string status: "ACTIVE"
    
    // 交互状态
    property bool hovered: false
    property bool selected: false
    property bool showActions: true
    
    // 信号
    signal clicked()
    signal doubleClicked()
    signal favoriteToggled(bool favorite)
    signal analyzeRequested()
    signal addToPortfolio()
    
    // ============ 私有属性 ============
    
    readonly property color categoryColor: getFactorColor(majorCategory)
    readonly property color statusColor: getStatusColor(status)
    readonly property int actionAreaWidth: showActions ? 224 : 0
    
    // ============ 视觉属性 ============
    
    implicitWidth: 800
    implicitHeight: 80
    radius: 8
    color: {
        if (selected) return Qt.rgba(categoryColor.r, categoryColor.g, categoryColor.b, 0.1)
        if (hovered) return "#334155"
        return "#1E293B"
    }
    border.color: selected ? categoryColor : "#475569"
    border.width: selected ? 2 : 1
    
    // ============ 主布局 ============
    
    RowLayout {
        anchors.fill: parent
        anchors.margins: 16
        spacing: 14
        
        // 类别标识
        Rectangle {
            Layout.preferredWidth: 4
            Layout.fillHeight: true
            color: categoryColor
            radius: 2
        }
        
        // 因子名称和类别
        ColumnLayout {
            Layout.preferredWidth: 220
            Layout.minimumWidth: 180
            Layout.maximumWidth: 260
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 4
            
            Text {
                text: displayName
                font.pixelSize: 16
                font.weight: Font.DemiBold
                color: "#F1F5F9"
                Layout.fillWidth: true
                elide: Text.ElideRight
            }
            
            Text {
                text: majorCategory
                font.pixelSize: 12
                color: categoryColor
            }
        }
        
        // 性能指标
        RowLayout {
            Layout.fillWidth: true
            spacing: 20
            
            // IC值
            Column {
                spacing: 2
                Layout.preferredWidth: 52
                
                Text {
                    text: "IC"
                    font.pixelSize: 11
                    color: "#94A3B8"
                }
                
                Text {
                    text: icValue.toFixed(3)
                    font.pixelSize: 16
                    font.weight: Font.DemiBold
                    color: categoryColor
                }
            }
            
            // IR值
            Column {
                spacing: 2
                Layout.preferredWidth: 52
                
                Text {
                    text: "IR"
                    font.pixelSize: 11
                    color: "#94A3B8"
                }
                
                Text {
                    text: irValue.toFixed(2)
                    font.pixelSize: 16
                    font.weight: Font.DemiBold
                    color: categoryColor
                }
            }
            
            // 换手率
            Column {
                spacing: 2
                Layout.preferredWidth: 82
                
                Text {
                    text: "换手率"
                    font.pixelSize: 11
                    color: "#94A3B8"
                }
                
                Text {
                    text: turnoverRate.toFixed(0) + "%/年"
                    font.pixelSize: 16
                    font.weight: Font.DemiBold
                    color: categoryColor
                }
            }
        }
        
        // 状态和操作
        RowLayout {
            id: actionBar
            spacing: 12
            Layout.preferredWidth: actionAreaWidth
            Layout.minimumWidth: actionAreaWidth
            Layout.maximumWidth: actionAreaWidth
            
            // 状态徽章
            Rectangle {
                width: statusText.width + 12
                height: 24
                radius: 12
                color: Qt.rgba(statusColor.r, statusColor.g, statusColor.b, 0.2)
                
                Text {
                    id: statusText
                    anchors.centerIn: parent
                    text: getStatusText(status)
                    font.pixelSize: 11
                    font.weight: Font.Medium
                    color: statusColor
                }
            }
            
            // 收藏按钮
            Text {
                visible: showActions
                text: isFavorite ? "⭐" : "☆"
                font.pixelSize: 16
                color: isFavorite ? "#F59E0B" : "#64748B"
                
                MouseArea {
                    anchors.fill: parent
                    cursorShape: Qt.PointingHandCursor
                    onClicked: favoriteToggled(!isFavorite)
                }
            }
            
            // 分析按钮
            Rectangle {
                visible: showActions
                width: 64
                height: 32
                radius: 6
                color: "#3B82F6"
                
                Text {
                    anchors.centerIn: parent
                    text: "分析"
                    font.pixelSize: 12
                    color: "white"
                }
                
                MouseArea {
                    anchors.fill: parent
                    cursorShape: Qt.PointingHandCursor
                    onClicked: analyzeRequested()
                }
            }
            
            // 构建因子组合按钮
            Rectangle {
                visible: showActions
                width: 110
                height: 32
                radius: 6
                color: "#10B981"
                
                Row {
                    anchors.centerIn: parent
                    spacing: 4
                    
                    Text {
                        text: "➕"
                        font.pixelSize: 12
                        color: "white"
                    }
                    
                    Text {
                        text: "加入组合"
                        font.pixelSize: 12
                        color: "white"
                    }
                }
                
                MouseArea {
                    anchors.fill: parent
                    cursorShape: Qt.PointingHandCursor
                    onClicked: addToPortfolio()
                }
            }
        }
    }
    
    // ============ 工具函数 ============
    
    // 根据因子大类获取颜色
    function getFactorColor(majorCategory) {
        switch (majorCategory) {
            case "动量类": return "#3B82F6";
            case "价值类": return "#F59E0B";
            case "质量类": return "#10B981";
            case "成长类": return "#8B5CF6";
            case "情绪类": return "#EC4899";
            default: return "#94A3B8";
        }
    }
    
    // 根据状态获取颜色
    function getStatusColor(status) {
        switch (status.toUpperCase()) {
            case "ACTIVE": return "#10B981";
            case "EXPERIMENTAL": return "#F59E0B";
            case "DEPRECATED": return "#EF4444";
            default: return "#64748B";
        }
    }
    
    // 根据状态获取文本
    function getStatusText(status) {
        switch (status.toUpperCase()) {
            case "ACTIVE": return "活跃";
            case "EXPERIMENTAL": return "实验";
            case "DEPRECATED": return "废弃";
            default: return "未知";
        }
    }
    
    // ============ 鼠标交互 ============
    
    MouseArea {
        id: hoverTracker
        anchors.fill: parent
        hoverEnabled: true
        acceptedButtons: Qt.NoButton
        
        onEntered: hovered = true
        onExited: hovered = false
    }

    MouseArea {
        anchors.fill: parent
        anchors.rightMargin: actionBar.visible ? (actionBar.width + 16) : 0
        cursorShape: Qt.PointingHandCursor

        onClicked: root.clicked()
        onDoubleClicked: root.doubleClicked()
    }
    
    // ============ 状态动画 ============
    
    states: [
        State {
            name: "hovered"
            when: hovered
            PropertyChanges { target: root; scale: 1.01 }
        }
    ]
    
    transitions: Transition {
        NumberAnimation {
            properties: "scale"
            duration: 150
            easing.type: Easing.OutQuad
        }
    }
}