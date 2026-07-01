import QtQuick 2.15
import QtQuick.Layouts 1.15
import QtQuick.Controls 2.15
// Utils 已迁移至 C++ Bridge.StrategyParamConfigHelper

Rectangle {
    id: root

    property int selectedStrategyTypeIndex: 0
    property var strategyProfile: ({})

    signal profileEdited(var profile)

    readonly property int compactControlWidth: Math.max(120, Math.min(width - 20, 148))

    readonly property var horizonOptions: [
        { label: "长线", value: "long_term" },
        { label: "波段", value: "swing" },
        { label: "短线", value: "short_term" },
        { label: "日内", value: "intraday" }
    ]
    readonly property var frequencyOptions: [
        { label: "低频", value: "low_frequency" },
        { label: "中频", value: "medium_frequency" },
        { label: "高频", value: "high_frequency" },
        { label: "事件驱动", value: "event_driven" }
    ]
    readonly property var marketScopeOptions: [
        { label: "A股", value: "a_share" },
        { label: "ETF", value: "etf" },
        { label: "期货", value: "futures" },
        { label: "通用", value: "general" }
    ]
    readonly property var executionStyleOptions: [
        { label: "收盘确认", value: "close_confirmed" },
        { label: "盘中确认", value: "intraday_confirmed" },
        { label: "开盘跟随", value: "open_followup" },
        { label: "Tick 驱动", value: "tick_driven" },
        { label: "事件确认", value: "event_confirmed" }
    ]

    function optionIndex(options, value, fallbackIndex) {
        var normalized = String(value || "").trim().toLowerCase()
        for (var index = 0; index < options.length; ++index) {
            if (String(options[index].value || "").trim().toLowerCase() === normalized) {
                return index
            }
        }
        return fallbackIndex
    }

    function updateProfileField(key, value) {
        var nextProfile = ({})
        var source = strategyProfile || ({})
        for (var field in source) {
            nextProfile[field] = source[field]
        }
        nextProfile.strategyTypeIndex = selectedStrategyTypeIndex
        nextProfile[key] = value
        profileEdited(nextProfile)
    }

    radius: 12
    implicitHeight: profileLayout.implicitHeight + 24
    color: "#0f172a"
    border.width: 1
    border.color: "#334155"

    ColumnLayout {
        id: profileLayout
        anchors.fill: parent
        anchors.margins: 12
        spacing: 12

        Text {
            text: "策略画像"
            font.pixelSize: 16
            font.weight: Font.DemiBold
            color: "#f8fafc"
        }

        Text {
            Layout.fillWidth: true
            text: "先定义风格、周期和执行方式，再生成默认规则骨架。"
            font.pixelSize: 12
            color: "#94a3b8"
            wrapMode: Text.WordWrap
        }

        Rectangle {
            Layout.preferredWidth: root.compactControlWidth
            Layout.alignment: Qt.AlignLeft
            radius: 10
            color: "#111827"
            border.width: 1
            border.color: "#1d4ed8"
            implicitHeight: typeColumn.implicitHeight + 16

            ColumnLayout {
                id: typeColumn
                anchors.fill: parent
                anchors.margins: 8
                spacing: 4

                Text {
                    text: "当前策略类型"
                    font.pixelSize: 11
                    color: "#93c5fd"
                }

                Text {
                    Layout.fillWidth: true
                    text: Bridge.StrategyParamConfigHelper.getStrategyTypeNameFromIndex(root.selectedStrategyTypeIndex)
                    font.pixelSize: 13
                    font.weight: Font.Medium
                    color: "#dbeafe"
                    wrapMode: Text.WordWrap
                }
            }
        }

        ColumnLayout {
            Layout.preferredWidth: root.compactControlWidth
            Layout.alignment: Qt.AlignLeft
            spacing: 8

            Text {
                text: "持有周期"
                font.pixelSize: 12
                color: "#cbd5e1"
            }

            ComboBox {
                Layout.preferredWidth: root.compactControlWidth
                Layout.alignment: Qt.AlignLeft
                model: root.horizonOptions
                textRole: "label"
                currentIndex: root.optionIndex(root.horizonOptions, (root.strategyProfile || {}).horizon, 1)
                onActivated: root.updateProfileField("horizon", root.horizonOptions[currentIndex].value)
            }

            Text {
                text: "交易频率"
                font.pixelSize: 12
                color: "#cbd5e1"
            }

            ComboBox {
                Layout.preferredWidth: root.compactControlWidth
                Layout.alignment: Qt.AlignLeft
                model: root.frequencyOptions
                textRole: "label"
                currentIndex: root.optionIndex(root.frequencyOptions, (root.strategyProfile || {}).tradingFrequency, 0)
                onActivated: root.updateProfileField("tradingFrequency", root.frequencyOptions[currentIndex].value)
            }

            Text {
                text: "市场范围"
                font.pixelSize: 12
                color: "#cbd5e1"
            }

            ComboBox {
                Layout.preferredWidth: root.compactControlWidth
                Layout.alignment: Qt.AlignLeft
                model: root.marketScopeOptions
                textRole: "label"
                currentIndex: root.optionIndex(root.marketScopeOptions, (root.strategyProfile || {}).marketScope, 0)
                onActivated: root.updateProfileField("marketScope", root.marketScopeOptions[currentIndex].value)
            }

            Text {
                text: "执行方式"
                font.pixelSize: 12
                color: "#cbd5e1"
            }

            ComboBox {
                Layout.preferredWidth: root.compactControlWidth
                Layout.alignment: Qt.AlignLeft
                model: root.executionStyleOptions
                textRole: "label"
                currentIndex: root.optionIndex(root.executionStyleOptions, (root.strategyProfile || {}).executionStyle, 0)
                onActivated: root.updateProfileField("executionStyle", root.executionStyleOptions[currentIndex].value)
            }
        }

        Item { Layout.fillHeight: true }
    }
}