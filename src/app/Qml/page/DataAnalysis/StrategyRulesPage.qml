// StrategyRulesPage.qml — 策略规则管理页面
import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import AStock.Bridge 1.0
import "../../components/DataAnalysis" as DAComponents
import "../../components" as Shared
import "../../components/Strategy/Creation" as StrategyCreation

Item {
    id: root
    anchors.fill: parent

    // ── 状态 ──
    property var templates: []
    property var aggregateStats: ({})
    property string selectedTemplateId: ""
    property var ruleAttribution: []
    property var selectedDetail: ({})
    property var selectedStats: ({})
    property var selectedParams: []
    property string filterPhase: "all"
    property string strategyFilterId: ""
    property string sortField: "displayName"
    property int sortOrder: Qt.AscendingOrder
    property bool isLoading: true
    property bool hasError: false
    property string errorMessage: ""
    property int currentPage: 1
    readonly property int pageSize: 20
    property int pendingRequestId: 0

    readonly property var phaseColors: ({
        "market": "#ef4444", "signal": "#3b82f6",
        "eligibility": "#10b981", "rebalance": "#f59e0b"
    })
    readonly property var phaseNames: ({
        "market": qsTr("市场环境"), "signal": qsTr("信号"),
        "eligibility": qsTr("资格"), "rebalance": qsTr("调仓")
    })
    function phaseLabel(p) { return phaseNames[p] || p }

    readonly property var filteredAndSorted: {
        var list = templates
        if (filterPhase !== "all") list = list.filter(function(t) { return t.phase === filterPhase })
        list = list.slice().sort(function(a, b) {
            var va = a[sortField] !== undefined ? a[sortField] : ""
            var vb = b[sortField] !== undefined ? b[sortField] : ""
            if (typeof va === "string") va = va.toLowerCase()
            if (typeof vb === "string") vb = vb.toLowerCase()
            if (va < vb) return sortOrder === Qt.AscendingOrder ? -1 : 1
            if (va > vb) return sortOrder === Qt.AscendingOrder ? 1 : -1
            return 0
        })
        return list
    }
    readonly property int totalPages: Math.max(1, Math.ceil(filteredAndSorted.length / pageSize))
    readonly property var pageTemplates: {
        var start = (currentPage - 1) * pageSize
        return filteredAndSorted.slice(start, start + pageSize)
    }

    Component.onCompleted: {
        templates = StrategyRuleStatsBridge.loadAllTemplates()
        isLoading = false
    }

    property bool _paramInit: false  // 防止初始化绑定触发保存

    onStrategyFilterIdChanged: refreshDetail()

    function selectTemplate(templateId) {
        selectedTemplateId = templateId
        _paramInit = true
        refreshDetail()
    }

    function refreshDetail() {
        if (!selectedTemplateId) return
        var tid = selectedTemplateId
        var reqId = ++pendingRequestId
        Qt.callLater(function() {
            if (reqId !== pendingRequestId) return
            try {
                selectedDetail = StrategyRuleStatsBridge.getTemplateDetail(tid)
                selectedStats = StrategyRuleStatsBridge.getTemplateStats(tid, strategyFilterId)
                selectedParams = StrategyRuleStatsBridge.extractTunableParams(tid)
                ruleAttribution = StrategyRuleStatsBridge.getRuleAttribution(strategyFilterId, tid)
                _paramInit = false
            } catch (e) { console.warn("[StrategyRules] 加载详情失败:", e); _paramInit = false }
        })
    }

    function toggleSort(field) {
        if (sortField === field) sortOrder = (sortOrder === Qt.AscendingOrder) ? Qt.DescendingOrder : Qt.AscendingOrder
        else { sortField = field; sortOrder = Qt.AscendingOrder }
    }
    function sortArrow(field) {
        if (sortField !== field) return ""
        return sortOrder === Qt.AscendingOrder ? " ▲" : " ▼"
    }
    function phaseBadgeColor(phase) { return phaseColors[phase] || "#64748B" }

    // ── UI ──
    Rectangle {
        anchors.fill: parent
        color: "#0F172A"

        ColumnLayout {
            anchors.fill: parent
            anchors.margins: 16
            spacing: 10

            // ── 顶部栏 ──
            RowLayout {
                Layout.fillWidth: true
                Text {
                    text: qsTr("策略规则管理"); font.pixelSize: 19; font.weight: Font.Bold
                    color: "#F8FAFC"
                }
                Item { Layout.fillWidth: true }
                Text { text: qsTr("策略:"); color: "#94A3B8"; font.pixelSize: 12 }
                ComboBox {
                    id: strategySelector
                    Layout.preferredWidth: 160
                    textRole: "name"
                    background: Rectangle { color: "#1E293B"; radius: 5; border.width: 1; border.color: "#334155" }
                    contentItem: Text { text: strategySelector.currentText || "选择策略"; color: "#F8FAFC"; font.pixelSize: 12; verticalAlignment: Text.AlignVCenter; leftPadding: 8 }
                    onCurrentIndexChanged: {
                        var m = strategySelector.model
                        if (currentIndex >= 0 && m && m[currentIndex]) {
                            strategyFilterId = m[currentIndex].strategyId || ""
                            refreshDetail()
                        }
                    }
                    Component.onCompleted: {
                        rebuildStrategyCombo()
                        if (strategySelector.model && strategySelector.model.length > 1)
                            strategySelector.currentIndex = 1
                    }

                    function rebuildStrategyCombo() {
                        var model = [{name: "全部策略", strategyId: ""}]
                        try {
                            var vm = StrategyBridge.listModel
                            if (vm && vm.count > 0) {
                                for (var i = 0; i < vm.count; i++) {
                                    var row = vm.getRow(i)
                                    model.push({name: row.strategyName || row.name || "", strategyId: row.strategyId || ""})
                                }
                            }
                        } catch (e) { console.warn("加载策略列表失败:", e) }
                        strategySelector.model = model
                    }
                }
                Shared.ButtonSmall { text: qsTr("刷新"); onClicked: { templates = StrategyRuleStatsBridge.loadAllTemplates() } }
            }

            // ── 阶段过滤 ──
            Row {
                Layout.fillWidth: true; spacing: 6
                Repeater {
                    model: [{ l: qsTr("全部"), p: "all" },{ l: qsTr("市场环境"), p: "market" },{ l: qsTr("信号"), p: "signal" },{ l: qsTr("资格"), p: "eligibility" },{ l: qsTr("调仓"), p: "rebalance" }]
                    delegate: Rectangle {
                        width: chipT.implicitWidth + 20; height: 28; radius: 14
                        color: filterPhase === modelData.p ? phaseBadgeColor(modelData.p) : "#1E293B"
                        border.width: 1; border.color: filterPhase === modelData.p ? phaseBadgeColor(modelData.p) : "#334155"
                        Text { id: chipT; anchors.centerIn: parent; text: modelData.l; font.pixelSize: 11; color: filterPhase === modelData.p ? "#FFF" : "#94A3B8" }
                        MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: { filterPhase = modelData.p; currentPage = 1 } }
                    }
                }
            }

            // ── 统计摘要 ──
            Row { Layout.fillWidth: true; spacing: 8
                DAComponents.RuleStatsCard { label: qsTr("总模板"); value: aggregateStats.totalTemplates || 0; cardColor: "#38BDF8" }
                DAComponents.RuleStatsCard { label: qsTr("已绑定"); value: aggregateStats.boundTemplates || 0; cardColor: "#10B981" }
                DAComponents.RuleStatsCard { label: qsTr("平均拦截率"); value: aggregateStats.avgBlockRate || -1; suffix: "%"; cardColor: "#EF4444" }
                DAComponents.RuleStatsCard { label: qsTr("平均胜率"); value: aggregateStats.avgWinRate || -1; suffix: "%"; cardColor: "#F59E0B" }
            }

            // ── 左右分栏（用 Item+anchors，不用 RowLayout 避免 polish loop）──
            Item {
                Layout.fillWidth: true
                Layout.fillHeight: true

                // 左侧面板
                Rectangle {
                    id: leftPanel
                    anchors.left: parent.left; anchors.top: parent.top; anchors.bottom: parent.bottom
                    width: parent.width * 0.62
                    color: "transparent"

                    ColumnLayout {
                        anchors.fill: parent; spacing: 4

                        // 表头
                        Rectangle {
                            Layout.fillWidth: true; height: 28; radius: 5; color: "#1E293B"
                            Row {
                                anchors.fill: parent; anchors.margins: 6
                                Repeater {
                                    model: [
                                        { f: "templateId", l: qsTr("模板ID"), w: 0.16 },
                                        { f: "displayName", l: qsTr("名称"), w: 0.20 },
                                        { f: "phase", l: qsTr("阶段"), w: 0.10 },
                                        { f: "rulesCount", l: qsTr("规则"), w: 0.06 },
                                        { f: "", l: qsTr("评估/命中/拦截率"), w: 0.28 },
                                        { f: "", l: "", w: 0.20 }
                                    ]
                                    delegate: Item {
                                        width: parent.width * modelData.w; height: parent.height
                                        Text {
                                            anchors.verticalCenter: parent.verticalCenter
                                            text: modelData.l + root.sortArrow(modelData.f); font.pixelSize: 10; font.weight: Font.DemiBold
                                            color: "#94A3B8"
                                        }
                                        MouseArea {
                                            anchors.fill: parent; enabled: modelData.f !== ""
                                            cursorShape: enabled ? Qt.PointingHandCursor : Qt.ArrowCursor
                                            onClicked: root.toggleSort(modelData.f)
                                        }
                                    }
                                }
                            }
                        }

                        // 列表
                        ListView {
                            id: templateList
                            Layout.fillWidth: true; Layout.fillHeight: true
                            model: pageTemplates; spacing: 1; clip: true
                            delegate: Rectangle {
                                width: templateList.width; height: 30; radius: 3
                                color: selectedTemplateId === modelData.templateId ? "#1E3A5F" : "transparent"
                                border.width: selectedTemplateId === modelData.templateId ? 1 : 0; border.color: "#3B82F6"

                                Row {
                                    anchors.fill: parent; anchors.margins: 4
                                    Text { width: parent.width * 0.16; text: modelData.templateId || ""; font.pixelSize: 10; color: "#CBD5E1"; elide: Text.ElideRight; verticalAlignment: Text.AlignVCenter }
                                    Text { width: parent.width * 0.20; text: modelData.displayName || ""; font.pixelSize: 10; color: "#F8FAFC"; elide: Text.ElideRight; verticalAlignment: Text.AlignVCenter }
                                    Rectangle {
                                        width: parent.width * 0.10; height: 20; radius: 3; color: Qt.rgba(0,0,0,0)
                                        anchors.verticalCenter: parent.verticalCenter
                                        Text { anchors.centerIn: parent; text: phaseLabel(modelData.phase); font.pixelSize: 9; color: phaseBadgeColor(modelData.phase) }
                                    }
                                    Text { width: parent.width * 0.06; text: modelData.rulesCount || ""; font.pixelSize: 10; color: "#94A3B8"; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter }
                                    Text {
                                        width: parent.width * 0.28; font.pixelSize: 10; color: "#94A3B8"; verticalAlignment: Text.AlignVCenter
                                        text: {
                                            var s = StrategyRuleStatsBridge.getTemplateStats(modelData.templateId, "")
                                            var ev = s.evaluated || 0; var ht = s.hits || 0
                                            var br = s.blockRate >= 0 ? (s.blockRate*100).toFixed(0) + "%" : "无"
                                            return ev + "/" + ht + "/" + br
                                        }
                                    }
                                    Text {
                                        width: parent.width * 0.20; visible: selectedTemplateId === modelData.templateId
                                        text: "◀ 已选"; font.pixelSize: 9; color: "#3B82F6"; verticalAlignment: Text.AlignVCenter
                                    }
                                }

                                // 点击 — 在 Row 之后（上层）
                                MouseArea {
                                    anchors.fill: parent
                                    onClicked: root.selectTemplate(modelData.templateId)
                                }
                            }
                        }

                        // 分页
                        DAComponents.RulePaginationBar {
                            Layout.fillWidth: true
                            currentPage: root.currentPage; totalPages: root.totalPages
                            onPageChanged: function(p) { root.currentPage = p }
                        }
                    }
                }

                // 右侧面板
                Rectangle {
                    id: rightPanel
                    anchors.right: parent.right; anchors.top: parent.top; anchors.bottom: parent.bottom
                    width: parent.width * 0.36
                    color: "#1E293B"; radius: 6; border.width: 1; border.color: "#334155"

                    // 空态
                    Item {
                        anchors.fill: parent
                        visible: selectedTemplateId === ""
                        Text { anchors.centerIn: parent; text: qsTr("← 点击左侧模板查看详情"); font.pixelSize: 13; color: "#64748B" }
                    }

                    // 详情 (单列滚动)
                    Flickable {
                        id: detailFlick
                        anchors.fill: parent; anchors.margins: 10
                        visible: selectedTemplateId !== ""
                        contentHeight: detailCol.implicitHeight; clip: true; boundsBehavior: Flickable.StopAtBounds
                        ScrollBar.vertical: ScrollBar {}

                        Column {
                            id: detailCol; width: detailFlick.width; spacing: 8

                            RowLayout {
                                width: parent.width
                                Text { text: selectedDetail.displayName || ""; font.pixelSize: 14; font.weight: Font.Bold; color: "#F8FAFC"; elide: Text.ElideRight; Layout.fillWidth: true }
                                Rectangle { Layout.preferredWidth: 40; Layout.preferredHeight: 20; radius: 3; color: "#1E293B"
                                    Text { anchors.centerIn: parent; text: "刷新"; color: "#94A3B8"; font.pixelSize: 10 }
                                    MouseArea { anchors.fill: parent; onClicked: refreshDetail() }
                                }
                            }
                            Text { width: parent.width; text: selectedDetail.summary || ""; font.pixelSize: 11; color: "#94A3B8"; wrapMode: Text.WordWrap; maximumLineCount: 3; elide: Text.ElideRight }

                            Rectangle { width: parent.width; height: 1; color: "#334155" }

                            Row { spacing: 6
                                DAComponents.RuleStatsCard { label: qsTr("评估"); value: selectedStats.evaluated || 0; cardColor: "#38BDF8" }
                                DAComponents.RuleStatsCard { label: qsTr("命中"); value: selectedStats.hits || 0; cardColor: "#10B981" }
                                DAComponents.RuleStatsCard { label: qsTr("拦截率"); value: selectedStats.blockRate !== undefined ? selectedStats.blockRate : -1; suffix: "%"; cardColor: "#EF4444" }
                                DAComponents.RuleStatsCard { label: qsTr("胜率"); value: selectedStats.winRate !== undefined ? selectedStats.winRate : -1; suffix: "%"; cardColor: "#F59E0B" }
                            }

                            Rectangle { width: parent.width; height: 1; color: "#334155" }

                            // 规则结构
                            StrategyCreation.RuleTemplateStructureView {
                                width: parent.width; compact: true; showTemplateHeader: false
                                bindingData: {
                                    var d = selectedDetail || {}
                                    return { templateId: d.templateId || "", templateName: d.displayName || "", templateDescription: d.summary || "", phase: d.phase || "", rules: d.rules || [] }
                                }
                            }

                            Rectangle { width: parent.width; height: 1; color: "#334155" }

                            // 参数编辑器
                            DAComponents.RuleParamEditor {
                                width: parent.width
                                params: selectedParams; editable: true
                                onParamChanged: {
                                    var cfg = {}; cfg[key] = value
                                    StrategyRuleStatsBridge.updateTemplateParams(selectedTemplateId, cfg, strategyFilterId)
                                }
                            }

                            Rectangle { width: parent.width; height: 1; color: "#334155" }

                            // ── 规则归因 ──
                            RowLayout {
                                width: parent.width
                                Text { text: qsTr("归因"); font.pixelSize: 12; font.weight: Font.DemiBold; color: "#F8FAFC"; Layout.fillWidth: true }
                                Text {
                                    text: ruleAttribution.length > 0 && ruleAttribution[0].dateRange ? ruleAttribution[0].dateRange : ""
                                    color: "#64748B"; font.pixelSize: 9
                                }
                            }

                            // 表头
                            RowLayout {
                                width: parent.width
                                Text { text: ""; Layout.preferredWidth: 60 }
                                Text { text: "评估"; color: "#38BDF8"; font.pixelSize: 9; Layout.preferredWidth: 40 }
                                Text { text: "命中"; color: "#10B981"; font.pixelSize: 9; Layout.preferredWidth: 35 }
                                Text { text: "拦截盈"; color: "#F59E0B"; font.pixelSize: 9; Layout.preferredWidth: 50 }
                                Text { text: "出场盈"; color: "#EF4444"; font.pixelSize: 9; Layout.preferredWidth: 50 }
                                Text { text: "挑顶"; color: "#94A3B8"; font.pixelSize: 9; Layout.preferredWidth: 30 }
                                Text { text: "卖飞"; color: "#94A3B8"; font.pixelSize: 9; Layout.preferredWidth: 30 }
                            }

                            Repeater {
                                model: ruleAttribution
                                delegate: Column {
                                    width: parent.width; spacing: 2
                                    RowLayout {
                                        width: parent.width
                                        Text { text: (modelData.ruleId || "").substring(0, 20); color: "#94A3B8"; font.pixelSize: 9; Layout.preferredWidth: 60; elide: Text.ElideRight }
                                        Text { text: modelData.evaluated || 0; color: "#38BDF8"; font.pixelSize: 9; Layout.preferredWidth: 40 }
                                        Text { text: modelData.hits || 0; color: "#10B981"; font.pixelSize: 9; Layout.preferredWidth: 35 }
                                        Text {
                                            text: (modelData.preventedPnL||0) >= 0 ? "+" + (modelData.preventedPnL||0).toFixed(1) : (modelData.preventedPnL||0).toFixed(1)
                                            color: (modelData.preventedPnL||0) >= 0 ? "#10B981" : "#EF4444"; font.pixelSize: 9; Layout.preferredWidth: 50
                                        }
                                        Text {
                                            text: (modelData.exitPnL||0) >= 0 ? "+" + (modelData.exitPnL||0).toFixed(1) : (modelData.exitPnL||0).toFixed(1)
                                            color: (modelData.exitPnL||0) >= 0 ? "#10B981" : "#EF4444"; font.pixelSize: 9; Layout.preferredWidth: 50
                                        }
                                        Text { text: modelData.topBoughtCount || 0; color: "#F59E0B"; font.pixelSize: 9; Layout.preferredWidth: 30 }
                                        Text { text: modelData.missedGainCount || 0; color: "#EF4444"; font.pixelSize: 9; Layout.preferredWidth: 30 }
                                        Text {
                                            text: (modelData.netContribution||0) >= 0 ? "✅" : ((modelData.netContribution||0) < -2 ? "❌" : "⚪")
                                            color: (modelData.netContribution||0) >= 0 ? "#10B981" : "#EF4444"; font.pixelSize: 10; Layout.preferredWidth: 16
                                        }
                                    }
                                    Rectangle { width: parent.width; height: 1; color: "#1E293B" }
                                }
                            }
                            Text { visible: ruleAttribution.length === 0; text: qsTr("选中策略后显示归因数据"); color: "#64748B"; font.pixelSize: 10 }
                        }
                    }
                }
            }
        }

        // ── 加载/错误/空 遮罩 ──
        Item {
            anchors.fill: parent
            visible: isLoading || hasError
            Rectangle { anchors.fill: parent; color: "#0F172A" }
            Column { anchors.centerIn: parent; spacing: 10
                BusyIndicator { anchors.horizontalCenter: parent.horizontalCenter; visible: isLoading }
                Text { anchors.horizontalCenter: parent.horizontalCenter; visible: isLoading; text: qsTr("加载中..."); color: "#94A3B8" }
                Text { anchors.horizontalCenter: parent.horizontalCenter; visible: !isLoading && hasError; text: "⚠️ " + root.errorMessage; color: "#FCA5A5" }
                Shared.ButtonSmall { anchors.horizontalCenter: parent.horizontalCenter; visible: !isLoading && hasError; text: qsTr("重试")
                    onClicked: { templates = StrategyRuleStatsBridge.loadAllTemplates(); isLoading = false; hasError = false }
                }
            }
        }
    }
}
