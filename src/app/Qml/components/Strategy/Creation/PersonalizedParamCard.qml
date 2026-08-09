import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import AStock.Bridge 1.0 as Bridge
import "../../FactorWorkbench/Creation/components" as PluginComponents

Rectangle {
    id: panelRoot

    property var strategyService: Bridge.StrategyBridge

    // 数据流入
    required property var paramComponents
    required property int minColumnWidth
    required property int maxColumns
    required property bool parametersValid
    required property bool hasConfigs
    required property bool cardExpanded
    required property var personalizedParameterConfigs

    // 暴露内部 DynamicParamGenerator 供父组件 JS 直接调用
    property alias dynamicGenerator: personalizedDynamicGenerator

    // 事件流出
    signal paramsChanged()
    signal validationChanged(bool allValid)
    signal toggleExpand()

    Layout.fillWidth: true
    Layout.alignment: Qt.AlignTop
    radius: 8
    color: "#1a2332"
    border.width: 1
    border.color: "#334155"
    implicitHeight: personalizedParameterCardLayout.implicitHeight + 14

    ColumnLayout {
        id: personalizedParameterCardLayout
        anchors.fill: parent
        anchors.margins: 8
        spacing: 6

        RowLayout {
            Layout.fillWidth: true
            spacing: 4

            Text {
                text: strategyService.tr('strategyCreation.personalizedParameters')
                font.pixelSize: 12
                font.weight: Font.Medium
                color: "#e2e8f0"
            }

            Item { Layout.fillWidth: true }

            Text {
                text: panelRoot.hasConfigs
                      ? (panelRoot.cardExpanded ? "收起" : "展开")
                      : "自动收起"
                font.pixelSize: 9
                font.weight: Font.Medium
                color: panelRoot.hasConfigs ? "#93c5fd" : "#64748b"
            }

            MouseArea {
                Layout.preferredWidth: 30
                Layout.preferredHeight: 18
                enabled: panelRoot.hasConfigs
                cursorShape: enabled ? Qt.PointingHandCursor : Qt.ArrowCursor
                onClicked: panelRoot.toggleExpand()
            }
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: 4

            Text {
                text: strategyService.tr('strategyCreation.configuredParameters') + ": " +
                      (personalizedDynamicGenerator && personalizedDynamicGenerator.configsList ? personalizedDynamicGenerator.configsList.length : 0)
                font.pixelSize: 10
                color: "#94a3b8"
            }

            Item { Layout.fillWidth: true }

            Text {
                text: panelRoot.parametersValid ?
                      strategyService.tr('strategyCreation.parameterValidationPassed') :
                      strategyService.tr('strategyCreation.parameterValidationRequired')
                font.pixelSize: 10
                font.weight: Font.Medium
                color: panelRoot.parametersValid ? "#10b981" : "#ef4444"
            }
        }

        Text {
            Layout.fillWidth: true
            visible: !panelRoot.hasConfigs
            text: "当前策略类型无个性化参数，卡片已自动收起。"
            font.pixelSize: 9
            color: "#94a3b8"
            wrapMode: Text.WordWrap
        }

        PluginComponents.DynamicParamGenerator {
            id: personalizedDynamicGenerator
            visible: panelRoot.hasConfigs && panelRoot.cardExpanded
            Layout.fillWidth: true
            Layout.preferredHeight: visible ? Math.max(150, implicitHeight) : 0
            minColumnWidth: panelRoot.minColumnWidth
            maxColumns: panelRoot.maxColumns
            showGroups: false

            paramRegistry: panelRoot.paramComponents

            onParamsChanged: function() {
                panelRoot.paramsChanged()
            }

            onValidationChanged: function(allValid) {
                panelRoot.validationChanged(!!allValid)
            }
        }
    }
}
