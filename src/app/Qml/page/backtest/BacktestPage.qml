import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import QtQuick.Shapes 1.15
import ConsoleUi 1.0
import AStock.Bridge 1.0

Page {
    id: backtestPage
    property bool pickingStart: true
    property string preselectedSymbol: ""

    // 顶部导航
    Rectangle {
        id: headerBar
        color: "#161e33"
        height: 70
        width: parent.width
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        border.color: "#2d3748"
        border.width: 1
        z: 10
        RowLayout {
            anchors.fill: parent
            spacing: 24
            Rectangle {
                color: "transparent"
                width: 180
                height: 70
                RowLayout {
                    anchors.fill: parent
                    spacing: 18
                    Rectangle {
                        width: 36; height: 36; radius: 8
                        gradient: Gradient {
                            GradientStop { position: 0.0; color: "#3b82f6" }
                            GradientStop { position: 1.0; color: "#1d4ed8" }
                        }
                        Text { anchors.centerIn: parent; text: "Q"; color: "white"; font.bold: true; font.pixelSize: 18 }
                    }
                    ColumnLayout {
                        Layout.alignment: Qt.AlignHCenter
                        spacing: 2
                        Text { text: "策略回测分析"; font.pixelSize: 18; font.bold: true; color: "#f1f5f9" }
                        Text { text: "Performance Backtesting & Analytics Dashboard"; font.pixelSize: 12; color: "#64748b" }
                    }
                }
            }
            Item { Layout.fillWidth: true }
            ComboBox {
                id: timeframeCombo
                width: 120; height: 36
                model: ["1 Day", "1 Week", "1 Month", "3 Months", "6 Months", "1 Year", "All Time"]
                currentIndex: 4
            }
        }
    }

    // 主内容区
    RowLayout {
        anchors.top: headerBar.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        spacing: 0

        // 左侧配置面板
        Rectangle {
            color: "#161e33"
            width: 300
            Layout.fillHeight: true
            border.color: "#2d3748"
            border.width: 1
            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 24
                spacing: 24
                // 策略配置
                ColumnLayout {
                    spacing: 8
                    RowLayout {
                        spacing: 10
                        Rectangle { width: 24; height: 24; radius: 6; color: "#3b82f610"; Text { anchors.centerIn: parent; text: "🤖"; color: "#3b82f6"; font.pixelSize: 14 } }
                        Text { text: "策略配置"; font.pixelSize: 15; font.bold: true; color: "#f1f5f9" }
                    }
                    ComboBox { id: strategyCombo; Layout.fillWidth: true; model: ["双均线交叉策略", "RSI超买超卖策略", "MACD策略", "布林带策略", "动量策略", "均值回归策略"] }
                    RowLayout {
                        Layout.fillWidth: true
                        Text { text: "短期均线周期"; color: "#94a3b8"; font.pixelSize: 13 }
                        Slider { id: shortMaSlider; from: 5; to: 50; value: 20; Layout.fillWidth: true }
                        Text { text: shortMaSlider.value.toFixed(0); color: "#3b82f6"; font.pixelSize: 13 }
                    }
                    RowLayout {
                        Layout.fillWidth: true
                        Text { text: "长期均线周期"; color: "#94a3b8"; font.pixelSize: 13 }
                        Slider { id: longMaSlider; from: 50; to: 200; value: 60; Layout.fillWidth: true }
                        Text { text: longMaSlider.value.toFixed(0); color: "#3b82f6"; font.pixelSize: 13 }
                    }
                    RowLayout {
                        Layout.fillWidth: true
                        Text { text: "初始资金"; color: "#94a3b8"; font.pixelSize: 13 }
                        TextField { id: capitalField; text: "100000"; Layout.fillWidth: true }
                    }
                }
                // 回测周期
                ColumnLayout {
                    spacing: 8
                    RowLayout {
                        spacing: 10
                        Rectangle { width: 24; height: 24; radius: 6; color: "#3b82f610"; Text { anchors.centerIn: parent; text: "📅"; color: "#3b82f6"; font.pixelSize: 14 } }
                        Text { text: "回测周期"; font.pixelSize: 15; font.bold: true; color: "#f1f5f9" }
                    }
                    RowLayout {
                        Layout.fillWidth: true
                        Text { text: "开始日期"; color: "#94a3b8"; font.pixelSize: 13 }
                        TextField { id: startDateField; text: "2023-01-01"; Layout.fillWidth: true }
                    }
                    RowLayout {
                        Layout.fillWidth: true
                        Text { text: "结束日期"; color: "#94a3b8"; font.pixelSize: 13 }
                        TextField { id: endDateField; text: "2023-12-31"; Layout.fillWidth: true }
                    }
                    CheckBox { id: autoPeriod; checked: true; text: "使用最近一年数据" }
                }
                // 高级选项
                ColumnLayout {
                    spacing: 8
                    RowLayout {
                        spacing: 10
                        Rectangle { width: 24; height: 24; radius: 6; color: "#3b82f610"; Text { anchors.centerIn: parent; text: "⚙️"; color: "#3b82f6"; font.pixelSize: 14 } }
                        Text { text: "高级选项"; font.pixelSize: 15; font.bold: true; color: "#f1f5f9" }
                    }
                    GridLayout {
                        columns: 2
                        CheckBox { id: slippage; text: "滑点模拟" }
                        CheckBox { id: commission; text: "交易费用"; checked: true }
                        CheckBox { id: stopLoss; text: "止盈止损" }
                        CheckBox { id: parallel; text: "并行计算" }
                        CheckBox { id: realtime; text: "实时更新" }
                        CheckBox { id: logging; text: "详细日志" }
                    }
                }
                // 开始回测按钮
                ButtonPrimary {
                    text: backtestController.running ? "回测中..." : "开始回测"
                    enabled: !backtestController.running
                    onClicked: startBacktest()
                    Layout.fillWidth: true
                }
            }
        }

        // 中间内容区域
        Rectangle {
            color: "#161e33"
            Layout.fillWidth: true
            Layout.fillHeight: true
            border.color: "#2d3748"
            border.width: 1
            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 24
                spacing: 24
                // 图表卡片
                Rectangle {
                    color: "#222c44"
                    radius: 12
                    border.color: "#2d3748"
                    border.width: 1
                    Layout.fillWidth: true
                    height: 380
                    ColumnLayout {
                        anchors.fill: parent
                        anchors.margins: 12
                        RowLayout {
                            Layout.fillWidth: true
                            Text { text: "净值曲线对比分析"; font.pixelSize: 16; font.bold: true; color: "#f1f5f9" }
                            Item { Layout.fillWidth: true }
                            TabBar {
                                id: chartTabs
                                TabButton { text: "净值曲线" }
                                TabButton { text: "收益曲线" }
                                TabButton { text: "回撤曲线" }
                                TabButton { text: "对比分析" }
                            }
                        }
                        Rectangle {
                            color: "transparent"
                            Layout.fillWidth: true
                            height: 320
                            // TODO: ChartView/Canvas for QML chart
                            Text { anchors.centerIn: parent; text: "[净值曲线图表占位]"; color: "#94a3b8" }
                        }
                    }
                }
                // 性能指标网格
                GridLayout {
                    columns: 4
                    rowSpacing: 16
                    columnSpacing: 16
                    Layout.fillWidth: true
                    Rectangle {
                        color: "#222c44"; radius: 8; border.color: "#2d3748"; border.width: 1
                        Layout.fillWidth: true; Layout.fillHeight: true
                        ColumnLayout {
                            anchors.fill: parent; anchors.margins: 8
                            Text { text: "总收益率"; color: "#94a3b8"; font.pixelSize: 13 }
                            Text { text: "32.45%"; color: "#10b981"; font.pixelSize: 24; font.bold: true }
                            Text { text: "基准: 18.2%"; color: "#64748b"; font.pixelSize: 11 }
                        }
                    }
                    Rectangle {
                        color: "#222c44"; radius: 8; border.color: "#2d3748"; border.width: 1
                        Layout.fillWidth: true; Layout.fillHeight: true
                        ColumnLayout {
                            anchors.fill: parent; anchors.margins: 8
                            Text { text: "年化收益率"; color: "#94a3b8"; font.pixelSize: 13 }
                            Text { text: "28.7%"; color: "#10b981"; font.pixelSize: 24; font.bold: true }
                            Text { text: "年化波动: 16.3%"; color: "#64748b"; font.pixelSize: 11 }
                        }
                    }
                    Rectangle {
                        color: "#222c44"; radius: 8; border.color: "#2d3748"; border.width: 1
                        Layout.fillWidth: true; Layout.fillHeight: true
                        ColumnLayout {
                            anchors.fill: parent; anchors.margins: 8
                            Text { text: "最大回撤"; color: "#94a3b8"; font.pixelSize: 13 }
                            Text { text: "-12.3%"; color: "#ef4444"; font.pixelSize: 24; font.bold: true }
                            Text { text: "发生日期: 2023-03-15"; color: "#64748b"; font.pixelSize: 11 }
                        }
                    }
                    Rectangle {
                        color: "#222c44"; radius: 8; border.color: "#2d3748"; border.width: 1
                        Layout.fillWidth: true; Layout.fillHeight: true
                        ColumnLayout {
                            anchors.fill: parent; anchors.margins: 8
                            Text { text: "夏普比率"; color: "#94a3b8"; font.pixelSize: 13 }
                            Text { text: "2.45"; color: "#3b82f6"; font.pixelSize: 24; font.bold: true }
                            Text { text: "无风险利率: 2.5%"; color: "#64748b"; font.pixelSize: 11 }
                        }
                    }
                    Rectangle {
                        color: "#222c44"; radius: 8; border.color: "#2d3748"; border.width: 1
                        Layout.fillWidth: true; Layout.fillHeight: true
                        ColumnLayout {
                            anchors.fill: parent; anchors.margins: 8
                            Text { text: "胜率"; color: "#94a3b8"; font.pixelSize: 13 }
                            Text { text: "62.3%"; color: "#10b981"; font.pixelSize: 24; font.bold: true }
                            Text { text: "盈利交易: 97笔"; color: "#64748b"; font.pixelSize: 11 }
                        }
                    }
                    Rectangle {
                        color: "#222c44"; radius: 8; border.color: "#2d3748"; border.width: 1
                        Layout.fillWidth: true; Layout.fillHeight: true
                        ColumnLayout {
                            anchors.fill: parent; anchors.margins: 8
                            Text { text: "盈亏比"; color: "#94a3b8"; font.pixelSize: 13 }
                            Text { text: "1.85"; color: "#10b981"; font.pixelSize: 24; font.bold: true }
                            Text { text: "平均盈利/平均亏损"; color: "#64748b"; font.pixelSize: 11 }
                        }
                    }
                    Rectangle {
                        color: "#222c44"; radius: 8; border.color: "#2d3748"; border.width: 1
                        Layout.fillWidth: true; Layout.fillHeight: true
                        ColumnLayout {
                            anchors.fill: parent; anchors.margins: 8
                            Text { text: "波动率"; color: "#94a3b8"; font.pixelSize: 13 }
                            Text { text: "18.2%"; color: "#f59e0b"; font.pixelSize: 24; font.bold: true }
                            Text { text: "年化标准差"; color: "#64748b"; font.pixelSize: 11 }
                        }
                    }
                    Rectangle {
                        color: "#222c44"; radius: 8; border.color: "#2d3748"; border.width: 1
                        Layout.fillWidth: true; Layout.fillHeight: true
                        ColumnLayout {
                            anchors.fill: parent; anchors.margins: 8
                            Text { text: "卡玛比率"; color: "#94a3b8"; font.pixelSize: 13 }
                            Text { text: "2.33"; color: "#3b82f6"; font.pixelSize: 24; font.bold: true }
                            Text { text: "收益/最大回撤"; color: "#64748b"; font.pixelSize: 11 }
                        }
                    }
                }
            }
        }

        // 右侧结果面板
        Rectangle {
            color: "#161e33"
            width: 320
            Layout.fillHeight: true
            border.color: "#2d3748"
            border.width: 1
            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 24
                spacing: 24
                // 回测状态
                Rectangle {
                    color: "#1e293b"
                    radius: 8
                    border.color: "#2d3748"
                    border.width: 1
                    Layout.fillWidth: true
                    ColumnLayout {
                        anchors.fill: parent
                        anchors.margins: 8
                        spacing: 8
                        Text { text: "回测已完成"; font.pixelSize: 16; font.bold: true; color: "#f1f5f9" }
                        Text { text: "策略表现优于基准 14.25%"; font.pixelSize: 13; color: "#64748b" }
                    }
                }
                // 交易记录
                ColumnLayout {
                    spacing: 8
                    RowLayout {
                        Layout.fillWidth: true
                        Text { text: "交易记录"; font.pixelSize: 15; font.bold: true; color: "#f1f5f9" }
                        Item { Layout.fillWidth: true }
                        ButtonSecondary { text: "导出"; width: 60; height: 32 }
                    }
                    Rectangle {
                        color: "#222c44"
                        radius: 8
                        border.color: "#2d3748"
                        border.width: 1
                        Layout.fillWidth: true
                        height: 180
                        // TODO: 交易记录列表
                        Text { anchors.centerIn: parent; text: "[交易记录占位]"; color: "#94a3b8" }
                    }
                }
                // 操作按钮
                ColumnLayout {
                    spacing: 12
                    ButtonPrimary { text: "保存回测结果"; Layout.fillWidth: true }
                    ButtonPrimary { text: "生成详细报告"; Layout.fillWidth: true }
                    ButtonPrimary { text: "对比其他策略"; Layout.fillWidth: true }
                }
            }
        }
    }

    // // 回测控制器（桥接到 C++ 引擎侧）
    // BacktestController { id: backtestController }

    // // 开始回测函数（保留原有逻辑，后续可补充参数绑定）
    // function startBacktest() {
    //     // TODO: 绑定参数到backtestController
    //     backtestController.run()
    // }
}