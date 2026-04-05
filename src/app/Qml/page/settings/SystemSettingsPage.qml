import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import AStock.Bridge 1.0 as Bridge

Item {
    id: root

    property var configService
    property var strategyService: Bridge.StrategyService
    property var draftConfiguration: ({})
    property var strategyOptions: []
    property string feedbackMessage: ""
    property bool feedbackError: false
    property string selectedBoundStrategyId: ""
    property string selectedBoundStrategyName: ""

    function accountProfileValue() {
        return accountProfileBox && accountProfileBox.selectedProfileValue
            ? accountProfileBox.selectedProfileValue
            : "live"
    }

    function activeAccountIdValue() {
        var liveAccountId = liveAccountIdField ? liveAccountIdField.text.trim() : (draftConfiguration.liveAccountId || draftConfiguration.accountId || "")
        var simAccountId = simAccountIdField ? simAccountIdField.text.trim() : (draftConfiguration.simAccountId || "")
        return accountProfileValue() === "simulation"
            ? simAccountId
            : liveAccountId
    }

    signal showMessageRequested(string message)

    function syncBoundStrategySelection() {
        var targetId = draftConfiguration.boundStrategyId || ""
        selectedBoundStrategyId = targetId
        selectedBoundStrategyName = draftConfiguration.boundStrategyName || ""

        if (!strategyOptions || strategyOptions.length === 0 || !boundStrategyBox) {
            return
        }

        var matchedIndex = 0
        for (var index = 0; index < strategyOptions.length; ++index) {
            if ((strategyOptions[index].value || "") === targetId) {
                matchedIndex = index
                break
            }
        }

        boundStrategyBox.currentIndex = matchedIndex
        selectedBoundStrategyId = strategyOptions[matchedIndex] ? (strategyOptions[matchedIndex].value || "") : ""
        selectedBoundStrategyName = strategyOptions[matchedIndex] ? (strategyOptions[matchedIndex].name || "") : ""
    }

    function reloadStrategyOptions() {
        var options = [
            {
                label: "未绑定业务策略",
                value: "",
                name: ""
            }
        ]

        try {
            if (strategyService && strategyService.initialize) {
                strategyService.initialize()
            }

            if (strategyService && strategyService.getAllStrategies) {
                var strategies = strategyService.getAllStrategies() || []
                for (var index = 0; index < strategies.length; ++index) {
                    var rawStrategy = strategies[index] || ({})
                    var strategyId = rawStrategy.strategy_id || rawStrategy.strategyId || rawStrategy.id || ""
                    if (!strategyId) {
                        continue
                    }

                    var strategyName = rawStrategy.strategy_name || rawStrategy.strategyName || rawStrategy.name || strategyId
                    options.push({
                        label: strategyName + " (" + strategyId + ")",
                        value: strategyId,
                        name: strategyName
                    })
                }
            }
        } catch (error) {
            feedbackError = true
            feedbackMessage = "加载业务策略列表失败: " + error
        }

        strategyOptions = options
        syncBoundStrategySelection()
    }

    function syncFieldsFromDraft() {
        if (!enabledSwitch || !tokenField || !liveAccountIdField || !simAccountIdField
                || !gmStrategyIdField || !symbolsField || !serverUrlField || !accountProfileBox) {
            Qt.callLater(syncFieldsFromDraft)
            return
        }

        enabledSwitch.checked = !!draftConfiguration.enabled
        tokenField.text = draftConfiguration.token || ""
        liveAccountIdField.text = draftConfiguration.liveAccountId || (!draftConfiguration.simtradeOnly ? (draftConfiguration.accountId || "") : "")
        simAccountIdField.text = draftConfiguration.simAccountId || (draftConfiguration.simtradeOnly ? (draftConfiguration.accountId || "") : "")
        gmStrategyIdField.text = draftConfiguration.gmStrategyId || draftConfiguration.runtimeStrategyId || draftConfiguration.strategyId || ""
        symbolsField.text = draftConfiguration.symbols || ""
        serverUrlField.text = draftConfiguration.serverUrl || ""
        syncBoundStrategySelection()

        var savedAccountProfile = draftConfiguration.accountProfile || (draftConfiguration.simtradeOnly ? "simulation" : "live")
        accountProfileBox.currentIndex = savedAccountProfile === "simulation" ? 1 : 0
        accountProfileBox.selectedProfileValue = accountProfileBox.model[accountProfileBox.currentIndex].value

    }

    function reloadConfiguration() {
        if (!configService || !configService.loadConfiguration) {
            return
        }

        draftConfiguration = configService.loadConfiguration()
        syncFieldsFromDraft()
        if (configService.refreshClientProcessStatus) {
            configService.refreshClientProcessStatus()
        }
        feedbackError = false
        feedbackMessage = "已从配置文件读取交易连接参数"
    }

    function saveConfiguration() {
        if (!configService || !configService.saveConfiguration) {
            return
        }

        var payload = {
            enabled: enabledSwitch.checked,
            token: tokenField.text,
            accountProfile: accountProfileValue(),
            liveAccountId: liveAccountIdField.text.trim(),
            simAccountId: simAccountIdField.text.trim(),
            accountId: activeAccountIdValue(),
            simtradeOnly: false,
            readOnly: draftConfiguration.readOnly !== undefined ? !!draftConfiguration.readOnly : true,
            boundStrategyId: selectedBoundStrategyId,
            boundStrategyName: selectedBoundStrategyName,
            gmStrategyId: gmStrategyIdField.text.trim(),
            runtimeStrategyId: gmStrategyIdField.text.trim(),
            strategyId: gmStrategyIdField.text.trim(),
            mode: "1",
            serverUrl: serverUrlField.text,
            symbols: symbolsField.text,
            updatedAt: draftConfiguration.updatedAt || "",
            provider: "jujin"
        }

        if (configService.saveConfiguration(payload)) {
            draftConfiguration = configService.currentConfiguration
            syncFieldsFromDraft()
            feedbackError = false
            feedbackMessage = "掘金连接配置已保存，下一次连接将读取新 token"
            showMessageRequested(feedbackMessage)
        }
    }

    Component.onCompleted: {
        Qt.callLater(reloadConfiguration)
        Qt.callLater(reloadStrategyOptions)
    }

    Connections {
        target: configService
        function onConfigurationSaved(configuration) {
            draftConfiguration = configuration
            syncFieldsFromDraft()
        }
        function onErrorOccurred(message) {
            feedbackError = true
            feedbackMessage = message
            showMessageRequested(message)
        }
    }

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
                implicitHeight: headerColumn.implicitHeight + 32

                ColumnLayout {
                    id: headerColumn
                    anchors.fill: parent
                    anchors.margins: 16
                    spacing: 10

                    Text {
                        text: "系统设置 / 掘金连接"
                        font.pixelSize: 28
                        font.bold: true
                        color: "#F8FAFC"
                    }

                    Text {
                        Layout.fillWidth: true
                        wrapMode: Text.WordWrap
                        text: "在这里维护掘金 token、实盘/仿真账户、固定掘金策略 ID 与业务策略绑定。当前交易连接固定使用实时交易会话，实盘/仿真只通过账户 ID 切换。"
                        font.pixelSize: 14
                        color: "#94A3B8"
                    }

                    Rectangle {
                        Layout.fillWidth: true
                        radius: 14
                        color: feedbackError ? "#3F1D24" : "#132338"
                        border.color: feedbackError ? "#F87171" : "#38BDF8"
                        border.width: 1
                        implicitHeight: feedbackText.implicitHeight + 20

                        Text {
                            id: feedbackText
                            anchors.fill: parent
                            anchors.margins: 10
                            text: feedbackMessage.length > 0
                                  ? feedbackMessage
                                  : "配置文件位置: " + (configService && configService.configFilePath ? configService.configFilePath : "")
                            wrapMode: Text.WordWrap
                            font.pixelSize: 13
                            color: feedbackError ? "#FECACA" : "#BAE6FD"
                        }
                    }

                    Rectangle {
                        Layout.fillWidth: true
                        radius: 14
                        color: configService && configService.marketConnectorCompiled ? "#10261D" : "#2A1B14"
                        border.color: configService && configService.marketConnectorCompiled ? "#22C55E" : "#FB923C"
                        border.width: 1
                        implicitHeight: buildStatusText.implicitHeight + 20

                        Text {
                            id: buildStatusText
                            anchors.fill: parent
                            anchors.margins: 10
                            text: configService && configService.marketConnectorBuildStatus
                                  ? configService.marketConnectorBuildStatus
                                  : "尚未获取连接器构建状态"
                            wrapMode: Text.WordWrap
                            font.pixelSize: 13
                            color: configService && configService.marketConnectorCompiled ? "#DCFCE7" : "#FFEDD5"
                        }
                    }

                    Rectangle {
                        Layout.fillWidth: true
                        radius: 14
                        color: configService && configService.clientProcessRunning ? "#10261D" : "#2A1B14"
                        border.color: configService && configService.clientProcessRunning ? "#22C55E" : "#FB923C"
                        border.width: 1
                        implicitHeight: processStatusRow.implicitHeight + 20

                        RowLayout {
                            id: processStatusRow
                            anchors.fill: parent
                            anchors.margins: 10
                            spacing: 12

                            Rectangle {
                                width: 12
                                height: 12
                                radius: 6
                                color: configService && configService.clientProcessRunning ? "#22C55E" : "#F97316"
                            }

                            Text {
                                Layout.fillWidth: true
                                text: configService && configService.clientProcessStatus
                                      ? configService.clientProcessStatus
                                      : "尚未检查客户端进程"
                                wrapMode: Text.WordWrap
                                font.pixelSize: 13
                                color: configService && configService.clientProcessRunning ? "#DCFCE7" : "#FFEDD5"
                            }

                            Button {
                                text: "重新检测"
                                onClicked: {
                                    if (configService && configService.refreshClientProcessStatus) {
                                        configService.refreshClientProcessStatus()
                                    }
                                }
                            }
                        }
                    }

                    Text {
                        Layout.fillWidth: true
                        wrapMode: Text.WordWrap
                        text: "当前监控进程名: " + (configService && configService.clientProcessNames
                              ? configService.clientProcessNames.join(", ")
                              : "")
                        font.pixelSize: 12
                        color: "#94A3B8"
                    }
                }
            }

            Rectangle {
                Layout.fillWidth: true
                radius: 24
                color: "#111827"
                border.color: "#1F2A44"
                border.width: 1
                implicitHeight: formColumn.implicitHeight + 32

                ColumnLayout {
                    id: formColumn
                    anchors.fill: parent
                    anchors.margins: 16
                    spacing: 18

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 12

                        Text {
                            text: "连接控制"
                            font.pixelSize: 20
                            font.bold: true
                            color: "#F8FAFC"
                        }

                        Item { Layout.fillWidth: true }

                        Text {
                            text: !(configService && configService.marketConnectorCompiled)
                                  ? "当前构建不可启用"
                                  : (enabledSwitch.checked ? "已启用连接" : "未启用连接")
                            font.pixelSize: 13
                            color: !(configService && configService.marketConnectorCompiled)
                                   ? "#FB923C"
                                   : (enabledSwitch.checked ? "#34D399" : "#94A3B8")
                        }

                        Switch {
                            id: enabledSwitch
                            checked: !!draftConfiguration.enabled
                            enabled: !!(configService && configService.marketConnectorCompiled)
                        }
                    }

                    GridLayout {
                        Layout.fillWidth: true
                        columns: root.width > 1200 ? 2 : 1
                        columnSpacing: 16
                        rowSpacing: 16

                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 8

                            Text {
                                text: "掘金 Token"
                                font.pixelSize: 14
                                color: "#E2E8F0"
                            }

                            TextField {
                                id: tokenField
                                Layout.fillWidth: true
                                text: draftConfiguration.token || ""
                                echoMode: TextInput.Password
                                placeholderText: "请输入最新的掘金 token"
                                color: "#F8FAFC"
                                placeholderTextColor: "#64748B"
                                selectByMouse: true
                                background: Rectangle {
                                    radius: 12
                                    color: "#0F172A"
                                    border.color: tokenField.activeFocus ? "#38BDF8" : "#334155"
                                    border.width: 1
                                }
                            }
                        }

                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 8

                            Text {
                                text: "账户环境"
                                font.pixelSize: 14
                                color: "#E2E8F0"
                            }

                            ComboBox {
                                id: accountProfileBox
                                Layout.fillWidth: true
                                model: [
                                    { label: "实盘账户", value: "live" },
                                    { label: "仿真账户", value: "simulation" }
                                ]
                                textRole: "label"
                                property string selectedProfileValue: model[currentIndex] ? model[currentIndex].value : "live"
                                onActivated: selectedProfileValue = model[currentIndex].value
                            }
                        }

                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 8

                            Text {
                                text: "实盘账户 ID"
                                font.pixelSize: 14
                                color: "#E2E8F0"
                            }

                            TextField {
                                id: liveAccountIdField
                                Layout.fillWidth: true
                                text: draftConfiguration.liveAccountId || ""
                                placeholderText: "填写掘金实盘账户 ID"
                                color: "#F8FAFC"
                                placeholderTextColor: "#64748B"
                                selectByMouse: true
                                background: Rectangle {
                                    radius: 12
                                    color: "#0F172A"
                                    border.color: liveAccountIdField.activeFocus ? "#38BDF8" : "#334155"
                                    border.width: 1
                                }
                            }
                        }

                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 8

                            Text {
                                text: "仿真账户 ID"
                                font.pixelSize: 14
                                color: "#E2E8F0"
                            }

                            TextField {
                                id: simAccountIdField
                                Layout.fillWidth: true
                                text: draftConfiguration.simAccountId || ""
                                placeholderText: "填写掘金仿真账户 ID"
                                color: "#F8FAFC"
                                placeholderTextColor: "#64748B"
                                selectByMouse: true
                                background: Rectangle {
                                    radius: 12
                                    color: "#0F172A"
                                    border.color: simAccountIdField.activeFocus ? "#38BDF8" : "#334155"
                                    border.width: 1
                                }
                            }
                        }

                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 8

                            Text {
                                text: "绑定业务策略"
                                font.pixelSize: 14
                                color: "#E2E8F0"
                            }

                            ComboBox {
                                id: boundStrategyBox
                                Layout.fillWidth: true
                                model: strategyOptions
                                textRole: "label"
                                onActivated: {
                                    var selected = strategyOptions[currentIndex] || ({})
                                    selectedBoundStrategyId = selected.value || ""
                                    selectedBoundStrategyName = selected.name || ""
                                }
                            }
                        }

                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 8

                            Text {
                                text: "掘金策略 ID"
                                font.pixelSize: 14
                                color: "#E2E8F0"
                            }

                            TextField {
                                id: gmStrategyIdField
                                Layout.fillWidth: true
                                text: draftConfiguration.gmStrategyId || draftConfiguration.runtimeStrategyId || draftConfiguration.strategyId || ""
                                placeholderText: "填写掘金客户端里那条固定策略的真实 ID"
                                color: "#F8FAFC"
                                placeholderTextColor: "#64748B"
                                selectByMouse: true
                                background: Rectangle {
                                    radius: 12
                                    color: "#0F172A"
                                    border.color: gmStrategyIdField.activeFocus ? "#38BDF8" : "#334155"
                                    border.width: 1
                                }
                            }

                            Text {
                                Layout.fillWidth: true
                                wrapMode: Text.WordWrap
                                text: "这是一条固定的掘金外部策略 ID。系统里的业务策略仍通过“绑定业务策略”单独区分。"
                                font.pixelSize: 12
                                color: "#94A3B8"
                            }

                            Text {
                                Layout.fillWidth: true
                                wrapMode: Text.WordWrap
                                text: "当前生效账户 ID: " + activeAccountIdValue()
                                font.pixelSize: 12
                                color: "#BAE6FD"
                            }
                        }

                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 8

                            Text {
                                text: "交易会话模式"
                                font.pixelSize: 14
                                color: "#E2E8F0"
                            }

                            TextField {
                                Layout.fillWidth: true
                                text: "固定为 1 - 实时交易会话"
                                readOnly: true
                                color: "#CBD5E1"
                                selectByMouse: true
                                background: Rectangle {
                                    radius: 12
                                    color: "#0F172A"
                                    border.color: "#334155"
                                    border.width: 1
                                }
                            }

                            Text {
                                Layout.fillWidth: true
                                wrapMode: Text.WordWrap
                                text: "仿真账户不等于 SDK 回测模式。当前链路固定使用 mode=1，避免因 mode=2 或 simtrade_only 导致运行时无法进入可交易状态。"
                                font.pixelSize: 12
                                color: "#94A3B8"
                            }
                        }
                    }

                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 8

                        Text {
                            text: "行情订阅标的"
                            font.pixelSize: 14
                            color: "#E2E8F0"
                        }

                        TextField {
                            id: symbolsField
                            Layout.fillWidth: true
                            text: draftConfiguration.symbols || ""
                            placeholderText: "用逗号分隔，例如 600000.SH,000001.SZ"
                            color: "#F8FAFC"
                            placeholderTextColor: "#64748B"
                            selectByMouse: true
                            background: Rectangle {
                                radius: 12
                                color: "#0F172A"
                                border.color: symbolsField.activeFocus ? "#38BDF8" : "#334155"
                                border.width: 1
                            }
                        }
                    }

                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 8

                        Text {
                            text: "服务器地址"
                            font.pixelSize: 14
                            color: "#E2E8F0"
                        }

                        TextField {
                            id: serverUrlField
                            Layout.fillWidth: true
                            text: draftConfiguration.serverUrl || ""
                            placeholderText: "可选，自定义服务地址时填写"
                            color: "#F8FAFC"
                            placeholderTextColor: "#64748B"
                            selectByMouse: true
                            background: Rectangle {
                                radius: 12
                                color: "#0F172A"
                                border.color: serverUrlField.activeFocus ? "#38BDF8" : "#334155"
                                border.width: 1
                            }
                        }
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 12

                        Button {
                            text: "重新读取"
                            onClicked: reloadConfiguration()
                        }

                        Button {
                            text: "恢复默认"
                            onClicked: {
                                draftConfiguration = configService && configService.defaultConfiguration
                                                     ? configService.defaultConfiguration()
                                                     : ({})
                                syncFieldsFromDraft()
                                feedbackError = false
                                feedbackMessage = "已恢复为默认连接配置，保存后生效"
                            }
                        }

                        Item { Layout.fillWidth: true }

                        Button {
                            text: "保存配置"
                            highlighted: true
                            enabled: tokenField.text.trim().length > 0
                            onClicked: saveConfiguration()
                        }
                    }

                    Text {
                        Layout.fillWidth: true
                        wrapMode: Text.WordWrap
                        text: "说明：实盘绑定现在区分业务策略 ID 与掘金固定策略 ID。界面里选择的是系统内业务策略，掘金策略 ID 需要手工填写为客户端那条固定策略；未检测到掘金客户端时，即使 token 正确也无法使用对应接口。"
                        font.pixelSize: 13
                        color: "#94A3B8"
                    }
                }
            }
        }
    }
}