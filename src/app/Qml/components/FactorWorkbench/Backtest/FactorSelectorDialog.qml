import QtQuick 2.15
import QtQuick.Layouts 1.15
import QtQuick.Controls 2.15
import QtQml 2.15
import AStock.Bridge 1.0 as Bridge

Dialog {
    id: root

    property Bridge.FactorBacktestController factorBacktestController: null
    property var factorService: null
    property Bridge.FactorViewModel factorViewModel: null
    property var selectedFactorIds: []
    property string dataSourceMode: "cache"
    property bool requireSupportValidation: true
    property bool supportMapRequested: false
    property bool supportMapLoading: false
    property var factorSupportMap: ({})
    property var supportMapRefreshCallback: null

    property var allFactors: []
    property var filteredFactors: []
    property string activeFactorId: ""
    property string searchText: ""
    property bool selectedOnlyFilter: false
    property var factorDetailCache: ({})

    readonly property var activeFactorRecord: factorRecord(activeFactorId)
    readonly property var activeFactorDetail: factorDetail(activeFactorId)
    readonly property int visibleSupportedFactorCount: countVisibleSupportedFactors()
    readonly property bool allVisibleSupportedSelected: visibleSupportedFactorCount > 0
        && countVisibleSelectedSupportedFactors() === visibleSupportedFactorCount

    signal factorsSelected(var factorIds)
    signal dialogClosed()

    modal: true
    closePolicy: Popup.NoAutoClose
    width: 1080
    height: 700
    x: parent ? Math.round((parent.width - width) / 2) : 0
    y: parent ? Math.round((parent.height - height) / 2) : 0
    padding: 0

    background: Rectangle {
        radius: 18
        color: "#08111f"
        border.width: 1
        border.color: "#1e293b"
    }

    function normalizedFactorIdKey(factorId) {
        return String(factorId === undefined || factorId === null ? "" : factorId).trim()
    }

    function isSelectedFactor(factorId) {
        var normalizedId = normalizedFactorIdKey(factorId)
        return !!normalizedId && selectedFactorIds.indexOf(normalizedId) !== -1
    }

    function normalizeSymbolList(source) {
        var input = []
        if (Array.isArray(source)) {
            input = source
        } else if (typeof source === "string" && String(source).trim() !== "") {
            input = String(source).split(/[\s,;，；]+/)
        }

        var normalized = []
        var seen = ({})
        for (var index = 0; index < input.length; ++index) {
            var symbol = String(input[index] === undefined || input[index] === null ? "" : input[index]).trim()
            if (!symbol || seen[symbol]) {
                continue
            }
            seen[symbol] = true
            normalized.push(symbol)
        }
        return normalized
    }

    function supportInfoForFactor(factorId) {
        if (!requireSupportValidation) {
            return {
                supported: true,
                requiredFields: [],
                missingFields: [],
                reason: ""
            }
        }

        var factorKey = normalizedFactorIdKey(factorId)
        if (factorSupportMap && factorSupportMap[factorKey] !== undefined) {
            return factorSupportMap[factorKey]
        }

        if (supportMapLoading) {
            return {
                supported: false,
                requiredFields: [],
                missingFields: [],
                reason: "支持图加载中"
            }
        }

        return {
            supported: false,
            requiredFields: [],
            missingFields: [],
            reason: supportMapRequested ? "校验结果未返回" : "请先点击开始校验"
        }
    }

    function isFactorSupported(factorId) {
        return supportInfoForFactor(factorId).supported !== false
    }

    function factorUiMetaFor(typeValue) {
        var numericType = Number(typeValue)
        if (!isFinite(numericType) || numericType < 0) {
            return ({})
        }
        return Bridge.FactorMetaService.getFactorUiMeta(numericType) || ({})
    }

    function factorCategoryColor(typeValue) {
        var meta = factorUiMetaFor(typeValue)
        if (meta.color !== undefined && meta.color !== null) {
            return Qt.color(String(meta.color))
        }
        return Qt.color("#94A3B8")
    }

    function factorCategoryLabel(typeValue) {
        var meta = factorUiMetaFor(typeValue)
        return meta.displayName !== undefined && meta.displayName !== null ? String(meta.displayName) : "未分类"
    }

    function factorRecord(factorId) {
        var normalizedId = normalizedFactorIdKey(factorId)
        if (!normalizedId) {
            return ({})
        }

        for (var index = 0; index < allFactors.length; ++index) {
            if (normalizedFactorIdKey(allFactors[index].factorId) === normalizedId) {
                return allFactors[index]
            }
        }
        return ({})
    }

    function factorDetail(factorId) {
        var normalizedId = normalizedFactorIdKey(factorId)
        if (!normalizedId || !factorService || typeof factorService.getFactorById !== "function") {
            return ({})
        }
        if (factorDetailCache[normalizedId] !== undefined) {
            return factorDetailCache[normalizedId]
        }

        // 不在属性绑定内修改 factorDetailCache，避免 QML 检测到绑定循环
        var detail = factorService.getFactorById(normalizedId) || ({})
        factorDetailCache[normalizedId] = detail
        return detail
    }

    function factorDisplayName(record, detail) {
        var result = String((detail && (detail.displayName || detail.factorName || detail.name))
                            || (record && (record.displayName || record.factorName || record.name))
                            || normalizedFactorIdKey(record && record.factorId)).trim()
        return result || "未命名因子"
    }

    function factorDescription(record, detail) {
        return String((detail && detail.description) || (record && record.description) || "暂无描述").trim()
    }

    function factorCoreRating(record, detail) {
        var rating = Number((detail && detail.coreRating) || (record && record.coreRating) || 0)
        return isFinite(rating) && rating > 0 ? Math.round(rating) : 0
    }

    function factorCoreRatingLabel(record, detail) {
        var rating = factorCoreRating(record, detail)
        switch (rating) {
        case 3: return "优秀"
        case 2: return "良好"
        case 1: return "合格"
        default: return "不合格"
        }
    }

    function factorCoreRatingColor(value) {
        var rating = Number(value)
        if (!isFinite(rating)) rating = 0
        switch (rating) {
        case 3: return "#10B981"
        case 2: return "#38BDF8"
        case 1: return "#F59E0B"
        default: return "#EF4444"
        }
    }

    function hasBacktestMetrics(record) {
        var ic = Number((record && record.icValue) || 0)
        var ir = Number((record && record.irValue) || 0)
        return isFinite(ic) && isFinite(ir) && (Math.abs(ic) > 1e-9 || Math.abs(ir) > 1e-9)
    }

    function factorEffectiveRangeText(detail) {
        var startDate = String((detail && (detail.actualStartDate || detail.effectiveStartDate)) || "").trim()
        var endDate = String((detail && detail.effectiveEndDate) || "").trim()
        if (startDate && endDate) {
            return startDate + " 至 " + endDate
        }
        if (startDate || endDate) {
            return startDate || endDate
        }
        return "未记录"
    }

    function factorWarmupText(detail) {
        var days = Number(detail && detail.warmupTrimmedTradingDays)
        if (!isFinite(days) || days < 0) {
            return "未记录"
        }
        return Math.round(days) + " 天"
    }

    function factorStatusText(record, detail) {
        var rawStatus = String((detail && detail.status) || (record && record.status) || "").trim()
        return rawStatus || "状态未知"
    }

    function previewSymbolsText(symbols, maxCount) {
        var list = normalizeSymbolList(symbols)
        if (list.length === 0) {
            return "未记录"
        }

        var limit = Math.max(1, Number(maxCount) || 6)
        var preview = list.slice(0, limit).join("、")
        return list.length > limit ? (preview + " 等" + list.length + "只") : preview
    }

    function strategyPoolSummaryText() {
        return "策略回测标的在启动前由缓存数据集决定"
    }

    function strategyPoolHintText(detail) {
        return "因子选择只影响排序/打分，不携带任何股票池信息。"
    }

    function matchesSearch(record) {
        var keyword = String(searchText || "").trim().toLowerCase()
        if (!keyword) {
            return true
        }

        var haystacks = [
            record.factorId,
            record.factorName,
            record.displayName,
            record.majorCategory,
            record.subCategory,
            record.description
        ]
        for (var index = 0; index < haystacks.length; ++index) {
            var haystack = String(haystacks[index] || "").toLowerCase()
            if (haystack.indexOf(keyword) !== -1) {
                return true
            }
        }
        return false
    }

    function rebuildAllFactors() {
        var nextFactors = []
        if (factorViewModel && typeof factorViewModel.getAllFactors === "function") {
            nextFactors = factorViewModel.getAllFactors() || []
        } else if (factorViewModel) {
            for (var index = 0; index < factorViewModel.rowCount(); ++index) {
                var row = factorViewModel.getRow(index)
                if (row) {
                    nextFactors.push(row)
                }
            }
        }
        allFactors = nextFactors
        rebuildFilteredFactors()
    }

    function rebuildFilteredFactors() {
        var nextFactors = []
        for (var index = 0; index < allFactors.length; ++index) {
            var record = allFactors[index] || ({})
            var factorId = normalizedFactorIdKey(record.factorId)
            if (!factorId || !matchesSearch(record)) {
                continue
            }
            if (selectedOnlyFilter && !isSelectedFactor(factorId)) {
                continue
            }
            nextFactors.push(record)
        }
        filteredFactors = nextFactors
        ensureActiveFactor()
    }

    function ensureActiveFactor() {
        var desiredActiveId = normalizedFactorIdKey(activeFactorId)
        for (var index = 0; index < filteredFactors.length; ++index) {
            if (normalizedFactorIdKey(filteredFactors[index].factorId) === desiredActiveId) {
                return
            }
        }

        for (var selectedIndex = 0; selectedIndex < filteredFactors.length; ++selectedIndex) {
            var selectedId = normalizedFactorIdKey(filteredFactors[selectedIndex].factorId)
            if (isSelectedFactor(selectedId)) {
                activeFactorId = selectedId
                return
            }
        }

        activeFactorId = filteredFactors.length > 0
            ? normalizedFactorIdKey(filteredFactors[0].factorId)
            : ""
    }

    function countVisibleSupportedFactors() {
        var count = 0
        for (var index = 0; index < filteredFactors.length; ++index) {
            var factorId = normalizedFactorIdKey(filteredFactors[index].factorId)
            if (factorId && isFactorSupported(factorId)) {
                count++
            }
        }
        return count
    }

    function countVisibleSelectedSupportedFactors() {
        var count = 0
        for (var index = 0; index < filteredFactors.length; ++index) {
            var factorId = normalizedFactorIdKey(filteredFactors[index].factorId)
            if (factorId && isSelectedFactor(factorId) && isFactorSupported(factorId)) {
                count++
            }
        }
        return count
    }

    function sanitizeSelectedFactors() {
        var nextSelected = []
        var seen = ({})
        for (var index = 0; index < selectedFactorIds.length; ++index) {
            var factorId = normalizedFactorIdKey(selectedFactorIds[index])
            if (!factorId || seen[factorId]) {
                continue
            }
            seen[factorId] = true
            nextSelected.push(factorId)
        }

        if (JSON.stringify(nextSelected) !== JSON.stringify(selectedFactorIds)) {
            selectedFactorIds = nextSelected
            return
        }

        ensureActiveFactor()
    }

    function toggleFactorSelection(factorId) {
        var normalizedId = normalizedFactorIdKey(factorId)
        if (!normalizedId) {
            return
        }
        if (requireSupportValidation && (!supportMapRequested || supportMapLoading || !isFactorSupported(normalizedId))) {
            return
        }

        var nextSelected = selectedFactorIds.slice()
        var existingIndex = nextSelected.indexOf(normalizedId)
        if (existingIndex === -1) {
            nextSelected.push(normalizedId)
        } else {
            nextSelected.splice(existingIndex, 1)
        }
        selectedFactorIds = nextSelected
        activeFactorId = normalizedId
    }

    function selectAllVisibleSupported() {
        var nextSelected = []
        var seen = ({})
        if (!allVisibleSupportedSelected) {
            for (var index = 0; index < selectedFactorIds.length; ++index) {
                var existingId = normalizedFactorIdKey(selectedFactorIds[index])
                if (existingId && !seen[existingId]) {
                    seen[existingId] = true
                    nextSelected.push(existingId)
                }
            }
            for (var visibleIndex = 0; visibleIndex < filteredFactors.length; ++visibleIndex) {
                var visibleId = normalizedFactorIdKey(filteredFactors[visibleIndex].factorId)
                if (!visibleId || !isFactorSupported(visibleId) || seen[visibleId]) {
                    continue
                }
                seen[visibleId] = true
                nextSelected.push(visibleId)
            }
        }
        selectedFactorIds = nextSelected
    }

    function clearSelection() {
        selectedFactorIds = []
    }

    function buildSelectedFactorPayload() {
        return {
            factorIds: selectedFactorIds.slice()
        }
    }

    function startSupportMapRefresh() {
        if (!requireSupportValidation || supportMapLoading) {
            return
        }
        supportMapRequested = true
        supportMapLoading = true
        if (supportMapRefreshCallback) {
            supportMapRefreshCallback()
        }
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        Rectangle {
            Layout.fillWidth: true
            radius: 18
            color: "#0f172a"
            border.width: 0
            implicitHeight: headerContent.implicitHeight + 24

            ColumnLayout {
                id: headerContent
                anchors.fill: parent
                anchors.margins: 18
                spacing: 10

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 12

                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 4

                        Text {
                            text: "选择排序因子"
                            font.pixelSize: 20
                            font.weight: Font.DemiBold
                            color: "#e2e8f0"
                        }

                        Text {
                            Layout.fillWidth: true
                            text: "直接读取因子详情和有效区间。策略回测标的在启动前由缓存数据集决定。"
                            font.pixelSize: 12
                            color: "#94a3b8"
                            wrapMode: Text.WordWrap
                        }
                    }

                    ToolButton {
                        implicitWidth: 36
                        implicitHeight: 36
                        text: "×"
                        font.pixelSize: 22
                        onClicked: root.close()

                        background: Rectangle {
                            radius: 10
                            color: parent.down ? "#1e293b" : (parent.hovered ? "#172033" : "transparent")
                        }

                        contentItem: Text {
                            text: parent.text
                            horizontalAlignment: Text.AlignHCenter
                            verticalAlignment: Text.AlignVCenter
                        }
                    }
                }

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 10

                    Rectangle {
                        Layout.fillWidth: true
                        radius: 12
                        color: "#0b1220"
                        border.width: 1
                        border.color: "#1d4ed8"
                        implicitHeight: strategyPoolChip.implicitHeight + 12

                        Text {
                            id: strategyPoolChip
                            anchors.fill: parent
                            anchors.margins: 10
                            text: strategyPoolSummaryText()
                            font.pixelSize: 11
                            color: "#bfdbfe"
                            wrapMode: Text.WordWrap
                            verticalAlignment: Text.AlignVCenter
                        }
                    }

                    Rectangle {
                        radius: 12
                        color: "#111827"
                        border.width: 1
                        border.color: "#334155"
                        implicitWidth: selectedCountChip.implicitWidth + 18
                        implicitHeight: selectedCountChip.implicitHeight + 12

                        Text {
                            id: selectedCountChip
                            anchors.centerIn: parent
                            text: "已选 " + selectedFactorIds.length + " 个"
                            font.pixelSize: 11
                            color: selectedFactorIds.length > 0 ? "#60a5fa" : "#cbd5e1"
                        }
                    }
                }
            }
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.margins: 12
            spacing: 12

            Rectangle {
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.preferredWidth: 628
                radius: 16
                color: "#0b1220"
                border.width: 1
                border.color: "#1e293b"

                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 12
                    spacing: 10

                    Rectangle {
                        Layout.fillWidth: true
                        Layout.preferredHeight: requireSupportValidation ? 52 : 42
                        radius: 12
                        color: requireSupportValidation
                            ? (supportMapRequested ? "#082f1d" : "#172033")
                            : "#101826"
                        border.width: 1
                        border.color: requireSupportValidation
                            ? (supportMapRequested ? "#22c55e" : "#334155")
                            : "#334155"

                        RowLayout {
                            anchors.fill: parent
                            anchors.leftMargin: 12
                            anchors.rightMargin: 12
                            spacing: 10

                            BusyIndicator {
                                running: supportMapLoading
                                visible: supportMapLoading
                                width: 18
                                height: 18
                            }

                            Text {
                                Layout.fillWidth: true
                                text: requireSupportValidation
                                    ? (supportMapLoading
                                        ? "正在校验当前数据源可用因子，完成后列表会自动刷新。"
                                        : (supportMapRequested
                                            ? "当前数据源可用因子已校验，可继续选择。"
                                            : "先校验当前数据源，再选择可用因子。"))
                                    : "策略编辑阶段直接选择排序因子，不在这里按数据源过滤。"
                                font.pixelSize: 12
                                color: requireSupportValidation
                                    ? (supportMapRequested ? "#bbf7d0" : "#cbd5e1")
                                    : "#cbd5e1"
                                wrapMode: Text.WordWrap
                            }

                            Button {
                                visible: requireSupportValidation
                                text: supportMapLoading ? "校验中" : "开始校验"
                                enabled: !supportMapLoading
                                onClicked: startSupportMapRefresh()
                            }
                        }
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 8

                        TextField {
                            id: searchField
                            Layout.fillWidth: true
                            placeholderText: "搜索因子名称、ID、分类或描述"
                            text: root.searchText
                            onTextChanged: {
                                root.searchText = text
                                root.rebuildFilteredFactors()
                            }
                        }

                        Button {
                            text: root.allVisibleSupportedSelected ? "取消可见全选" : "选择可见"
                            enabled: !supportMapLoading && (!requireSupportValidation || supportMapRequested)
                            onClicked: root.selectAllVisibleSupported()
                        }

                        Button {
                            text: root.selectedOnlyFilter ? "查看全部" : "只看已选"
                            onClicked: {
                                root.selectedOnlyFilter = !root.selectedOnlyFilter
                                root.rebuildFilteredFactors()
                            }
                        }

                        Button {
                            text: "清空"
                            enabled: root.selectedFactorIds.length > 0
                            onClicked: root.clearSelection()
                        }
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 6

                        Text {
                            text: "可见 " + filteredFactors.length + " 个"
                            font.pixelSize: 12
                            color: "#cbd5e1"
                        }

                        Text {
                            text: requireSupportValidation
                                ? ("可选 " + visibleSupportedFactorCount + " 个")
                                : ("已选 " + selectedFactorIds.length + " 个")
                            font.pixelSize: 12
                            color: "#93c5fd"
                        }

                        Item { Layout.fillWidth: true }

                        Text {
                            text: root.selectedOnlyFilter ? "当前仅显示已选因子" : "单击卡片查看详情，点右侧按钮加入选择"
                            font.pixelSize: 11
                            color: "#64748b"
                        }
                    }

                    Rectangle {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        radius: 14
                        color: "#08111f"
                        border.width: 1
                        border.color: "#172033"

                        ScrollView {
                            anchors.fill: parent
                            anchors.margins: 8
                            clip: true

                            ListView {
                                id: factorListView
                                width: parent.width
                                model: root.filteredFactors
                                spacing: 8
                                clip: true
                                boundsBehavior: Flickable.StopAtBounds

                                delegate: Rectangle {
                                    required property int index
                                    required property var modelData

                                    width: factorListView.width
                                    height: 74
                                    radius: 12

                                    property string factorId: root.normalizedFactorIdKey(modelData.factorId)
                                    property bool selected: root.isSelectedFactor(factorId)
                                    property var supportInfo: root.supportInfoForFactor(factorId)
                                    property bool supported: supportInfo.supported !== false
                                    property bool active: root.activeFactorId === factorId

                                    color: !supported
                                        ? "#111827"
                                        : (active ? "#11233f" : (delegateMouseArea.containsMouse ? "#0f1b30" : "#0b1220"))
                                    border.width: 1
                                    border.color: selected
                                        ? "#38bdf8"
                                        : (active ? "#2563eb" : "#1e293b")
                                    opacity: supported ? 1.0 : 0.62

                                    ColumnLayout {
                                        anchors.fill: parent
                                        anchors.margins: 8
                                        spacing: 4

                                        RowLayout {
                                            Layout.fillWidth: true
                                            spacing: 8

                                            Rectangle {
                                                radius: 9
                                                color: selected ? "#0ea5e9" : "#172033"
                                                border.width: 1
                                                border.color: selected ? "#38bdf8" : "#334155"
                                                implicitWidth: 42
                                                implicitHeight: 18

                                                Text {
                                                    anchors.centerIn: parent
                                                    text: selected ? "已选" : "候选"
                                                    font.pixelSize: 9
                                                    color: selected ? "white" : "#cbd5e1"
                                                }
                                            }

                                            Text {
                                                Layout.fillWidth: true
                                                text: root.factorDisplayName(modelData, null)
                                                font.pixelSize: 12
                                                font.weight: Font.Medium
                                                color: supported ? "#e2e8f0" : "#94a3b8"
                                                elide: Text.ElideRight
                                            }

                                            Button {
                                                text: selected ? "移除" : "加入"
                                                enabled: supported && (!root.requireSupportValidation || root.supportMapRequested) && !root.supportMapLoading
                                                onClicked: root.toggleFactorSelection(factorId)
                                            }
                                        }

                                        Text {
                                            Layout.fillWidth: true
                                            text: supported
                                                ? root.factorDescription(modelData, null)
                                                : (supportInfo.reason || "当前缓存不支持该因子")
                                            font.pixelSize: 9
                                            color: supported ? "#94a3b8" : "#f59e0b"
                                            elide: Text.ElideRight
                                        }

                                        Flow {
                                            Layout.fillWidth: true
                                            spacing: 4

                                            Repeater {
                                                model: {
                                                    var pills = [
                                                        { label: root.factorCategoryLabel(modelData.factorType), fg: root.factorCategoryColor(modelData.factorType), bg: "#0f172a" },
                                                        { label: "ID " + factorId, fg: "#cbd5e1", bg: "#111827" }
                                                    ]
                                                    var rating = root.factorCoreRating(modelData, null)
                                                    if (rating > 0) {
                                                        pills.push({ label: root.factorCoreRatingLabel(modelData, null), fg: root.factorCoreRatingColor(rating), bg: "#2a2110" })
                                                        var ic = Number(modelData.icValue || 0)
                                                        if (isFinite(ic) && Math.abs(ic) > 1e-9) {
                                                            pills.push({ label: "IC " + ic.toFixed(3), fg: "#93c5fd", bg: "#0f172a" })
                                                        }
                                                        var ir = Number(modelData.irValue || 0)
                                                        if (isFinite(ir) && Math.abs(ir) > 1e-9) {
                                                            pills.push({ label: "IR " + ir.toFixed(2), fg: "#93c5fd", bg: "#0f172a" })
                                                        }
                                                    } else {
                                                        pills.push({ label: "未回测", fg: "#64748b", bg: "#1e293b" })
                                                    }
                                                    return pills
                                                }

                                                delegate: Rectangle {
                                                    required property var modelData
                                                    radius: 9
                                                    color: modelData.bg
                                                    border.width: 1
                                                    border.color: Qt.darker(modelData.bg, 1.12)
                                                    implicitWidth: pillText.implicitWidth + 10
                                                    implicitHeight: pillText.implicitHeight + 6

                                                    Text {
                                                        id: pillText
                                                        anchors.centerIn: parent
                                                        text: modelData.label
                                                        font.pixelSize: 8
                                                        color: modelData.fg
                                                    }
                                                }
                                            }
                                        }
                                    }

                                    MouseArea {
                                        id: delegateMouseArea
                                        anchors.fill: parent
                                        hoverEnabled: true
                                        acceptedButtons: Qt.LeftButton
                                        onClicked: root.activeFactorId = factorId
                                    }
                                }
                            }
                        }
                    }
                }
            }

            Rectangle {
                Layout.preferredWidth: 368
                Layout.fillHeight: true
                radius: 16
                color: "#0b1220"
                border.width: 1
                border.color: "#1e293b"

                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 12
                    spacing: 10

                    Text {
                        text: "因子详情"
                        font.pixelSize: 16
                        font.weight: Font.DemiBold
                        color: "#e2e8f0"
                    }

                    Rectangle {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        radius: 14
                        color: "#08111f"
                        border.width: 1
                        border.color: "#172033"

                        Loader {
                            anchors.fill: parent
                            anchors.margins: 12
                            sourceComponent: root.activeFactorId !== "" ? detailPaneComponent : emptyPaneComponent
                        }

                        Component {
                            id: detailPaneComponent

                            ScrollView {
                                id: detailPaneScroll
                                clip: true
                                contentWidth: availableWidth
                                ScrollBar.horizontal.policy: ScrollBar.AlwaysOff

                                ColumnLayout {
                                    width: detailPaneScroll.availableWidth
                                    spacing: 12

                                    Rectangle {
                                        Layout.fillWidth: true
                                        radius: 14
                                        color: "#0f172a"
                                        border.width: 1
                                        border.color: "#1e293b"
                                        implicitHeight: detailHeaderColumn.implicitHeight + 20

                                        ColumnLayout {
                                            id: detailHeaderColumn
                                            anchors.fill: parent
                                            anchors.margins: 12
                                            spacing: 8

                                            Text {
                                                Layout.fillWidth: true
                                                text: root.factorDisplayName(root.activeFactorRecord, root.activeFactorDetail)
                                                font.pixelSize: 18
                                                font.weight: Font.DemiBold
                                                color: "#e2e8f0"
                                                wrapMode: Text.WordWrap
                                            }

                                            Text {
                                                Layout.fillWidth: true
                                                text: root.activeFactorId
                                                font.pixelSize: 11
                                                color: "#60a5fa"
                                                wrapMode: Text.WrapAnywhere
                                            }

                                            Text {
                                                Layout.fillWidth: true
                                                text: root.factorDescription(root.activeFactorRecord, root.activeFactorDetail)
                                                font.pixelSize: 12
                                                color: "#94a3b8"
                                                wrapMode: Text.WordWrap
                                            }
                                        }
                                    }

                                    ColumnLayout {
                                        Layout.fillWidth: true
                                        spacing: 10

                                        Repeater {
                                            model: {
                                                var rating = root.factorCoreRating(root.activeFactorRecord, root.activeFactorDetail)
                                                var backtestOutputText = "仅保留因子信息"
                                                if (rating > 0) {
                                                    var parts = []
                                                    var ic = Number((root.activeFactorRecord && root.activeFactorRecord.icValue) || 0)
                                                    var ir = Number((root.activeFactorRecord && root.activeFactorRecord.irValue) || 0)
                                                    if (isFinite(ic) && Math.abs(ic) > 1e-9) {
                                                        parts.push("IC " + ic.toFixed(3))
                                                    }
                                                    if (isFinite(ir) && Math.abs(ir) > 1e-9) {
                                                        parts.push("IR " + ir.toFixed(2))
                                                    }
                                                    if (parts.length > 0) {
                                                        backtestOutputText = parts.join("  ")
                                                    }
                                                }
                                                return [
                                                    { title: "分类", value: root.factorCategoryLabel(root.activeFactorRecord.factorType) },
                                                    { title: "核心评分", value: rating > 0 ? root.factorCoreRatingLabel(root.activeFactorRecord, root.activeFactorDetail) : "未评" },
                                                    { title: "回测输出", value: backtestOutputText },
                                                    { title: "有效区间", value: root.factorEffectiveRangeText(root.activeFactorDetail) },
                                                    { title: "预热裁剪", value: root.factorWarmupText(root.activeFactorDetail) },
                                                    { title: "状态", value: root.factorStatusText(root.activeFactorRecord, root.activeFactorDetail) }
                                                ]
                                            }

                                            delegate: Rectangle {
                                                required property var modelData
                                                Layout.fillWidth: true
                                                radius: 12
                                                color: "#0f172a"
                                                border.width: 1
                                                border.color: "#1e293b"
                                                implicitHeight: metricColumn.implicitHeight + 14

                                                RowLayout {
                                                    id: metricColumn
                                                    anchors.fill: parent
                                                    anchors.margins: 10
                                                    spacing: 12

                                                    Text {
                                                        Layout.preferredWidth: 72
                                                        text: modelData.title
                                                        font.pixelSize: 10
                                                        color: "#64748b"
                                                    }

                                                    Text {
                                                        Layout.fillWidth: true
                                                        text: modelData.value
                                                        font.pixelSize: 12
                                                        color: "#e2e8f0"
                                                        wrapMode: Text.WordWrap
                                                    }
                                                }
                                            }
                                        }
                                    }

                                    Rectangle {
                                        Layout.fillWidth: true
                                        radius: 12
                                        color: "#0f172a"
                                        border.width: 1
                                        border.color: "#1d4ed8"
                                        implicitHeight: strategyContextColumn.implicitHeight + 18

                                        ColumnLayout {
                                            id: strategyContextColumn
                                            anchors.fill: parent
                                            anchors.margins: 10
                                            spacing: 6

                                            Text {
                                                text: "当前策略上下文"
                                                font.pixelSize: 12
                                                font.weight: Font.Medium
                                                color: "#bfdbfe"
                                            }

                                            Text {
                                                Layout.fillWidth: true
                                                text: root.strategyPoolSummaryText()
                                                font.pixelSize: 12
                                                color: "#e2e8f0"
                                                wrapMode: Text.WordWrap
                                            }

                                            Text {
                                                Layout.fillWidth: true
                                                text: root.strategyPoolHintText(root.activeFactorDetail)
                                                font.pixelSize: 11
                                                color: "#93c5fd"
                                                wrapMode: Text.WordWrap
                                            }

                                            Text {
                                                Layout.fillWidth: true
                                                text: "策略回测标的在启动前由缓存数据集决定，不在这里继承或展示股票池。"
                                                font.pixelSize: 11
                                                color: "#94a3b8"
                                                wrapMode: Text.WordWrap
                                            }
                                        }
                                    }

                                    Rectangle {
                                        Layout.fillWidth: true
                                        radius: 12
                                        color: "#0f172a"
                                        border.width: 1
                                        border.color: "#1e293b"
                                        implicitHeight: factorPoolColumn.implicitHeight + 18

                                        ColumnLayout {
                                            id: factorPoolColumn
                                            anchors.fill: parent
                                            anchors.margins: 10
                                            spacing: 6

                                            Text {
                                                text: "回测输出说明"
                                                font.pixelSize: 12
                                                font.weight: Font.Medium
                                                color: "#e2e8f0"
                                            }

                                            Text {
                                                Layout.fillWidth: true
                                                text: "因子回测结果不再携带股票池明细。"
                                                font.pixelSize: 11
                                                color: "#94a3b8"
                                                wrapMode: Text.WordWrap
                                            }
                                        }
                                    }

                                    Button {
                                        Layout.fillWidth: true
                                        text: root.isSelectedFactor(root.activeFactorId) ? "从已选列表移除" : "加入已选因子"
                                        enabled: !root.requireSupportValidation || (root.supportMapRequested && !root.supportMapLoading && root.isFactorSupported(root.activeFactorId))
                                        onClicked: root.toggleFactorSelection(root.activeFactorId)
                                    }
                                }
                            }
                        }

                        Component {
                            id: emptyPaneComponent

                            Item {
                                ColumnLayout {
                                    anchors.centerIn: parent
                                    width: Math.min(parent.width - 32, 280)
                                    spacing: 8

                                    Text {
                                        Layout.fillWidth: true
                                        text: "从左侧选择一个因子"
                                        font.pixelSize: 16
                                        font.weight: Font.Medium
                                        color: "#e2e8f0"
                                        horizontalAlignment: Text.AlignHCenter
                                    }

                                    Text {
                                        Layout.fillWidth: true
                                        text: "这里会展示因子基本信息、有效区间和最近回测摘要，不再包含股票池关系。"
                                        font.pixelSize: 12
                                        color: "#94a3b8"
                                        wrapMode: Text.WordWrap
                                        horizontalAlignment: Text.AlignHCenter
                                    }
                                }
                            }
                        }
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 10

                        Item { Layout.fillWidth: true }

                        Button {
                            text: "取消"
                            onClicked: root.close()
                        }

                        Button {
                            text: "确认选择"
                            enabled: root.selectedFactorIds.length > 0
                            onClicked: {
                                factorsSelected(root.selectedFactorIds)
                                root.close()
                            }
                        }
                    }
                }
            }
        }
    }

    Connections {
        target: factorViewModel
        function onDataUpdated() {
            root.rebuildAllFactors()
        }
        function onCountChanged() {
            root.rebuildAllFactors()
        }
    }

    onOpened: {
        selectedOnlyFilter = selectedFactorIds.length > 0
        rebuildAllFactors()
        sanitizeSelectedFactors()
    }

    onSearchTextChanged: rebuildFilteredFactors()
    onSelectedOnlyFilterChanged: rebuildFilteredFactors()
    onSelectedFactorIdsChanged: sanitizeSelectedFactors()
    onFactorSupportMapChanged: sanitizeSelectedFactors()
    onClosed: dialogClosed()

    Component.onCompleted: {
        rebuildAllFactors()
        sanitizeSelectedFactors()
    }
}