// FactorCard.qml
// 因子卡片组件 - 基于FactorCardEnhanced的原始实现
// 保留所有原有接口和功能，确保与因子库页面完全兼容
import QtQuick 2.15
import QtQuick.Layouts 1.15
import QtQuick.Controls 2.15
import Qt5Compat.GraphicalEffects 6.0
import "../utils/FactorDataAdapter.js" as FactorAdapter

/**
 * 因子卡片组件
 * 保持与FactorLibraryPage.qml完全兼容的原始实现
 * 不继承BaseQuantCard，保持原有的因子卡片功能
 */
Rectangle {
    id: factorCard
    
    // ============ 因子特有属性（原有接口） ============
    
    property string factorId: ""               // 因子ID
    property string factorName: ""             // 因子名称
    property string majorCategory: "动量类"     // 因子大类
    property string subCategory: "趋势动量"     // 因子子类
    
    // 因子特有性能指标
    property real icValue: 0.0                 // 信息系数
    property real irValue: 0.0                 // 信息比率
    property int validityDays: 20              // 有效期（天）
    property real turnoverRate: 32             // 换手率（%/年）
    
    // 图表数据
    property var groupReturns: []              // 分组收益数据
    
    // 显示控制
    property bool selected: false
    property bool showMiniChart: true
    property bool showGroupReturns: true
    property bool showActions: true
    
    // 元数据
    property string description: ""            // 描述
    property bool isFavorite: false           // 是否收藏
    property bool isRecommended: false        // 是否推荐
    property string status: "ACTIVE"          // 状态
    property var tags: []                     // 标签数组
    property string creator: ""               // 创建者
    property string createDate: ""            // 创建日期
    
    // ============ 信号 ============
    
    signal clicked()
    signal doubleClicked()
    signal favoriteToggled(bool favorite)
    signal previewRequested()
    signal analyzeRequested()
    signal addToPortfolio()
    signal editRequested()
    signal deleteRequested()
    
    // ============ 计算属性 ============
    
    // 获取类别颜色
    property color categoryColor: FactorAdapter.getFactorCategoryColor(majorCategory)
    
    // 卡片尺寸
    property int cardWidth: 190
    property int cardHeight: 260
    property int spacingSmall: 8
    property int spacingMedium: 12
    property int borderRadius: 10
    
    // ============ 视觉属性 ============
    
    implicitWidth: cardWidth
    implicitHeight: cardHeight
    radius: borderRadius
    color: "#1E293B"
    border.color: "#334155"
    border.width: 1
    
    layer.enabled: true
    layer.effect: DropShadow {
        horizontalOffset: 0
        verticalOffset: 2
        radius: 8
        color: Qt.rgba(0, 0, 0, 0.2)
        spread: 0.1
    }
    
    // ============ 主布局 ============
    
    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 20  // 从16改为20以增加控件与边框的距离
        spacing: 12
        
        // 标题行
        RowLayout {
            spacing: spacingSmall
            
            // 类别图标
            Rectangle {
                width: 32
                height: 32
                radius: 6
                color: Qt.rgba(categoryColor.r, categoryColor.g, categoryColor.b, 0.2)
                
                Text {
                    anchors.centerIn: parent
                    text: FactorAdapter.getCategoryIcon(majorCategory)
                    font.pixelSize: 14
                }
            }
            
            // 标题和元信息
            ColumnLayout {
                Layout.fillWidth: true
                spacing: 2
                
                // 标题行
                RowLayout {
                    spacing: 4
                    
                    Text {
                        text: factorName || "未命名"
                        font.pixelSize: 16
                        font.weight: Font.DemiBold
                        color: "#F1F5F9"
                        elide: Text.ElideRight
                        Layout.fillWidth: true
                    }
                    
                    // 收藏按钮
                    Text {
                        text: isFavorite ? "⭐" : "☆"
                        font.pixelSize: 14
                        color: isFavorite ? "#F59E0B" : "#94A3B8"
                        
                        MouseArea {
                            anchors.fill: parent
                            cursorShape: Qt.PointingHandCursor
                            onClicked: favoriteToggled(!isFavorite)
                        }
                    }
                }
                
                // 类别和创建信息
                RowLayout {
                    spacing: 6
                    
                    Text {
                        text: majorCategory + (subCategory ? " · " + subCategory : "")
                        font.pixelSize: 12
                        color: categoryColor
                    }
                    
                    Item { Layout.fillWidth: true }
                    
                // 创建信息
                Text {
                    text: creator + (createDate ? " · " + createDate : "")
                    font.pixelSize: 11
                    color: "#FFFFFF"
                    visible: creator || createDate
                }
                }
            }
        }
        
        // 描述区域
        Text {
            Layout.fillWidth: true
            Layout.preferredHeight: 32
            text: description || "暂无描述"
            font.pixelSize: 13
            color: "#FFFFFF"
            wrapMode: Text.WordWrap
            maximumLineCount: 2
            elide: Text.ElideRight
        }
        
        // 性能指标区域
        GridLayout {
            Layout.fillWidth: true
            columns: 2
            rowSpacing: 4
            columnSpacing: 8
            
            // IC指标
            MetricItem {
                label: "IC"
                value: icValue
                format: "%.3f"
                metricColor: categoryColor
            }
            
            // IR指标
            MetricItem {
                label: "IR"
                value: irValue
                format: "%.2f"
                metricColor: categoryColor
            }
            
            // 换手率
            MetricItem {
                label: "换手率"
                value: turnoverRate
                format: "%.0f"
                unit: "%/年"
                metricColor: categoryColor
            }
            
            // 有效期
            MetricItem {
                label: "有效期"
                value: validityDays
                format: "%d"
                unit: "天"
                metricColor: categoryColor
            }
        }
        
        // 迷你图表区域
        Item {
            Layout.fillWidth: true
            Layout.preferredHeight: showMiniChart && groupReturns && groupReturns.length > 0 ? 32 : 0  // 减小高度，与BaseQuantCard保持一致
            visible: showMiniChart && groupReturns && groupReturns.length > 0
            
            // 简化图表
            Rectangle {
                anchors.fill: parent
                radius: 6
                color: "#334155"
            }
        }
        
        // 标签和操作按钮区域
        ColumnLayout {
            spacing: spacingSmall
            
            // 标签区域
            Flow {
                Layout.fillWidth: true
                spacing: 4
                
                Repeater {
                    model: tags && tags.length > 0 ? tags : ["量化"]
                    
                    delegate: Rectangle {
                        height: 20
                        radius: 10
                        color: Qt.rgba(categoryColor.r, categoryColor.g, categoryColor.b, 0.2)
                        
                        Text {
                            anchors.centerIn: parent
                            text: modelData
                            font.pixelSize: 10
                            font.weight: Font.Medium
                            color: categoryColor
                            leftPadding: 8
                            rightPadding: 8
                        }
                    }
                }
                
                // 推荐标签
                Rectangle {
                    visible: isRecommended
                    height: 20
                    radius: 10
                    color: Qt.rgba(0.063, 0.725, 0.506, 0.2)  // #10B981 with alpha
                    
                    Row {
                        spacing: 4
                        anchors.centerIn: parent
                        
                        Text {
                            text: "🔥"
                            font.pixelSize: 10
                            color: "#10B981"
                        }
                        
                        Text {
                            text: "推荐"
                            font.pixelSize: 10
                            font.weight: Font.Medium
                            color: "#10B981"
                            rightPadding: 8
                        }
                    }
                }
            }
            
            // 操作按钮区域
            Item {
                Layout.fillWidth: true
                Layout.preferredHeight: showActions ? 32 : 0
                visible: showActions
                
                Row {
                    anchors.centerIn: parent
                    spacing: 4
                    
                    // 预览按钮
                    ActionButton {
                        icon: "👁️"
                        tooltip: "预览因子"
                        buttonColor: categoryColor
                        onClicked: previewRequested()
                    }
                    
                    // 分析按钮
                    ActionButton {
                        icon: "📊"
                        tooltip: "详细分析"
                        buttonColor: categoryColor
                        onClicked: analyzeRequested()
                    }
                    
                    // 添加到组合按钮
                    ActionButton {
                        icon: "➕"
                        tooltip: "添加到组合"
                        buttonColor: "#10B981"
                        onClicked: addToPortfolio()
                    }
                    
                    // 编辑按钮
                    ActionButton {
                        icon: "✏️"
                        tooltip: "编辑因子"
                        buttonColor: "#F59E0B"
                        onClicked: editRequested()
                    }
                    
                    // 删除按钮
                    ActionButton {
                        icon: "🗑️"
                        tooltip: "删除因子"
                        buttonColor: "#EF4444"
                        onClicked: deleteRequested()
                    }
                }
            }
        }
    }
    
    // ============ 自定义组件 ============
    
    // 性能指标组件
    component MetricItem: Rectangle {
        property string label: ""
        property real value: 0
        property string format: "%.2f"
        property string unit: ""
        property color metricColor: "#3B82F6"  // 重命名以避免冲突
        
        implicitWidth: metricContent.width + 12
        implicitHeight: 28
        radius: 6
        color: Qt.rgba(metricColor.r, metricColor.g, metricColor.b, 0.1)
        
        Row {
            id: metricContent
            anchors.centerIn: parent
            spacing: 4
            
            Text {
                text: label + ":"
                font.pixelSize: 12
                color: "#FFFFFF"
            }
            
            Text {
                text: {
                    var formatted = format.replace("%d", Math.round(value)).replace("%.2f", value.toFixed(2)).replace("%.3f", value.toFixed(3))
                    return formatted + (unit ? " " + unit : "")
                }
                font.pixelSize: 13
                font.weight: Font.DemiBold
                color: metricColor
            }
        }
    }
    
    // 操作按钮组件
    component ActionButton: Rectangle {
        property string icon: ""
        property string tooltip: ""
        property color buttonColor: "#3B82F6"
        signal clicked()
        
        width: 28
        height: 28
        radius: 6
        color: Qt.rgba(buttonColor.r, buttonColor.g, buttonColor.b, 0.2)
        
        Text {
            anchors.centerIn: parent
            text: icon
            font.pixelSize: 12
        }
        
        MouseArea {
            anchors.fill: parent
            cursorShape: Qt.PointingHandCursor
            
            onClicked: {
                parent.clicked()
            }
        }
    }
    
    // ============ 鼠标交互 ============
    // 因子卡片不响应点击，只响应操作按钮
    
    MouseArea {
        anchors.fill: parent
        hoverEnabled: true
        cursorShape: Qt.ArrowCursor  // 使用箭头光标，表示不可点击
        acceptedButtons: Qt.NoButton  // 不接受任何点击
        
        // 悬停效果
        onEntered: {
            hovered = true
        }
        
        onExited: {
            hovered = false
        }
    }
    
    // 添加悬停状态属性
    property bool hovered: false
    
    // 根据悬停状态更新卡片颜色
    onHoveredChanged: {
        // 这里可以添加悬停时的视觉反馈，但不改变交互行为
    }
    
    // ============ 初始化 ============
    
    Component.onCompleted: {
        // 确保属性正确初始化
        if (typeof groupReturns === 'undefined' || groupReturns === null) {
            groupReturns = []
        }
    }
}
