import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import AStock.Bridge 1.0 as Bridge
import "../../../utils/TradingConstants.js" as Const
import "../../../utils/TradingWidgetBase.js" as Base

// ============================================================================
// StrategyMonitorWidget — 策略监控 (动态布局适配)
//
// optimalHeight = 标题(28) + 状态卡片行(72) + 规则命中表头(16) + 5条*行高(24*5=120) + 边距(24) = 260
//                取整 = 300
//
// 密度模式:
//   Compact  (< 165px):  仅策略名 + 状态灯 + 最近1条命中
//   Normal   (165-345px): 状态卡片 + 最近3条命中
//   Expanded (> 345px):  全部卡片 + 可滚动命中历史
//
// 数据来源:
//   - 策略列表: StrategyBridge.list() + onStrategiesChanged
//   - 规则命中: 从 TradeExecutionBridge 执行信号中提取 (recentRuleHits 始终为空)
//
// Bridges: StrategyBridge + TradeExecutionBridge
// ============================================================================

Rectangle {
    id: root
    property var widgetConfig: ({})
    color: Const.tradingPanelBg
    radius: 10
    border.color: Const.tradingPanelBorder
    border.width: 1
    clip: true

    // ============ 动态适配 ============
    readonly property real optimalHeight: 300
    property real scaleFactor: Base.computeScaleFactor(height, optimalHeight)
    property string densityMode: "normal"

    onHeightChanged: {
        var newMode = Base.computeDensityMode(height, optimalHeight, densityMode)
        if (newMode !== densityMode) densityMode = newMode
    }

    function s(v) { return Base.scaleValue(v, scaleFactor) }

    // ============ 当前策略 ============
    readonly property string highlightId: widgetConfig && widgetConfig.strategyId
        ? String(widgetConfig.strategyId) : ""
    readonly property string highlightName: widgetConfig && widgetConfig.strategyName
        ? String(widgetConfig.strategyName) : ""

    // ============ Bridge 数据 ============

    property var strategies: Bridge.StrategyBridge
        && Bridge.StrategyBridge.list
        ? Bridge.StrategyBridge.list() : []
    property string selId: Bridge.StrategyBridge
        ? (Bridge.StrategyBridge.selId || "") : ""

    Connections {
        target: Bridge.StrategyBridge
        function onStrategiesChanged() {
            if (Bridge.StrategyBridge && Bridge.StrategyBridge.list)
                strategies = Bridge.StrategyBridge.list()
        }
        function onSelIdChanged() {
            selId = Bridge.StrategyBridge ? (Bridge.StrategyBridge.selId || "") : ""
        }
        function onStarted(strategyId) {
            if (Bridge.StrategyBridge && Bridge.StrategyBridge.list)
                strategies = Bridge.StrategyBridge.list()
        }
        function onStopped(strategyId) {
            if (Bridge.StrategyBridge && Bridge.StrategyBridge.list)
                strategies = Bridge.StrategyBridge.list()
        }
    }

    readonly property var currentStrategy: {
        var targetId = highlightId || selId
        if (targetId) {
            for (var i = 0; i < strategies.length; i++) {
                if (String(strategies[i].strategyId || "") === targetId) return strategies[i]
            }
        }
        if (strategies.length > 0) return strategies[0]
        return null
    }

    readonly property string strategyName: highlightName
        || (currentStrategy ? String(currentStrategy.strategyName || currentStrategy.name || "") : "")
    readonly property string strategyStatus: currentStrategy
        ? String(currentStrategy.displayStatus || "未知") : "未绑定"
    readonly property bool isRunning: currentStrategy
        ? String(currentStrategy.displayStatus || "") === "运行中" : false

    // ============ 规则命中 (从执行信号中提取) ============
    property var ruleHits: []  // [{ time, stage, reasonCode, ruleId, symbol, detail }]
    readonly property int maxRuleHits: 20

    function addRuleHit(stage, reasonCode, ruleId, symbol, detail) {
        var entry = {
            time: Qt.formatDateTime(new Date(), "hh:mm:ss"),
            _timestamp: Date.now(),
            stage: stage || "",
            reasonCode: reasonCode || "",
            ruleId: ruleId || "",
            symbol: symbol || "",
            detail: detail || ""
        }
        var next = ruleHits.slice()
        next.unshift(entry)  // 最新在前
        if (next.length > maxRuleHits) next = next.slice(0, maxRuleHits)
        ruleHits = next
    }

    readonly property int visibleRuleHits: densityMode === "compact" ? 1
                                          : densityMode === "expanded" ? 10
                                          : 3

    // 从 orderStatusPublished 中提取规则命中信息
    Connections {
        target: Bridge.TradeExecutionBridge
        function onOrderStatusPublished(orderStatus) {
            if (!orderStatus) return
            var reasonCode = String(orderStatus.reasonCode || "")
            var ruleId = String(orderStatus.ruleId || "")
            if (reasonCode || ruleId) {
                root.addRuleHit(
                    orderStatus.stageCode || orderStatus.statusOrigin || "",
                    reasonCode,
                    ruleId,
                    String(orderStatus.symbol || ""),
                    String(orderStatus.message || "")
                )
            }
        }
    }

    // ============ 状态卡数据 ============
    // 今日信号计数: 从已记录的执行日志中统计
    readonly property int signalCount: {
        var c = 0
        for (var i = 0; i < ruleHits.length; i++) {
            // 仅统计最近 60 秒内的命中
            if (ruleHits[i]._timestamp && (Date.now() - ruleHits[i]._timestamp < 60000)) c++
        }
        return c
    }

    // ============ 布局 ============
    ColumnLayout {
        anchors.fill: parent
        anchors.margins: s(6)
        spacing: s(4)

        // --- 标题行 ---
        RowLayout {
            Layout.fillWidth: true
            Layout.preferredHeight: s(28)
            spacing: s(6)

            Text {
                text: "策略监控"
                color: Const.tradingTitleText
                font.pixelSize: s(12)
                font.weight: Font.DemiBold
            }
            Text {
                text: strategyName ? "当前: " + strategyName : ""
                color: Const.tradingLabelSecondary
                font.pixelSize: s(10)
                elide: Text.ElideRight
                Layout.fillWidth: true
            }
            // 状态指示灯
            Rectangle {
                width: s(8); height: s(8); radius: s(4)
                color: isRunning ? "#10B981" : "#64748B"
            }
        }

        // --- Compact: 仅策略名 + 状态灯 + 最近1条命中 ---
        ColumnLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: s(2)
            visible: densityMode === "compact"

            Text {
                text: strategyStatus + (isRunning ? " · 运行中" : "")
                color: isRunning ? "#10B981" : "#94A3B8"
                font.pixelSize: s(10)
                Layout.fillWidth: true
            }
            RuleHitRow {
                Layout.fillWidth: true
                hit: ruleHits.length > 0 ? ruleHits[0] : null
                visible: ruleHits.length > 0
            }
        }

        // --- Normal: 状态卡片 + 最近3条 ---
        ColumnLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: s(4)
            visible: densityMode === "normal"

            // 状态卡片
            RowLayout {
                Layout.fillWidth: true
                Layout.preferredHeight: s(52)
                spacing: s(5)

                StatusCard { l: "策略状态"; v: strategyStatus
                             vc: isRunning ? "#10B981" : "#94A3B8" }
                StatusCard { l: "今日信号"; v: String(signalCount); vc: "#F1F5F9" }
                StatusCard { l: "最后评估"; v: ruleHits.length > 0 ? ruleHits[0].time : "--"
                             vc: "#F1F5F9" }
            }

            // 规则命中
            Text {
                text: "最近规则命中"
                color: "#64748B"
                font.pixelSize: s(9)
                visible: ruleHits.length > 0
            }
            Repeater {
                model: Math.min(ruleHits.length, visibleRuleHits)
                RuleHitRow {
                    Layout.fillWidth: true
                    hit: ruleHits[index]
                }
            }
            Text {
                text: "暂无规则命中记录"
                color: Const.tradingEmptyText
                font.pixelSize: s(9)
                visible: ruleHits.length === 0
                Layout.fillWidth: true
            }
        }

        // --- Expanded: 全部卡片 + 可滚动命中 ---
        ColumnLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: s(4)
            visible: densityMode === "expanded"

            RowLayout {
                Layout.fillWidth: true
                Layout.preferredHeight: s(62)
                spacing: s(6)

                StatusCard { l: "策略状态"; v: strategyStatus
                             vc: isRunning ? "#10B981" : "#94A3B8" }
                StatusCard { l: "今日信号"; v: String(signalCount); vc: "#F1F5F9" }
                StatusCard { l: "最后评估"; v: ruleHits.length > 0 ? ruleHits[0].time : "--"
                             vc: "#F1F5F9" }
                StatusCard { l: "命中总数"; v: String(ruleHits.length); vc: "#F1F5F9" }
            }

            Text {
                text: "规则命中历史 (" + ruleHits.length + ")"
                color: "#64748B"
                font.pixelSize: s(9)
                visible: ruleHits.length > 0
            }
            ListView {
                Layout.fillWidth: true
                Layout.fillHeight: true
                model: ruleHits
                clip: true
                spacing: s(1)

                delegate: RuleHitRow {
                    width: parent ? parent.width : 100
                    hit: modelData
                }
            }
            Text {
                text: "暂无规则命中记录"
                color: Const.tradingEmptyText
                font.pixelSize: s(9)
                visible: ruleHits.length === 0
                Layout.fillWidth: true
            }
        }
    }

    // ============ 内部组件 ============
    component StatusCard: Rectangle {
        property string l: ""; property string v: ""; property color vc: "#F1F5F9"
        Layout.fillWidth: true; Layout.fillHeight: true
        radius: 6; color: "#0d1728"; border.color: "#21354c"; border.width: 1

        ColumnLayout {
            anchors.fill: parent; anchors.margins: root.s(6); spacing: root.s(1)
            Text {
                text: parent.parent.l; color: "#64748B"; font.pixelSize: root.s(9)
                elide: Text.ElideRight; Layout.fillWidth: true
            }
            Text {
                text: parent.parent.v; color: parent.parent.vc; font.pixelSize: root.s(12)
                font.weight: Font.DemiBold; elide: Text.ElideRight; Layout.fillWidth: true
            }
        }
    }

    component RuleHitRow: RowLayout {
        property var hit: null
        Layout.fillWidth: true
        Layout.preferredHeight: root.s(20)
        spacing: root.s(4)

        // stage badge
        Rectangle {
            width: root.s(32); height: root.s(16); radius: 3
            color: "transparent"
            border.color: hit ? hitStageColor(hit) : "#334155"
            border.width: 1
            Text {
                anchors.centerIn: parent
                text: hit ? hitStageBadge(hit) : "--"
                color: hit ? hitStageColor(hit) : "#64748B"
                font.pixelSize: root.s(7); font.weight: Font.Medium
            }
        }
        Text {
            text: hit ? (String(hit.reasonCode || hit.ruleId || "规则命中")) : ""
            color: "#F1F5F9"
            font.pixelSize: root.s(9)
            font.weight: Font.DemiBold
            elide: Text.ElideRight
            Layout.preferredWidth: root.s(120)
        }
        Text {
            text: hit ? (String(hit.symbol || "") + " " + String(hit.detail || "")) : ""
            color: "#94A3B8"
            font.pixelSize: root.s(8)
            elide: Text.ElideRight
            Layout.fillWidth: true
        }
        Text {
            text: hit ? String(hit.time || "") : ""
            color: "#64748B"
            font.pixelSize: root.s(8)
        }
    }

    // ============ 辅助函数 ============
    function hitStageColor(hit) {
        if (!hit) return "#334155"
        var s = String(hit.stage || "")
        if (s.indexOf("Execution") >= 0 || s.indexOf("调度") >= 0) return "#F59E0B"
        if (s.indexOf("Risk") >= 0 || s.indexOf("风控") >= 0) return "#FB7185"
        if (s.indexOf("Broker") >= 0 || s.indexOf("券商") >= 0) return "#38BDF8"
        return "#94A3B8"
    }

    function hitStageBadge(hit) {
        if (!hit) return "--"
        var s = String(hit.stage || "")
        if (s.indexOf("Execution") >= 0 || s.indexOf("调度") >= 0) return "调度"
        if (s.indexOf("Risk") >= 0 || s.indexOf("风控") >= 0) return "风控"
        if (s.indexOf("Broker") >= 0 || s.indexOf("券商") >= 0) return "券商"
        return "规则"
    }
}
