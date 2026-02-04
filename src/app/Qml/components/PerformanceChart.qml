// components/PerformanceChart.qml
import QtQuick 2.15
import QtCharts 2.3
import "../utils/Constants.js" as Constants

Rectangle {
    id: performanceChart
    implicitWidth: 320
    implicitHeight: 250
    radius: Constants.borderRadiusXLarge
    color: Constants.secondaryBg
    border.color: Constants.borderColor
    
    // 属性
    property var strategyData: []
    property var benchmarkData: []
    property string timeRange: "1年"
    
    // 信号
    signal timeRangeChanged(string newRange)
    
    ColumnLayout {
        anchors.fill: parent
        anchors.margins: Constants.spacingXLarge
        
        // 头部
        RowLayout {
            Text {
                text: "策略收益曲线"
                font.pixelSize: Constants.fontSizeLarge
                font.weight: Font.DemiBold
                color: Constants.textPrimary
            }
            
            Item { Layout.fillWidth: true }
            
            // 时间范围选择
            ComboBox {
                model: ["1周", "1月", "3月", "1年"]
                currentIndex: 3
                height: 32
                background: Rectangle {
                    radius: Constants.borderRadiusMedium
                    color: Constants.tertiaryBg
                    border.color: Constants.borderLight
                }
                
                onCurrentTextChanged: performanceChart.timeRangeChanged(currentText)
            }
        }
        
        // 图表
        ChartView {
            id: chartView
            Layout.fillWidth: true
            Layout.fillHeight: true
            theme: ChartView.ChartThemeDark
            antialiasing: true
            legend.visible: true
            legend.alignment: Qt.AlignTop
            legend.labelColor: Constants.textSecondary
            legend.font.pixelSize: Constants.fontSizeSmall
            
            // 策略净值线
            LineSeries {
                id: strategySeries
                name: "策略净值"
                color: Constants.accentBlue
                width: 3
                pointVisible: true
                pointLabelsVisible: false
            }
            
            // 基准净值线
            LineSeries {
                id: benchmarkSeries
                name: "基准净值"
                color: Constants.textSecondary
                width: 2
                style: Qt.DashLine
                pointVisible: true
                pointLabelsVisible: false
            }
            
            // X轴
            ValueAxis {
                id: axisX
                min: 0
                max: 12
                tickCount: 13
                labelFormat: "%d"
                labelsColor: Constants.textSecondary
                gridLineColor: Qt.rgba(148/255, 163/255, 184/255, 0.1)
            }
            
            // Y轴
            ValueAxis {
                id: axisY
                min: 80
                max: 160
                tickCount: 5
                labelsColor: Constants.textSecondary
                gridLineColor: Qt.rgba(148/255, 163/255, 184/255, 0.1)
            }
        }
    }
    
    // 更新数据
    function updateData(strategyData, benchmarkData) {
        // 清空数据
        strategySeries.clear();
        benchmarkSeries.clear();
        
        // 添加新数据
        for (var i = 0; i < strategyData.length; i++) {
            strategySeries.append(i, strategyData[i]);
            benchmarkSeries.append(i, benchmarkData[i]);
        }
        
        // 更新坐标轴范围
        axisX.max = strategyData.length - 1;
        axisY.min = Math.min(
            Math.min.apply(null, strategyData),
            Math.min.apply(null, benchmarkData)
        ) * 0.95;
        axisY.max = Math.max(
            Math.max.apply(null, strategyData),
            Math.max.apply(null, benchmarkData)
        ) * 1.05;
    }
}