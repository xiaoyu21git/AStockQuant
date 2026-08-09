// SignalOutputPanel.qml — 信号输出配置面板 (v0.16.0)
// 全局信号模式开关 + 格式/推送目标配置 + 信号历史查询
import QtQuick 2.15
import QtQuick.Layouts 1.15
import QtQuick.Controls 2.15
import AStock.Bridge 1.0

Rectangle {
    id: root

    // ── 公共属性 ──
    property string selectedStrategyId: ""
    property bool signalEnabled: false
    property string currentFormat: "ths"
    property string currentTarget: "file"
    property string outputPath: "./signals/"
    property string queryDate: new Date().toISOString().slice(0, 10)
    property var historyData: []
    property var statsData: ({})

    // ── 颜色常量 (与 StrategyLibraryPage 一致) ──
    readonly property color textPrimary: "#F1F5F9"
    readonly property color textSecondary: "#94A3B8"
    readonly property color textTertiary: "#64748B"
    readonly property color primaryBg: "#0F172A"
    readonly property color secondaryBg: "#1E293B"
    readonly property color tertiaryBg: "#334155"
    readonly property color accentBlue: "#3B82F6"
    readonly property color borderColor: "#475569"
    readonly property color successGreen: "#10B981"
    readonly property color riseRed: "#EF4444"
    readonly property color warningAmber: "#F59E0B"

    Layout.fillWidth: true
    Layout.fillHeight: true
    color: primaryBg

    // ── 意图映射 ──
    function intentLabel(intent) {
        var map = {0: "持有", 1: "开仓", 2: "加仓", 3: "减仓", 4: "清仓"}
        return map[intent] !== undefined ? map[intent] : "未知"
    }

    function intentColor(intent) {
        var map = {0: textSecondary, 1: riseRed, 2: warningAmber, 3: accentBlue, 4: successGreen}
        return map[intent] !== undefined ? map[intent] : textSecondary
    }

    // ── 加载配置 ──
    function loadConfig() {
        if (!StrategyBridge) return
        var cfg = StrategyBridge.signalConfig()
        if (!cfg || Object.keys(cfg).length === 0) return
        signalEnabled = cfg.enabled === "true"
        currentFormat = cfg.signalFormat || "ths"
        currentTarget = cfg.pushTarget || "file"
        outputPath = cfg.signalOutputPath || "./signals/"
    }

    // ── 保存配置 ──
    function saveConfig() {
        if (!StrategyBridge) return
        var cfg = {
            enabled: signalEnabled ? "true" : "false",
            signalFormat: currentFormat,
            pushTarget: currentTarget,
            signalOutputPath: outputPath
        }
        StrategyBridge.setSignalConfig(cfg)
        loadHistory()
    }

    // ── 加载信号历史 ──
    function loadHistory() {
        if (!StrategyBridge || !queryDate) {
            historyData = []
            statsData = {}
            return
        }
        historyData = StrategyBridge.getSignalHistory(selectedStrategyId, queryDate)
        statsData = StrategyBridge.getSignalStats(selectedStrategyId, queryDate, queryDate)
    }

    // ── 格式化时间显示 ──
    function formatTime(isoStr) {
        if (!isoStr) return "--"
        var t = isoStr.indexOf("T") >= 0 ? isoStr.split("T")[1] : isoStr
        return t.length >= 8 ? t.slice(0, 8) : (t.length >= 5 ? t.slice(0, 5) : t)
    }

    // ── 初始化 ──
    Component.onCompleted: {
        loadConfig()
        loadHistory()
    }

    // ── 主布局 ──
    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 16
        spacing: 12

        // ═══════════════════════════════════════════════
        // 标题栏 + 启用开关
        // ═══════════════════════════════════════════════
        RowLayout {
            Layout.fillWidth: true
            spacing: 12

            Text {
                text: "📡 信号输出配置"
                font.pixelSize: 16
                font.weight: Font.DemiBold
                color: textPrimary
            }

            Item { Layout.fillWidth: true }

            // 启用/禁用 开关
            Rectangle {
                id: enableToggle
                width: 72; height: 34; radius: 17
                color: signalEnabled ? Qt.rgba(16/255, 185/255, 129/255, 0.18) : tertiaryBg
                border.color: signalEnabled ? successGreen : borderColor
                border.width: signalEnabled ? 2 : 1

                Text {
                    anchors.centerIn: parent
                    text: signalEnabled ? "已启用" : "已禁用"
                    font.pixelSize: 12
                    color: signalEnabled ? successGreen : textSecondary
                }

                MouseArea {
                    anchors.fill: parent
                    onClicked: {
                        signalEnabled = !signalEnabled
                        saveConfig()
                    }
                }
            }

            // 状态指示点
            Rectangle {
                width: 10; height: 10; radius: 5
                color: signalEnabled ? successGreen : textTertiary
            }
        }

        // ═══════════════════════════════════════════════
        // 配置区
        // ═══════════════════════════════════════════════
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: configGrid.implicitHeight + 24
            radius: 10
            color: secondaryBg
            border.color: Qt.rgba(71/255, 85/255, 105/255, 0.22)
            border.width: 1

            GridLayout {
                id: configGrid
                anchors.fill: parent
                anchors.margins: 16
                columns: 2
                columnSpacing: 24
                rowSpacing: 12

                // 信号格式
                Text {
                    text: "信号格式"
                    font.pixelSize: 13; color: textSecondary
                    Layout.preferredWidth: 80
                }
                ComboBox {
                    id: formatCombo
                    Layout.preferredWidth: 200; Layout.preferredHeight: 34
                    model: [{l: "同花顺(THS)", v: "ths"}, {l: "通达信(TDX)", v: "tdx"}, {l: "内部JSON", v: "internal"}]
                    textRole: "l"
                    currentIndex: {
                        for (var i = 0; i < model.length; i++) {
                            if (model[i].v === currentFormat) return i
                        }
                        return 0
                    }
                    onCurrentIndexChanged: {
                        if (currentIndex >= 0) {
                            currentFormat = model[currentIndex].v
                            saveConfig()
                        }
                    }
                    background: Rectangle {
                        radius: 6; color: primaryBg
                        border.color: tertiaryBg; border.width: 1
                    }
                    contentItem: Text {
                        text: parent.displayText; font.pixelSize: 13
                        color: textPrimary; verticalAlignment: Text.AlignVCenter
                        leftPadding: 10
                    }
                }

                // 推送目标
                Text {
                    text: "推送目标"
                    font.pixelSize: 13; color: textSecondary
                    Layout.preferredWidth: 80
                }
                ComboBox {
                    id: targetCombo
                    Layout.preferredWidth: 200; Layout.preferredHeight: 34
                    model: [{l: "本地文件", v: "file"}, {l: "Socket", v: "socket"}]
                    textRole: "l"
                    currentIndex: {
                        for (var i = 0; i < model.length; i++) {
                            if (model[i].v === currentTarget) return i
                        }
                        return 0
                    }
                    onCurrentIndexChanged: {
                        if (currentIndex >= 0) {
                            currentTarget = model[currentIndex].v
                            saveConfig()
                        }
                    }
                    background: Rectangle {
                        radius: 6; color: primaryBg
                        border.color: tertiaryBg; border.width: 1
                    }
                    contentItem: Text {
                        text: parent.displayText; font.pixelSize: 13
                        color: textPrimary; verticalAlignment: Text.AlignVCenter
                        leftPadding: 10
                    }
                }

                // 输出路径
                Text {
                    text: "输出路径"
                    font.pixelSize: 13; color: textSecondary
                    Layout.preferredWidth: 80
                }
                RowLayout {
                    Layout.preferredWidth: 400
                    spacing: 8

                    Rectangle {
                        Layout.fillWidth: true; Layout.preferredHeight: 34
                        radius: 6; color: primaryBg
                        border.color: tertiaryBg; border.width: 1

                        TextInput {
                            id: pathInput
                            anchors.fill: parent
                            anchors.margins: 8
                            text: outputPath
                            font.pixelSize: 13; color: textPrimary
                            verticalAlignment: Text.AlignVCenter
                            onEditingFinished: {
                                outputPath = text
                                saveConfig()
                            }
                        }
                    }

                    Rectangle {
                        width: 60; height: 34; radius: 6
                        color: accentBlue
                        Text {
                            anchors.centerIn: parent
                            text: "浏览"; font.pixelSize: 12; color: "white"
                        }
                        MouseArea {
                            anchors.fill: parent
                            onClicked: {
                                // Phase 1: 直接编辑文本即可, 浏览对话框留待后续
                            }
                        }
                    }
                }
            }
        }

        // ═══════════════════════════════════════════════
        // 统计卡片
        // ═══════════════════════════════════════════════
        RowLayout {
            Layout.fillWidth: true
            Layout.preferredHeight: 80
            spacing: 12

            Repeater {
                model: [
                    { title: "总信号数", value: statsData.totalSignals || 0, color: accentBlue },
                    { title: "推送成功", value: statsData.pushedCount || 0, color: successGreen },
                    { title: "推送失败", value: statsData.failedCount || 0, color: riseRed },
                    { title: "平均得分", value: statsData.avgScore ? Number(statsData.avgScore).toFixed(2) : "--", color: warningAmber }
                ]

                Rectangle {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    Layout.preferredHeight: 80
                    radius: 10
                    color: secondaryBg
                    border.color: Qt.rgba(71/255, 85/255, 105/255, 0.22)
                    border.width: 1

                    Column {
                        anchors.centerIn: parent
                        spacing: 6

                        Text {
                            anchors.horizontalCenter: parent.horizontalCenter
                            text: modelData.value
                            font.pixelSize: 22; font.weight: Font.Bold
                            color: modelData.color
                        }
                        Text {
                            anchors.horizontalCenter: parent.horizontalCenter
                            text: modelData.title
                            font.pixelSize: 12; color: textSecondary
                        }
                    }
                }
            }
        }

        // ═══════════════════════════════════════════════
        // 信号历史表格
        // ═══════════════════════════════════════════════
        Rectangle {
            Layout.fillWidth: true
            Layout.fillHeight: true
            radius: 10
            color: secondaryBg
            border.color: Qt.rgba(71/255, 85/255, 105/255, 0.22)
            border.width: 1

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 12
                spacing: 8

                // 表头行: 日期选择 + 标题
                RowLayout {
                    Layout.fillWidth: true
                    spacing: 12

                    Text {
                        text: "📋 信号历史"
                        font.pixelSize: 14; font.weight: Font.DemiBold
                        color: textPrimary
                    }

                    Item { Layout.fillWidth: true }

                    Text {
                        text: "交易日:"
                        font.pixelSize: 12; color: textSecondary
                    }

                    Rectangle {
                        width: 140; height: 30; radius: 6
                        color: primaryBg
                        border.color: tertiaryBg; border.width: 1

                        TextInput {
                            id: dateInput
                            anchors.fill: parent
                            anchors.margins: 6
                            text: queryDate
                            font.pixelSize: 12; color: textPrimary
                            verticalAlignment: Text.AlignVCenter
                            onEditingFinished: {
                                queryDate = text
                                loadHistory()
                            }
                        }
                    }

                    Rectangle {
                        width: 50; height: 30; radius: 6
                        color: accentBlue
                        Text {
                            anchors.centerIn: parent
                            text: "查询"; font.pixelSize: 12; color: "white"
                        }
                        MouseArea {
                            anchors.fill: parent
                            onClicked: loadHistory()
                        }
                    }
                }

                // 表头
                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 32
                    radius: 6
                    color: Qt.rgba(15/255, 23/255, 42/255, 0.6)

                    Row {
                        anchors.fill: parent
                        anchors.leftMargin: 10; anchors.rightMargin: 10
                        spacing: 0

                        Text { width: 80; text: "时间"; font.pixelSize: 11; font.weight: Font.DemiBold; color: textSecondary; verticalAlignment: Text.AlignVCenter; height: parent.height }
                        Text { width: 90; text: "代码"; font.pixelSize: 11; font.weight: Font.DemiBold; color: textSecondary; verticalAlignment: Text.AlignVCenter; height: parent.height }
                        Text { width: 100; text: "名称"; font.pixelSize: 11; font.weight: Font.DemiBold; color: textSecondary; verticalAlignment: Text.AlignVCenter; height: parent.height }
                        Text { width: 60; text: "意图"; font.pixelSize: 11; font.weight: Font.DemiBold; color: textSecondary; verticalAlignment: Text.AlignVCenter; height: parent.height }
                        Text { width: 70; text: "得分"; font.pixelSize: 11; font.weight: Font.DemiBold; color: textSecondary; verticalAlignment: Text.AlignVCenter; height: parent.height }
                        Text { width: 60; text: "状态"; font.pixelSize: 11; font.weight: Font.DemiBold; color: textSecondary; verticalAlignment: Text.AlignVCenter; height: parent.height }
                    }
                }

                // 数据行
                ListView {
                    id: historyList
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    clip: true
                    model: historyData
                    boundsBehavior: Flickable.StopAtBounds

                    delegate: Rectangle {
                        width: historyList.width
                        height: 34
                        color: index % 2 ? "transparent" : Qt.rgba(15/255, 23/255, 42/255, 0.3)
                        radius: 4

                        Row {
                            anchors.fill: parent
                            anchors.leftMargin: 10; anchors.rightMargin: 10
                            spacing: 0

                            Text {
                                width: 80; height: parent.height
                                text: root.formatTime(modelData.signalTime)
                                font.pixelSize: 12; color: textTertiary
                                verticalAlignment: Text.AlignVCenter; elide: Text.ElideRight
                            }
                            Text {
                                width: 90; height: parent.height
                                text: modelData.symbol || "--"
                                font.pixelSize: 12; color: textPrimary; font.family: "Courier New"
                                verticalAlignment: Text.AlignVCenter; elide: Text.ElideRight
                            }
                            Text {
                                width: 100; height: parent.height
                                text: modelData.stockName || "--"
                                font.pixelSize: 12; color: textPrimary
                                verticalAlignment: Text.AlignVCenter; elide: Text.ElideRight
                            }
                            Text {
                                width: 60; height: parent.height
                                text: root.intentLabel(modelData.intent)
                                font.pixelSize: 12; font.weight: Font.DemiBold
                                color: root.intentColor(modelData.intent)
                                verticalAlignment: Text.AlignVCenter
                            }
                            Text {
                                width: 70; height: parent.height
                                text: modelData.score ? Number(modelData.score).toFixed(2) : "--"
                                font.pixelSize: 12; color: warningAmber; font.family: "Courier New"
                                verticalAlignment: Text.AlignVCenter
                            }
                            Row {
                                width: 60; height: parent.height
                                spacing: 4

                                Rectangle {
                                    width: 8; height: 8; radius: 4
                                    anchors.verticalCenter: parent.verticalCenter
                                    color: modelData.pushed ? successGreen : riseRed
                                }
                                Text {
                                    width: 44; height: parent.height
                                    text: modelData.pushed ? "成功" : "失败"
                                    font.pixelSize: 11
                                    color: modelData.pushed ? successGreen : riseRed
                                    verticalAlignment: Text.AlignVCenter
                                }
                            }
                        }
                    }

                    // 空状态
                    Rectangle {
                        visible: historyList.count === 0
                        anchors.fill: parent
                        color: "transparent"

                        Text {
                            anchors.centerIn: parent
                            text: signalEnabled ? "暂无信号记录" : "信号模式未启用"
                            font.pixelSize: 14; color: textTertiary
                        }
                    }
                }
            }
        }
    }
}
