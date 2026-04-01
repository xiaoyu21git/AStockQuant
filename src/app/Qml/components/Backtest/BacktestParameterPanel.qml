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
    property var dynamicParamValues: ({})
    property bool parametersLoaded: false
    property bool syncingStrategySelection: false
    property bool syncingDataSourceSelection: false
    property var paramRegistry: null

    property alias strategyComboBox: strategyComboBox
    property alias universeComboBox: universeComboBox
    property alias indexPoolComboBox: indexPoolComboBox
    property alias dataSourceComboBox: dataSourceComboBox
    property alias startDatePicker: startDatePicker
    property alias endDatePicker: endDatePicker
    property alias dynamicParamGenerator: dynamicParamGenerator

    signal strategyOptionSelected(int index, var option)
    signal universeOptionSelected(int index, var option)
    signal indexOptionSelected(int index, var option)
    signal dataSourceOptionSelected(int index, var option)
    signal startDateSelected(string dateText)
    signal endDateSelected(string dateText)
    signal dynamicParamsChanged(var newValues)

    implicitHeight: 520
    radius: 12
    color: "#1E293B"

    ColumnLayout {
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
            spacing: 12

            BacktestLabeledComboBox {
                id: strategyComboBox
                label: "选择策略"
                fieldWidth: 200
                options: root.strategyOptions
                textRole: "displayText"
                placeholder: "请选择策略"
                currentIndex: -1
                onOptionSelected: function(index, option) {
                    if (root.syncingStrategySelection || index < 0 || index >= root.strategyOptions.length) {
                        return
                    }
                    root.strategyOptionSelected(index, option)
                }
            }

            BacktestLabeledComboBox {
                id: universeComboBox
                label: "股票池"
                fieldWidth: 150
                options: root.universeOptions
                textRole: "label"
                placeholder: "请选择股票池"
                currentIndex: 0
                onOptionSelected: function(index, option) {
                    if (index < 0 || index >= root.universeOptions.length) {
                        return
                    }
                    root.universeOptionSelected(index, option)
                }
            }

            BacktestLabeledComboBox {
                id: indexPoolComboBox
                visible: root.selectedUniverseType === "index"
                label: "指数"
                fieldWidth: 170
                options: root.indexPoolOptions
                textRole: "label"
                placeholder: "请选择指数"
                currentIndex: 0
                onOptionSelected: function(index, option) {
                    if (index < 0 || index >= root.indexPoolOptions.length) {
                        return
                    }
                    root.indexOptionSelected(index, option)
                }
            }

            BacktestLabeledComboBox {
                id: dataSourceComboBox
                label: "数据源"
                fieldWidth: 180
                options: root.dataSourceOptions
                textRole: "label"
                placeholder: "请选择数据源"
                currentIndex: 0
                onOptionSelected: function(index, option) {
                    if (root.syncingDataSourceSelection || index < 0 || index >= root.dataSourceOptions.length) {
                        return
                    }
                    root.dataSourceOptionSelected(index, option)
                }
            }

            Text {
                Layout.fillWidth: true
                text: root.selectedStrategyName ? ("当前策略: " + root.selectedStrategyName + " (" + root.selectedStrategyId + ")") : "当前未注入策略，可直接在本页选择或从策略库跳转"
                font.pixelSize: 12
                color: root.selectedStrategyName ? "#38BDF8" : "#94A3B8"
                wrapMode: Text.WordWrap
            }

            Item { Layout.fillWidth: true }
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: 12

            BacktestLabeledDatePicker {
                id: startDatePicker
                label: "开始日期"
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
                fieldWidth: 180
                fieldHeight: 36
                selectedDate: root.selectedEndDate
                onDateChanged: function(dateText) {
                    root.endDateSelected(dateText)
                }
            }

            Text {
                Layout.fillWidth: true
                text: "回测日期由你直接选择。"
                font.pixelSize: 12
                color: "#94A3B8"
                wrapMode: Text.WordWrap
            }
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 330
            radius: 8
            color: "#0F172A"
            border.width: 1
            border.color: "#334155"

            ColumnLayout {
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
                    text: "配置资金管理、交易成本和风险控制等参数"
                    font.pixelSize: 11
                    color: "#94A3B8"
                    wrapMode: Text.WordWrap
                    Layout.fillWidth: true
                }

                PluginComponents.DynamicParamGenerator {
                    id: dynamicParamGenerator
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    paramRegistry: root.paramRegistry
                    configs: root.dynamicParamConfigs
                    values: root.dynamicParamValues

                    onParamsChanged: function(newValues) {
                        root.dynamicParamsChanged(newValues)
                    }
                }

                Text {
                    Layout.alignment: Qt.AlignHCenter
                    text: root.parametersLoaded ? "参数配置已加载" : "正在加载回测参数配置..."
                    font.pixelSize: 11
                    color: root.parametersLoaded ? "#10B981" : "#F59E0B"
                }
            }
        }
    }
}