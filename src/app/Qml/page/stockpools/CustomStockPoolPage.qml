import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import AStock.Bridge 1.0 as Bridge
import "../../utils/CustomStockPoolStore.js" as CustomStockPoolStore

Rectangle {
    id: root
    color: "#0F172A"

    readonly property var strategyService: Bridge.StrategyService
    readonly property var factorService: Bridge.FactorService

    property var stockPools: []
    property var strategies: []
    property var factors: []
    property string selectedPoolId: ""
    property string editingPoolId: ""
    property string editingPoolName: ""
    property string editingSymbolsText: ""
    property string editingNotes: ""
    property var selectedStrategyIds: []
    property var selectedFactorIds: []
    property string feedbackMessage: ""
    property bool feedbackError: false

    function normalizeId(value) {
        return String(value || "").trim()
    }

    function strategyIdOf(strategy) {
        return normalizeId(strategy && (strategy.strategyId || strategy.strategy_id || strategy.id))
    }

    function strategyNameOf(strategy) {
        return String(strategy && (strategy.strategyName || strategy.strategy_name || strategy.name) || "未命名策略")
    }

    function factorIdOf(factor) {
        return normalizeId(factor && (factor.factorId || factor.factor_id || factor.id))
    }

    function factorNameOf(factor) {
        return String(factor && (factor.displayName || factor.factorName || factor.factor_name || factor.name) || "未命名因子")
    }

    function setFeedback(message, isError) {
        feedbackMessage = String(message || "").trim()
        feedbackError = !!isError
    }

    function refreshEntities() {
        strategies = strategyService && strategyService.getAllStrategies ? strategyService.getAllStrategies() : []
        factors = factorService && factorService.getAllFactors ? factorService.getAllFactors() : []
    }

    function refreshPools() {
        stockPools = CustomStockPoolStore.CustomStockPoolStore.listPools()
    }

    function refreshAll() {
        refreshPools()
        refreshEntities()
        if (selectedPoolId) {
            loadPoolIntoEditor(selectedPoolId)
        } else {
            createNewPoolDraft()
        }
    }

    function resolvePoolStrategyIds(poolId) {
        var normalizedPoolId = normalizeId(poolId)
        var ids = []
        for (var index = 0; index < strategies.length; ++index) {
            var strategy = strategies[index]
            var binding = CustomStockPoolStore.CustomStockPoolStore.extractLinkedStockPool(strategy)
            var strategyId = strategyIdOf(strategy)
            if (binding.poolId === normalizedPoolId && strategyId) {
                ids.push(strategyId)
            }
        }
        return ids
    }

    function resolvePoolFactorIds(poolId) {
        var normalizedPoolId = normalizeId(poolId)
        var ids = []
        for (var index = 0; index < factors.length; ++index) {
            var factor = factors[index]
            var binding = CustomStockPoolStore.CustomStockPoolStore.extractLinkedStockPool(factor)
            var factorId = factorIdOf(factor)
            if (binding.poolId === normalizedPoolId && factorId) {
                ids.push(factorId)
            }
        }
        return ids
    }

    function bindingCountText(poolId) {
        return resolvePoolStrategyIds(poolId).length + " 策略 / " + resolvePoolFactorIds(poolId).length + " 因子"
    }

    function loadPoolIntoEditor(poolId) {
        var pool = CustomStockPoolStore.CustomStockPoolStore.getPoolById(poolId)
        if (!pool || !pool.id) {
            createNewPoolDraft()
            return
        }

        selectedPoolId = pool.id
        editingPoolId = pool.id
        editingPoolName = pool.name
        editingSymbolsText = (pool.symbols || []).join(", ")
        editingNotes = pool.notes || ""
        selectedStrategyIds = resolvePoolStrategyIds(pool.id)
        selectedFactorIds = resolvePoolFactorIds(pool.id)
    }

    function createNewPoolDraft() {
        selectedPoolId = ""
        editingPoolId = ""
        editingPoolName = ""
        editingSymbolsText = ""
        editingNotes = ""
        selectedStrategyIds = []
        selectedFactorIds = []
    }

    function toggleSelection(targetIds, itemId, checked) {
        var normalizedItemId = normalizeId(itemId)
        var nextIds = targetIds.slice()
        var existingIndex = nextIds.indexOf(normalizedItemId)

        if (checked && existingIndex === -1) {
            nextIds.push(normalizedItemId)
        } else if (!checked && existingIndex !== -1) {
            nextIds.splice(existingIndex, 1)
        }

        return nextIds
    }

    function currentPoolRecord() {
        return {
            id: editingPoolId,
            name: editingPoolName,
            symbols: CustomStockPoolStore.CustomStockPoolStore.normalizeSymbolList(editingSymbolsText),
            notes: editingNotes
        }
    }

    function updateStrategyBinding(strategyId, pool) {
        if (!strategyService || !strategyService.getStrategyById || !strategyService.updateStrategy) {
            return false
        }

        var detail = strategyService.getStrategyById(strategyId) || ({})
        if (!detail || Object.keys(detail).length === 0) {
            return false
        }

        var updated = pool
            ? CustomStockPoolStore.CustomStockPoolStore.applyLinkedStockPool(detail, pool)
            : CustomStockPoolStore.CustomStockPoolStore.clearLinkedStockPool(detail)
        return !!strategyService.updateStrategy(strategyId, updated)
    }

    function updateFactorBinding(factorId, pool) {
        if (!factorService || !factorService.getFactorById || !factorService.updateFactor) {
            return false
        }

        var detail = factorService.getFactorById(factorId) || ({})
        if (!detail || Object.keys(detail).length === 0) {
            return false
        }

        var updated = pool
            ? CustomStockPoolStore.CustomStockPoolStore.applyLinkedStockPool(detail, pool)
            : CustomStockPoolStore.CustomStockPoolStore.clearLinkedStockPool(detail)
        return !!factorService.updateFactor(factorId, updated)
    }

    function syncBindingsForPool(poolId, savedPool) {
        var failedStrategies = []
        var failedFactors = []
        var currentlyBoundStrategyIds = resolvePoolStrategyIds(poolId)
        var currentlyBoundFactorIds = resolvePoolFactorIds(poolId)

        for (var strategyIndex = 0; strategyIndex < currentlyBoundStrategyIds.length; ++strategyIndex) {
            var boundStrategyId = currentlyBoundStrategyIds[strategyIndex]
            if (selectedStrategyIds.indexOf(boundStrategyId) !== -1) {
                continue
            }
            if (!updateStrategyBinding(boundStrategyId, null)) {
                failedStrategies.push(boundStrategyId)
            }
        }

        for (strategyIndex = 0; strategyIndex < selectedStrategyIds.length; ++strategyIndex) {
            var selectedStrategyId = selectedStrategyIds[strategyIndex]
            if (!updateStrategyBinding(selectedStrategyId, savedPool)) {
                failedStrategies.push(selectedStrategyId)
            }
        }

        for (var factorIndex = 0; factorIndex < currentlyBoundFactorIds.length; ++factorIndex) {
            var boundFactorId = currentlyBoundFactorIds[factorIndex]
            if (selectedFactorIds.indexOf(boundFactorId) !== -1) {
                continue
            }
            if (!updateFactorBinding(boundFactorId, null)) {
                failedFactors.push(boundFactorId)
            }
        }

        for (factorIndex = 0; factorIndex < selectedFactorIds.length; ++factorIndex) {
            var selectedFactorId = selectedFactorIds[factorIndex]
            if (!updateFactorBinding(selectedFactorId, savedPool)) {
                failedFactors.push(selectedFactorId)
            }
        }

        return {
            failedStrategies: failedStrategies,
            failedFactors: failedFactors
        }
    }

    function saveCurrentPool() {
        try {
            var savedPool = CustomStockPoolStore.CustomStockPoolStore.savePool(currentPoolRecord())
            var syncResult = syncBindingsForPool(editingPoolId, savedPool)
            refreshAll()
            selectedPoolId = savedPool.id
            loadPoolIntoEditor(savedPool.id)

            if (syncResult.failedStrategies.length === 0 && syncResult.failedFactors.length === 0) {
                setFeedback("股票池已保存，并完成策略/因子绑定同步", false)
            } else {
                setFeedback("股票池已保存，但部分绑定同步失败", true)
            }
        } catch (error) {
            setFeedback(error.message || "股票池保存失败", true)
        }
    }

    function deleteCurrentPool() {
        if (!editingPoolId) {
            setFeedback("当前还没有可删除的股票池", true)
            return
        }

        var poolId = editingPoolId
        var syncResult = syncBindingsForPool(poolId, null)
        if (!CustomStockPoolStore.CustomStockPoolStore.deletePool(poolId)) {
            setFeedback("股票池删除失败", true)
            return
        }

        refreshAll()
        if (syncResult.failedStrategies.length === 0 && syncResult.failedFactors.length === 0) {
            setFeedback("股票池已删除，原绑定已清空", false)
        } else {
            setFeedback("股票池已删除，但部分绑定未能清空", true)
        }
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 20
        spacing: 16

        Rectangle {
            Layout.fillWidth: true
            radius: 16
            color: "#111c33"
            border.width: 1
            border.color: "#22314f"

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 18
                spacing: 8

                Text {
                    text: "自选股票池"
                    font.pixelSize: 24
                    font.bold: true
                    color: "#f8fafc"
                }

                Text {
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                    text: "这里维护独立于回测池的自选股票池。绑定到策略或因子后，只同步关联引用和标的快照，不会改写策略回测使用的 symbol_pool。"
                    font.pixelSize: 13
                    color: "#94a3b8"
                }

                Rectangle {
                    Layout.fillWidth: true
                    Layout.topMargin: 4
                    visible: feedbackMessage.length > 0
                    radius: 10
                    color: feedbackError ? "#3b1720" : "#10261b"
                    border.width: 1
                    border.color: feedbackError ? "#7f1d1d" : "#166534"

                    Text {
                        anchors.fill: parent
                        anchors.margins: 12
                        wrapMode: Text.WordWrap
                        text: feedbackMessage
                        font.pixelSize: 12
                        color: feedbackError ? "#fecaca" : "#bbf7d0"
                    }
                }
            }
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 16

            Rectangle {
                Layout.preferredWidth: 320
                Layout.fillHeight: true
                radius: 16
                color: "#111827"
                border.width: 1
                border.color: "#23324d"

                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 14
                    spacing: 12

                    RowLayout {
                        Layout.fillWidth: true

                        Text {
                            text: "股票池列表"
                            font.pixelSize: 16
                            font.bold: true
                            color: "#f8fafc"
                        }

                        Item { Layout.fillWidth: true }

                        Rectangle {
                            Layout.preferredWidth: 76
                            Layout.preferredHeight: 32
                            radius: 16
                            color: "#2563eb"

                            Text {
                                anchors.centerIn: parent
                                text: "新建"
                                font.pixelSize: 12
                                color: "white"
                            }

                            MouseArea {
                                anchors.fill: parent
                                cursorShape: Qt.PointingHandCursor
                                onClicked: root.createNewPoolDraft()
                            }
                        }
                    }

                    ScrollView {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        clip: true

                        Column {
                            width: parent.width
                            spacing: 10

                            Repeater {
                                model: stockPools

                                delegate: Rectangle {
                                    width: parent.width
                                    height: 98
                                    radius: 14
                                    color: String(modelData.id || "") === root.editingPoolId ? "#1d3557" : "#0f172a"
                                    border.width: 1
                                    border.color: String(modelData.id || "") === root.editingPoolId ? "#60a5fa" : "#243b53"

                                    ColumnLayout {
                                        anchors.fill: parent
                                        anchors.margins: 12
                                        spacing: 6

                                        Text {
                                            Layout.fillWidth: true
                                            text: modelData.name || "未命名股票池"
                                            font.pixelSize: 14
                                            font.bold: true
                                            color: "#f8fafc"
                                            elide: Text.ElideRight
                                        }

                                        Text {
                                            Layout.fillWidth: true
                                            text: (modelData.symbols || []).slice(0, 3).join("、") + ((modelData.symbols || []).length > 3 ? " 等" + modelData.symbols.length + " 只" : "")
                                            font.pixelSize: 12
                                            color: "#93c5fd"
                                            elide: Text.ElideRight
                                        }

                                        Text {
                                            Layout.fillWidth: true
                                            text: root.bindingCountText(modelData.id)
                                            font.pixelSize: 11
                                            color: "#94a3b8"
                                        }
                                    }

                                    MouseArea {
                                        anchors.fill: parent
                                        cursorShape: Qt.PointingHandCursor
                                        onClicked: root.loadPoolIntoEditor(modelData.id)
                                    }
                                }
                            }
                        }
                    }
                }
            }

            Rectangle {
                Layout.fillWidth: true
                Layout.fillHeight: true
                radius: 16
                color: "#111827"
                border.width: 1
                border.color: "#23324d"

                ScrollView {
                    anchors.fill: parent
                    anchors.margins: 14
                    clip: true

                    ColumnLayout {
                        width: parent.width
                        spacing: 14

                        Text {
                            text: editingPoolId ? "编辑股票池" : "新建股票池"
                            font.pixelSize: 18
                            font.bold: true
                            color: "#f8fafc"
                        }

                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 6

                            Text {
                                text: "股票池名称"
                                font.pixelSize: 13
                                color: "#cbd5e1"
                            }

                            TextField {
                                Layout.fillWidth: true
                                text: root.editingPoolName
                                placeholderText: "例如：盘中观察池 / 行业轮动池"
                                color: "#f8fafc"
                                onTextChanged: root.editingPoolName = text
                                background: Rectangle {
                                    implicitHeight: 40
                                    radius: 8
                                    color: "#0f172a"
                                    border.width: 1
                                    border.color: "#334155"
                                }
                            }
                        }

                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 6

                            Text {
                                text: "股票代码"
                                font.pixelSize: 13
                                color: "#cbd5e1"
                            }

                            TextArea {
                                Layout.fillWidth: true
                                Layout.preferredHeight: 110
                                text: root.editingSymbolsText
                                placeholderText: "输入股票代码，支持逗号、空格或换行分隔，例如：600000.SH, 000001.SZ"
                                wrapMode: TextEdit.Wrap
                                color: "#f8fafc"
                                onTextChanged: root.editingSymbolsText = text
                                background: Rectangle {
                                    radius: 8
                                    color: "#0f172a"
                                    border.width: 1
                                    border.color: "#334155"
                                }
                            }
                        }

                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 6

                            Text {
                                text: "备注"
                                font.pixelSize: 13
                                color: "#cbd5e1"
                            }

                            TextArea {
                                Layout.fillWidth: true
                                Layout.preferredHeight: 72
                                text: root.editingNotes
                                placeholderText: "记录这个池子的用途、选股标准或维护说明"
                                wrapMode: TextEdit.Wrap
                                color: "#f8fafc"
                                onTextChanged: root.editingNotes = text
                                background: Rectangle {
                                    radius: 8
                                    color: "#0f172a"
                                    border.width: 1
                                    border.color: "#334155"
                                }
                            }
                        }

                        Rectangle {
                            Layout.fillWidth: true
                            radius: 12
                            color: "#0f172a"
                            border.width: 1
                            border.color: "#23324d"

                            ColumnLayout {
                                anchors.fill: parent
                                anchors.margins: 12
                                spacing: 10

                                Text {
                                    text: "绑定策略"
                                    font.pixelSize: 14
                                    font.bold: true
                                    color: "#f8fafc"
                                }

                                Repeater {
                                    model: strategies

                                    delegate: CheckBox {
                                        Layout.fillWidth: true
                                        checked: root.selectedStrategyIds.indexOf(root.strategyIdOf(modelData)) !== -1
                                        text: root.strategyNameOf(modelData)
                                        onToggled: root.selectedStrategyIds = root.toggleSelection(root.selectedStrategyIds, root.strategyIdOf(modelData), checked)
                                    }
                                }
                            }
                        }

                        Rectangle {
                            Layout.fillWidth: true
                            radius: 12
                            color: "#0f172a"
                            border.width: 1
                            border.color: "#23324d"

                            ColumnLayout {
                                anchors.fill: parent
                                anchors.margins: 12
                                spacing: 10

                                Text {
                                    text: "绑定因子"
                                    font.pixelSize: 14
                                    font.bold: true
                                    color: "#f8fafc"
                                }

                                Repeater {
                                    model: factors

                                    delegate: CheckBox {
                                        Layout.fillWidth: true
                                        checked: root.selectedFactorIds.indexOf(root.factorIdOf(modelData)) !== -1
                                        text: root.factorNameOf(modelData)
                                        onToggled: root.selectedFactorIds = root.toggleSelection(root.selectedFactorIds, root.factorIdOf(modelData), checked)
                                    }
                                }
                            }
                        }

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 10

                            Rectangle {
                                Layout.preferredWidth: 96
                                Layout.preferredHeight: 38
                                radius: 19
                                color: "#2563eb"

                                Text {
                                    anchors.centerIn: parent
                                    text: "保存"
                                    font.pixelSize: 13
                                    font.bold: true
                                    color: "white"
                                }

                                MouseArea {
                                    anchors.fill: parent
                                    cursorShape: Qt.PointingHandCursor
                                    onClicked: root.saveCurrentPool()
                                }
                            }

                            Rectangle {
                                Layout.preferredWidth: 96
                                Layout.preferredHeight: 38
                                radius: 19
                                color: "#1e293b"
                                border.width: 1
                                border.color: "#475569"

                                Text {
                                    anchors.centerIn: parent
                                    text: "重置"
                                    font.pixelSize: 13
                                    color: "#e2e8f0"
                                }

                                MouseArea {
                                    anchors.fill: parent
                                    cursorShape: Qt.PointingHandCursor
                                    onClicked: root.createNewPoolDraft()
                                }
                            }

                            Item { Layout.fillWidth: true }

                            Rectangle {
                                Layout.preferredWidth: 110
                                Layout.preferredHeight: 38
                                radius: 19
                                color: editingPoolId ? "#3f1d1d" : "#111827"
                                border.width: 1
                                border.color: editingPoolId ? "#b91c1c" : "#334155"

                                Text {
                                    anchors.centerIn: parent
                                    text: "删除股票池"
                                    font.pixelSize: 13
                                    color: editingPoolId ? "#fecaca" : "#64748b"
                                }

                                MouseArea {
                                    anchors.fill: parent
                                    enabled: !!editingPoolId
                                    cursorShape: enabled ? Qt.PointingHandCursor : Qt.ForbiddenCursor
                                    onClicked: root.deleteCurrentPool()
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    onVisibleChanged: {
        if (visible) {
            refreshAll()
        }
    }

    Component.onCompleted: refreshAll()
}