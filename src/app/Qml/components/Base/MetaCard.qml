// MetaCard.qml - 通用卡片组件
// 配置驱动，支持多种字段类型和操作
import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15

/**
 * 通用卡片组件
 * 完全由配置驱动，用于展示单个数据项
 * 支持：文本、数字、徽章、标签等字段类型
 */
Rectangle {
    id: root
    
    // ============ 输入属性 ============
    
    // 数据对象
    property var data: ({})
    // 字段配置数组
    property var fields: []
    // 卡片标题
    property string title: ""
    // 标题图标
    property string titleIcon: ""
    // 卡片副标题
    property string subtitle: ""
    // 操作按钮
    property var actions: []
    // 是否显示边框
    property bool showBorder: true
    // 是否可点击
    property bool clickable: true
    // 是否选中
    property bool selected: false
    // 卡片高度
    property int cardHeight: 180
    
    // ============ 输出信号 ============
    
    // 卡片点击
    signal clicked()
    // 卡片双击
    signal doubleClicked()
    // 操作按钮触发
    signal actionTriggered(string actionName, var cardData)
    
    // ============ 内部状态 ============
    
    property bool hovered: false
    
    // 可见字段（过滤掉不可见字段）
    property var visibleFields: fields ? fields.filter(function(f) { 
        return f.visible !== false 
    }) : []
    
    // ============ 视觉样式 ============
    
    implicitHeight: cardHeight
    radius: 8  // borderRadiusLg
    color: {
        if (selected) return Qt.rgba(0x18/255.0, 0x90/255.0, 0xFF/255.0, 0.1)
        if (hovered) return "#F5F5F5"
        return "#FAFAFA"
    }
    border.color: selected ? "#1890FF" : "#D9D9D9"
    border.width: showBorder ? (selected ? 2 : 1) : 0
    
    // 阴影效果
    layer.enabled: true
    layer.effect: DropShadow {
        horizontalOffset: 0
        verticalOffset: 1
        radius: 3
        color: Qt.rgba(0, 0, 0, 0.1)
        spread: 0.2
    }
    
    // ============ 主布局 ============
    
    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 16  // spacing4
        spacing: 12  // spacing3
        
        // 标题行
        RowLayout {
            Layout.fillWidth: true
            spacing: 8  // spacing2
            
            // 标题图标
            Rectangle {
                width: 32
                height: 32
                radius: 6  // borderRadiusMd
                color: Qt.rgba(0x18/255.0, 0x90/255.0, 0xFF/255.0, 0.2)
                visible: titleIcon !== ""
                
                Text {
                    anchors.centerIn: parent
                    text: titleIcon
                    font.pixelSize: 14  // fontSizeMd
                    color: "#1890FF"  // factorMomentum
                }
            }
            
            // 标题文本
            ColumnLayout {
                Layout.fillWidth: true
                spacing: 4  // spacing1
                
                Text {
                    text: title
                    font.pixelSize: 16  // fontSizeLg
                    font.weight: Font.DemiBold
                    color: "#262626"  // textPrimary
                    elide: Text.ElideRight
                }
                
                Text {
                    text: subtitle
                    font.pixelSize: 12  // fontSizeSm
                    color: "#666666"  // textSecondary
                    visible: subtitle !== ""
                    elide: Text.ElideRight
                }
            }
            
            // 操作按钮
            Row {
                spacing: 4  // spacing1
                visible: actions.length > 0
                
                Repeater {
                    model: actions
                    
                    delegate: Rectangle {
                        width: 32
                        height: 32
                        radius: 6  // borderRadiusMd
                        color: "transparent"
                        border.color: "#D9D9D9"  // borderDefault
                        border.width: 1
                        
                        Text {
                            anchors.centerIn: parent
                            text: modelData.icon || ""
                            font.pixelSize: 14  // fontSizeMd
                            color: "#666666"  // textSecondary
                        }
                        
                        MouseArea {
                            anchors.fill: parent
                            cursorShape: Qt.PointingHandCursor
                            
                            onClicked: {
                                root.actionTriggered(modelData.name, root.data)
                            }
                        }
                    }
                }
            }
        }
        
        // 字段内容区域
        GridLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            columns: 2
            columnSpacing: 16  // spacing4
            rowSpacing: 8  // spacing2
            
            Repeater {
                model: visibleFields
                
                // 字段标签
                delegate: Text {
                    text: modelData.label || modelData.name + ":"
                    font.pixelSize: 12
                    color: "#666666"
                    horizontalAlignment: Text.AlignRight
                    Layout.alignment: Qt.AlignRight | Qt.AlignTop
                }
            }
            
            Repeater {
                model: visibleFields
                
                // 字段值
                delegate: Loader {
                    Layout.fillWidth: true
                    
                    sourceComponent: getFieldRenderer(modelData.type)
                    
                    property var field: modelData
                    property var value: root.data[field.name]
                }
            }
        }
    }
    
    // ============ 字段渲染器组件 ============
    
    // 文本渲染
    Component {
        id: textRenderer
        
        Text {
            text: value || "-"
            font.pixelSize: 12
            color: "#262626"
            wrapMode: Text.WordWrap
            maximumLineCount: 2
            elide: Text.ElideRight
        }
    }
    
    // 数字渲染
    Component {
        id: numberRenderer
        
        Text {
            text: {
                if (value === undefined || value === null) return "-"
                if (field.format) {
                    return field.format.arg(value)
                }
                if (field.unit) {
                    return value.toFixed(field.precision || 2) + field.unit
                }
                return value.toFixed(field.precision || 2)
            }
            font.pixelSize: 12
            color: field.valueMap && field.valueMap[value] ? 
                   field.valueMap[value].color : "#262626"
            font.bold: field.valueMap && field.valueMap[value]
        }
    }
    
    // 徽章渲染
    Component {
        id: badgeRenderer
        
        Rectangle {
            height: 24
            width: Math.min(badgeText.width + 16, 120)
            radius: 12
            color: {
                if (field.valueMap && field.valueMap[value]) {
                    return field.valueMap[value].color + "20"
                }
                return "#F0F0F0"
            }
            
            Text {
                id: badgeText
                anchors.centerIn: parent
                text: field.valueMap && field.valueMap[value] ? 
                      field.valueMap[value].label : (value || "-")
                color: field.valueMap && field.valueMap[value] ? 
                       field.valueMap[value].color : "#666666"
                font.pixelSize: 12
            }
        }
    }
    
    // 标签渲染
    Component {
        id: tagsRenderer
        
        Flow {
            spacing: 4
            
            Repeater {
                model: Array.isArray(value) ? value.slice(0, 3) : [] // 限制最多显示3个标签
                
                delegate: Rectangle {
                    height: 20
                    width: tagText.width + 12
                    radius: 10
                    color: "#F0F0F0"
                    
                    Text {
                        id: tagText
                        anchors.centerIn: parent
                        text: modelData
                        font.pixelSize: 10
                        color: "#666666"
                    }
                }
            }
            
            // 显示更多标签提示
            Text {
                text: Array.isArray(value) && value.length > 3 ? "..." : ""
                font.pixelSize: 12
                color: "#999999"
                visible: Array.isArray(value) && value.length > 3
            }
        }
    }
    
    // 布尔值渲染
    Component {
        id: booleanRenderer
        
        Text {
            text: value ? "✓" : "✗"
            font.pixelSize: 14
            color: value ? "#52C41A" : "#F5222D"
        }
    }
    
    // 进度条渲染
    Component {
        id: progressRenderer
        
        ColumnLayout {
            spacing: 4
            
            Rectangle {
                height: 6
                width: parent.width
                radius: 3
                color: "#F0F0F0"
                
                Rectangle {
                    width: parent.width * Math.min(Math.max(value || 0, 0), 1)
                    height: parent.height
                    radius: parent.radius
                    color: "#1890FF"
                }
            }
            
            Text {
                text: Math.round((value || 0) * 100) + "%"
                font.pixelSize: 10
                color: "#666666"
                horizontalAlignment: Text.AlignHCenter
                Layout.fillWidth: true
            }
        }
    }
    
    // 获取字段渲染器
    function getFieldRenderer(type) {
        switch(type) {
            case "text": return textRenderer
            case "number": return numberRenderer
            case "badge": return badgeRenderer
            case "tags": return tagsRenderer
            case "boolean": return booleanRenderer
            case "progress": return progressRenderer
            default: return textRenderer
        }
    }
    
    // ============ 鼠标交互 ============
    
    MouseArea {
        anchors.fill: parent
        hoverEnabled: true
        cursorShape: root.clickable ? Qt.PointingHandCursor : Qt.ArrowCursor
        enabled: root.clickable
        
        onEntered: hovered = true
        onExited: hovered = false
        onClicked: root.clicked()
        onDoubleClicked: root.doubleClicked()
    }
    
    // ============ 状态动画 ============
    
    states: [
        State {
            name: "hovered"
            when: hovered && clickable
            PropertyChanges { target: root; scale: 1.02 }
        },
        State {
            name: "selected"
            when: selected
            PropertyChanges { target: root; border.width: 2 }
        }
    ]
    
    transitions: Transition {
        NumberAnimation {
            properties: "scale, border.width"
            duration: 200
            easing.type: Easing.OutCubic
        }
    }
}