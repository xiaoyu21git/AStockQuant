import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import AStock.Bridge 1.0 as Bridge
import "../../components/Trading" as TradingComponents

Item {
    id: root

    readonly property var configService: Bridge.TradingConnectionConfigService
    readonly property var currentTradingConfiguration: configService && configService.currentConfiguration
        ? configService.currentConfiguration
        : ({})
    readonly property var configuredRuntimeRuleDefaults: currentTradingConfiguration.runtimeRuleDefaults || ({})
    readonly property string boundStrategyId: String(currentTradingConfiguration.boundStrategyId || "")
    readonly property string boundStrategyName: String(currentTradingConfiguration.boundStrategyName || "")

    Rectangle {
        anchors.fill: parent
        color: "#0F172A"
    }

    ScrollView {
        anchors.fill: parent
        clip: true

        ColumnLayout {
            width: Math.max(root.width - 48, 920)
            spacing: 20
            anchors.margins: 24

            Rectangle {
                Layout.fillWidth: true
                radius: 24
                color: "#111C34"
                border.color: "#22314F"
                border.width: 1
                implicitHeight: monitoringHeader.implicitHeight + 32

                ColumnLayout {
                    id: monitoringHeader
                    anchors.fill: parent
                    anchors.margins: 16
                    spacing: 10

                    Text {
                        text: "看盘面 / 运行时规则"
                        font.pixelSize: 28
                        font.bold: true
                        color: "#F8FAFC"
                    }

                    Text {
                        Layout.fillWidth: true
                        wrapMode: Text.WordWrap
                        text: boundStrategyId
                            ? ("当前已绑定策略 “" + (boundStrategyName || boundStrategyId) + "”。本页用于观察行情驱动下的规则评估链，确认市场环境、范围、信号和执行门禁是否按预期工作。")
                            : "本页用于观察行情驱动下的规则评估链。若还未绑定策略，可先到策略卡片或交易配置里完成绑定，再回到这里看实时门禁结果。"
                        font.pixelSize: 14
                        color: "#94A3B8"
                    }
                }
            }

            TradingComponents.RuntimeRuleObserverPanel {
                Layout.fillWidth: true
                configuredRuntimeRuleDefaults: root.configuredRuntimeRuleDefaults
                highlightStrategyId: root.boundStrategyId
                highlightStrategyName: root.boundStrategyName
            }
        }
    }
}