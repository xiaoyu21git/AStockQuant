import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "qrc:/Qml/component" as Components
import AStock.Engine 1.0

Page {
    id: backtestPage
    property bool pickingStart: true
    // 从高位股监控页跳转时，可以预选标的
    property string preselectedSymbol: ""

    // 顶部工具栏
    header: ToolBar {
        RowLayout {
            anchors.fill: parent

            Button {
                text: "← 返回"
                onClicked: stackView.pop()
            }

            Label {
                text: "策略回测"
                font.pixelSize: 16
                font.bold: true
            }

            Item { Layout.fillWidth: true }

            Label {
                text: "回测配置 + 结果分析"
                color: "#888"
                font.pixelSize: 12
            }
        }
    }

    // 日期时间选择弹窗（深色网格风格，仅日期）
    Components.DateTimePicker {
        id: dateTimePicker
        dateFormat: "yyyy-MM-dd"
        timeFormat: "hh:mm:ss"
        showTime: false

        onConfirmed: function(dt) {
            var s = Qt.formatDateTime(dt, "yyyy-MM-dd")
            if (backtestPage.pickingStart)
                startDate.text = s
            else
                endDate.text = s
        }
    }

    // 页面加载完成后，根据传入的预选标的调整下拉框
    Component.onCompleted: {
        if (preselectedSymbol !== "") {
            var m = symbolCombo.model
            var found = -1
            for (var i = 0; i < m.length; ++i) {
                if (m[i] === preselectedSymbol) {
                    found = i
                    break
                }
            }
            if (found >= 0) {
                symbolCombo.currentIndex = found
            } else {
                // 如果预选标的不在默认列表里（比如来自高位股扫描的任意 A 股），
                // 就把它插入到列表最前面并选中
                m.unshift(preselectedSymbol)
                symbolCombo.model = m
                symbolCombo.currentIndex = 0
            }
        }
    }

    // 主内容：左右分栏布局
    RowLayout {
        anchors.fill: parent
        anchors.margins: 8
        spacing: 8

        // 左侧：回测配置面板
        GroupBox {
            title: "回测配置"
            Layout.preferredWidth: 320
            Layout.fillHeight: true

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 8
                spacing: 8

                GridLayout {
                    columns: 2
                    rowSpacing: 6
                    columnSpacing: 8
                    Layout.fillWidth: true

                    Label { text: "开始日期:" }
                    RowLayout {
                        Layout.fillWidth: true
                        TextField {
                            id: startDate
                            text: "2023-01-01"
                            placeholderText: "YYYY-MM-DD"
                            readOnly: true
                            Layout.fillWidth: true
                            validator: RegularExpressionValidator {
                                regularExpression: /^(19|20)\d\d-(0[1-9]|1[0-2])-(0[1-9]|[12]\d|3[01])$/
                            }

                            MouseArea {
                                anchors.fill: parent
                                onClicked: {
                                    var d = new Date(startDate.text)
                                    if (isNaN(d.getTime())) d = new Date()
                                    dateTimePicker.hasSelection = false
                                    dateTimePicker.dateTime = Qt.formatDateTime(d, "yyyy-MM-dd hh:mm:ss")
                                    backtestPage.pickingStart = true
                                    dateTimePicker.x = (backtestPage.width - dateTimePicker.width) / 2
                                    dateTimePicker.y = (backtestPage.height - dateTimePicker.height) / 2
                                    dateTimePicker.open()
                                }
                            }
                        }
                    }

                    Label { text: "结束日期:" }
                    RowLayout {
                        Layout.fillWidth: true
                        TextField {
                            id: endDate
                            text: "2023-12-31"
                            placeholderText: "YYYY-MM-DD"
                            readOnly: true
                            Layout.fillWidth: true
                            validator: RegularExpressionValidator {
                                regularExpression: /^(19|20)\d\d-(0[1-9]|1[0-2])-(0[1-9]|[12]\d|3[01])$/
                            }

                            MouseArea {
                                anchors.fill: parent
                                onClicked: {
                                    var d2 = new Date(endDate.text)
                                    if (isNaN(d2.getTime())) d2 = new Date()
                                    dateTimePicker.hasSelection = false
                                    dateTimePicker.dateTime = Qt.formatDateTime(d2, "yyyy-MM-dd hh:mm:ss")
                                    backtestPage.pickingStart = false
                                    dateTimePicker.x = (backtestPage.width - dateTimePicker.width) / 2
                                    dateTimePicker.y = (backtestPage.height - dateTimePicker.height) / 2
                                    dateTimePicker.open()
                                }
                            }
                        }
                    }

                    Label { text: "标的代码:" }
                    ComboBox {
                        id: symbolCombo
                        // 与 astock_init.sql 中示例 symbol_info 顺序保持一致，方便映射到 symbol_id
                        model: [
                            "000001.SZ",
                            "000002.SZ",
                            "000300.SH",
                            "000905.SH",
                            "600000.SH",
                            "600036.SH",
                            "AAPL",
                            "MSFT"
                        ]
                        currentIndex: 0
                    }

                    Label { text: "初始资金:" }
                    TextField {
                        id: capital
                        text: "100000"
                        validator: DoubleValidator { bottom: 0 }
                    }

                    Label { text: "最大仓位比例:" }
                    TextField {
                        id: maxPositionRatioField
                        text: "1.0"
                        placeholderText: "0.0 - 1.0"
                        validator: DoubleValidator { bottom: 0; top: 1 }
                    }

                    Label { text: "选择策略:" }
                    ComboBox {
                        id: strategyCombo
                        model: ["移动平均线策略", "RSI策略", "布林带策略"]
                    }

                    Label { text: "手续费率(‰):" }
                    TextField {
                        id: commissionRateField
                        text: "1.0"    // 对应 0.001
                        placeholderText: "例如 1.0 表示万分之一"
                        validator: DoubleValidator { bottom: 0; top: 10 }
                    }

                    Label { text: "滑点(bps):" }
                    TextField {
                        id: slippageRateField
                        text: "5"      // 对应 0.0005
                        placeholderText: "例如 5 表示 5bp"
                        validator: DoubleValidator { bottom: 0; top: 100 }
                    }

                    Label { text: "最小成交量阈值:" }
                    TextField {
                        id: minVolumeField
                        text: "0"      // 0 表示不开启过滤
                        placeholderText: "小于该成交量的bar不交易"
                        validator: DoubleValidator { bottom: 0 }
                    }

                    Label { text: "数据源:" }
                    ComboBox {
                        id: dataSourceCombo
                        model: ["模拟数据", "数据库"]
                        currentIndex: 0
                        onCurrentIndexChanged: {
                            console.log("[BacktestPage] 数据源切换为:", currentText)
                        }
                    }
                }

                // 配置下方操作按钮
                RowLayout {
                    Layout.fillWidth: true
                    spacing: 8

                    Button {
                        text: backtestController.running ? "回测中..." : "开始回测"
                        Layout.fillWidth: true
                        highlighted: true
                        enabled: !backtestController.running
                        onClicked: startBacktest()
                    }

                    Button {
                        text: "停止"
                        enabled: false
                    }
                }

                Button {
                    text: "导出结果"
                    Layout.fillWidth: true
                }
            }
        }

        // 右侧：结果展示区，占据主要空间
        Rectangle {
            Layout.fillWidth: true
            Layout.fillHeight: true
            color: "#f5f5f7"

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 8
                spacing: 6

                // 结果 Tab
                TabBar {
                    id: resultTabs
                    Layout.fillWidth: true

                    TabButton { text: "交易记录" }
                    TabButton { text: "收益曲线" }
                    TabButton { text: "统计指标" }
                }

                StackLayout {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    currentIndex: resultTabs.currentIndex

                    // 交易记录
                    ScrollView {
                        clip: true
                        ListView {
                            id: tradeList
                            anchors.fill: parent
                            anchors.margins: 4
                            model: GlobalTradeModel

                            delegate: Rectangle {
                                width: tradeList.width
                                height: 40
                                color: index % 2 === 0 ? "#f9f9f9" : "white"

                                RowLayout {
                                    anchors.fill: parent
                                    anchors.margins: 5

                                    Label {
                                        text: model.time || "2023-01-01"
                                        Layout.preferredWidth: 120
                                    }
                                    Label {
                                        text: model.symbol || "000001"
                                        Layout.preferredWidth: 80
                                    }
                                    Label {
                                        text: model.price || "10.00"
                                        Layout.preferredWidth: 80
                                    }
                                    Label {
                                        text: model.volume || "100"
                                        Layout.preferredWidth: 80
                                    }
                                    Label {
                                        text: model.side || "买入"
                                        Layout.preferredWidth: 60
                                    }
                                }
                            }
                        }
                    }

                    // 收益曲线
                    Rectangle {
                        color: "white"

                        ColumnLayout {
                            anchors.fill: parent
                            anchors.margins: 10

                            Label {
                                text: "资金曲线（时间 / 权益）"
                                font.bold: true
                            }

                            Components.EquityChart {
                                id: equityChart
                                Layout.fillWidth: true
                                Layout.fillHeight: true
                                model: GlobalEquityModel
                            }
                        }
                    }

                    // 统计指标
                    Rectangle {
                        color: "white"

                        ColumnLayout {
                            anchors.fill: parent
                            anchors.margins: 16
                            spacing: 12

                            Label {
                                text: "统计指标"
                                font.bold: true
                                font.pixelSize: 14
                            }

                            Label {
                                text: "后续在这里展示回测的收益、风险等关键统计指标。"
                                color: "#888"
                                wrapMode: Text.WordWrap
                            }
                        }
                    }
                }
            }
        }
    }

    // 回测控制器（桥接到 C++ 引擎侧）
    BacktestController {
        id: backtestController
    }

    // 开始回测函数
    function startBacktest() {
        console.log("开始回测...")
        console.log("配置:", {
            start: startDate.text,
            end: endDate.text,
            capital: capital.text,
            strategy: strategyCombo.currentText,
            dataSource: dataSourceCombo.currentText,
            maxPositionRatio: maxPositionRatioField.text,
            commissionRate: commissionRateField.text,
            slippageRate: slippageRateField.text,
            minVolume: minVolumeField.text
        })

        // 基本日期校验：非空、格式正确、开始<=结束
        if (!startDate.acceptableInput || !endDate.acceptableInput) {
            console.warn("[BacktestPage] 开始/结束日期格式不正确，应为 YYYY-MM-DD")
            return
        }

        var s = new Date(startDate.text)
        var e = new Date(endDate.text)
        if (isNaN(s.getTime()) || isNaN(e.getTime())) {
            console.warn("[BacktestPage] 无法解析开始/结束日期")
            return
        }
        if (s > e) {
            console.warn("[BacktestPage] 开始日期不能晚于结束日期")
            return
        }

        // 将 UI 中的配置传递到 C++ 的 BacktestController
        backtestController.dataSource = dataSourceCombo.currentText
        backtestController.symbol     = symbolCombo.currentText
        backtestController.startDate  = startDate.text
        backtestController.endDate    = endDate.text
        backtestController.capital    = Number(capital.text)
        backtestController.strategy   = strategyCombo.currentText
        backtestController.maxPositionRatio = Number(maxPositionRatioField.text)
        backtestController.commissionRate   = Number(commissionRateField.text) / 1000.0
        backtestController.slippageRate     = Number(slippageRateField.text) / 10000.0
        backtestController.minVolume        = Number(minVolumeField.text)
        backtestController.run()
    }
}