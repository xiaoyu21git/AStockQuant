// components/MetricItem.qml
import QtQuick 2.15
import ConsoleUi 1.0 as Constants

Item {
    id: metricItem
    implicitWidth: contentRow.implicitWidth + 20
    implicitHeight: contentColumn.implicitHeight + 12
    
    // 属性
    property string label: ""            // 标签文本（必填）
    property string value: ""            // 数值（必填）
    property string unit: ""             // 单位（可选）
    property string colorType: "default" // default, positive, negative, warning, accent, success, danger
    property int labelFontSize: Constants.fontSizeSmall
    property int valueFontSize: Constants.fontSizeMedium
    property bool showTrendIcon: false   // 是否显示趋势图标
    property string trend: "neutral"     // up, down, neutral
    property bool valueBold: true        // 数值是否加粗
    property bool compactMode: false     // 紧凑模式，减少间距
    
    // 背景相关属性
    property bool showBackground: false
    property color backgroundColor: Constants.surfaceLight
    property real backgroundOpacity: 0.8
    property real backgroundRadius: 4
    property color borderColor: "transparent"
    property real borderWidth: 0
    
    // 兼容旧代码的 isPositive 属性
    property bool isPositive: true
    
    // 内部颜色计算逻辑
    property color _valueColor: {
        // 优先使用 colorType（如果明确设置了的话）
        if (colorType !== "default") {
            switch(colorType) {
            case "positive": 
            case "success": 
                return Constants.profitGreen;
            case "negative": 
            case "danger": 
                return Constants.lossRed;
            case "warning": 
                return Constants.warningAmber;
            case "accent": 
                return Constants.accentBlue;
            default: 
                return Constants.textPrimary;
            }
        }
        // 如果 colorType 是 default，使用 isPositive
        return isPositive ? Constants.profitGreen : Constants.lossRed;
    }
    
    // 对外暴露的 valueColor 属性
    property alias valueColor: metricItem._valueColor
    
    // 计算属性 - 趋势图标颜色
    property color trendColor: {
        switch(trend) {
        case "up": return Constants.profitGreen;
        case "down": return Constants.lossRed;
        default: return Constants.textSecondary;
        }
    }
    
    // 背景矩形（可选）
    Rectangle {
        id: backgroundRect
        anchors.fill: parent
        color: backgroundColor
        opacity: backgroundOpacity
        radius: backgroundRadius
        border.color: borderColor
        border.width: borderWidth
        visible: showBackground
    }
    
    // 主要内容布局
    Column {
        id: contentColumn
        anchors.centerIn: parent
        spacing: compactMode ? 2 : 4
        
        // 标签行
        Row {
            id: labelRow
            spacing: 4
            
            Text {
                id: labelText
                text: metricItem.label
                font.pixelSize: metricItem.labelFontSize
                color: Constants.textTertiary
                opacity: 0.9
            }
            
            // 趋势图标（可选）
            Text {
                id: trendIcon
                visible: showTrendIcon && trend !== "neutral"
                text: {
                    if (trend === "up") return "↑";
                    if (trend === "down") return "↓";
                    return "";
                }
                font.pixelSize: metricItem.labelFontSize - 1
                font.bold: true
                color: trendColor
                anchors.verticalCenter: labelText.verticalCenter
            }
        }
        
        // 数值行
        Row {
            id: valueRow
            spacing: 2
            
            Text {
                id: valueText
                text: formatValue(metricItem.value)
                font.pixelSize: metricItem.valueFontSize
                font.weight: valueBold ? Font.DemiBold : Font.Normal
                color: _valueColor
            }
            
            // 单位（可选）
            Text {
                id: unitText
                visible: metricItem.unit !== ""
                text: metricItem.unit
                font.pixelSize: metricItem.valueFontSize - 2
                color: Constants.textSecondary
                anchors.baseline: valueText.baseline
            }
        }
    }
    
    // 格式化数值
    function formatValue(val) {
        // 如果值是百分比字符串（如"+12.4%"，"-8.2%"），提取数值部分
        var strVal = val.toString();
        
        // 移除百分比符号和正负号用于解析
        var cleanVal = strVal.replace(/[+\-%]/g, '');
        
        // 尝试解析为数字
        var num = parseFloat(cleanVal);
        if (isNaN(num)) {
            return strVal; // 如果不是数字，直接返回原始字符串
        }
        
        // 检查原始值是否有百分比符号
        var hasPercent = strVal.includes('%');
        
        // 根据数值大小进行格式化
        if (Math.abs(num) >= 1000) {
            return num.toLocaleString(Qt.locale(), 'f', 0); // 千位分隔符
        } else if (Math.abs(num) < 0.01 && num !== 0) {
            return num.toExponential(2); // 科学计数法
        } else {
            // 保留2位小数，但移除不必要的0
            var formatted = num.toFixed(2);
            return formatted.replace(/\.?0+$/, '');
        }
    }
    
    // 工具函数：根据数值自动判断颜色类型
    function autoColorType(val, isPercentage) {
        var strVal = val.toString();
        var cleanVal = strVal.replace(/[+\-%]/g, '');
        var num = parseFloat(cleanVal);
        
        if (isNaN(num)) return "default";
        
        if (isPercentage) {
            // 对于百分比，检查是否有负号
            if (strVal.startsWith('-')) return "negative";
            if (num > 0) return "positive";
            return "default";
        }
        
        // 对于绝对值判断
        if (num > 0) return "positive";
        if (num < 0) return "negative";
        return "default";
    }
    
    // 工具函数：自动判断趋势
    function autoTrend(currentVal, previousVal) {
        var currentStr = currentVal.toString();
        var previousStr = previousVal.toString();
        
        var currentClean = currentStr.replace(/[+\-%]/g, '');
        var previousClean = previousStr.replace(/[+\-%]/g, '');
        
        var current = parseFloat(currentClean);
        var previous = parseFloat(previousClean);
        
        if (isNaN(current) || isNaN(previous)) return "neutral";
        
        if (current > previous) return "up";
        if (current < previous) return "down";
        return "neutral";
    }
    
    // 兼容性：设置 colorType 时自动更新 isPositive
    onColorTypeChanged: {
        if (colorType === "positive" || colorType === "success") {
            isPositive = true;
        } else if (colorType === "negative" || colorType === "danger") {
            isPositive = false;
        }
    }
    
    // 兼容性：设置 isPositive 时自动更新 colorType
    onIsPositiveChanged: {
        if (colorType === "default") {
            // 如果当前是 default，根据 isPositive 设置颜色类型
            colorType = isPositive ? "positive" : "negative";
        }
    }
    
    // 提供便捷的设置背景的方法
    function setBackground(enabled, color, opacity, radius) {
        showBackground = enabled;
        if (color !== undefined) backgroundColor = color;
        if (opacity !== undefined) backgroundOpacity = opacity;
        if (radius !== undefined) backgroundRadius = radius;
    }
    
    // 提供便捷的清除背景的方法
    function clearBackground() {
        showBackground = false;
        backgroundColor = Constants.surfaceLight;
        backgroundOpacity = 0.8;
        backgroundRadius = 4;
    }
}