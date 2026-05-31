import QtQuick 2.15
import QtQuick.Layouts 1.15
import QtQuick.Controls 2.15
import "../FactorWorkbench/Creation/components" as PluginComponents

Rectangle {
    id: root

    property var strategyOptions: []
    property var universeOptions: []
    property var indexPoolOptions: []
    property var dataSourceOptions: []
    property string selectedStrategyName: ""
    property string selectedStrategyId: ""
    property string selectedUniverseType: "market"
    property string selectedIndexSymbol: "000300.SH"
    property string selectedStartDate: ""
    property string selectedEndDate: ""
    property var dynamicParamConfigs: []
    property var dynamicParamGroups: []
    property var dynamicParamValues: ({})
    property var cacheDatasetOptions: []
    property bool parametersLoaded: false
    property bool dateSelectionEnabled: true
    property bool syncingStrategySelection: false
    property bool syncingDataSourceSelection: false
    property bool syncingCacheDatasetSelection: false
    property bool showStrategySelector: true
    property bool showDataSourceSelector: true
    property bool showCacheDatasetSelector: false
    property int selectedCacheDatasetIndex: 0
    property var paramRegistry: null

    property alias strategyComboBox: strategyComboBox
    property alias universeComboBox: universeComboBox
    property alias indexPoolComboBox: indexPoolComboBox
    property alias dataSourceComboBox: dataSourceComboBox
    property alias cacheDatasetComboBox: cacheDatasetComboBox
    property alias startDatePicker: startDatePicker
    property alias endDatePicker: endDatePicker
    property alias dynamicParamGenerator: dynamicParamGenerator

    signal strategyOptionSelected(int index, var option)
    signal universeOptionSelected(int index, var option)
    signal indexOptionSelected(int index, var option)
    signal dataSourceOptionSelected(int index, var option)
    signal cacheDatasetOptionSelected(int index, var option)
    signal startDateSelected(string dateText)
    signal endDateSelected(string dateText)
    signal dynamicParamsChanged(var newValues)

    readonly property bool showDynamicParamForm: root.parametersLoaded && root.dynamicParamConfigs.length > 0
    readonly property bool showFallbackRuntimeForm: root.parametersLoaded && root.dynamicParamConfigs.length === 0
    readonly property int parameterFormMaxWidth: 960
    readonly property int parameterFormMinColumnWidth: 320
    readonly property int selectedDataSourceMode: {
        var rawMode = root.dynamicParamValues && root.dynamicParamValues.dataSourceMode !== undefined
            ? Number(root.dynamicParamValues.dataSourceMode)
            : 0
        return isFinite(rawMode) ? rawMode : 0
    }
    readonly property bool showUniverseSelectionControls: root.selectedDataSourceMode !== 1

    function findOptionIndex(options, expectedValue, candidateKeys) {
        var normalizedExpected = String(expectedValue === undefined || expectedValue === null ? "" : expectedValue)
        if (!normalizedExpected.length || !Array.isArray(options)) {
            return -1
        }

        for (var index = 0; index < options.length; ++index) {
            var option = options[index]
            if (!option || typeof option !== "object") {
                continue
            }

            for (var keyIndex = 0; keyIndex < candidateKeys.length; ++keyIndex) {
                var candidateKey = candidateKeys[keyIndex]
                if (String(option[candidateKey] === undefined || option[candidateKey] === null ? "" : option[candidateKey]) === normalizedExpected) {
                    return index
                }
            }
        }

        return -1
    }

    function syncSelection(comboBox, options, expectedValue, candidateKeys, syncFlagName) {
        if (!comboBox) {
            return
        }

        var resolvedIndex = findOptionIndex(options, expectedValue, candidateKeys)
        root[syncFlagName] = true
        comboBox.currentIndex = resolvedIndex
        root[syncFlagName] = false
    }

    function numericRuntimeValue(key, fallbackValue) {
        var value = root.dynamicParamValues && root.dynamicParamValues[key] !== undefined
            ? root.dynamicParamValues[key]
            : fallbackValue
        var parsed = Number(value)
        return isFinite(parsed) ? parsed : fallbackValue
    }

    function integerRuntimeValue(key, fallbackValue) {
        var parsed = Math.floor(numericRuntimeValue(key, fallbackValue))
        return isFinite(parsed) && parsed > 0 ? parsed : fallbackValue
    }

    function updateRuntimeParameter(key, rawValue, fallbackValue, asInteger) {
        var parsed = asInteger ? Math.floor(Number(rawValue)) : Number(rawValue)
        var normalizedValue = isFinite(parsed) ? parsed : fallbackValue
        var nextValues = ({})

        for (var entryKey in root.dynamicParamValues) {
            if (root.dynamicParamValues.hasOwnProperty(entryKey)) {
                nextValues[entryKey] = root.dynamicParamValues[entryKey]
            }
        }

        nextValues[key] = normalizedValue
        root.dynamicParamsChanged(nextValues)
    }

    implicitHeight: contentColumn.implicitHeight + 28
    Layout.preferredHeight: implicitHeight
    radius: 12
    color: "#1E293B"

    onSelectedStrategyIdChanged: syncSelection(strategyComboBox, root.strategyOptions, root.selectedStrategyId, ["strategyId", "id", "value"], "syncingStrategySelection")
    onStrategyOptionsChanged: syncSelection(strategyComboBox, root.strategyOptions, root.selectedStrategyId, ["strategyId", "id", "value"], "syncingStrategySelection")
    onSelectedUniverseTypeChanged: syncSelection(universeComboBox, root.universeOptions, root.selectedUniverseType, ["value", "id", "label"], "syncingStrategySelection")
    onUniverseOptionsChanged: syncSelection(universeComboBox, root.universeOptions, root.selectedUniverseType, ["value", "id", "label"], "syncingStrategySelection")
    onSelectedIndexSymbolChanged: syncSelection(indexPoolComboBox, root.indexPoolOptions, root.selectedIndexSymbol, ["value", "symbol", "id", "label"], "syncingStrategySelection")
    onIndexPoolOptionsChanged: syncSelection(indexPoolComboBox, root.indexPoolOptions, root.selectedIndexSymbol, ["value", "symbol", "id", "label"], "syncingStrategySelection")
    onDataSourceOptionsChanged: syncSelection(dataSourceComboBox, root.dataSourceOptions, root.dynamicParamValues.dataSourceMode, ["value", "id", "label"], "syncingDataSourceSelection")

    ColumnLayout {
        id: contentColumn
        anchors.fill: parent
        anchors.margins: 14
        spacing: 10

        Text {
            text: "⚙️ 策略配置"
            font.pixelSize: 16
            font.weight: Font.DemiBold
            color: "#F1F5F9"
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: 12

            BacktestLabeledComboBox {
                id: strategyComboBox
                visible: root.showStrategySelector
                label: "选择策略"
                fieldWidth: 200
                options: root.strategyOptions
                textRole: "displayText"
                placeholder: "请选择策略"
                currentIndex: root.findOptionIndex(root.strategyOptions, root.selectedStrategyId, ["strategyId", "id", "value"])
                onOptionSelected: function(index, option) {
                    if (root.syncingStrategySelection || index < 0 || index >= root.strategyOptions.length) {
                        return
                    }
                    root.strategyOptionSelected(index, option)
                }
            }

            BacktestLabeledComboBox {
                id: dataSourceComboBox
                visible: root.showDataSourceSelector
                label: "数据源"
                fieldWidth: 180
                options: root.dataSourceOptions
                textRole: "label"
                placeholder: "请选择数据源"
                currentIndex: root.findOptionIndex(root.dataSourceOptions, root.dynamicParamValues.dataSourceMode, ["value", "id", "label"])
                onOptionSelected: function(index, option) {
                    if (root.syncingDataSourceSelection || index < 0 || index >= root.dataSourceOptions.length) {
                        return
                    }
                    root.dataSourceOptionSelected(index, option)
                }
            }

            BacktestLabeledComboBox {
                id: cacheDatasetComboBox
                visible: root.showCacheDatasetSelector
                label: "清洗数据"
                fieldWidth: 260
                options: root.cacheDatasetOptions
                textRole: "label"
                placeholder: "请选择清洗数据"
                currentIndex: root.selectedCacheDatasetIndex
                onOptionSelected: function(index, option) {
                    if (root.syncingCacheDatasetSelection || index < 0 || index >= root.cacheDatasetOptions.length) {
                        return
                    }
                    root.cacheDatasetOptionSelected(index, option)
                }
            }

            BacktestLabeledComboBox {
                id: universeComboBox
                visible: root.showUniverseSelectionControls
                label: "股票池"
                fieldWidth: 150
                options: root.universeOptions
                textRole: "label"
                placeholder: "请选择股票池"
                currentIndex: root.findOptionIndex(root.universeOptions, root.selectedUniverseType, ["value", "id", "label"])
                onOptionSelected: function(index, option) {
                    if (index < 0 || index >= root.universeOptions.length) {
                        return
                    }
                    root.universeOptionSelected(index, option)
                }
            }

            BacktestLabeledComboBox {
                id: indexPoolComboBox
                visible: root.showUniverseSelectionControls && root.selectedUniverseType === "index"
                label: "指数"
                fieldWidth: 170
                options: root.indexPoolOptions
                textRole: "label"
                placeholder: "请选择指数"
                currentIndex: root.findOptionIndex(root.indexPoolOptions, root.selectedIndexSymbol, ["value", "symbol", "id", "label"])
                onOptionSelected: function(index, option) {
                    if (index < 0 || index >= root.indexPoolOptions.length) {
                        return
                    }
                    root.indexOptionSelected(index, option)
                }
            }

            Item { Layout.fillWidth: true }
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: 12

            BacktestLabeledDatePicker {
                id: startDatePicker
                label: "开始日期"
                enabled: root.dateSelectionEnabled
                fieldWidth: 180
                fieldHeight: 36
                selectedDate: root.selectedStartDate
                onDateChanged: function(dateText) {
                    root.startDateSelected(dateText)
                }
            }

            BacktestLabeledDatePicker {
                id: endDatePicker
                label: "结束日期"
                enabled: root.dateSelectionEnabled
                fieldWidth: 180
                fieldHeight: 36
                selectedDate: root.selectedEndDate
                onDateChanged: function(dateText) {
                    root.endDateSelected(dateText)
                }
            }

            Text {
                Layout.fillWidth: true
                text: root.showCacheDatasetSelector
                    ? "缓存K线模式直接沿用所选清洗数据窗口和缓存股票池，不再单独选择指数成分。"
                    : "原始K线模式由你直接选择回测日期，并可继续配置股票池和指数范围。"
                font.pixelSize: 12
                color: "#94A3B8"
                wrapMode: Text.WordWrap
            }
        }

        Rectangle {
            id: paramConfigCard
            Layout.fillWidth: true
            implicitHeight: paramConfigColumn.implicitHeight + 20
            Layout.preferredHeight: implicitHeight
            radius: 8
            color: "#0F172A"
            border.width: 1
            border.color: "#334155"

            ColumnLayout {
                id: paramConfigColumn
                anchors.fill: parent
                anchors.margins: 10
                spacing: 6

                Text {
                    text: "📊 回测参数配置"
                    font.pixelSize: 13
                    font.weight: Font.DemiBold
                    color: "#F1F5F9"
                }

                Text {
                    text: root.showDynamicParamForm
                        ? "配置资金管理、交易成本和风险控制等参数"
                        : "当前策略没有动态参数元信息，以下显示内置回测运行参数。"
                    font.pixelSize: 11
                    color: "#94A3B8"
                    wrapMode: Text.WordWrap
                    Layout.fillWidth: true
                }

                Item {
                    Layout.fillWidth: true
                    visible: root.showDynamicParamForm
                    implicitHeight: Math.max(120, dynamicParamGenerator.implicitHeight)

                    PluginComponents.DynamicParamGenerator {
                        id: dynamicParamGenerator
                        anchors.horizontalCenter: parent.horizontalCenter
                        width: Math.min(parent.width, root.parameterFormMaxWidth)
                        height: Math.max(120, implicitHeight)
                        minColumnWidth: root.parameterFormMinColumnWidth
                        maxColumns: width >= (root.parameterFormMinColumnWidth * 2 + 16) ? 2 : 1
                        paramRegistry: root.paramRegistry
                        configs: root.dynamicParamConfigs
                        groups: root.dynamicParamGroups
                        showGroups: root.dynamicParamGroups.length > 0
                        values: root.dynamicParamValues

                        onParamsChanged: function(newValues) {
                            root.dynamicParamsChanged(newValues)
                        }
                    }
                }

                Item {
                    Layout.fillWidth: true
                    visible: root.showFallbackRuntimeForm
                    implicitHeight: fallbackRuntimeCard.implicitHeight

                    Rectangle {
                        id: fallbackRuntimeCard
                        anchors.horizontalCenter: parent.horizontalCenter
                        width: Math.min(parent.width, root.parameterFormMaxWidth)
                        radius: 10
                        color: "#111827"
                        border.width: 1
                        border.color: "#23324A"
                        implicitHeight: fallbackRuntimeColumn.implicitHeight + 24

                        ColumnLayout {
                            id: fallbackRuntimeColumn
                            anchors.fill: parent
                            anchors.margins: 12
                            spacing: 10

                            GridLayout {
                                Layout.fillWidth: true
                                columns: width >= 840 ? 3 : (width >= 560 ? 2 : 1)
                                columnSpacing: 12
                                rowSpacing: 10

                                ColumnLayout {
                                    Layout.fillWidth: true
                                    spacing: 4

                                    Text {
                                        text: "初始资金"
                                        font.pixelSize: 12
                                        color: "#CBD5E1"
                                    }

                                    TextField {
                                        id: initialCapitalField
                                        Layout.fillWidth: true
                                        text: String(root.numericRuntimeValue("initialCapital", 1000000))
                                        color: "#F8FAFC"
                                        validator: DoubleValidator { bottom: 0 }
                                        background: Rectangle {
                                            radius: 8
                                            color: "#0F172A"
                                            border.color: initialCapitalField.activeFocus ? "#38BDF8" : "#334155"
                                            border.width: 1
                                        }
                                        onEditingFinished: root.updateRuntimeParameter("initialCapital", text, 1000000, false)
                                    }
                                }

                                ColumnLayout {
                                    Layout.fillWidth: true
                                    spacing: 4

                                    Text {
                                        text: "手续费率"
                                        font.pixelSize: 12
                                        color: "#CBD5E1"
                                    }

                                    TextField {
                                        id: commissionRateField
                                        Layout.fillWidth: true
                                        text: String(root.numericRuntimeValue("commissionRate", 0.001))
                                        color: "#F8FAFC"
                                        validator: DoubleValidator { bottom: 0 }
                                        background: Rectangle {
                                            radius: 8
                                            color: "#0F172A"
                                            border.color: commissionRateField.activeFocus ? "#38BDF8" : "#334155"
                                            border.width: 1
                                        }
                                        onEditingFinished: root.updateRuntimeParameter("commissionRate", text, 0.001, false)
                                    }
                                }

                                ColumnLayout {
                                    Layout.fillWidth: true
                                    spacing: 4

                                    Text {
                                        text: "滑点率"
                                        font.pixelSize: 12
                                        color: "#CBD5E1"
                                    }

                                    TextField {
                                        id: slippageRateField
                                        Layout.fillWidth: true
                                        text: String(root.numericRuntimeValue("slippageRate", 0.001))
                                        color: "#F8FAFC"
                                        validator: DoubleValidator { bottom: 0 }
                                        background: Rectangle {
                                            radius: 8
                                            color: "#0F172A"
                                            border.color: slippageRateField.activeFocus ? "#38BDF8" : "#334155"
                                            border.width: 1
                                        }
                                        onEditingFinished: root.updateRuntimeParameter("slippageRate", text, 0.001, false)
                                    }
                                }

                                ColumnLayout {
                                    Layout.fillWidth: true
                                    spacing: 4

                                    Text {
                                        text: "单票仓位上限"
                                        font.pixelSize: 12
                                        color: "#CBD5E1"
                                    }

                                    TextField {
                                        id: maxPositionField
                                        Layout.fillWidth: true
                                        text: String(root.numericRuntimeValue("maxPositionPercent", 0.2))
                                        color: "#F8FAFC"
                                        validator: DoubleValidator { bottom: 0; top: 1 }
                                        background: Rectangle {
                                            radius: 8
                                            color: "#0F172A"
                                            border.color: maxPositionField.activeFocus ? "#38BDF8" : "#334155"
                                            border.width: 1
                                        }
                                        onEditingFinished: root.updateRuntimeParameter("maxPositionPercent", text, 0.2, false)
                                    }
                                }

                                ColumnLayout {
                                    Layout.fillWidth: true
                                    spacing: 4

                                    Text {
                                        text: "最大回撤限制"
                                        font.pixelSize: 12
                                        color: "#CBD5E1"
                                    }

                                    TextField {
                                        id: maxDrawdownField
                                        Layout.fillWidth: true
                                        text: String(root.numericRuntimeValue("maxDrawdownLimit", 0.15))
                                        color: "#F8FAFC"
                                        validator: DoubleValidator { bottom: 0; top: 1 }
                                        background: Rectangle {
                                            radius: 8
                                            color: "#0F172A"
                                            border.color: maxDrawdownField.activeFocus ? "#38BDF8" : "#334155"
                                            border.width: 1
                                        }
                                        onEditingFinished: root.updateRuntimeParameter("maxDrawdownLimit", text, 0.15, false)
                                    }
                                }

                                ColumnLayout {
                                    Layout.fillWidth: true
                                    spacing: 4

                                    Text {
                                        text: "调仓周期(天)"
                                        font.pixelSize: 12
                                        color: "#CBD5E1"
                                    }
 
                                    TextField {
                                        id: rebalanceDaysField
                                        Layout.fillWidth: true
                                        text: String(root.integerRuntimeValue("rebalanceDays", 5))
                                        color: "#F8FAFC"
                                        validator: IntValidator { bottom: 1 }
                                        background: Rectangle {
                                            radius: 8
                                            color: "#0F172A"
                                            border.color: rebalanceDaysField.activeFocus ? "#38BDF8" : "#334155"
                                            border.width: 1
                                        }
                                        onEditingFinished: root.updateRuntimeParameter("rebalanceDays", text, 5, true)
                                    }
                                }
                            }

                            Text {
                                Layout.fillWidth: true
                                text: "这些字段直接写入 strategy runtimeParameters；当前页不再假装已有动态配置。"
                                font.pixelSize: 11
                                color: "#64748B"
                                wrapMode: Text.WordWrap
                            }
                        }
                    }
                }

                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 168
                    visible: !root.parametersLoaded
                    radius: 10
                    color: "#111827"
                    border.width: 1
                    border.color: "#23324A"

                    ColumnLayout {
                        anchors.centerIn: parent
                        spacing: 8

                        BusyIndicator {
                            Layout.alignment: Qt.AlignHCenter
                            running: parent.parent.visible
                        }

                        Text {
                            Layout.alignment: Qt.AlignHCenter
                            text: "正在准备回测参数..."
                            font.pixelSize: 12
                            color: "#CBD5E1"
                        }

                        Text {
                            Layout.alignment: Qt.AlignHCenter
                            text: "参数就绪前不渲染动态表单，避免页面先显示后跳变。"
                            font.pixelSize: 11
                            color: "#64748B"
                        }
                    }
                }

                Text {
                    Layout.alignment: Qt.AlignHCenter
                    text: root.parametersLoaded
                        ? (root.showDynamicParamForm ? "参数配置已加载" : "已切换为内置回测参数表单")
                        : "正在加载回测参数配置..."
                    font.pixelSize: 11
                    color: root.parametersLoaded ? "#10B981" : "#F59E0B"
                }
            }
        }
    }
}