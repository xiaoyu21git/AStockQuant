import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import AStock.Bridge 1.0 as Bridge
import "../../utils/StartupGateFormatter.js" as StartupGateFormatter

Item {
    id: root
    property var configService
    property var marketDataService: Bridge.MarketDataBridge
    property var marketCalendarService: Bridge.TradingMarketCalendarService
    property var runtimeStatusService: Bridge.TradingRuntimeStatusService
    property var draftConfiguration: ({})
    property var strategyOptions: []
    property string feedbackMessage: ""
    property bool feedbackError: false
    property string selectedBoundStrategyId: ""
    property string selectedBoundStrategyName: ""
    property string boundStrategySymbolsPreview: ""
    property var latestLiveValidationReport: ({ errors: [], warnings: [], checkedAt: "" })
    property var latestStartupGate: ({})

    function accountProfileValue() {
        return accountProfileBox && accountProfileBox.selectedProfileValue ? accountProfileBox.selectedProfileValue : "live"
    }
    function activeAccountIdValue() {
        var lid = liveAccountIdField ? liveAccountIdField.text.trim() : ""
        var sid = simAccountIdField ? simAccountIdField.text.trim() : ""
        return accountProfileValue() === "simulation" ? sid : lid
    }
    function syncBoundStrategySelection() {
        var targetId = draftConfiguration.boundStrategyId || ""
        selectedBoundStrategyId = targetId
        selectedBoundStrategyName = draftConfiguration.boundStrategyName || ""
        if (!strategyOptions || strategyOptions.length === 0 || !boundStrategyBox) return
        var mi = 0
        for (var i = 0; i < strategyOptions.length; ++i) {
            if ((strategyOptions[i].value || "") === targetId) { mi = i; break }
        }
        boundStrategyBox.currentIndex = mi
        selectedBoundStrategyId = strategyOptions[mi] ? (strategyOptions[mi].value || "") : ""
        selectedBoundStrategyName = strategyOptions[mi] ? (strategyOptions[mi].name || "") : ""
    }
    function persistBoundStrategySelection() {
        saveConfiguration({ successMessage: selectedBoundStrategyId ? ("已绑定策略 \"" + (selectedBoundStrategyName || selectedBoundStrategyId) + "\"") : "已取消绑定" })
    }
    function synchronizeBoundStrategyFields() {
        // no-op: symbols preview is omitted for simplicity
    }

    function loadStrategyOptions() {
        var options = [{ label: "未绑定业务策略", value: "", name: "" }]
        try {
            var sb = Bridge.StrategyBridge
            if (sb && sb.list) {
                var items = sb.list() || []
                for (var i = 0; i < items.length; ++i) {
                    var it = items[i] || {}
                    var sid = it.strategyId || ""
                    if (!sid) continue
                    var sn = it.strategyName || it.name || sid
                    options.push({ label: sn + " (" + sid + ")", value: sid, name: sn })
                }
            }
        } catch (e) {
            console.log("loadStrategyOptions error:", e)
        }
        strategyOptions = options
        syncBoundStrategySelection()
    }

    function reloadConfiguration() {
        if (!configService || !configService.loadConfiguration) return
        draftConfiguration = configService.loadConfiguration()
        syncFieldsFromDraft()
        feedbackError = false
        feedbackMessage = "已从配置文件读取交易连接参数"
    }
    function syncFieldsFromDraft() {
        if (!tokenField || !enabledSwitch) { Qt.callLater(syncFieldsFromDraft); return }
        enabledSwitch.checked = !!draftConfiguration.enabled
        tokenField.text = draftConfiguration.token || ""
        liveAccountIdField.text = draftConfiguration.liveAccountId || ""
        simAccountIdField.text = draftConfiguration.simAccountId || ""
        gmStrategyIdField.text = draftConfiguration.gmStrategyId || draftConfiguration.runtimeStrategyId || draftConfiguration.strategyId || ""
        serverUrlField.text = draftConfiguration.serverUrl || ""
        syncBoundStrategySelection()
        var savedProfile = draftConfiguration.accountProfile || "live"
        accountProfileBox.currentIndex = savedProfile === "simulation" ? 1 : 0
        accountProfileBox.selectedProfileValue = accountProfileBox.model[accountProfileBox.currentIndex].value
    }
    function buildConfigurationPayload() {
        return {
            enabled: enabledSwitch.checked,
            autoExecuteRuntimeCandidates: false,
            liveUnlockConfirmed: true,
            token: tokenField.text,
            accountProfile: accountProfileValue(),
            liveAccountId: liveAccountIdField.text.trim(),
            simAccountId: simAccountIdField.text.trim(),
            accountId: activeAccountIdValue(),
            simtradeOnly: false,
            readOnly: true,
            boundStrategyId: selectedBoundStrategyId,
            boundStrategyName: selectedBoundStrategyName,
            gmStrategyId: gmStrategyIdField.text.trim(),
            runtimeStrategyId: gmStrategyIdField.text.trim(),
            strategyId: gmStrategyIdField.text.trim(),
            mode: "1",
            serverUrl: serverUrlField.text,
            symbols: "",
            provider: "jujin",
            updatedAt: draftConfiguration.updatedAt || ""
        }
    }
    function saveConfiguration(options) {
        if (!configService || !configService.saveConfiguration) return false
        var payload = buildConfigurationPayload()
        if (configService.saveConfiguration(payload)) {
            draftConfiguration = configService.currentConfiguration
            syncFieldsFromDraft()
            feedbackError = false
            feedbackMessage = (options && options.successMessage) || "配置已保存"
            return true
        }
        return false
    }

    Component.onCompleted: {
        Qt.callLater(reloadConfiguration)
        Qt.callLater(loadStrategyOptions)
    }

    Rectangle { anchors.fill: parent; color: "#0F172A" }

    ScrollView {
        anchors.fill: parent; clip: true
        ColumnLayout {
            width: Math.max(root.width - 48, 920); spacing: 20; anchors.margins: 24
            // Header
            Rectangle {
                Layout.fillWidth: true; radius: 24; color: "#111C34"; border.color: "#22314F"; border.width: 1
                implicitHeight: 110
                ColumnLayout {
                    anchors.fill: parent; anchors.margins: 16; spacing: 8
                    Text { text: "系统设置 / 掘金连接"; font.pixelSize: 28; font.bold: true; color: "#F8FAFC" }
                    Text { Layout.fillWidth: true; wrapMode: Text.WordWrap; font.pixelSize: 14; color: "#94A3B8"
                        text: "配置掘金 Token、仿真账户、业务策略绑定与运行时规则参数。" }
                    Text { id: feedbackText; Layout.fillWidth: true; font.pixelSize: 12; color: feedbackError ? "#FECACA" : "#BAE6FD"
                        text: feedbackMessage.length > 0 ? feedbackMessage : "配置文件位置: " + (configService && configService.configFilePath ? configService.configFilePath : "") }
                }
            }

            // Settings form
            Rectangle {
                Layout.fillWidth: true; radius: 24; color: "#111827"; border.color: "#1F2A44"; border.width: 1
                implicitHeight: formColumn.implicitHeight + 32
                ColumnLayout {
                    id: formColumn; anchors.fill: parent; anchors.margins: 16; spacing: 18

                    // Enable switch
                    RowLayout {
                        Layout.fillWidth: true; spacing: 12
                        Text { text: "连接控制"; font.pixelSize: 20; font.bold: true; color: "#F8FAFC" }
                        Item { Layout.fillWidth: true }
                        Switch { id: enabledSwitch; checked: !!draftConfiguration.enabled }
                    }

                    // Token
                    ColumnLayout { Layout.fillWidth: true; spacing: 8
                        Text { text: "掘金 Token"; font.pixelSize: 14; color: "#E2E8F0" }
                        TextField { id: tokenField; Layout.fillWidth: true; text: draftConfiguration.token || ""
                            echoMode: TextInput.Password; placeholderText: "请输入最新的掘金 token"
                            color: "#F8FAFC"; placeholderTextColor: "#64748B"; selectByMouse: true
                            background: Rectangle { radius: 12; color: "#0F172A"; border.color: tokenField.activeFocus ? "#38BDF8" : "#334155"; border.width: 1 }
                        }
                    }

                    // Account profile
                    ColumnLayout { Layout.fillWidth: true; spacing: 8
                        Text { text: "账户环境"; font.pixelSize: 14; color: "#E2E8F0" }
                        ComboBox { id: accountProfileBox; Layout.fillWidth: true
                            model: [{ label: "实盘账户", value: "live" }, { label: "仿真账户", value: "simulation" }]
                            textRole: "label"
                            property string selectedProfileValue: model[currentIndex] ? model[currentIndex].value : "live"
                            onActivated: selectedProfileValue = model[currentIndex].value
                        }
                    }

                    // Account IDs
                    ColumnLayout { Layout.fillWidth: true; spacing: 8
                        Text { text: "实盘账户 ID"; font.pixelSize: 14; color: "#E2E8F0" }
                        TextField { id: liveAccountIdField; Layout.fillWidth: true; color: "#F8FAFC"; placeholderTextColor: "#64748B"
                            background: Rectangle { radius: 12; color: "#0F172A"; border.color: liveAccountIdField.activeFocus ? "#38BDF8" : "#334155"; border.width: 1 }
                        }
                    }
                    ColumnLayout { Layout.fillWidth: true; spacing: 8
                        Text { text: "仿真账户 ID"; font.pixelSize: 14; color: "#E2E8F0" }
                        TextField { id: simAccountIdField; Layout.fillWidth: true; color: "#F8FAFC"; placeholderTextColor: "#64748B"
                            background: Rectangle { radius: 12; color: "#0F172A"; border.color: simAccountIdField.activeFocus ? "#38BDF8" : "#334155"; border.width: 1 }
                        }
                    }

                    // Strategy binding
                    ColumnLayout { Layout.fillWidth: true; spacing: 8
                        Text { text: "绑定业务策略"; font.pixelSize: 14; color: "#E2E8F0" }
                        ComboBox { id: boundStrategyBox; Layout.fillWidth: true; model: strategyOptions; textRole: "label"
                            onActivated: {
                                var s = strategyOptions[currentIndex] || ({})
                                selectedBoundStrategyId = s.value || ""
                                selectedBoundStrategyName = s.name || ""
                                persistBoundStrategySelection()
                            }
                        }
                    }

                    // GM strategy ID
                    ColumnLayout { Layout.fillWidth: true; spacing: 8
                        Text { text: "掘金策略 ID"; font.pixelSize: 14; color: "#E2E8F0" }
                        TextField { id: gmStrategyIdField; Layout.fillWidth: true; color: "#F8FAFC"; placeholderTextColor: "#64748B"
                            background: Rectangle { radius: 12; color: "#0F172A"; border.color: gmStrategyIdField.activeFocus ? "#38BDF8" : "#334155"; border.width: 1 }
                        }
                    }

                    // Server URL
                    ColumnLayout { Layout.fillWidth: true; spacing: 8
                        Text { text: "服务器地址"; font.pixelSize: 14; color: "#E2E8F0" }
                        TextField { id: serverUrlField; Layout.fillWidth: true; color: "#F8FAFC"; placeholderTextColor: "#64748B"
                            background: Rectangle { radius: 12; color: "#0F172A"; border.color: serverUrlField.activeFocus ? "#38BDF8" : "#334155"; border.width: 1 }
                        }
                    }

                    // Buttons
                    RowLayout {
                        Layout.fillWidth: true; spacing: 12
                        Button { text: "重新读取"; onClicked: reloadConfiguration() }
                        Item { Layout.fillWidth: true }
                        Button { text: "保存配置"; highlighted: true; enabled: tokenField.text.trim().length > 0; onClicked: saveConfiguration() }
                    }
                    Text { Layout.fillWidth: true; wrapMode: Text.WordWrap; font.pixelSize: 13; color: "#94A3B8"
                        text: "实盘绑定区分业务策略 ID 与掘金固定策略 ID。选择系统内业务策略，掘金策略 ID 需手工填写。" }
                }
            }
        }
    }
}