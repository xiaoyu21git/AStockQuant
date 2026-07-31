import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import ConsoleUi 1.0 as ConsoleUiComponents
import AStock.Bridge 1.0 as Bridge
import "../../components/FactorWorkbench/Creation/components" as PluginComponents
import "../../utils/RiskBacktestMetaLoader.js" as RiskBacktestMeta


Item {
    id: riskConfigPage

    readonly property bool compactLayout: width < 1180
    readonly property bool narrowLayout: width < 900
    readonly property int pagePadding: narrowLayout ? 12 : 24
    readonly property int sectionSpacing: narrowLayout ? 18 : 24
    readonly property bool extraWideLayout: width >= 1500
    readonly property bool forceFourStatCards: width >= 1180
    readonly property bool forceTwoColumnSections: width >= 1180
    readonly property int controlValueWidth: narrowLayout ? 64 : 84
    readonly property int compactValueBoxWidth: narrowLayout ? 82 : 92
    readonly property int stepperButtonSize: 28
    readonly property int cardInnerPadding: narrowLayout ? 18 : 24
    readonly property int sectionHeaderHeight: 28
    readonly property int sectionIntroHeight: narrowLayout ? 34 : 36
    readonly property int cardRadius: 20
    readonly property int subPanelRadius: 16
    readonly property int compactGap: 16
    readonly property int subgroupGap: 10

    readonly property color pageBg: "#0F172A"
    readonly property color pageBgTop: "#111827"
    readonly property color pageText: "#E2E8F0"
    readonly property color secondaryText: "#94A3B8"
    readonly property color subtleText: "#64748B"
    readonly property color cardBg: "#1E293B"
    readonly property color elevatedCardBg: "#111827"
    readonly property color cardBorder: "#334155"
    readonly property color cardBorderSoft: "#273449"
    readonly property color cardShadow: "#30000000"
    readonly property color tabHover: "#1E293B"
    readonly property color tabTrack: "#0F172A"
    readonly property color primaryBlue: "#3B82F6"
    readonly property color primaryBlueHover: "#2563EB"
    readonly property color primaryBlueSoft: "#172554"
    readonly property color successGreen: "#10B981"
    readonly property color successSoft: "#0F2F22"
    readonly property color warningOrange: "#F97316"
    readonly property color warningSoft: "#3A2A10"
    readonly property color dangerRed: "#EF4444"
    readonly property color dangerSoft: "#3B1215"
    readonly property color progressBg: "#334155"
    readonly property color shadowColor: "#12000000"
    readonly property color headerStripBg: "#131F33"
    readonly property color insetPanelBg: "#132238"
    readonly property color insetPanelBorder: "#26486E"

    property var dynamicParamConfigs: []
    property var dynamicParamGroups: []
    property var dynamicParamValues: ({})
    property var pendingPersistedValues: ({})
    property var strategySnapshots: []
    property var activeRiskStrategy: ({})
    property var activeBacktestRecord: ({})
    property var localActionHistory: []
    property string focusedStrategyId: ""
    property var externalRiskContext: ({})
    property bool parametersLoaded: false
    readonly property var riskConfigService: Bridge.RiskConfigService
    readonly property var riskMonitorService: Bridge.RiskControlBridge
    readonly property var strategyService: null
    readonly property int loadedParamCount: dynamicParamConfigs.length
    readonly property real varUsagePercent: riskMonitorService ? riskMonitorService.varUsagePercent : 68
    readonly property real currentDrawdownPercent: riskMonitorService ? riskMonitorService.currentDrawdownPercent : -4.2
    readonly property real currentTotalExposurePercent: riskMonitorService ? riskMonitorService.currentTotalExposurePercent : riskSummary.maxTotalExposure
    readonly property real currentVarBudgetAmount: riskMonitorService ? riskMonitorService.varBudgetAmount : 0
    readonly property real currentEstimatedVarAmount: riskMonitorService ? riskMonitorService.estimatedVarAmount : 0
    property real varWarningPercent: 80
    property real orderSizeLimit: 100
    property real turnoverLimit: 5000
    property real slippageLimit: 0.2
    property real level1Breaker: 2
    property real level2Breaker: 5
    property real level3Breaker: 8
    property bool autoStopEnabled: true

    property var riskSummary: ({
        stopLossPercent: 10.0,
        takeProfitPercent: 20.0,
        maxDrawdownLimit: 12.0,
        maxPositionPercent: 15.0,
        maxTotalExposure: 67.0,
        maxIndustryExposure: 30.0,
        maxThemeExposure: 25.0,
        maxDailyLoss: -5.0,
        maxCorrelation: 70.0
    })

    property var monitorStats: []
    property var positionRisks: []
    property var alertItems: []
    property var historyItems: []
    property string positionRiskSource: "allocation"
    property string positionRiskSourceLabel: ""
    property bool riskServicesWarmupQueued: false

    function ensureRiskServicesReady() {
        if (riskConfigService && typeof riskConfigService.initialize === "function") {
            riskConfigService.initialize()
        }
        if (riskMonitorService && typeof riskMonitorService.initializeAsync === "function") {
            riskMonitorService.initializeAsync()
        } else if (riskMonitorService && typeof riskMonitorService.initialize === "function") {
            riskMonitorService.initialize()
        }
        if (strategyService && typeof strategyService.initializeAsync === "function") {
            strategyService.initializeAsync()
        } else if (strategyService && typeof strategyService.initialize === "function") {
            strategyService.initialize()
        }
    }

    function scheduleRiskServicesWarmup() {
        if (!visible || riskServicesWarmupQueued) {
            return
        }

        riskServicesWarmupQueued = true
        Qt.callLater(function() {
            riskServicesWarmupQueued = false
            if (!visible) {
                return
            }
            ensureRiskServicesReady()
            refreshRiskOverviewData()
        })
    }

    Component.onCompleted: {
        pendingPersistedValues = loadPersistedConfiguration()
        applyPersistedAuxiliaryConfiguration(pendingPersistedValues)
        if (visible) {
            scheduleRiskServicesWarmup()
        }
    }

    onVisibleChanged: {
        if (!visible) {
            return
        }
        scheduleRiskServicesWarmup()
    }

    PluginComponents.ParamComponents {
        id: paramComponents

        Component.onCompleted: {
            if (typeof paramComponents.registerAllComponents === "function") {
                paramComponents.registerAllComponents()
            }
            riskConfigPage.initDynamicParams()
            paramLoadWatchdog.start()
        }
    }

    Timer {
        id: paramLoadWatchdog
        interval: 1500
        repeat: false
        onTriggered: {
            if (!riskConfigPage.parametersLoaded) {
                riskConfigPage.generateFallbackParamConfigs()
            }
        }
    }

    Connections {
        target: strategyService
        ignoreUnknownSignals: true

        function onStrategiesLoaded() {
            refreshRiskOverviewData()
        }

        function onDataChanged() {
            refreshRiskOverviewData()
        }

        function onInitializedChanged() {
            refreshRiskOverviewData()
        }
    }

    Connections {
        target: riskMonitorService
        ignoreUnknownSignals: true

        function onCurrentDrawdownPercentChanged() {
            alertItems = buildAlertItems(activeRiskStrategy, activeBacktestRecord, positionRisks)
        }

        function onVarUsagePercentChanged() {
            alertItems = buildAlertItems(activeRiskStrategy, activeBacktestRecord, positionRisks)
        }

        function onCurrentTotalExposurePercentChanged() {
            alertItems = buildAlertItems(activeRiskStrategy, activeBacktestRecord, positionRisks)
        }
    }

    Rectangle {
        anchors.fill: parent
        gradient: Gradient {
            GradientStop { position: 0.0; color: pageBgTop }
            GradientStop { position: 0.35; color: "#172033" }
            GradientStop { position: 1.0; color: pageBg }
        }
    }

    ScrollView {
            id: riskScrollView
            anchors.fill: parent
            anchors.margins: pagePadding
            clip: true
            contentWidth: availableWidth
            background: Rectangle {
                color: "transparent"
            }
            ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
            ScrollBar.vertical.policy: ScrollBar.AlwaysOff

            ColumnLayout {
                width: Math.max(0, riskScrollView.availableWidth)
                spacing: sectionSpacing

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 6

                    Text {
                        text: "风险控制模块"
                        font.pixelSize: 26
                        font.weight: Font.DemiBold
                        color: pageText
                    }

                    Text {
                        text: "配置规则 → 实时监控 → 自动执行 → 复盘分析"
                        font.pixelSize: 14
                        color: secondaryText
                    }

                    Text {
                        visible: focusedStrategyId.length > 0
                        text: activeRiskStrategy && Object.keys(activeRiskStrategy).length > 0
                            ? ("当前焦点组合: " + resolveStrategyName(activeRiskStrategy) + " · 来自组合策略上下文")
                            : "当前焦点组合: 等待同步策略上下文"
                        font.pixelSize: 12
                        color: primaryBlue
                    }
                }

                GridLayout {
                    Layout.fillWidth: true
                    columns: forceFourStatCards ? 4 : (width >= 900 ? 2 : 1)
                    rowSpacing: compactGap
                    columnSpacing: compactGap

                    Repeater {
                        model: 4

                        delegate: Rectangle {
                            readonly property var statCardData: [
                                { label: "当前组合净值", value: "1.284", note: "今日 +0.32%", tone: pageText },
                                { label: "当前回撤", value: currentDrawdownPercent.toFixed(1) + "%", note: drawdownStatusText(), tone: drawdownToneColor() },
                                { label: "风险预算使用率", value: Math.round(varUsagePercent) + "%", note: riskBudgetUsageNote(), tone: varUsageToneColor() },
                                { label: "当前总仓位", value: currentTotalExposurePercent.toFixed(1) + "%", note: exposureUsageNote(), tone: pageText }
                            ][index] || ({ label: "", value: "", note: "", tone: pageText })
                            Layout.fillWidth: true
                            Layout.preferredHeight: 138
                            radius: subPanelRadius
                            color: cardBg
                            border.color: cardBorder
                            border.width: 1

                            ColumnLayout {
                                anchors.fill: parent
                                anchors.margins: cardInnerPadding
                                spacing: 10

                                Text {
                                    text: statCardData.label
                                    font.pixelSize: 13
                                    color: secondaryText
                                }

                                Text {
                                    text: statCardData.value
                                    font.pixelSize: 32
                                    font.weight: Font.Bold
                                    font.family: "Consolas"
                                    color: statCardData.tone
                                }

                                Text {
                                    text: statCardData.note
                                    font.pixelSize: 12
                                    color: subtleText
                                    wrapMode: Text.WordWrap
                                    Layout.fillWidth: true
                                }
                            }
                        }
                    }
                }

                Rectangle {
                    Layout.fillWidth: true
                    radius: cardRadius
                    color: elevatedCardBg
                    border.color: cardBorder
                    border.width: 1
                    implicitHeight: configCardBody.implicitHeight + 76

                    ColumnLayout {
                        anchors.fill: parent

                        Rectangle {
                            Layout.fillWidth: true
                            Layout.preferredHeight: 66
                            color: headerStripBg
                            border.color: cardBorderSoft
                            border.width: 0

                            RowLayout {
                                anchors.fill: parent
                                anchors.leftMargin: cardInnerPadding
                                anchors.rightMargin: cardInnerPadding
                                spacing: 12

                                Text {
                                    text: "风控规则配置"
                                    font.pixelSize: 16
                                    font.weight: Font.DemiBold
                                    color: pageText
                                }

                                Item { Layout.fillWidth: true }

                                ConsoleUiComponents.ActionButton {
                                    label: "保存全部规则"
                                    tone: "primary"
                                    buttonWidth: 110
                                    buttonHeight: 38
                                    labelSize: 13
                                    onClicked: saveRiskConfiguration()
                                }

                                ConsoleUiComponents.ActionButton {
                                    label: "应用配置"
                                    tone: "success"
                                    buttonWidth: 96
                                    buttonHeight: 38
                                    labelSize: 13
                                    onClicked: applyRiskConfiguration()
                                }

                                ConsoleUiComponents.ActionButton {
                                    label: "恢复默认"
                                    tone: "muted"
                                    buttonWidth: 96
                                    buttonHeight: 38
                                    labelSize: 13
                                    onClicked: resetRiskDefaults()
                                }
                            }
                        }

                        ColumnLayout {
                            id: configCardBody
                            Layout.fillWidth: true
                            Layout.leftMargin: cardInnerPadding
                            Layout.rightMargin: cardInnerPadding
                            Layout.topMargin: cardInnerPadding
                            Layout.bottomMargin: cardInnerPadding
                            spacing: 18

                            Item {
                                Layout.fillWidth: true
                                implicitHeight: configSectionsFlow.implicitHeight

                                Flow {
                                    id: configSectionsFlow
                                    anchors.left: parent.left
                                    anchors.right: parent.right
                                    spacing: 24

                                    Item {
                                        id: coreConfigSection
                                        width: forceTwoColumnSections
                                            ? Math.max(0, (configSectionsFlow.width - configSectionsFlow.spacing) / 2)
                                            : configSectionsFlow.width
                                        implicitHeight: coreConfigSectionColumn.implicitHeight

                                        ColumnLayout {
                                            id: coreConfigSectionColumn
                                            anchors.left: parent.left
                                            anchors.right: parent.right
                                            anchors.top: parent.top
                                            spacing: 12

                                            Rectangle {
                                                Layout.fillWidth: true
                                                Layout.preferredHeight: sectionHeaderHeight
                                                radius: 8
                                                color: insetPanelBg
                                                border.color: cardBorderSoft
                                                border.width: 1

                                                Text {
                                                    anchors.left: parent.left
                                                    anchors.verticalCenter: parent.verticalCenter
                                                    anchors.leftMargin: 12
                                                    text: "组合层面与持仓限制"
                                                    font.pixelSize: 13
                                                    font.weight: Font.DemiBold
                                                    color: secondaryText
                                                }
                                            }

                                            Text {
                                                Layout.fillWidth: true
                                                Layout.preferredHeight: sectionIntroHeight
                                                text: "核心参数直接决定仓位、回撤和单标的风险边界，保持常驻显示并支持快速微调。"
                                                font.pixelSize: 12
                                                color: subtleText
                                                wrapMode: Text.WordWrap
                                                verticalAlignment: Text.AlignVCenter
                                            }

                                            RowLayout {
                                                Layout.fillWidth: true
                                                spacing: 8

                                                Repeater {
                                                    model: [
                                                        { label: "配置已同步", tone: "success" },
                                                        { label: "回测已同步", tone: "info" },
                                                        { label: "执行已生效", tone: "warning" }
                                                    ]

                                                    delegate: Rectangle {
                                                        radius: 10
                                                        height: 24
                                                        width: statusChipLabel.implicitWidth + 18
                                                        color: modelData.tone === "success"
                                                            ? successSoft
                                                            : (modelData.tone === "warning" ? warningSoft : primaryBlueSoft)
                                                        border.color: modelData.tone === "success"
                                                            ? successGreen
                                                            : (modelData.tone === "warning" ? warningOrange : "#31539A")
                                                        border.width: 1

                                                        Text {
                                                            id: statusChipLabel
                                                            anchors.centerIn: parent
                                                            text: modelData.label
                                                            font.pixelSize: 11
                                                            font.weight: Font.Medium
                                                            color: modelData.tone === "success"
                                                                ? "#6EE7B7"
                                                                : (modelData.tone === "warning" ? "#FDBA74" : "#BFDBFE")
                                                        }
                                                    }
                                                }
                                            }

                                            Rectangle {
                                                Layout.fillWidth: true
                                                Layout.preferredHeight: Math.max(coreControlsColumn.implicitHeight, optionalControlsColumn.implicitHeight) + 28
                                                radius: subPanelRadius
                                                color: insetPanelBg
                                                border.color: cardBorderSoft
                                                border.width: 1

                                                ColumnLayout {
                                                    id: coreControlsColumn
                                                    anchors.left: parent.left
                                                    anchors.right: parent.right
                                                    anchors.top: parent.top
                                                    anchors.margins: 14
                                                    spacing: 0

                                            Text {
                                                Layout.fillWidth: true
                                                text: "组合与持仓风控"
                                                font.pixelSize: 13
                                                font.weight: Font.DemiBold
                                                color: secondaryText
                                            }

                                            Text {
                                                Layout.fillWidth: true
                                                text: "详细风险参数统一放在下方高级参数区展示与编辑，当前区域仅保留概览与常用开关。"
                                                font.pixelSize: 12
                                                color: subtleText
                                                wrapMode: Text.WordWrap
                                            }

                                            Rectangle {
                                                Layout.fillWidth: true
                                                Layout.preferredHeight: 58
                                                color: "transparent"
                                                border.color: cardBorderSoft
                                                border.width: 0

                                                RowLayout {
                                                    anchors.fill: parent
                                                    spacing: 14

                                                    ColumnLayout {
                                                        Layout.fillWidth: true
                                                        spacing: 2

                                                        Text {
                                                            text: "自动止损"
                                                            font.pixelSize: 14
                                                            font.weight: Font.Medium
                                                            color: pageText
                                                        }

                                                        Text {
                                                            text: "达到止损线自动执行平仓"
                                                            font.pixelSize: 12
                                                            color: subtleText
                                                        }
                                                    }

                                                    Switch {
                                                        id: stopSwitch
                                                        Layout.alignment: Qt.AlignVCenter
                                                        checked: autoStopEnabled
                                                        onToggled: autoStopEnabled = checked
                                                    }
                                                }
                                            }
                                                }
                                            }
                                        }
                                    }

                                    Item {
                                        id: optionalConfigSection
                                        width: forceTwoColumnSections
                                            ? Math.max(0, (configSectionsFlow.width - configSectionsFlow.spacing) / 2)
                                            : configSectionsFlow.width
                                        implicitHeight: optionalConfigSectionColumn.implicitHeight

                                        ColumnLayout {
                                            id: optionalConfigSectionColumn
                                            anchors.left: parent.left
                                            anchors.right: parent.right
                                            anchors.top: parent.top
                                            spacing: 12

                                            Rectangle {
                                                Layout.fillWidth: true
                                                Layout.preferredHeight: sectionHeaderHeight
                                                radius: 8
                                                color: insetPanelBg
                                                border.color: cardBorderSoft
                                                border.width: 1

                                                Text {
                                                    anchors.left: parent.left
                                                    anchors.verticalCenter: parent.verticalCenter
                                                    anchors.leftMargin: 12
                                                    text: "可选执行参数"
                                                    font.pixelSize: 13
                                                    font.weight: Font.DemiBold
                                                    color: secondaryText
                                                }
                                            }

                                            Text {
                                                Layout.fillWidth: true
                                                Layout.preferredHeight: sectionIntroHeight
                                                text: "执行风控和熔断更偏向交易侧保护，当前保留配置能力，但不和左侧做不对称折叠。"
                                                font.pixelSize: 12
                                                color: subtleText
                                                wrapMode: Text.WordWrap
                                                verticalAlignment: Text.AlignVCenter
                                            }

                                            RowLayout {
                                                Layout.fillWidth: true
                                                spacing: 8

                                                Repeater {
                                                    model: [
                                                        { label: "配置已同步", tone: "success" },
                                                        { label: "回测已透传", tone: "info" },
                                                        { label: "执行部分生效", tone: "warning" }
                                                    ]

                                                    delegate: Rectangle {
                                                        radius: 10
                                                        height: 24
                                                        width: optionalStatusChipLabel.implicitWidth + 18
                                                        color: modelData.tone === "success"
                                                            ? successSoft
                                                            : (modelData.tone === "warning" ? warningSoft : primaryBlueSoft)
                                                        border.color: modelData.tone === "success"
                                                            ? successGreen
                                                            : (modelData.tone === "warning" ? warningOrange : "#31539A")
                                                        border.width: 1

                                                        Text {
                                                            id: optionalStatusChipLabel
                                                            anchors.centerIn: parent
                                                            text: modelData.label
                                                            font.pixelSize: 11
                                                            font.weight: Font.Medium
                                                            color: modelData.tone === "success"
                                                                ? "#6EE7B7"
                                                                : (modelData.tone === "warning" ? "#FDBA74" : "#BFDBFE")
                                                        }
                                                    }
                                                }
                                            }

                                            Rectangle {
                                                Layout.fillWidth: true
                                                Layout.preferredHeight: Math.max(coreControlsColumn.implicitHeight, optionalControlsColumn.implicitHeight) + 28
                                                radius: subPanelRadius
                                                color: insetPanelBg
                                                border.color: cardBorderSoft
                                                border.width: 1

                                                ColumnLayout {
                                                    id: optionalControlsColumn
                                                    anchors.left: parent.left
                                                    anchors.right: parent.right
                                                    anchors.top: parent.top
                                                    anchors.margins: 14
                                                    spacing: 0

                                            Text {
                                                Layout.fillWidth: true
                                                text: "交易执行与熔断"
                                                font.pixelSize: 13
                                                font.weight: Font.DemiBold
                                                color: secondaryText
                                            }

                                            Text {
                                                Layout.fillWidth: true
                                                text: "执行侧参数也统一由下方高级参数区管理，避免同一配置在页面上重复渲染。"
                                                font.pixelSize: 12
                                                color: subtleText
                                                wrapMode: Text.WordWrap
                                            }
                                                }
                                            }
                                        }
                                    }
                                }
                            }

                            Rectangle {
                                Layout.fillWidth: true
                                radius: subPanelRadius
                                color: insetPanelBg
                                border.color: insetPanelBorder
                                border.width: 1
                                implicitHeight: advancedParamsColumn.implicitHeight + 32

                                ColumnLayout {
                                    id: advancedParamsColumn
                                    anchors.left: parent.left
                                    anchors.right: parent.right
                                    anchors.top: parent.top
                                    anchors.margins: 16
                                    spacing: 12

                                    RowLayout {
                                        Layout.fillWidth: true

                                        Text {
                                            text: "高级参数"
                                            font.pixelSize: 14
                                            font.weight: Font.DemiBold
                                            color: pageText
                                        }

                                        Item { Layout.fillWidth: true }

                                        Text {
                                            text: parametersLoaded ? "已加载 " + loadedParamCount + " 项" : "正在加载参数"
                                            font.pixelSize: 12
                                            color: parametersLoaded ? successGreen : warningOrange
                                        }
                                    }

                                    PluginComponents.DynamicParamGenerator {
                                        id: dynamicParamGenerator
                                        Layout.fillWidth: true
                                        Layout.preferredHeight: 360
                                        visible: parametersLoaded && dynamicParamConfigs.length > 0
                                        minColumnWidth: width < 900 ? 280 : 320
                                        maxColumns: extraWideLayout ? 3 : (width < 900 ? 1 : 2)
                                        paramRegistry: paramComponents
                                        configs: riskConfigPage.dynamicParamConfigs
                                        groups: riskConfigPage.dynamicParamGroups
                                        showGroups: riskConfigPage.dynamicParamGroups.length > 0
                                        values: riskConfigPage.dynamicParamValues

                                        onParamsChanged: function(newValues) {
                                            riskConfigPage.dynamicParamValues = newValues
                                            riskConfigPage.updateRiskSummary(newValues)
                                        }
                                    }

                                    Rectangle {
                                        Layout.fillWidth: true
                                        Layout.preferredHeight: 176
                                        visible: !parametersLoaded
                                        radius: 12
                                        color: insetPanelBg
                                        border.color: insetPanelBorder
                                        border.width: 1

                                        ColumnLayout {
                                            anchors.centerIn: parent
                                            spacing: 8

                                            BusyIndicator {
                                                Layout.alignment: Qt.AlignHCenter
                                                running: parent.parent.visible
                                            }

                                            Text {
                                                Layout.alignment: Qt.AlignHCenter
                                                text: "正在准备风控参数..."
                                                font.pixelSize: 12
                                                color: pageText
                                            }

                                            Text {
                                                Layout.alignment: Qt.AlignHCenter
                                                text: "参数未就绪前只显示占位，不让动态表单后置跳出。"
                                                font.pixelSize: 11
                                                color: subtleText
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }
                }

                GridLayout {
                    Layout.fillWidth: true
                    columns: forceTwoColumnSections ? 2 : 1
                    rowSpacing: 24
                    columnSpacing: 24

                    Rectangle {
                        Layout.fillWidth: true
                        radius: cardRadius
                        color: cardBg
                        border.color: cardBorder
                        border.width: 1
                        implicitHeight: holdingsCardColumn.implicitHeight + 48

                        ColumnLayout {
                            id: holdingsCardColumn
                            anchors.left: parent.left
                            anchors.right: parent.right
                            anchors.top: parent.top
                            anchors.margins: cardInnerPadding
                            spacing: 12

                            RowLayout {
                                Layout.fillWidth: true
                                spacing: 12

                                Text {
                                    text: "组合风险明细"
                                    font.pixelSize: 16
                                    font.weight: Font.DemiBold
                                    color: pageText
                                }

                                Item { Layout.fillWidth: true }
                            }

                            RowLayout {
                                Layout.fillWidth: true
                                spacing: compactGap

                                Repeater {
                                    model: ["对象", "权重占比", "风险状态", "操作建议"]
                                    delegate: Text {
                                        Layout.fillWidth: true
                                        Layout.minimumWidth: 0
                                        text: modelData
                                        font.pixelSize: 14
                                        font.weight: Font.Medium
                                        color: secondaryText
                                    }
                                }
                            }

                            Repeater {
                                model: riskConfigPage.positionRisks.length

                                delegate: Rectangle {
                                    readonly property var positionRiskData: riskConfigPage.positionRisks[index] || ({})
                                    Layout.fillWidth: true
                                    Layout.preferredHeight: 42
                                    color: "transparent"
                                    border.color: cardBorderSoft
                                    border.width: index === riskConfigPage.positionRisks.length - 1 ? 0 : 1

                                    RowLayout {
                                        anchors.fill: parent
                                        spacing: 8

                                        Text {
                                            Layout.fillWidth: true
                                            Layout.minimumWidth: 0
                                            text: positionRiskData.name || ""
                                            font.pixelSize: 14
                                            color: pageText
                                            elide: Text.ElideRight
                                        }

                                        Text {
                                            Layout.fillWidth: true
                                            Layout.minimumWidth: 0
                                            text: positionRiskData.ratio || ""
                                            font.pixelSize: 14
                                            color: pageText
                                        }

                                        Rectangle {
                                            Layout.fillWidth: true
                                            Layout.minimumWidth: 0
                                            Layout.preferredHeight: 28
                                            radius: 14
                                            color: badgeBackground(positionRiskData.badgeType)

                                            Text {
                                                anchors.centerIn: parent
                                                text: positionRiskData.badgeText || ""
                                                font.pixelSize: 12
                                                font.weight: Font.Medium
                                                color: badgeTextColor(positionRiskData.badgeType)
                                            }
                                        }

                                        Text {
                                            Layout.fillWidth: true
                                            Layout.minimumWidth: 0
                                            text: positionRiskData.badgeType === "danger"
                                                ? "需要收缩配置"
                                                : (positionRiskData.badgeType === "warning" ? "接近上限" : "继续观察")
                                            font.pixelSize: 14
                                            color: positionRiskData.badgeType === "danger"
                                                ? dangerRed
                                                : (positionRiskData.badgeType === "warning" ? warningOrange : secondaryText)
                                            elide: Text.ElideRight
                                        }
                                    }
                                }
                            }

                            Text {
                                visible: positionRisks.length === 0
                                text: activeRiskStrategy && Object.keys(activeRiskStrategy).length > 0
                                    ? "当前关联策略未提供可展示的组合权重明细"
                                    : "StrategyService 中暂无可用策略数据"
                                font.pixelSize: 13
                                color: subtleText
                            }

                            Rectangle {
                                Layout.fillWidth: true
                                Layout.preferredHeight: 6
                                radius: 3
                                color: progressBg

                                Rectangle {
                                    width: parent.width * highestPositionRatio() / 100
                                    height: parent.height
                                    radius: 3
                                    color: highestPositionRatio() >= getConfigValue("maxPositionPercent", 15)
                                        ? dangerRed
                                        : warningOrange
                                }
                            }

                            Text {
                                text: "单项上限 " + getConfigValue("maxPositionPercent", 15).toFixed(0)
                                    + "% · 当前最高 " + highestPositionRatio().toFixed(1) + "%"
                                font.pixelSize: 12
                                color: secondaryText
                            }

                            ColumnLayout {
                                Layout.fillWidth: true
                                spacing: 6

                                Text {
                                    text: "组合权重概览"
                                    font.pixelSize: 13
                                    font.weight: Font.Medium
                                    color: secondaryText
                                }

                                Text {
                                    text: weightSummaryText()
                                    font.pixelSize: 14
                                    color: pageText
                                }

                                Rectangle {
                                    Layout.fillWidth: true
                                    Layout.preferredHeight: 6
                                    radius: 3
                                    color: progressBg

                                    Rectangle {
                                        width: parent.width * aggregateDisplayedWeightRatio()
                                        height: parent.height
                                        radius: 3
                                        color: primaryBlue
                                    }
                                }

                                Text {
                                    text: "已展示权重 " + displayedWeightTotal().toFixed(1)
                                        + "%"
                                    font.pixelSize: 12
                                    color: secondaryText
                                }
                            }
                        }
                    }

                    Rectangle {
                        Layout.fillWidth: true
                        radius: cardRadius
                        color: elevatedCardBg
                        border.color: cardBorder
                        border.width: 1
                        implicitHeight: forceTwoColumnSections
                            ? Math.max(alertsCardColumn.implicitHeight + 48, holdingsCardColumn.implicitHeight + 48)
                            : alertsCardColumn.implicitHeight + 48

                        ColumnLayout {
                            id: alertsCardColumn
                            anchors.left: parent.left
                            anchors.right: parent.right
                            anchors.top: parent.top
                            anchors.margins: cardInnerPadding
                            spacing: 12

                            RowLayout {
                                Layout.fillWidth: true
                                spacing: 12

                                Text {
                                    text: "实时预警"
                                    font.pixelSize: 16
                                    font.weight: Font.DemiBold
                                    color: pageText
                                }

                                Item { Layout.fillWidth: true }
                            }

                            Repeater {
                                model: riskConfigPage.alertItems.length

                                delegate: Rectangle {
                                    readonly property var alertItemData: riskConfigPage.alertItems[index] || ({})
                                    Layout.fillWidth: true
                                    Layout.preferredHeight: 72
                                    color: "transparent"
                                    border.color: cardBorderSoft
                                    border.width: index === riskConfigPage.alertItems.length - 1 ? 0 : 1

                                    RowLayout {
                                        anchors.fill: parent
                                        spacing: 12

                                        Rectangle {
                                            width: 32
                                            height: 32
                                            radius: 10
                                            color: alertBackground(alertItemData.level)

                                            Text {
                                                anchors.centerIn: parent
                                                text: alertItemData.icon || ""
                                                font.pixelSize: 15
                                                color: alertForeground(alertItemData.level)
                                            }
                                        }

                                        ColumnLayout {
                                            Layout.fillWidth: true
                                            spacing: 4

                                            Text {
                                                Layout.fillWidth: true
                                                text: alertItemData.title || ""
                                                font.pixelSize: 14
                                                font.weight: Font.Medium
                                                color: pageText
                                                wrapMode: Text.WordWrap
                                            }

                                            Text {
                                                text: alertItemData.time || ""
                                                font.pixelSize: 12
                                                color: subtleText
                                            }
                                        }
                                    }
                                }
                            }

                            Text {
                                visible: alertItems.length === 0
                                text: "当前没有可生成的真实预警项"
                                font.pixelSize: 13
                                color: subtleText
                            }
                        }
                    }
                }

                Rectangle {
                    Layout.fillWidth: true
                    radius: cardRadius
                    color: cardBg
                    border.color: cardBorder
                    border.width: 1
                    implicitHeight: actionsCardColumn.implicitHeight + 48

                    ColumnLayout {
                        id: actionsCardColumn
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.top: parent.top
                        anchors.margins: cardInnerPadding
                        spacing: compactGap

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 12

                            Text {
                                text: "风控操作"
                                font.pixelSize: 16
                                font.weight: Font.DemiBold
                                color: pageText
                            }

                            Item { Layout.fillWidth: true }
                        }

                        Flow {
                            Layout.fillWidth: true
                            spacing: compactGap

                            Repeater {
                                model: 4

                                delegate: ConsoleUiComponents.ActionButton {
                                    readonly property var actionButtonData: [
                                        { text: "一键减仓30%", style: "outline", action: "减仓30%" },
                                        { text: "全部平仓", style: "danger", action: "全部平仓" },
                                        { text: "清除预警", style: "outline", action: "清除预警" },
                                        { text: "导出风控报告", style: "primary", action: "导出风控报告" }
                                    ][index] || ({ text: "", style: "outline", action: "" })
                                    label: actionButtonData.text
                                    tone: actionButtonData.style === "primary"
                                        ? "primary"
                                        : (actionButtonData.style === "danger" ? "danger" : "muted")
                                    buttonWidth: Math.max(132, actionButtonData.text.length * 13 + 28)
                                    buttonHeight: 40
                                    labelSize: 13
                                    onClicked: appendHistory("【操作】" + actionButtonData.action + " 已执行")
                                }
                            }
                        }

                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 8

                            Text {
                                text: "最近操作记录"
                                font.pixelSize: 13
                                font.weight: Font.Medium
                                color: secondaryText
                            }

                            Repeater {
                                model: riskConfigPage.historyItems.length

                                delegate: Rectangle {
                                    readonly property var historyItemData: riskConfigPage.historyItems[index] || ({})
                                    Layout.fillWidth: true
                                    Layout.preferredHeight: 36
                                    color: "transparent"
                                    border.color: cardBorderSoft
                                    border.width: index === riskConfigPage.historyItems.length - 1 ? 0 : 1

                                    Text {
                                        anchors.left: parent.left
                                        anchors.right: parent.right
                                        anchors.verticalCenter: parent.verticalCenter
                                        anchors.leftMargin: 0
                                        anchors.rightMargin: 0
                                        text: (historyItemData.time || "") + " · " + (historyItemData.content || "")
                                        font.pixelSize: 13
                                        color: secondaryText
                                        elide: Text.ElideRight
                                    }
                                }
                            }

                            Text {
                                visible: historyItems.length === 0
                                text: "暂无来自策略服务或页面操作的记录"
                                font.pixelSize: 13
                                color: subtleText
                            }
                        }
                    }
                }
            }
        }

    function badgeBackground(type) {
        if (type === "warning") {
            return "#3A2A10"
        }
        if (type === "danger") {
            return "#3B1215"
        }
        return "#0F2F22"
    }

    function badgeTextColor(type) {
        if (type === "warning") {
            return "#FDBA74"
        }
        if (type === "danger") {
            return "#FCA5A5"
        }
        return "#6EE7B7"
    }

    function statusDotColor(type) {
        if (type === "yellow") {
            return "#F59E0B"
        }
        if (type === "red") {
            return dangerRed
        }
        return successGreen
    }

    function alertBackground(level) {
        if (level === "high") {
            return dangerSoft
        }
        if (level === "medium") {
            return warningSoft
        }
        return primaryBlueSoft
    }

    function alertForeground(level) {
        if (level === "high") {
            return dangerRed
        }
        if (level === "medium") {
            return "#FBBF24"
        }
        return "#93C5FD"
    }

    function actionButtonBg(style) {
        if (style === "primary") {
            return primaryBlue
        }
        if (style === "danger") {
            return dangerSoft
        }
        return cardBg
    }

    function actionButtonBorder(style) {
        if (style === "primary") {
            return "transparent"
        }
        if (style === "danger") {
            return "#7F1D1D"
        }
        return progressBg
    }

    function actionButtonText(style) {
        if (style === "primary") {
            return "#FFFFFF"
        }
        if (style === "danger") {
            return "#FCA5A5"
        }
        return pageText
    }

    function statAccentColor(trendType) {
        if (trendType === "up") {
            return successGreen
        }
        if (trendType === "down") {
            return warningOrange
        }
        return primaryBlue
    }

    function configSummaryText() {
        if (!parametersLoaded) {
            return "等待规则加载"
        }
        if (calculateRiskScore() < 3) {
            return "维持当前保护级别"
        }
        if (calculateRiskScore() < 7) {
            return "微调仓位与回撤阈值"
        }
        return "建议先收紧高风险阈值"
    }

    function preferredRiskParamGroups() {
        return [
            {
                id: "riskCore",
                name: "基础风险控制",
                description: "先配置止损、止盈、最大回撤和自动止损等基础保护参数。",
                minColumnWidth: 560,
                maxColumns: 2,
                params: ["stopLossPercent", "takeProfitPercent", "maxDrawdownLimit", "autoStopEnabled"]
            },
            {
                id: "exposureControl",
                name: "仓位与暴露控制",
                description: "集中处理仓位分配、总暴露、集中度和持仓数量限制。",
                minColumnWidth: 620,
                maxColumns: 2,
                params: ["positionSizingMethod", "maxTotalExposure", "maxPositionPercent", "maxIndustryExposure", "maxThemeExposure"]
            },
            {
                id: "executionLimits",
                name: "执行与交易限制",
                description: "约束日内成交规模、VaR 预警和单日损失等执行风险。",
                minColumnWidth: 760,
                maxColumns: 1,
                params: ["varWarningPercent", "orderSizeLimit", "turnoverLimit", "slippageLimit", "maxDailyLoss"]
            },
            {
                id: "breakerRules",
                name: "熔断与相关性限制",
                description: "用于控制极端波动场景下的熔断阈值和持仓相关性。",
                minColumnWidth: 760,
                maxColumns: 1,
                params: ["level1Breaker", "level2Breaker", "level3Breaker", "maxCorrelation"]
            }
        ]
    }

    function buildDynamicParamGroups(configs) {
        var configIdMap = ({})
        ;(configs || []).forEach(function(config) {
            if (config && config.id) {
                configIdMap[config.id] = true
            }
        })

        var groups = []
        preferredRiskParamGroups().forEach(function(group) {
            var resolvedParams = (group.params || []).filter(function(paramId) {
                return !!configIdMap[paramId]
            })

            if (resolvedParams.length === 0) {
                return
            }

            groups.push({
                id: group.id,
                name: group.name,
                description: group.description,
                minColumnWidth: group.minColumnWidth,
                maxColumns: group.maxColumns,
                params: resolvedParams
            })
        })

        return groups
    }

    function orderDynamicParamConfigs(configs) {
        var configMap = ({})
        var ordered = []
        var appended = ({})

        ;(configs || []).forEach(function(config) {
            if (config && config.id) {
                configMap[config.id] = config
            }
        })

        preferredRiskParamGroups().forEach(function(group) {
            ;(group.params || []).forEach(function(paramId) {
                if (!configMap[paramId] || appended[paramId]) {
                    return
                }

                appended[paramId] = true
                ordered.push(configMap[paramId])
            })
        })

        ;(configs || []).forEach(function(config) {
            if (!config || !config.id || appended[config.id]) {
                return
            }

            appended[config.id] = true
            ordered.push(config)
        })

        return ordered
    }

    function numberOrDefault(value, fallback) {
        var numericValue = Number(value)
        return isNaN(numericValue) ? fallback : numericValue
    }

    function parseTimestamp(value) {
        if (!value) {
            return 0
        }
        var normalizedValue = String(value).replace(" ", "T")
        var timestamp = Date.parse(normalizedValue)
        return isNaN(timestamp) ? 0 : timestamp
    }

    function getStrategyParameters(strategy) {
        if (!strategy) {
            return ({})
        }
        return strategy.parameters || ({})
    }

    function getStrategyPerformance(strategy) {
        if (!strategy) {
            return ({})
        }
        return strategy.performanceMetrics || ({})
    }

    function getLatestBacktest(strategy) {
        var performance = getStrategyPerformance(strategy)
        return performance.latestBacktest || ({})
    }

    function getStrategyAdvancedOptions(strategy) {
        return ({})
    }

    function getBacktestHistory(strategy) {
        var performance = getStrategyPerformance(strategy)
        return performance.backtestHistory || []
    }

    function resolveStrategyName(strategy) {
        if (!strategy) {
            return "未命名策略"
        }
        return strategy.strategyName || "未命名策略"
    }

    function resolveStrategyId(strategy) {
        if (!strategy) {
            return ""
        }
        return strategy.strategyId || ""
    }

    function normalizePercentFromRuntime(value) {
        var numericValue = Number(value)
        if (isNaN(numericValue)) {
            return 0
        }
        return Math.abs(numericValue) <= 1 ? numericValue * 100 : numericValue
    }

    function firstDefinedValue(source, keys) {
        if (!source) {
            return undefined
        }

        for (var index = 0; index < keys.length; ++index) {
            var key = keys[index]
            if (source[key] !== undefined && source[key] !== null && source[key] !== "") {
                return source[key]
            }
        }

        return undefined
    }

    function resolveStrategyConfigAliases(key) {
        switch (key) {
        case "maxPositionPercent":
            return ["maxPositionPercent"]
        case "maxTotalExposure":
            return ["maxTotalExposure"]
        default:
            return [key]
        }
    }

    function resolveFocusedStrategyConfigValue(key) {
        var strategy = activeRiskStrategy && Object.keys(activeRiskStrategy).length > 0
            ? activeRiskStrategy
            : resolveExternalPortfolioStrategy()
        if (!strategy || Object.keys(strategy).length === 0) {
            return undefined
        }

        var parameters = getStrategyParameters(strategy)
        var advancedOptions = getStrategyAdvancedOptions(strategy)
        var optimizationConfig = advancedOptions.optimization_config || ({})
        var latestBacktest = resolveActiveBacktest(strategy)
        var runtimeParameters = latestBacktest.runtimeParameters || ({})
        var runtimeConfig = parameters.backtest_runtime || strategy.backtest_runtime || ({})
        var aliases = resolveStrategyConfigAliases(key)
        var sources = [runtimeParameters, optimizationConfig, runtimeConfig, parameters, strategy]

        for (var index = 0; index < sources.length; ++index) {
            var value = firstDefinedValue(sources[index], aliases)
            if (value !== undefined) {
                return value
            }
        }

        return undefined
    }

    function loadStrategySnapshots() {
        if (!strategyService || typeof strategyService.getAllStrategies !== "function") {
            return []
        }

        var strategies = strategyService.getAllStrategies() || []
        var snapshots = []
        for (var index = 0; index < strategies.length; ++index) {
            var strategy = strategies[index] || ({})
            var strategyId = resolveStrategyId(strategy)
            if (strategyId && typeof strategyService.getStrategyById === "function") {
                var detail = strategyService.getStrategyById(strategyId)
                if (detail && Object.keys(detail).length > 0) {
                    snapshots.push(detail)
                    continue
                }
            }
            snapshots.push(strategy)
        }
        return snapshots
    }

    function resolveStrategySortTimestamp(strategy) {
        var latest = getLatestBacktest(strategy)
        var performance = getStrategyPerformance(strategy)
        return Math.max(
            parseTimestamp(latest.recordedAt),
            parseTimestamp(performance.lastBacktestAt),
            parseTimestamp(strategy ? (strategy.updated_at || strategy.updatedAt || strategy.created_at || strategy.createdAt) : "")
        )
    }

    function hasBacktestRecord(strategy) {
        var latest = getLatestBacktest(strategy)
        return latest && Object.keys(latest).length > 0
    }

    function isPortfolioStrategy(strategy) {
        var storedTypeIndex = Number(strategy && strategy.strategyTypeIndex)
        return Number.isFinite(storedTypeIndex)
            && Math.floor(storedTypeIndex) === StrategyCreation5
    }

    function resolveExternalPortfolioStrategy() {
        var strategy = externalRiskContext && externalRiskContext.strategy
            ? externalRiskContext.strategy
            : ({})
        return isPortfolioStrategy(strategy) ? strategy : ({})
    }

    function selectActiveRiskStrategy(strategies) {
        if (focusedStrategyId) {
            for (var focusedIndex = 0; focusedIndex < strategies.length; ++focusedIndex) {
                var focusedStrategy = strategies[focusedIndex] || ({})
                if (String(resolveStrategyId(focusedStrategy)) === String(focusedStrategyId)) {
                    return isPortfolioStrategy(focusedStrategy) ? focusedStrategy : ({})
                }
            }
        }

        var externalStrategy = resolveExternalPortfolioStrategy()
        if (Object.keys(externalStrategy).length > 0) {
            return externalStrategy
        }

        var bestPortfolio = null
        var bestPortfolioTs = -1

        for (var index = 0; index < strategies.length; ++index) {
            var strategy = strategies[index]
            if (!strategy) {
                continue
            }

            var timestamp = resolveStrategySortTimestamp(strategy)
            if (isPortfolioStrategy(strategy) && hasBacktestRecord(strategy) && timestamp >= bestPortfolioTs) {
                bestPortfolio = strategy
                bestPortfolioTs = timestamp
            }
        }

        if (bestPortfolio) {
            return bestPortfolio
        }

        for (var fallbackIndex = 0; fallbackIndex < strategies.length; ++fallbackIndex) {
            var fallback = strategies[fallbackIndex]
            if (isPortfolioStrategy(fallback)) {
                return fallback
            }
        }
        return ({})
    }

    function parseAllocationList(rawValue) {
        if (!rawValue) {
            return []
        }
        if (rawValue instanceof Array) {
            return rawValue
        }
        if (typeof rawValue === "string") {
            try {
                var parsed = JSON.parse(rawValue)
                return parsed instanceof Array ? parsed : []
            } catch (error) {
                console.warn("RiskConfigurationPage: failed to parse allocations", error)
            }
        }
        return []
    }

    function normalizeAllocationName(item, index) {
        if (!item) {
            return "配置项 " + (index + 1)
        }
        return item.display_name
            || item.factor_id
            || ("配置项 " + (index + 1))
    }

    function normalizeAllocationWeight(item) {
        if (!item) {
            return 0
        }
        return normalizePercentFromRuntime(
            item.weight !== undefined ? item.weight
                : (item.ratio !== undefined ? item.ratio
                    : (item.allocation !== undefined ? item.allocation : item.value))
        )
    }

    function getBacktestTradeRecords(backtestRecord) {
        if (!backtestRecord || typeof backtestRecord !== "object") {
            return []
        }
        if (backtestRecord.tradeRecords instanceof Array) {
            return backtestRecord.tradeRecords
        }
        return []
    }

    function findLatestTradeSnapshotTimestamp(tradeRecords) {
        var latestTimestamp = 0
        for (var index = 0; index < tradeRecords.length; ++index) {
            var trade = tradeRecords[index] || ({})
            var exitTimestamp = parseTimestamp(trade.exitTime)
            var entryTimestamp = parseTimestamp(trade.entryTime)
            latestTimestamp = Math.max(latestTimestamp, exitTimestamp, entryTimestamp)
        }
        return latestTimestamp
    }

    function normalizeTradeDirection(direction) {
        var normalized = String(direction || "long").toLowerCase()
        return normalized === "short" ? "short" : "long"
    }

    function buildActualPositionRisks(backtestRecord) {
        var tradeRecords = getBacktestTradeRecords(backtestRecord)
        if (tradeRecords.length === 0) {
            return []
        }

        var snapshotTimestamp = findLatestTradeSnapshotTimestamp(tradeRecords)
        if (snapshotTimestamp <= 0) {
            return []
        }

        var exposureBySymbol = ({})
        var totalExposure = 0

        for (var index = 0; index < tradeRecords.length; ++index) {
            var trade = tradeRecords[index] || ({})
            var symbol = trade.symbol || ""
            if (!symbol) {
                continue
            }

            var entryTimestamp = parseTimestamp(trade.entryTime)
            var exitTimestamp = parseTimestamp(trade.exitTime)
            if (entryTimestamp <= 0) {
                continue
            }

            var effectiveExit = exitTimestamp > 0 ? exitTimestamp : snapshotTimestamp
            if (entryTimestamp > snapshotTimestamp || effectiveExit < snapshotTimestamp) {
                continue
            }

            var quantity = Math.abs(numberOrDefault(trade.quantity, 0))
            var price = numberOrDefault(trade.exitPrice !== undefined ? trade.exitPrice : trade.exit_price, 0)
            if (price <= 0) {
                price = numberOrDefault(trade.entryPrice !== undefined ? trade.entryPrice : trade.entry_price, 0)
            }
            var marketValue = quantity * Math.max(price, 0)
            if (marketValue <= 0) {
                continue
            }

            if (!exposureBySymbol[symbol]) {
                exposureBySymbol[symbol] = {
                    symbol: symbol,
                    marketValue: 0,
                    quantity: 0,
                    direction: normalizeTradeDirection(trade.direction),
                    snapshotTime: trade.exitTime || trade.entryTime || "",
                    notes: trade.notes || ""
                }
            }

            exposureBySymbol[symbol].marketValue += marketValue
            exposureBySymbol[symbol].quantity += quantity
            totalExposure += marketValue
        }

        if (totalExposure <= 0) {
            return []
        }

        var limit = getConfigValue("maxPositionPercent", 15)
        var rows = []
        for (var key in exposureBySymbol) {
            if (!Object.prototype.hasOwnProperty.call(exposureBySymbol, key)) {
                continue
            }
            var position = exposureBySymbol[key]
            var ratioValue = position.marketValue / totalExposure * 100
            var badgeType = "normal"
            var badgeText = "回测内"
            var statusText = "正常"
            var statusType = "green"

            if (ratioValue >= limit) {
                badgeType = "danger"
                badgeText = "超出上限"
                statusText = "高风险"
                statusType = "red"
            } else if (ratioValue >= limit * 0.8) {
                badgeType = "warning"
                badgeText = "接近上限"
                statusText = "预警"
                statusType = "yellow"
            }

            rows.push({
                name: position.symbol,
                ratio: ratioValue.toFixed(1) + "%",
                ratioValue: ratioValue,
                badgeText: badgeText,
                badgeType: badgeType,
                statusText: statusText,
                statusType: statusType,
                source: "actualBacktest",
                snapshotTime: position.snapshotTime,
                notes: position.notes,
                quantity: position.quantity
            })
        }

        rows.sort(function(left, right) {
            return (right.ratioValue || 0) - (left.ratioValue || 0)
        })
        return rows.slice(0, 8)
    }

    function buildPositionRisks(strategy) {
        var actualRows = buildActualPositionRisks(activeBacktestRecord || ({}))
        if (actualRows.length > 0) {
            positionRiskSource = "actualBacktest"
            positionRiskSourceLabel = "最近一次回测实际持仓"
            return actualRows
        }

        if (externalRiskContext
                && externalRiskContext.snapshot
                && externalRiskContext.snapshot.status === "success"
                && externalRiskContext.snapshot.positions
                && externalRiskContext.snapshot.positions.length > 0
                && (!focusedStrategyId || String(externalRiskContext.strategyId || "") === String(focusedStrategyId))) {
            positionRiskSource = "candidateSnapshot"
            positionRiskSourceLabel = String((externalRiskContext.snapshot.diagnostics || {}).universeSourceLabel || "")
            return externalRiskContext.snapshot.positions
        }

        var snapshot = ({})
        if (strategy && Object.keys(strategy).length > 0 && riskMonitorService) {
            snapshot = riskMonitorService.buildPortfolioSnapshot(strategy, activeBacktestRecord || ({})) || ({})
        }

        if (snapshot.status === "success" && snapshot.positions && snapshot.positions.length > 0) {
            positionRiskSource = "candidateSnapshot"
            positionRiskSourceLabel = String((snapshot.diagnostics || {}).universeSourceLabel || "")
            return snapshot.positions
        }

        var parameters = getStrategyParameters(strategy)
        var allocations = parseAllocationList(parameters.portfolio_allocations_json)
        var limit = getConfigValue("maxPositionPercent", 15)
        var rows = []

        for (var index = 0; index < allocations.length; ++index) {
            var allocation = allocations[index]
            var ratioValue = normalizeAllocationWeight(allocation)
            var badgeType = "normal"
            var badgeText = "正常"
            var statusText = "正常"
            var statusType = "green"

            if (ratioValue >= limit) {
                badgeType = "danger"
                badgeText = "超出上限"
                statusText = "高风险"
                statusType = "red"
            } else if (ratioValue >= limit * 0.8) {
                badgeType = "warning"
                badgeText = "接近上限"
                statusText = "预警"
                statusType = "yellow"
            }

            rows.push({
                name: normalizeAllocationName(allocation, index),
                ratio: ratioValue.toFixed(1) + "%",
                ratioValue: ratioValue,
                badgeText: badgeText,
                badgeType: badgeType,
                statusText: statusText,
                statusType: statusType
            })
        }

        rows.sort(function(left, right) {
            return (right.ratioValue || 0) - (left.ratioValue || 0)
        })
        positionRiskSource = "allocation"
        positionRiskSourceLabel = "组合配置权重"
        return rows.slice(0, 8)
    }

    function buildMonitorStats(strategy, latestBacktest) {
        var stats = []
        var summary = latestBacktest.summary || ({})
        if (latestBacktest && Object.keys(latestBacktest).length > 0) {
            var totalReturnPercent = numberOrDefault(summary.returns, 0)
            var netValue = 1 + totalReturnPercent / 100
            var maxDrawdown = Math.abs(numberOrDefault(summary.maxDrawdown, 0))
            var winRate = numberOrDefault(summary.winRate, 0)
            var tradeCount = numberOrDefault(summary.tradesCount, 0)
            var sharpeRatio = numberOrDefault(summary.sharpeRatio, 0)

            stats.push({ label: "最近回测净值", value: netValue.toFixed(3), note: resolveStrategyName(strategy), tone: pageText })
            stats.push({ label: "最大回撤", value: "-" + maxDrawdown.toFixed(1) + "%", note: drawdownRiskNote(maxDrawdown), tone: maxDrawdown >= getConfigValue("maxDrawdownLimit", 12) ? dangerRed : warningOrange })
            stats.push({ label: "交易胜率", value: winRate.toFixed(1) + "%", note: "夏普比率 " + sharpeRatio.toFixed(2), tone: winRate >= 50 ? successGreen : warningOrange })
            stats.push({ label: "交易次数", value: Math.round(tradeCount).toString(), note: "运行 " + Math.round(numberOrDefault(summary.runningDays, 0)) + " 天", tone: pageText })
            return stats
        }

        var portfolioCount = 0
        for (var index = 0; index < strategySnapshots.length; ++index) {
            if (isPortfolioStrategy(strategySnapshots[index])) {
                portfolioCount += 1
            }
        }

        stats.push({ label: "已保存策略", value: strategySnapshots.length.toString(), note: "来自 StrategyService", tone: pageText })
        stats.push({ label: "组合策略", value: portfolioCount.toString(), note: activeRiskStrategy && Object.keys(activeRiskStrategy).length > 0 ? resolveStrategyName(activeRiskStrategy) : "暂无主策略", tone: pageText })
        stats.push({ label: "已应用规则", value: Object.keys(loadPersistedConfiguration()).length.toString(), note: parametersLoaded ? ("已加载 " + loadedParamCount + " 项") : "等待规则加载", tone: primaryBlue })
        stats.push({ label: "当前风险级别", value: getRiskLevelText(), note: configSummaryText(), tone: getRiskLevelColor() })
        return stats
    }

    function buildHistoryContent(strategy, historyEntry) {
        var summary = historyEntry && historyEntry.summary ? historyEntry.summary : ({})
        return resolveStrategyName(strategy)
            + " 回测完成 · 收益 " + numberOrDefault(summary.returns, 0).toFixed(2) + "%"
            + " · 最大回撤 " + numberOrDefault(summary.maxDrawdown, 0).toFixed(2) + "%"
            + " · 交易 " + Math.round(numberOrDefault(summary.tradesCount, 0)) + " 次"
    }

    function buildAlertItems(strategy, latestBacktest, positions) {
        var alerts = []
        var summary = latestBacktest.summary || ({})
        var drawdownLimit = getConfigValue("maxDrawdownLimit", 12)
        var maxDrawdown = Math.abs(numberOrDefault(summary.maxDrawdown, 0))
        var winRate = numberOrDefault(summary.winRate, 0)
        var varWarningThreshold = getConfigValue("varWarningPercent", 80)
        var warningPositions = 0
        var dangerPositions = 0

        for (var index = 0; index < positions.length; ++index) {
            if (positions[index].badgeType === "danger") {
                dangerPositions += 1
            } else if (positions[index].badgeType === "warning") {
                warningPositions += 1
            }
        }

        if (latestBacktest && Object.keys(latestBacktest).length > 0) {
            if (drawdownLimit > 0 && maxDrawdown >= drawdownLimit) {
                alerts.push({ level: "high", icon: "!", title: "最近一次回测最大回撤已超过配置阈值，建议先收紧仓位与止损", time: latestBacktest.recordedAt || "" })
            } else if (drawdownLimit > 0 && maxDrawdown >= drawdownLimit * 0.8) {
                alerts.push({ level: "medium", icon: "~", title: "最近一次回测最大回撤已接近阈值 " + drawdownLimit.toFixed(1) + "%", time: latestBacktest.recordedAt || "" })
            }

            if (dangerPositions > 0) {
                alerts.push({ level: "high", icon: "!", title: (positionRiskSource === "actualBacktest" ? "最近一次回测持仓中有 " : "候选持仓中有 ") + dangerPositions + " 项目标仓位超出单项上限", time: latestBacktest.recordedAt || "" })
            } else if (warningPositions > 0) {
                alerts.push({ level: "medium", icon: "~", title: (positionRiskSource === "actualBacktest" ? "最近一次回测持仓中有 " : "候选持仓中有 ") + warningPositions + " 项目标仓位接近单项上限", time: latestBacktest.recordedAt || "" })
            }

            if (winRate > 0 && winRate < 45) {
                alerts.push({ level: "low", icon: "i", title: "最近一次回测胜率为 " + winRate.toFixed(1) + "% ，建议复核调仓频率与止损参数", time: latestBacktest.recordedAt || "" })
            }
        }

        if (varWarningThreshold > 0 && varUsagePercent >= varWarningThreshold) {
            alerts.push({
                level: varUsagePercent >= 100 ? "high" : "medium",
                icon: varUsagePercent >= 100 ? "!" : "~",
                title: "实时风险预算使用率已达 " + varUsagePercent.toFixed(1) + "% ，超过预警阈值 " + varWarningThreshold.toFixed(1) + "%",
                time: Qt.formatDateTime(new Date(), "yyyy-MM-dd HH:mm:ss")
            })
        }

        if (alerts.length === 0) {
            alerts.push({
                level: "low",
                icon: "i",
                title: strategy && Object.keys(strategy).length > 0
                    ? (hasBacktestRecord(strategy)
                        ? "已接入真实策略与回测数据，当前未发现超限项"
                        : "已接入真实策略数据，但该策略暂无回测记录")
                    : "已接入 RiskConfigService，等待 StrategyService 提供策略数据",
                time: latestBacktest.recordedAt || Qt.formatDateTime(new Date(), "yyyy-MM-dd HH:mm:ss")
            })
        }

        return alerts.slice(0, 4)
    }

    function buildHistoryItems(strategy) {
        var combined = []
        var history = getBacktestHistory(strategy)

        for (var localIndex = 0; localIndex < localActionHistory.length; ++localIndex) {
            combined.push(localActionHistory[localIndex])
        }

        for (var index = 0; index < history.length; ++index) {
            var historyEntry = history[index] || ({})
            combined.push({
                time: historyEntry.recordedAt || historyEntry.lastBacktestAt || "",
                content: buildHistoryContent(strategy, historyEntry)
            })
        }

        return combined.slice(0, 8)
    }

    function resolveActiveBacktest(strategy) {
        if (externalRiskContext
                && externalRiskContext.latestBacktest
                && Object.keys(externalRiskContext.latestBacktest).length > 0
                && (!focusedStrategyId || String(externalRiskContext.strategyId || "") === String(resolveStrategyId(strategy) || focusedStrategyId))) {
            return externalRiskContext.latestBacktest
        }
        return getLatestBacktest(strategy)
    }

    function refreshRiskOverviewData() {
        strategySnapshots = loadStrategySnapshots()
        activeRiskStrategy = selectActiveRiskStrategy(strategySnapshots)
        activeBacktestRecord = resolveActiveBacktest(activeRiskStrategy)
        positionRisks = buildPositionRisks(activeRiskStrategy)
        monitorStats = buildMonitorStats(activeRiskStrategy, activeBacktestRecord)
        alertItems = buildAlertItems(activeRiskStrategy, activeBacktestRecord, positionRisks)
        historyItems = buildHistoryItems(activeRiskStrategy)
    }

    function applyExternalContext(context) {
        externalRiskContext = context || ({})
        focusedStrategyId = String((context || {}).strategyId || resolveStrategyId((context || {}).strategy) || "")

        var strategyLabel = String((context || {}).strategyName || resolveStrategyName((context || {}).strategy) || "未命名策略")
        var newHistory = []
        newHistory.push({
            time: String((context || {}).recordedAt || Qt.formatDateTime(new Date(), "yyyy-MM-dd HH:mm:ss")),
            content: "从组合策略同步风险上下文 · " + strategyLabel
        })

        for (var index = 0; index < localActionHistory.length && index < 7; ++index) {
            newHistory.push(localActionHistory[index])
        }
        localActionHistory = newHistory
        refreshRiskOverviewData()
    }

    function drawdownRiskNote(maxDrawdown) {
        var remaining = getConfigValue("maxDrawdownLimit", 12) - maxDrawdown
        return remaining <= 0 ? "已超过当前阈值" : ("距离阈值剩余 " + remaining.toFixed(1) + "%")
    }

    function riskBudgetUsageNote() {
        if (currentVarBudgetAmount <= 0) {
            return "等待实时账户与风控预算"
        }
        return "预算 ¥" + Math.round(currentVarBudgetAmount).toLocaleString() + " · 估算占用 ¥" + Math.round(currentEstimatedVarAmount).toLocaleString()
    }

    function exposureUsageNote() {
        var maxExposure = getConfigValue("maxTotalExposure", 67)
        if (maxExposure <= 0) {
            return "未配置总仓位预算"
        }
        return "距上限剩余 " + Math.max(0, maxExposure - currentTotalExposurePercent).toFixed(1) + "%"
    }

    function highestPositionRatio() {
        if (positionRisks.length === 0) {
            return 0
        }
        return numberOrDefault(positionRisks[0].ratioValue, 0)
    }

    function displayedWeightTotal() {
        var total = 0
        for (var index = 0; index < positionRisks.length; ++index) {
            total += numberOrDefault(positionRisks[index].ratioValue, 0)
        }
        return total
    }

    function aggregateDisplayedWeightRatio() {
        return Math.max(0, Math.min(1, displayedWeightTotal() / 100))
    }

    function weightSummaryText() {
        if (positionRisks.length === 0) {
            return activeRiskStrategy && Object.keys(activeRiskStrategy).length > 0
                ? (resolveStrategyName(activeRiskStrategy) + " 暂无可生成的持仓快照")
                : "等待策略服务提供持仓上下文"
        }
        var displayedCount = positionRisks.length
        var averageWeight = displayedWeightTotal() / Math.max(1, displayedCount)
        var prefix = "配置权重"
        if (positionRiskSource === "actualBacktest") {
            prefix = "最近回测实际持仓"
        } else if (positionRiskSource === "candidateSnapshot") {
            prefix = "最新候选持仓"
        }
        if (positionRiskSource === "candidateSnapshot" && positionRiskSourceLabel.length > 0) {
            prefix += " · 来源: " + positionRiskSourceLabel
        }
        return prefix + " · 已展示 " + displayedCount + " 项 · 最高权重 " + highestPositionRatio().toFixed(1)
            + "% · 平均权重 " + averageWeight.toFixed(1) + "%"
    }

    function getConfigValue(key, fallback) {
        var rawValue = resolveFocusedStrategyConfigValue(key)
        if (rawValue === undefined || rawValue === null || rawValue === "") {
            rawValue = dynamicParamValues[key]
        }
        if (rawValue === undefined || rawValue === null || rawValue === "") {
            return fallback
        }

        var numericValue = Number(rawValue)
        if (isNaN(numericValue)) {
            return fallback
        }

        return Math.abs(numericValue) <= 1 ? numericValue * 100 : numericValue
    }

    function setConfigValue(key, value) {
        var updatedValues = cloneObject(dynamicParamValues)
        updatedValues[key] = value
        dynamicParamValues = updatedValues

        if (dynamicParamGenerator) {
            dynamicParamGenerator.setValue(key, value)
        }

        updateRiskSummary(updatedValues)
    }

    function resolveControlValue(control) {
        if (control.target === "dynamic") {
            return getConfigValue(control.key, control.fallback)
        }
        return riskConfigPage[control.key]
    }

    function applyControlValue(control, rawValue) {
        var numericValue = Number(rawValue)
        if (isNaN(numericValue)) {
            return
        }

        if (control.target === "dynamic") {
            setConfigValue(control.key, control.negativeStorage ? -numericValue : numericValue)
            return
        }

        riskConfigPage[control.key] = numericValue
    }

    function stepControlValue(control, direction) {
        var stepSize = Number(control.step !== undefined ? control.step : 1)
        if (isNaN(stepSize) || stepSize <= 0) {
            stepSize = 1
        }

        var nextValue = resolveControlValue(control) + stepSize * direction
        var minValue = Number(control.min)
        var maxValue = Number(control.max)
        if (!isNaN(minValue)) {
            nextValue = Math.max(minValue, nextValue)
        }
        if (!isNaN(maxValue)) {
            nextValue = Math.min(maxValue, nextValue)
        }

        var decimals = control.decimals !== undefined ? control.decimals : 0
        nextValue = Number(nextValue.toFixed(decimals))
        applyControlValue(control, nextValue)
    }

    function formatControlValue(control) {
        var numericValue = resolveControlValue(control)
        var decimals = control.decimals !== undefined ? control.decimals : 0
        var prefix = control.negativeDisplay ? "-" : ""
        return prefix + Number(numericValue).toFixed(decimals) + (control.suffix || "")
    }

    function drawdownStatusText() {
        var remaining = getConfigValue("maxDrawdownLimit", 12) - Math.abs(currentDrawdownPercent)
        if (remaining <= 0) {
            return "⚠ 已达止损线"
        }
        return "距止损线 " + remaining.toFixed(1) + "%"
    }

    function drawdownToneColor() {
        return Math.abs(currentDrawdownPercent) >= getConfigValue("maxDrawdownLimit", 12) ? dangerRed : warningOrange
    }

    function varUsageToneColor() {
        return varUsagePercent >= varWarningPercent ? dangerRed : pageText
    }

    function resetRiskDefaults() {
        varWarningPercent = 80
        orderSizeLimit = 100
        turnoverLimit = 5000
        slippageLimit = 0.2
        level1Breaker = 2
        level2Breaker = 5
        level3Breaker = 8
        autoStopEnabled = true

        generateFallbackParamConfigs()
        appendHistory("【操作】风控规则已恢复默认配置")
    }

    function appendHistory(message) {
        var timeString = Qt.formatDateTime(new Date(), "yyyy-MM-dd HH:mm:ss")
        var newHistory = [{ time: timeString, content: message }]
        for (var index = 0; index < localActionHistory.length && index < 7; ++index) {
            newHistory.push(localActionHistory[index])
        }
        localActionHistory = newHistory
        historyItems = buildHistoryItems(activeRiskStrategy)
    }

    function initDynamicParams() {
        generateDynamicParamConfigs()
    }

    function generateDynamicParamConfigs() {
        RiskBacktestMeta.loadMetaFile("qrc:/config/views/risk_backtest_params.json", function(meta) {
            if (meta) {
                dynamicParamConfigs = []
                var riskParamConfigs = RiskBacktestMeta.getParameterConfigs("risk", "qrc:/config/views/risk_backtest_params.json")

                riskParamConfigs.forEach(function(paramConfig) {
                    var config = {
                        id: paramConfig.id,
                        type: paramConfig.type,
                        label: paramConfig.label,
                        description: paramConfig.description,
                        default: paramConfig.default,
                        category: paramConfig.category,
                        group: paramConfig.group || paramConfig.category || "风险管理"
                    }

                    switch (paramConfig.type) {
                        case "slider":
                            config.min = paramConfig.min
                            config.max = paramConfig.max
                            config.step = paramConfig.step || 0.01
                            config.unit = paramConfig.unit || ""
                            config.decimals = paramConfig.decimals !== undefined
                                ? paramConfig.decimals
                                : ((config.step && config.step < 1) ? 4 : 0)
                            break
                        case "select":
                            config.type = "select"
                            config.options = paramConfig.options || []
                            config.multiple = paramConfig.multiple || false
                            break
                        case "toggle":
                            config.type = "toggle"
                            config.trueLabel = paramConfig.trueLabel || "是"
                            config.falseLabel = paramConfig.falseLabel || "否"
                            break
                    }

                    if (paramConfig.visibleWhen) {
                        config.visibleWhen = paramConfig.visibleWhen
                    }

                    dynamicParamConfigs.push(config)
                })

                dynamicParamConfigs = orderDynamicParamConfigs(dynamicParamConfigs)
                dynamicParamGroups = buildDynamicParamGroups(dynamicParamConfigs)

                if (dynamicParamGenerator) {
                    dynamicParamGenerator.reloadConfigs(dynamicParamConfigs, dynamicParamGroups)
                }

                initDynamicValues()

                parametersLoaded = true
                restorePersistedConfiguration()
                updateRiskSummary(dynamicParamValues)
            } else {
                generateFallbackParamConfigs()
            }
        })
    }

    function generateFallbackParamConfigs() {
        dynamicParamConfigs = [
            { id: "stopLossPercent", type: "slider", label: "止损比例", description: "单个头寸的最大亏损比例", min: 1, max: 50, step: 0.5, default: 10, unit: "%", category: "risk", group: "基础风险控制" },
            { id: "takeProfitPercent", type: "slider", label: "止盈比例", description: "单个头寸的目标盈利比例", min: 5, max: 200, step: 1, default: 20, unit: "%", category: "risk", group: "基础风险控制" },
            { id: "maxDrawdownLimit", type: "slider", label: "最大回撤限制", description: "策略总体账户的最大允许回撤比例", min: 5, max: 50, step: 1, default: 12, unit: "%", category: "risk", group: "组合层风险" },
            { id: "maxPositionPercent", type: "slider", label: "单票集中度上限", description: "单一个股最大持仓比例", min: 5, max: 25, step: 1, default: 15, unit: "%", category: "position", group: "持仓层风险" },
            { id: "maxIndustryExposure", type: "slider", label: "单行业集中度上限", description: "同一行业总持仓比例限制", min: 15, max: 50, step: 5, default: 30, unit: "%", category: "industry", group: "持仓层风险" },
            { id: "maxDailyLoss", type: "slider", label: "单日最大亏损", description: "日内净值最大亏损限制", min: -15, max: -2, step: 1, default: -5, unit: "%", category: "account", group: "熔断机制" },
            { id: "maxTotalExposure", type: "slider", label: "最大总仓位", description: "所有持仓总市值占资金比例", min: 10, max: 100, step: 1, default: 67, unit: "%", category: "position", group: "组合层风险" },
            { id: "maxCorrelation", type: "slider", label: "最大持仓相关性", description: "持仓股票间最大允许相关性", min: 0, max: 100, step: 1, default: 70, unit: "%", category: "other", group: "其他配置" }
        ]

        dynamicParamConfigs = orderDynamicParamConfigs(dynamicParamConfigs)
        dynamicParamGroups = buildDynamicParamGroups(dynamicParamConfigs)

        if (dynamicParamGenerator) {
            dynamicParamGenerator.reloadConfigs(dynamicParamConfigs, dynamicParamGroups)
        }

        initDynamicValues()
        parametersLoaded = true
        restorePersistedConfiguration()
    }

    function initDynamicValues() {
        var values = {}
        dynamicParamConfigs.forEach(function(config) {
            if (config.default !== undefined) {
                values[config.id] = config.default
            }
        })
        dynamicParamValues = values

        if (dynamicParamGenerator) {
            dynamicParamGenerator.setValues(values)
        }

        updateRiskSummary(values)
        restorePersistedConfiguration()
    }

    function cloneObject(source) {
        var target = {}
        if (!source) {
            return target
        }
        for (var key in source) {
            if (Object.prototype.hasOwnProperty.call(source, key)) {
                target[key] = source[key]
            }
        }
        return target
    }

    function auxiliaryRiskConfiguration() {
        return {
            varWarningPercent: varWarningPercent,
            orderSizeLimit: orderSizeLimit,
            turnoverLimit: turnoverLimit,
            slippageLimit: slippageLimit,
            level1Breaker: level1Breaker,
            level2Breaker: level2Breaker,
            level3Breaker: level3Breaker,
            autoStopEnabled: autoStopEnabled
        }
    }

    function normalizePositiveIntOrDefault(value, fallback) {
        var numericValue = Number(value)
        if (isNaN(numericValue) || numericValue <= 0) {
            return fallback
        }
        return Math.floor(numericValue)
    }

    function buildPersistedConfiguration() {
        var merged = cloneObject(dynamicParamValues)
        var auxiliaryValues = auxiliaryRiskConfiguration()
        for (var key in auxiliaryValues) {
            if (Object.prototype.hasOwnProperty.call(auxiliaryValues, key)) {
                merged[key] = auxiliaryValues[key]
            }
        }
        return merged
    }

    function applyPersistedAuxiliaryConfiguration(configuration) {
        if (!configuration || typeof configuration !== "object") {
            return
        }

        if (configuration.varWarningPercent !== undefined) {
            varWarningPercent = numberOrDefault(configuration.varWarningPercent, varWarningPercent)
        }
        if (configuration.orderSizeLimit !== undefined) {
            orderSizeLimit = numberOrDefault(configuration.orderSizeLimit, orderSizeLimit)
        }
        if (configuration.turnoverLimit !== undefined) {
            turnoverLimit = numberOrDefault(configuration.turnoverLimit, turnoverLimit)
        }
        if (configuration.slippageLimit !== undefined) {
            slippageLimit = numberOrDefault(configuration.slippageLimit, slippageLimit)
        }
        if (configuration.level1Breaker !== undefined) {
            level1Breaker = numberOrDefault(configuration.level1Breaker, level1Breaker)
        }
        if (configuration.level2Breaker !== undefined) {
            level2Breaker = numberOrDefault(configuration.level2Breaker, level2Breaker)
        }
        if (configuration.level3Breaker !== undefined) {
            level3Breaker = numberOrDefault(configuration.level3Breaker, level3Breaker)
        }
        if (configuration.autoStopEnabled !== undefined) {
            autoStopEnabled = Boolean(configuration.autoStopEnabled)
        }
    }

    function configurationHasValues(values) {
        for (var key in values) {
            if (Object.prototype.hasOwnProperty.call(values, key)) {
                return true
            }
        }
        return false
    }

    function loadPersistedConfiguration() {
        if (!riskConfigService) {
            return {}
        }

        var savedConfig = {}
        if (typeof riskConfigService.loadCurrentConfiguration === "function") {
            savedConfig = riskConfigService.loadCurrentConfiguration()
        }
        if (!configurationHasValues(savedConfig) && typeof riskConfigService.loadAppliedConfiguration === "function") {
            savedConfig = riskConfigService.loadAppliedConfiguration()
        }
        return savedConfig || {}
    }

    function restorePersistedConfiguration() {
        if (!parametersLoaded || dynamicParamConfigs.length === 0) {
            return
        }

        if (!configurationHasValues(pendingPersistedValues)) {
            pendingPersistedValues = loadPersistedConfiguration()
        }
        if (!configurationHasValues(pendingPersistedValues)) {
            return
        }

        var restoredValues = cloneObject(dynamicParamValues)
        dynamicParamConfigs.forEach(function(config) {
            if (Object.prototype.hasOwnProperty.call(pendingPersistedValues, config.id)) {
                restoredValues[config.id] = pendingPersistedValues[config.id]
            }
        })

        dynamicParamValues = restoredValues
        if (dynamicParamGenerator) {
            dynamicParamGenerator.setValues(restoredValues)
        }
        applyPersistedAuxiliaryConfiguration(pendingPersistedValues)
        updateRiskSummary(restoredValues)
        pendingPersistedValues = ({})
    }

    function updateRiskSummary(values) {
        riskSummary = {
            stopLossPercent: normalizePercentValue(values.stopLossPercent, 10.0),
            takeProfitPercent: normalizePercentValue(values.takeProfitPercent, 20.0),
            maxDrawdownLimit: normalizePercentValue(values.maxDrawdownLimit, 12.0),
            maxPositionPercent: normalizePercentValue(values.maxPositionPercent, 15.0),
            maxTotalExposure: normalizePercentValue(values.maxTotalExposure, 67.0),
            maxIndustryExposure: normalizePercentValue(values.maxIndustryExposure, 30.0),
            maxThemeExposure: normalizePercentValue(values.maxThemeExposure, 25.0),
            maxDailyLoss: normalizeSignedPercentValue(values.maxDailyLoss, -5.0),
            maxCorrelation: normalizePercentValue(values.maxCorrelation, 70.0)
        }
    }

    function normalizePercentValue(value, fallback) {
        var numericValue = Number(value)
        if (isNaN(numericValue)) {
            return fallback
        }
        return Math.abs(numericValue) <= 1 ? numericValue * 100 : numericValue
    }

    function normalizeSignedPercentValue(value, fallback) {
        var numericValue = Number(value)
        if (isNaN(numericValue)) {
            return fallback
        }
        return Math.abs(numericValue) <= 1 ? numericValue * 100 : numericValue
    }

    function saveRiskConfiguration() {
        if (!riskConfigService || typeof riskConfigService.saveConfiguration !== "function") {
            return
        }
        var savedConfiguration = buildPersistedConfiguration()
        if (riskConfigService.saveConfiguration(savedConfiguration)) {
            pendingPersistedValues = cloneObject(savedConfiguration)
            appendHistory("【配置】风控规则已保存")
        }
    }

    function applyRiskConfiguration() {
        if (!riskConfigService || typeof riskConfigService.applyConfiguration !== "function") {
            return
        }
        var appliedConfiguration = buildPersistedConfiguration()
        if (riskConfigService.applyConfiguration(appliedConfiguration)) {
            pendingPersistedValues = cloneObject(appliedConfiguration)
            appendHistory("【配置】风控规则已应用到全局默认值")
        }
    }

    function getRiskLevelColor() {
        var riskScore = calculateRiskScore()
        if (riskScore < 3) {
            return successGreen
        }
        if (riskScore < 7) {
            return warningOrange
        }
        return dangerRed
    }

    function getRiskLevelText() {
        var riskScore = calculateRiskScore()
        if (riskScore < 3) {
            return "低风险"
        }
        if (riskScore < 7) {
            return "中风险"
        }
        return "高风险"
    }

    function calculateRiskScore() {
        var score = 0
        if (riskSummary.stopLossPercent < 5) score += 2
        else if (riskSummary.stopLossPercent < 10) score += 1
        if (riskSummary.takeProfitPercent > 30) score += 2
        else if (riskSummary.takeProfitPercent > 20) score += 1
        if (riskSummary.maxPositionPercent > 20) score += 2
        else if (riskSummary.maxPositionPercent > 15) score += 1
        if (riskSummary.maxTotalExposure > 90) score += 2
        else if (riskSummary.maxTotalExposure > 75) score += 1
        if (riskSummary.maxDailyLoss < -8) score += 2
        else if (riskSummary.maxDailyLoss < -5) score += 1
        return Math.min(score, 10)
    }

}
