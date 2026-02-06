// StatsCard.qml - 统计卡片组件（已添加点击事件）
import QtQuick 2.15
import QtQuick.Controls 2.15
import ConsoleUi 1.0 as Theme

Rectangle {
    id: statsCard
    width: parent.width 
    height: 150
    radius: 10
    color: Theme.darkCard
    border.color: Theme.darkBorder
    border.width: 1
    
    property string iconSource: "qrc:/icons/filter.svg"
    property string iconColor: Theme.primaryColor
    property string label: "统计标签"
    property string value: "0"
    property string trendText: "趋势文本"
    property bool trendUp: true
    
    // 点击信号
    signal cardClicked()
    
    // 鼠标交互区域 - 在这里添加点击事件
    MouseArea {
        anchors.fill: parent
        hoverEnabled: true
        cursorShape: Qt.PointingHandCursor  // 添加手型光标
        
        onEntered: {
            statsCard.scale = 1.05
            statsCard.z = 1
        }
        
        onExited: {
            statsCard.scale = 1.0
            statsCard.z = 0
        }
        
        // 添加点击事件 - 触发cardClicked信号
        onClicked: {
            console.log("StatsCard点击: " + statsCard.label)
            statsCard.cardClicked()  // 触发点击信号
        }
        
        // 添加按下效果
        onPressed: {
            statsCard.opacity = 0.8
        }
        
        onReleased: {
            statsCard.opacity = 1.0
        }
        
        // 添加右键菜单支持（可选）
        onPressAndHold: {
            console.log("长按StatsCard: " + statsCard.label)
        }
    }
    
    // 计算带透明度的颜色
    function getColorWithOpacity(baseColor, opacityPercent) {
        var color = Qt.color(baseColor)
        color.a = opacityPercent / 100.0
        return color
    }
    
    // 内容布局
    Column {
        anchors.fill: parent
        anchors.margins: 24
        spacing: 12
        
        // 头部
        Row {
            width: parent.width
            spacing: 10
            
            Text {
                text: statsCard.label
                font.pixelSize: 14
                color: "#aaa"
                anchors.verticalCenter: parent.verticalCenter
            }
            
            // 图标
            Rectangle {
                width: 50
                height: 50
                radius: 10
                color: getColorWithOpacity(statsCard.iconColor, 20)  // 20%透明度
                anchors.verticalCenter: parent.verticalCenter
                
                Image {
                    source: statsCard.iconSource
                    width: 24
                    height: 24
                    anchors.centerIn: parent
                    //color: statsCard.iconColor
                }
            }
        }
        
        // 数值
        Text {
            text: statsCard.value
            font.pixelSize: 44
            font.bold: true
            color: Theme.darkText
        }
        
        // 趋势
        Row {
            spacing: 5
            
            Image {
                source: statsCard.trendUp ? "qrc:/icons/arrow-up.svg" : "qrc:/icons/arrow-down.svg"
                width: 16
                height: 16
                anchors.verticalCenter: parent.verticalCenter
            }
            
            Text {
                text: statsCard.trendText
                font.pixelSize: 14
                font.weight: Font.Medium
                color: statsCard.trendUp ? Theme.successColor : Theme.dangerColor
                anchors.verticalCenter: parent.verticalCenter
            }
        }
    }
}