import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Rectangle {
    id: root
    
    // ========== 公开属性 ==========
    property string title: ""              // 卡片标题
    property string value: "0"             // 主数值
    property string subValue: ""           // 副数值（可选）
    property string icon: ""               // 图标（Emoji 或 Unicode）
    property string iconFont: "Segoe UI Emoji" // 图标字体
    property real iconSize: 24             // 图标大小
    
    // 颜色主题
    property color primaryColor: "#3498db" // 主色
    property color textColor: "white"      // 文字颜色
    
    // 趋势指示
    property real trendValue: 0.0          // 趋势值（正=上升，负=下降）
    property bool showTrend: trendValue !== 0.0 // 是否显示趋势
    
    // 样式
    property int radius: 12                // 圆角半径
    property bool showBorder: false        // 是否显示边框
    property bool showShadow: false        // 是否显示阴影（默认关闭，避免兼容性问题）
    property bool hoverEffect: true        // 是否启用悬停效果
    property bool clickable: false         // 是否可点击
    
    // 动画
    property bool animateValue: false      // 是否启用数值动画
    property int animationDuration: 800    // 动画持续时间
    
    // ========== 信号 ==========
    signal clicked()                       // 点击信号
    signal hovered()                       // 悬停信号
    signal pressed()                       // 按下信号
    signal released()                      // 释放信号
    
    // ========== 私有属性 ==========
    property real _animatedValue: 0
    property color _trendColor: trendValue > 0 ? "#2ecc71" : trendValue < 0 ? "#e74c3c" : "#95a5a6"
    property string _trendIcon: trendValue > 0 ? "↗" : trendValue < 0 ? "↘" : "→"
    property string _trendText: trendValue > 0 ? "+" + trendValue.toFixed(1) + "%" : 
                         trendValue < 0 ? trendValue.toFixed(1) + "%" : ""
    
    // ========== 外观 ==========
    width: 200
    height: 120
    // radius: root.radius
    // color: root.primaryColor
    
    // 边框
    border.width: showBorder ? 1 : 0
    border.color: Qt.darker(primaryColor, 1.3)
    
    // ========== 内容布局 ==========
    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 16
        spacing: 8
        
        // 第一行：图标和标题
        RowLayout {
            spacing: 12
            
            // 图标容器（带背景圆）
            Rectangle {
                width: 40
                height: 40
                radius: 20
                color: Qt.rgba(1, 1, 1, 0.2)  // 半透明白色背景
                
                // 图标
                Text {
                    anchors.centerIn: parent
                    text: root.icon
                    font.family: root.iconFont
                    font.pixelSize: root.iconSize
                    color: root.textColor
                }
            }
            
            // 标题区域
            ColumnLayout {
                spacing: 2
                Layout.fillWidth: true
                
                // 主标题
                Text {
                    text: root.title
                    color: root.textColor
                    font.pixelSize: 14
                    font.weight: Font.Medium
                    elide: Text.ElideRight
                    Layout.fillWidth: true
                }
                
                // 趋势指示器（可选）
                RowLayout {
                    visible: showTrend
                    spacing: 4
                    Layout.topMargin: 2
                    
                    // 趋势图标
                    Text {
                        text: _trendIcon
                        color: _trendColor
                        font.pixelSize: 12
                        font.bold: true
                    }
                    
                    // 趋势文本
                    Text {
                        text: _trendText
                        color: _trendColor
                        font.pixelSize: 11
                        font.bold: true
                    }
                    
                    Item { Layout.fillWidth: true } // 占位
                }
            }
        }
        
        // 第二行：主数值
        RowLayout {
            spacing: 4
            
            // 动画数值
            Text {
                id: valueText
                text: animateValue ? _animatedValue.toFixed(0) : root.value
                color: root.textColor
                font.pixelSize: 32
                font.weight: Font.Bold
                font.family: "Segoe UI, Arial"
            }
            
            // 副数值（可选）
            Text {
                visible: root.subValue !== ""
                text: root.subValue
                color: Qt.rgba(1, 1, 1, 0.8)
                font.pixelSize: 14
                font.weight: Font.Normal
                Layout.alignment: Qt.AlignBottom
                Layout.bottomMargin: 4
            }
            
            Item { Layout.fillWidth: true } // 占位
        }
        
        // 第三行：进度条（可选，用于展示比例）
        Rectangle {
            visible: root.trendValue !== 0
            Layout.fillWidth: true
            height: 3
            radius: 1.5
            color: Qt.rgba(1, 1, 1, 0.2)
            
            // 进度填充
            Rectangle {
                width: parent.width * Math.min(Math.abs(root.trendValue) / 100, 1)
                height: parent.height
                radius: parent.radius
                color: _trendColor
                
                Behavior on width {
                    NumberAnimation { duration: 300; easing.type: Easing.OutCubic }
                }
            }
        }
        
        Item { Layout.fillHeight: true } // 弹性填充
    }
    
    // ========== 数值动画 ==========
    NumberAnimation on _animatedValue {
        id: valueAnimation
        running: animateValue && root.visible
        from: 0
        to: parseFloat(root.value) || 0
        duration: animationDuration
        easing.type: Easing.OutCubic
    }
    
    // ========== 交互效果 ==========
    MouseArea {
        id: mouseArea
        anchors.fill: parent
        enabled: clickable || hoverEffect
        hoverEnabled: hoverEffect
        cursorShape: clickable ? Qt.PointingHandCursor : Qt.ArrowCursor
        
        // 点击事件
        onClicked: {
            if (clickable) {
                root.clicked()
            }
        }
        
        onPressed: {
            if (clickable) {
                root.pressed()
                pressAnimation.start()
            }
        }
        
        onReleased: {
            if (clickable) {
                root.released()
                releaseAnimation.start()
            }
        }
        
        // 悬停效果
        onEntered: {
            if (hoverEffect) {
                hoverAnimation.start()
                root.hovered()
            }
        }
        
        onExited: {
            if (hoverEffect) {
                exitAnimation.start()
            }
        }
    }
    
    // ========== 动画定义 ==========
    // 按下动画
    PropertyAnimation {
        id: pressAnimation
        target: root
        property: "scale"
        to: 0.98
        duration: 100
        easing.type: Easing.OutCubic
    }
    
    // 释放动画
    PropertyAnimation {
        id: releaseAnimation
        target: root
        property: "scale"
        to: 1.0
        duration: 200
        easing.type: Easing.OutBack
    }
    
    // 悬停动画
    ParallelAnimation {
        id: hoverAnimation
        PropertyAnimation {
            target: root
            property: "scale"
            to: 1.02
            duration: 200
            easing.type: Easing.OutCubic
        }
    }
    
    // 退出动画
    ParallelAnimation {
        id: exitAnimation
        PropertyAnimation {
            target: root
            property: "scale"
            to: 1.0
            duration: 200
            easing.type: Easing.OutCubic
        }
    }
    
    // ========== 工具函数 ==========
    // 设置数值（带动画）
    function setValue(newValue, animate) {
        if (animate !== undefined) {
            animateValue = animate
        }
        
        if (animateValue) {
            valueAnimation.stop()
            valueAnimation.from = _animatedValue
            valueAnimation.to = parseFloat(newValue) || 0
            valueAnimation.start()
        } else {
            value = newValue
        }
    }
    
    // 设置趋势值
    function setTrend(newTrend) {
        trendValue = newTrend
    }
    
    // 高亮卡片
    function highlight(duration) {
        var originalColor = primaryColor
        primaryColor = Qt.lighter(originalColor, 1.3)
        
        highlightTimer.interval = duration || 500
        highlightTimer.start()
    }
    
    Timer {
        id: highlightTimer
        onTriggered: primaryColor = Qt.darker(primaryColor, 1.3)
    }
    
    // ========== 初始化 ==========
    Component.onCompleted: {
        if (animateValue) {
            _animatedValue = 0
        }
    }
}