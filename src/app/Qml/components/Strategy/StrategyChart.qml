import QtQuick 2.15
import QtQuick.Layouts 1.15
import QtQuick.Controls 2.15

Rectangle {
    id: strategyChart
    radius: 16  // borderRadiusXLarge
    color: "#1E293B"  // secondaryBg
    border.color: "#475569"  // borderColor
    
    // 颜色常量
    readonly property color textPrimary: "#F1F5F9"
    readonly property color textSecondary: "#94A3B8"
    readonly property color accentBlue: "#3B82F6"
    readonly property color accentGreen: "#10B981"
    readonly property color accentRed: "#EF4444"
    readonly property color accentYellow: "#F59E0B"
    readonly property color tertiaryBg: "#334155"
    readonly property color gridColor: "#475569"
    readonly property color axisColor: "#64748B"
    
    readonly property int fontSizeSmall: 12
    readonly property int fontSizeMedium: 16
    
    readonly property real spacingMedium: 8
    readonly property real spacingLarge: 16
    
    readonly property real borderRadiusMedium: 8
    
    // 属性
    property var timeRanges: ["1天", "1周", "1月", "3月", "6月", "1年", "全部"]
    property int selectedTimeRange: 5  // 默认选择"1年"
    
    property var indicators: [
        { name: "累计收益率", value: "+15.8%", color: accentGreen },
        { name: "年化收益", value: "+18.2%", color: accentGreen },
        { name: "最大回撤", value: "-5.3%", color: accentRed },
        { name: "夏普比率", value: "2.1", color: accentBlue },
        { name: "胜率", value: "68%", color: accentGreen }
    ]
    
    // 基准净值属性
    property bool showBenchmark: true
    property string benchmarkName: "沪深300"
    property real benchmarkStartValue: 100
    property real benchmarkPerformance: 8.5  // 基准收益率百分比
    
    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 20
        spacing: spacingMedium
        
        // 标题栏
        RowLayout {
            Layout.fillWidth: true
            
            Text {
                text: "策略表现"
                font.pixelSize: fontSizeMedium
                font.weight: Font.DemiBold
                color: textPrimary
            }
            
            Item { Layout.fillWidth: true }
            
            // 基准切换按钮
            Rectangle {
                width: 120
                height: 28
                radius: borderRadiusMedium
                color: showBenchmark ? tertiaryBg : tertiaryBg
                border.color: showBenchmark ? accentYellow : tertiaryBg
                border.width: 1
                
                Row {
                    anchors.centerIn: parent
                    spacing: spacingMedium
                    
                    Rectangle {
                        width: 12
                        height: 2
                        color: accentYellow
                        anchors.verticalCenter: parent.verticalCenter
                    }
                    
                    Text {
                        text: benchmarkName
                        font.pixelSize: fontSizeSmall
                        color: showBenchmark ? accentYellow : textSecondary
                        anchors.verticalCenter: parent.verticalCenter
                    }
                    
                    // 切换按钮
                    Rectangle {
                        width: 20
                        height: 12
                        radius: 6
                        color: showBenchmark ? accentYellow : "#64748B"
                        anchors.verticalCenter: parent.verticalCenter
                        
                        Rectangle {
                            width: 8
                            height: 8
                            radius: 4
                            color: "white"
                            anchors.verticalCenter: parent.verticalCenter
                            x: showBenchmark ? parent.width - width - 2 : 2
                            
                            Behavior on x {
                                NumberAnimation { duration: 200 }
                            }
                        }
                        
                        MouseArea {
                            anchors.fill: parent
                            cursorShape: Qt.PointingHandCursor
                            onClicked: {
                                showBenchmark = !showBenchmark;
                                updateChart();
                            }
                        }
                    }
                }
            }
            
            // 时间范围选择器
            Row {
                spacing: spacingMedium
                
                Repeater {
                    model: timeRanges.length
                    
                    Rectangle {
                        width: 48
                        height: 28
                        radius: borderRadiusMedium
                        color: selectedTimeRange === index ? accentBlue : tertiaryBg
                        
                        Text {
                            anchors.centerIn: parent
                            text: timeRanges[index]
                            font.pixelSize: fontSizeSmall
                            color: selectedTimeRange === index ? "white" : textSecondary
                            font.weight: selectedTimeRange === index ? Font.Medium : Font.Normal
                        }
                        
                        MouseArea {
                            anchors.fill: parent
                            cursorShape: Qt.PointingHandCursor
                            onClicked: {
                                selectedTimeRange = index;
                                updateChart();
                            }
                        }
                    }
                }
            }
        }
        
        // 指标列表 - 添加基准对比
        Row {
            Layout.fillWidth: true
            Layout.topMargin: spacingMedium
            spacing: spacingLarge
            
            Repeater {
                model: indicators
                
                Column {
                    spacing: 2
                    
                    Text {
                        text: modelData.name
                        font.pixelSize: fontSizeSmall
                        color: textSecondary
                    }
                    
                    Text {
                        text: modelData.value
                        font.pixelSize: 14
                        font.weight: Font.Medium
                        color: modelData.color
                    }
                }
            }
            
            // 基准收益指标
            Column {
                spacing: 2
                
                Text {
                    text: "基准收益"
                    font.pixelSize: fontSizeSmall
                    color: textSecondary
                }
                
                Text {
                    text: (benchmarkPerformance > 0 ? "+" : "") + benchmarkPerformance.toFixed(1) + "%"
                    font.pixelSize: 14
                    font.weight: Font.Medium
                    color: benchmarkPerformance > 0 ? accentYellow : accentRed
                }
            }
            
            // 超额收益
            Column {
                spacing: 2
                
                Text {
                    text: "超额收益"
                    font.pixelSize: fontSizeSmall
                    color: textSecondary
                }
                
                Text {
                    text: {
                        var excessReturn = 15.8 - benchmarkPerformance;
                        return (excessReturn > 0 ? "+" : "") + excessReturn.toFixed(1) + "%";
                    }
                    font.pixelSize: 14
                    font.weight: Font.Medium
                    color: accentGreen
                }
            }
        }
        
        // 图表区域
        Rectangle {
            id: chartContainer
            Layout.fillWidth: true
            Layout.fillHeight: true
            color: "transparent"
            
            // 背景网格
            Canvas {
                id: gridCanvas
                anchors.fill: parent
                
                onPaint: {
                    var ctx = getContext("2d");
                    ctx.reset();
                    
                    // 绘制网格线
                    ctx.strokeStyle = gridColor;
                    ctx.lineWidth = 0.5;
                    
                    // 水平网格线
                    var horizontalLines = 6;
                    for (var i = 1; i < horizontalLines; i++) {
                        var y = i * height / horizontalLines;
                        ctx.beginPath();
                        ctx.moveTo(0, y);
                        ctx.lineTo(width, y);
                        ctx.stroke();
                    }
                    
                    // 垂直网格线
                    var verticalLines = 12;
                    for (var j = 1; j < verticalLines; j++) {
                        var x = j * width / verticalLines;
                        ctx.beginPath();
                        ctx.moveTo(x, 0);
                        ctx.lineTo(x, height);
                        ctx.stroke();
                    }
                }
            }
            
            // 坐标轴标签
            Column {
                anchors.left: parent.left
                anchors.top: parent.top
                anchors.bottom: parent.bottom
                width: 40
                
                Repeater {
                    model: 6
                    
                    Text {
                        width: parent.width
                        height: chartContainer.height / 6
                        text: {
                            var maxValue = 150;
                            var value = maxValue - (index * maxValue / 5);
                            return value.toFixed(0) + "%";
                        }
                        font.pixelSize: fontSizeSmall
                        color: axisColor
                        verticalAlignment: Text.AlignVCenter
                    }
                }
            }
            
            // 时间轴标签
            Row {
                anchors.left: parent.left
                anchors.leftMargin: 40
                anchors.right: parent.right
                anchors.bottom: parent.bottom
                height: 20
                
                Repeater {
                    model: 12
                    
                    Text {
                        width: chartContainer.width / 12
                        height: parent.height
                        text: {
                            var months = ["1月", "2月", "3月", "4月", "5月", "6月", 
                                        "7月", "8月", "9月", "10月", "11月", "12月"];
                            return months[index];
                        }
                        font.pixelSize: fontSizeSmall
                        color: axisColor
                        horizontalAlignment: Text.AlignHCenter
                    }
                }
            }
            
            // 主要图表
            Canvas {
                id: chartCanvas
                anchors.fill: parent
                anchors.leftMargin: 40
                anchors.rightMargin: 10
                anchors.topMargin: 10
                anchors.bottomMargin: 30
                
                onPaint: {
                    var ctx = getContext("2d");
                    ctx.reset();
                    
                    // 生成策略数据
                    var strategyData = generateStrategyData();
                    var benchmarkData = generateBenchmarkData();
                    
                    // 如果没有数据，直接返回
                    if (strategyData.length === 0) {
                        return;
                    }
                    
                    var allData = strategyData.concat(benchmarkData);
                    
                    // 防止空数组导致的错误
                    if (allData.length === 0) {
                        return;
                    }
                    
                    var maxVal = Math.max(...allData);
                    var minVal = Math.min(...allData);
                    
                    // 防止除零错误
                    var range = maxVal - minVal;
                    if (range === 0 || !isFinite(range)) {
                        range = 1;
                    }
                    
                    // 防止除零错误：如果数据点只有一个，使用width作为x坐标
                    var strategyWidthDivisor = Math.max(1, strategyData.length - 1);
                    var benchmarkWidthDivisor = Math.max(1, benchmarkData.length - 1);
                    
                    // 绘制策略线
                    ctx.strokeStyle = accentBlue;
                    ctx.lineWidth = 2;
                    ctx.beginPath();
                    
                    for (var i = 0; i < strategyData.length; i++) {
                        var x = i * width / strategyWidthDivisor;
                        var y = height - ((strategyData[i] - minVal) / range * height);
                        
                        if (i === 0) ctx.moveTo(x, y);
                        else ctx.lineTo(x, y);
                    }
                    ctx.stroke();
                    
                    // 填充策略区域
                    ctx.fillStyle = Qt.rgba(59/255, 130/255, 246/255, 0.1);
                    ctx.beginPath();
                    ctx.moveTo(0, height);
                    for (var i = 0; i < strategyData.length; i++) {
                        var x = i * width / strategyWidthDivisor;
                        var y = height - ((strategyData[i] - minVal) / range * height);
                        ctx.lineTo(x, y);
                    }
                    ctx.lineTo(width, height);
                    ctx.closePath();
                    ctx.fill();
                    
                    // 绘制基准线（如果开启）
                    if (showBenchmark && benchmarkData.length > 0) {
                        ctx.strokeStyle = accentYellow;
                        ctx.lineWidth = 2;
                        ctx.setLineDash([5, 3]);
                        ctx.beginPath();
                        
                        for (var j = 0; j < benchmarkData.length; j++) {
                            var benchX = j * width / benchmarkWidthDivisor;
                            var benchY = height - ((benchmarkData[j] - minVal) / range * height);
                            
                            if (j === 0) ctx.moveTo(benchX, benchY);
                            else ctx.lineTo(benchX, benchY);
                        }
                        ctx.stroke();
                        ctx.setLineDash([]);
                        
                        // 绘制基准数据点
                        ctx.fillStyle = accentYellow;
                        for (var k = 0; k < benchmarkData.length; k += 4) {
                            var pointX = k * width / benchmarkWidthDivisor;
                            var pointY = height - ((benchmarkData[k] - minVal) / range * height);
                            ctx.beginPath();
                            ctx.arc(pointX, pointY, 2, 0, Math.PI * 2);
                            ctx.fill();
                        }
                    }
                    
                    // 绘制策略数据点
                    ctx.fillStyle = accentBlue;
                    for (var l = 0; l < strategyData.length; l += 3) {
                        var strategyPointX = l * width / strategyWidthDivisor;
                        var strategyPointY = height - ((strategyData[l] - minVal) / range * height);
                        ctx.beginPath();
                        ctx.arc(strategyPointX, strategyPointY, 3, 0, Math.PI * 2);
                        ctx.fill();
                    }
                    
                    // 绘制基准线（0%）
                    var zeroY = height - ((100 - minVal) / range * height);
                    ctx.strokeStyle = gridColor;
                    ctx.lineWidth = 1;
                    ctx.setLineDash([5, 3]);
                    ctx.beginPath();
                    ctx.moveTo(0, zeroY);
                    ctx.lineTo(width, zeroY);
                    ctx.stroke();
                    ctx.setLineDash([]);
                    
                    // 绘制最大值和最小值标记
                    var maxIndex = strategyData.indexOf(Math.max(...strategyData));
                    var minIndex = strategyData.indexOf(Math.min(...strategyData));
                    
                    // 策略最大值标记（如果找到）
                    if (maxIndex >= 0) {
                        var maxX = maxIndex * width / strategyWidthDivisor;
                        var maxY = height - ((strategyData[maxIndex] - minVal) / range * height);
                        drawMarker(ctx, maxX, maxY, (strategyData[maxIndex]-100).toFixed(1) + "%", accentGreen);
                    }
                    
                    // 策略最小值标记（如果找到）
                    if (minIndex >= 0) {
                        var minX = minIndex * width / strategyWidthDivisor;
                        var minY = height - ((strategyData[minIndex] - minVal) / range * height);
                        drawMarker(ctx, minX, minY, (strategyData[minIndex]-100).toFixed(1) + "%", accentRed);
                    }
                    
                    // 如果显示基准，标记基准终点
                    if (showBenchmark && benchmarkData.length > 0) {
                        var benchEndX = width;
                        var benchEndY = height - ((benchmarkData[benchmarkData.length-1] - minVal) / range * height);
                        ctx.fillStyle = accentYellow;
                        ctx.font = "12px Arial";
                        ctx.textAlign = "right";
                        ctx.fillText(benchmarkName, benchEndX - 5, benchEndY - 10);
                    }
                }
            }
            
            // 鼠标悬停提示
            Rectangle {
                id: tooltip
                width: 120
                height: 80
                radius: borderRadiusMedium
                color: "#1E293B"
                border.color: accentBlue
                border.width: 1
                visible: false
                
                Column {
                    anchors.centerIn: parent
                    spacing: 4
                    
                    Text {
                        text: "时间：" + tooltip.time
                        font.pixelSize: fontSizeSmall
                        color: textSecondary
                        anchors.horizontalCenter: parent.horizontalCenter
                    }
                    
                    Text {
                        text: "策略收益：" + tooltip.strategyValue
                        font.pixelSize: fontSizeSmall
                        color: accentBlue
                        anchors.horizontalCenter: parent.horizontalCenter
                    }
                    
                    Text {
                        text: benchmarkName + "：" + tooltip.benchmarkValue
                        font.pixelSize: fontSizeSmall
                        color: showBenchmark ? accentYellow : textSecondary
                        anchors.horizontalCenter: parent.horizontalCenter
                        visible: showBenchmark
                    }
                }
            }
            
            MouseArea {
                anchors.fill: parent
                hoverEnabled: true
                
                onMouseXChanged: {
                    if (containsMouse && parent.width > 0) {
                        var strategyData = generateStrategyData();
                        var benchmarkData = generateBenchmarkData();
                        
                        if (strategyData.length === 0) {
                            tooltip.visible = false;
                            return;
                        }
                        
                        var allData = strategyData.concat(benchmarkData);
                        var maxVal = Math.max(...allData);
                        var minVal = Math.min(...allData);
                        var range = maxVal - minVal || 1;
                        
                        // 防止除零错误
                        var width = parent.width > 0 ? parent.width : 1;
                        var index = Math.min(strategyData.length - 1, Math.max(0, Math.floor(mouseX / width * strategyData.length)));
                        
                        // 确保索引有效
                        if (index < 0 || index >= strategyData.length) {
                            tooltip.visible = false;
                            return;
                        }
                        
                        var strategyValue = strategyData[index];
                        var benchmarkValue = benchmarkData[index];
                        
                        var strategyPercent = ((strategyValue - 100) / 100 * 100).toFixed(1);
                        var benchmarkPercent = ((benchmarkValue - 100) / 100 * 100).toFixed(1);
                        
                        tooltip.time = "第" + (index + 1) + "期";
                        tooltip.strategyValue = strategyPercent + "%";
                        tooltip.benchmarkValue = benchmarkPercent + "%";
                        tooltip.x = mouseX - tooltip.width / 2;
                        tooltip.y = mouseY - tooltip.height - 10;
                        tooltip.visible = true;
                    }
                }
                
                onExited: {
                    tooltip.visible = false;
                }
            }
            
            // 图例
            Column {
                anchors.top: parent.top
                anchors.right: parent.right
                anchors.margins: spacingMedium
                spacing: spacingMedium
                
                // 策略图例
                Row {
                    spacing: spacingMedium
                    
                    Rectangle {
                        width: 12
                        height: 2
                        color: accentBlue
                        anchors.verticalCenter: parent.verticalCenter
                    }
                    
                    Text {
                        text: "策略收益"
                        font.pixelSize: fontSizeSmall
                        color: textSecondary
                        anchors.verticalCenter: parent.verticalCenter
                    }
                }
                
                // 基准图例
                Row {
                    spacing: spacingMedium
                    visible: showBenchmark
                    
                    Rectangle {
                        width: 12
                        height: 2
                        color: accentYellow
                        anchors.verticalCenter: parent.verticalCenter
                    }
                    
                    Text {
                        text: benchmarkName
                        font.pixelSize: fontSizeSmall
                        color: textSecondary
                        anchors.verticalCenter: parent.verticalCenter
                    }
                }
            }
        }
    }
    
    // 生成策略数据
    function generateStrategyData() {
        var data = [];
        var value = 100;
        
        // 根据选择的时间范围调整数据点数量
        var points = getDataPoints();
        
        for (var i = 0; i < points; i++) {
            var change = (Math.random() - 0.2) * 4;
            value = value * (1 + change / 100);
            data.push(value);
        }
        return data;
    }
    
    // 生成基准数据
    function generateBenchmarkData() {
        var data = [];
        var value = benchmarkStartValue;
        
        var points = getDataPoints();
        var targetValue = benchmarkStartValue * (1 + benchmarkPerformance / 100);
        
        // 生成基准数据（相对平稳的上升趋势）
        for (var i = 0; i < points; i++) {
            var progress = i / points;
            var currentTarget = benchmarkStartValue + (targetValue - benchmarkStartValue) * progress;
            
            // 添加一些随机波动
            var fluctuation = (Math.random() - 0.5) * 1.5;
            value = currentTarget + fluctuation;
            data.push(value);
        }
        return data;
    }
    
    // 获取数据点数
    function getDataPoints() {
        if (selectedTimeRange === 0) return 24; // 1天
        else if (selectedTimeRange === 1) return 7; // 1周
        else if (selectedTimeRange === 2) return 30; // 1月
        else if (selectedTimeRange === 3) return 90; // 3月
        else if (selectedTimeRange === 4) return 180; // 6月
        else if (selectedTimeRange === 5) return 240; // 1年
        else return 365; // 全部
    }
    
    // 绘制标记点
    function drawMarker(ctx, x, y, text, color) {
        // 绘制标记点
        ctx.fillStyle = color;
        ctx.beginPath();
        ctx.arc(x, y, 4, 0, Math.PI * 2);
        ctx.fill();
        
        // 绘制标记文本
        ctx.fillStyle = color;
        ctx.font = "12px Arial";
        ctx.textAlign = "center";
        ctx.fillText(text, x, y - 10);
    }
    
    // 更新图表
    function updateChart() {
        gridCanvas.requestPaint();
        chartCanvas.requestPaint();
    }
    
    // 延迟初始化以避免组件未完全加载时崩溃
    Component.onCompleted: {
        console.log("StrategyChart组件初始化完成")
        // 延迟执行更新，确保所有组件都已加载
        initTimer.start()
    }
    
    Timer {
        id: initTimer
        interval: 100
        onTriggered: {
            console.log("开始延迟初始化图表")
            try {
                updateChart()
                console.log("图表更新完成")
            } catch (error) {
                console.error("图表更新时发生错误:", error)
            }
        }
    }
}