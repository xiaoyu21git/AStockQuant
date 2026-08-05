// CleaningRulesPage.qml — 清洗规则管理页面
import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import AStock.Bridge 1.0
import "../../components/DataAnalysis" as DAComponents
import "../../components" as Shared

Item {
    id: root
    anchors.fill: parent

    // ── 状态 ──
    property var allRules: []
    property var userConfig: ({})
    property var cleaningStats: ({})
    property bool isLoading: true
    property bool hasError: false
    property string errorMessage: ""
    property int currentPage: 1
    readonly property int pageSize: 15
    property string filterLevel: "all"    // all/必选/推荐/可选
    property string searchText: ""

    readonly property var levelColors: ({
        "必选": "#ef4444",
        "推荐": "#f59e0b",
        "可选": "#3b82f6"
    })

    // ── 计算属性 ──
    readonly property var filteredRules: {
        var list = allRules
        if (filterLevel !== "all") {
            list = list.filter(function(r) { return r.ruleLevel === filterLevel })
        }
        if (searchText !== "") {
            list = list.filter(function(r) {
                return r.ruleName.indexOf(searchText) >= 0 || r.ruleId.indexOf(searchText) >= 0
            })
        }
        // 按等级排序: 必选 → 推荐 → 可选
        var levelOrder = { "必选": 0, "推荐": 1, "可选": 2 }
        list = list.slice().sort(function(a, b) {
            var la = levelOrder[a.ruleLevel] !== undefined ? levelOrder[a.ruleLevel] : 99
            var lb = levelOrder[b.ruleLevel] !== undefined ? levelOrder[b.ruleLevel] : 99
            if (la !== lb) return la - lb
            return a.executionOrder - b.executionOrder
        })
        return list
    }
    readonly property int totalPages: Math.max(1, Math.ceil(filteredRules.length / pageSize))
    readonly property var pageRules: {
        var start = (currentPage - 1) * pageSize
        return filteredRules.slice(start, start + pageSize)
    }
    readonly property int enabledCount: {
        var count = 0
        for (var i = 0; i < allRules.length; i++) {
            var rid = allRules[i].ruleId
            if (userConfig.hasOwnProperty(rid) ? userConfig[rid] : allRules[i].defaultValue)
                count++
        }
        return count
    }

    // ── 初始化 ──
    Component.onCompleted: {
        loadRules()
    }

    function loadRules() {
        var defaults = [
            { ruleId: "fieldStandardization", ruleName: qsTr("字段标准化"), icon: "📋", cardColor: "#10b981", defaultValue: true, ruleLevel: qsTr("必选"), executionOrder: 0, description: qsTr("规范化字段格式、日期与元数据") },
            { ruleId: "completeness", ruleName: qsTr("完整性校验"), icon: "✅", cardColor: "#10b981", defaultValue: true, ruleLevel: qsTr("必选"), executionOrder: 5, description: qsTr("确保 symbol 和 trade_date 非空") },
            { ruleId: "duplicateRemoval", ruleName: qsTr("重复数据删除"), icon: "🗑️", cardColor: "#f97316", defaultValue: true, ruleLevel: qsTr("推荐"), executionOrder: 10, description: qsTr("按关键字段去重，避免重复记录") },
            { ruleId: "financialDateValidity", ruleName: qsTr("财务日期有效性"), icon: "🗓️", cardColor: "#06b6d4", defaultValue: true, ruleLevel: qsTr("推荐"), executionOrder: 15, description: qsTr("清洗无效的报告/披露日期") },
            { ruleId: "financialMetricSanitize", ruleName: qsTr("财务指标净化"), icon: "📈", cardColor: "#14b8a6", defaultValue: true, ruleLevel: qsTr("推荐"), executionOrder: 20, description: qsTr("清除 NaN/Inf/负值财务指标") },
            { ruleId: "reportDateAlignment", ruleName: qsTr("财报日期对齐"), icon: "📅", cardColor: "#22c55e", defaultValue: true, ruleLevel: qsTr("推荐"), executionOrder: 25, description: qsTr("将财报日期对齐到交易日历") },
            { ruleId: "survivorBias", ruleName: qsTr("生存者偏差处理"), icon: "🧬", cardColor: "#14b8a6", defaultValue: true, ruleLevel: qsTr("推荐"), executionOrder: 30, description: qsTr("删除已退市标的的退市后记录") },
            { ruleId: "suspensionFill", ruleName: qsTr("停牌填充"), icon: "⏸️", cardColor: "#6366f1", defaultValue: true, ruleLevel: qsTr("推荐"), executionOrder: 35, description: qsTr("停牌期间前向填充 OHLC 价格") },
            { ruleId: "missingValueFill", ruleName: qsTr("缺失值处理"), icon: "🔍", cardColor: "#ec4899", defaultValue: true, ruleLevel: qsTr("推荐"), executionOrder: 40, description: qsTr("前向填充可配置的最大回溯天数") },
            { ruleId: "adjustedPrice", ruleName: qsTr("价格复权"), icon: "🔁", cardColor: "#8b5cf6", defaultValue: true, ruleLevel: qsTr("推荐"), executionOrder: 45, description: qsTr("应用复权因子调整 OHLC 价格") },
            { ruleId: "newStockFilter", ruleName: qsTr("新股过滤"), icon: "🆕", cardColor: "#0ea5e9", defaultValue: false, ruleLevel: qsTr("可选"), executionOrder: 50, description: qsTr("过滤上市不足 N 日的新股") },
            { ruleId: "stFilter", ruleName: qsTr("ST 过滤"), icon: "⚠️", cardColor: "#ef4444", defaultValue: false, ruleLevel: qsTr("可选"), executionOrder: 55, description: qsTr("移除 ST/*ST 状态标的") },
            { ruleId: "priceValidity", ruleName: qsTr("价格有效性"), icon: "📊", cardColor: "#8b5cf6", defaultValue: true, ruleLevel: qsTr("推荐"), executionOrder: 60, description: qsTr("OHLC 链校验 + 价格区间过滤") },
            { ruleId: "volumeFilter", ruleName: qsTr("成交量过滤"), icon: "📉", cardColor: "#f59e0b", defaultValue: true, ruleLevel: qsTr("推荐"), executionOrder: 65, description: qsTr("按成交量范围过滤，允许停牌零量") },
            { ruleId: "limitMoveTag", ruleName: qsTr("涨跌停标记"), icon: "🏷️", cardColor: "#f59e0b", defaultValue: true, ruleLevel: qsTr("推荐"), executionOrder: 70, description: qsTr("标记涨停/跌停，标记可买/可卖") },
            { ruleId: "valuationSanitize", ruleName: qsTr("估值净化"), icon: "🧮", cardColor: "#06b6d4", defaultValue: true, ruleLevel: qsTr("推荐"), executionOrder: 75, description: qsTr("清除无效 PE/PB/市值") }
        ]

        allRules = defaults
        userConfig = DataCleaningServiceRefactored.loadUserRuleConfig()
    }

    function loadStats() {
        var stats = DataCleaningServiceRefactored.getLatestCleaningStats()
        if (stats && Object.keys(stats).length > 0) {
            cleaningStats = stats
        } else {
            cleaningStats = {}
        }
    }

    function isRuleEnabled(ruleId) {
        if (userConfig.hasOwnProperty(ruleId)) return userConfig[ruleId]
        for (var i = 0; i < allRules.length; i++) {
            if (allRules[i].ruleId === ruleId) return allRules[i].defaultValue
        }
        return false
    }

    function toggleRule(ruleId, enabled) {
        var cfg = {}
        for (var key in userConfig) cfg[key] = userConfig[key]
        cfg[ruleId] = enabled
        for (var j = 0; j < allRules.length; j++) {
            if (allRules[j].ruleId === ruleId && allRules[j].defaultValue === enabled) {
                delete cfg[ruleId]
                break
            }
        }
        userConfig = cfg
        var ok = DataCleaningServiceRefactored.saveUserRuleConfig(cfg)
        if (!ok) {
            userConfig = DataCleaningServiceRefactored.loadUserRuleConfig()
            console.warn("[CleaningRulesPage] 保存配置失败，已回滚")
        }
        root.enabledCount
    }

    function resetToDefaults() {
        DataCleaningServiceRefactored.saveUserRuleConfig({})
        userConfig = {}
        loadRules()
    }

    function getPassRate() {
        var total = cleaningStats.totalRecords || 0
        var cleaned = cleaningStats.cleanedRecords || 0
        if (total === 0) return -1
        return cleaned / total
    }

    // ── 颜色辅助 ──
    function levelColor(level) {
        return levelColors[level] || "#64748B"
    }

    // ── UI ──
    Rectangle {
        anchors.fill: parent
        color: "#0F172A"

        ColumnLayout {
            anchors.fill: parent
            anchors.margins: 20
            spacing: 16

            // 标题栏
            RowLayout {
                Layout.fillWidth: true
                Text {
                    text: qsTr("清洗规则管理")
                    font.pixelSize: 20
                    font.weight: Font.Bold
                    color: "#F8FAFC"
                }
                Item { Layout.fillWidth: true }
                Shared.ButtonSmall {
                    text: qsTr("刷新")
                    onClicked: reload()
                }
            }

            // 统计摘要行
            Row {
                Layout.fillWidth: true
                spacing: 12
                DAComponents.RuleStatsCard {
                    label: qsTr("总规则"); value: allRules.length; suffix: ""; cardColor: "#38BDF8"
                }
                DAComponents.RuleStatsCard {
                    label: qsTr("已启用"); value: root.enabledCount; suffix: ""; cardColor: "#10B981"
                }
                DAComponents.RuleStatsCard {
                    label: qsTr("上次清洗行数")
                    value: cleaningStats.totalRecords || 0
                    suffix: qsTr("行")
                    cardColor: "#F59E0B"
                    isLoading: isLoading
                }
                DAComponents.RuleStatsCard {
                    label: qsTr("通过率")
                    value: getPassRate()
                    suffix: "%"
                    cardColor: "#8B5CF6"
                    tooltipText: qsTr("清洗后保留记录 / 原始记录")
                }
            }

            // 过滤栏
            RowLayout {
                Layout.fillWidth: true
                spacing: 8

                TextField {
                    id: searchField
                    Layout.preferredWidth: 200
                    placeholderText: qsTr("搜索规则...")
                    placeholderTextColor: "#64748B"
                    color: "#F8FAFC"
                    background: Rectangle {
                        color: "#1E293B"; radius: 6
                        border.width: 1; border.color: "#334155"
                    }
                    onTextChanged: { searchText = text; currentPage = 1 }
                }

                Repeater {
                    model: [
                        { label: qsTr("全部"), level: "all" },
                        { label: qsTr("必选"), level: qsTr("必选") },
                        { label: qsTr("推荐"), level: qsTr("推荐") },
                        { label: qsTr("可选"), level: qsTr("可选") }
                    ]
                    delegate: Rectangle {
                        width: labelText.implicitWidth + 24; height: 32; radius: 16
                        color: filterLevel === modelData.level ? "#3B82F6" : "#1E293B"
                        border.width: 1; border.color: filterLevel === modelData.level ? "#3B82F6" : "#334155"
                        Text {
                            id: labelText
                            anchors.centerIn: parent
                            text: modelData.label
                            font.pixelSize: 12
                            color: filterLevel === modelData.level ? "#FFFFFF" : "#94A3B8"
                        }
                        MouseArea {
                            anchors.fill: parent
                            cursorShape: Qt.PointingHandCursor
                            onClicked: { filterLevel = modelData.level; currentPage = 1 }
                        }
                    }
                }
            }

            // 规则卡片列表
            Item {
                id: cardListContainer
                Layout.fillWidth: true
                Layout.fillHeight: true

                ScrollView {
                    id: cardScroll
                    anchors.fill: parent
                    clip: true
                    ScrollBar.vertical.policy: ScrollBar.AsNeeded

                    // 用 Flow 自适应排列，卡片宽度根据可用空间动态计算
                    Flow {
                        id: cardFlow
                        width: cardScroll.width - 16
                        spacing: 10

                        readonly property int cols: Math.max(1, Math.floor(width / 260))
                        readonly property real cardW: (width - spacing * (cols - 1)) / cols

                        Repeater {
                        model: pageRules
                        delegate: Rectangle {
                            width: cardFlow.cardW; height: 130
                            radius: 8
                            color: "#1E293B"
                            border.width: 1
                            border.color: isRuleEnabled(modelData.ruleId) ? "#10B981" : "#334155"

                            ColumnLayout {
                                anchors.fill: parent
                                anchors.margins: 12
                                spacing: 6

                                // 第一行: 图标 + 名称 + Switch
                                RowLayout {
                                    Layout.fillWidth: true
                                    Text { text: modelData.icon; font.pixelSize: 18 }
                                    Text {
                                        text: modelData.ruleName
                                        font.pixelSize: 13; font.weight: Font.DemiBold
                                        color: "#F8FAFC"
                                        Layout.fillWidth: true
                                        elide: Text.ElideRight
                                    }
                                    Switch {
                                        checked: isRuleEnabled(modelData.ruleId)
                                        onToggled: root.toggleRule(modelData.ruleId, checked)
                                    }
                                }

                                // 第二行: 执行顺序 + 等级徽章
                                RowLayout {
                                    spacing: 6
                                    Rectangle {
                                        radius: 4; width: 40; height: 20
                                        color: "#334155"
                                        Text {
                                            anchors.centerIn: parent
                                            text: "#" + modelData.executionOrder
                                            font.pixelSize: 10; color: "#94A3B8"
                                        }
                                    }
                                    Rectangle {
                                        radius: 4
                                        width: levelBadge.implicitWidth + 12; height: 20
                                        color: Qt.rgba(
                                            levelColor(modelData.ruleLevel).r,
                                            levelColor(modelData.ruleLevel).g,
                                            levelColor(modelData.ruleLevel).b,
                                            0.2
                                        )
                                        Text {
                                            id: levelBadge
                                            anchors.centerIn: parent
                                            text: modelData.ruleLevel
                                            font.pixelSize: 10; font.weight: Font.Medium
                                            color: levelColor(modelData.ruleLevel)
                                        }
                                    }
                                }

                                // 第三行: 描述
                                Text {
                                    Layout.fillWidth: true
                                    text: modelData.description
                                    font.pixelSize: 11; color: "#94A3B8"
                                    elide: Text.ElideRight; maximumLineCount: 2
                                    wrapMode: Text.WordWrap
                                }
                            }
                        }
                    }
                }
                }
            }

            // 底栏: 分页 + 操作
            RowLayout {
                Layout.fillWidth: true
                spacing: 12

                DAComponents.RulePaginationBar {
                    currentPage: root.currentPage
                    totalPages: root.totalPages
                    onPageChanged: { root.currentPage = page }
                }

                Item { Layout.fillWidth: true }

                Shared.ButtonSecondary {
                    text: qsTr("恢复默认")
                    onClicked: {
                        root.resetToDefaults()
                        root.reload()
                    }
                }
                Shared.ButtonPrimary {
                    text: qsTr("保存配置")
                    onClicked: {
                        if (!hasBridge()) return
                        var ok = DataCleaningServiceRefactored.saveUserRuleConfig(userConfig)
                        if (ok) console.log("[CleaningRulesPage] 配置已保存")
                        else console.warn("[CleaningRulesPage] 保存失败")
                    }
                }
            }
        }

        // ── 空态 / 错误态 / 加载态 ──
        Item {
            anchors.fill: parent
            visible: isLoading || hasError || allRules.length === 0

            Rectangle {
                anchors.fill: parent; color: "#0F172A"; opacity: 0.9
            }

            Column {
                anchors.centerIn: parent
                spacing: 12

                BusyIndicator {
                    anchors.horizontalCenter: parent.horizontalCenter
                    visible: isLoading
                }

                Text {
                    anchors.horizontalCenter: parent.horizontalCenter
                    visible: isLoading
                    text: qsTr("加载中...")
                    color: "#94A3B8"; font.pixelSize: 14
                }

                Text {
                    anchors.horizontalCenter: parent.horizontalCenter
                    visible: !isLoading && hasError
                    text: "⚠️"
                    font.pixelSize: 32
                }

                Text {
                    anchors.horizontalCenter: parent.horizontalCenter
                    visible: !isLoading && hasError
                    text: root.errorMessage
                    color: "#FCA5A5"; font.pixelSize: 14
                    horizontalAlignment: Text.AlignHCenter
                }

                Shared.ButtonSmall {
                    anchors.horizontalCenter: parent.horizontalCenter
                    visible: !isLoading && hasError
                    text: qsTr("重试")
                    onClicked: root.reload()
                }

                Text {
                    anchors.horizontalCenter: parent.horizontalCenter
                    visible: !isLoading && !hasError && allRules.length === 0
                    text: qsTr("暂无清洗规则")
                    color: "#64748B"; font.pixelSize: 16
                }
            }
        }
    }
}
