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
    required property var commonParameterConfigs
    required property bool parametersValid

    // 暴露内部 DynamicParamGenerator 供父组件 JS 直接调用
    property alias dynamicGenerator: commonDynamicGenerator

    // 事件流出
    signal paramsChanged()
    signal validationChanged(bool allValid)

    Layout.fillWidth: true
    Layout.alignment: Qt.AlignTop
    radius: 8
    color: "#0b1427"
    border.width: 1
    border.color: "#1e40af"
    implicitHeight: commonParameterCardLayout.implicitHeight + 14

    ColumnLayout {
        id: commonParameterCardLayout
        anchors.fill: parent
        anchors.margins: 8
        spacing: 6

        Text {
            text: strategyService.tr('strategyCreation.commonParameters')
            font.pixelSize: 12
            font.weight: Font.Medium
            color: "#dbeafe"
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: 4

            Text {
                text: strategyService.tr('strategyCreation.configuredParameters') + ": " +
                      (commonDynamicGenerator && commonDynamicGenerator.configsList ? commonDynamicGenerator.configsList.length : 0)
                font.pixelSize: 10
                color: "#93c5fd"
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

        PluginComponents.DynamicParamGenerator {
            id: commonDynamicGenerator
            Layout.fillWidth: true
            Layout.preferredHeight: Math.max(132, implicitHeight)
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
