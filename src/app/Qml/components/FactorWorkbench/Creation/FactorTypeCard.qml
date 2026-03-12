// FactorTypeCard.qml
// 因子类型卡片组件 - 缩小版本，移除选择按钮
import QtQuick 2.15
import QtQuick.Layouts 1.15

/**
 * 因子类型卡片组件
 * 用于显示和选择因子类型
 */
Rectangle {
    id: root
    
    // ============ 属性 ============
    
    property string typeId: ""
    property string typeName: ""
    property string description: ""
    property string icon: ""
    property color cardColor: "#3B82F6"
    property bool selected: false
    
    // ============ 信号 ============
    
    signal clicked()
    
    // ============ 外观 ============
    
    Layout.fillWidth: true
    Layout.fillHeight: true
    radius: 10  // 缩小圆角
    border.width: selected ? 2 : 0  // 选中时显示边框，未选中时不显示
    border.color: selected ? cardColor : "transparent"
    
    // 背景颜色 - 简化：选中时显示颜色，未选中时透明
    color: selected ? Qt.rgba(cardColor.r, cardColor.g, cardColor.b, 0.15) : "#0F172A"
    
    // ============ 布局 ============
    
    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 12  // 减少边距
        spacing: 6  // 减少间距
        
        // 图标和标题
        Row {
            spacing: 10  // 减少间距
            
            // 图标背景 - 缩小
            Rectangle {
                width: 32  // 缩小
                height: 32  // 缩小
                radius: 6  // 缩小
                color: Qt.rgba(cardColor.r, cardColor.g, cardColor.b, 0.2)
                
                Text {
                    anchors.centerIn: parent
                    text: root.icon
                    font.pixelSize: 14  // 缩小
                    color: cardColor
                }
            }
            
            // 标题
            Text {
                text: root.typeName
                font.pixelSize: 14  // 缩小
                font.weight: Font.DemiBold
                color: selected ? "#F1F5F9" : "#F1F5F9"
            }
        }
        
        // 描述
        Text {
            Layout.fillWidth: true
            text: root.description
            font.pixelSize: 11  // 缩小
            color: selected ? "#E2E8F0" : "#94A3B8"
            wrapMode: Text.WordWrap
            maximumLineCount: 2
            elide: Text.ElideRight
        }
        
        Item { Layout.fillHeight: true }
        
        // 移除选择按钮，只通过颜色和边框显示选中状态
    }
    
    // ============ 交互 ============
    
    MouseArea {
        anchors.fill: parent
        cursorShape: Qt.PointingHandCursor
        onClicked: root.clicked()
        hoverEnabled: true
        
        onEntered: {
            if (!selected) {
                root.border.width = 1
                root.border.color = Qt.rgba(cardColor.r, cardColor.g, cardColor.b, 0.3)
                root.color = Qt.rgba(cardColor.r, cardColor.g, cardColor.b, 0.05)
            }
        }
        
        onExited: {
            if (!selected) {
                root.border.width = 0
                root.border.color = "transparent"
                root.color = "#0F172A"
            }
        }
    }
}
