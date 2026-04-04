import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15

Item {
    id: root

    property var configService
    property var draftConfiguration: ({})
    property string feedbackMessage: ""
    property bool feedbackError: false

    signal showMessageRequested(string message)

    function syncFieldsFromDraft() {
        enabledSwitch.checked = !!draftConfiguration.enabled
        tokenField.text = draftConfiguration.token || ""
        accountIdField.text = draftConfiguration.accountId || ""
        strategyIdField.text = draftConfiguration.strategyId || "astock_quant_ui"
        symbolsField.text = draftConfiguration.symbols || ""
        serverUrlField.text = draftConfiguration.serverUrl || ""

        var savedMode = String(draftConfiguration.mode || "1")
        modeBox.currentIndex = savedMode === "2" ? 1 : 0
        modeBox.selectedModeValue = modeBox.model[modeBox.currentIndex].value
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
            accountId: accountIdField.text,
            strategyId: strategyIdField.text,
            mode: modeBox.selectedModeValue,
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

    Component.onCompleted: reloadConfiguration()

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
                        text: "在这里维护掘金 token 与连接参数。保存后会写入本地配置文件，后续行情连接优先读取这份配置。"
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
                                text: "账户 ID"
                                font.pixelSize: 14
                                color: "#E2E8F0"
                            }

                            TextField {
                                id: accountIdField
                                Layout.fillWidth: true
                                text: draftConfiguration.accountId || ""
                                placeholderText: "可选，用于实盘账户绑定"
                                color: "#F8FAFC"
                                placeholderTextColor: "#64748B"
                                selectByMouse: true
                                background: Rectangle {
                                    radius: 12
                                    color: "#0F172A"
                                    border.color: accountIdField.activeFocus ? "#38BDF8" : "#334155"
                                    border.width: 1
                                }
                            }
                        }

                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 8

                            Text {
                                text: "策略 ID"
                                font.pixelSize: 14
                                color: "#E2E8F0"
                            }

                            TextField {
                                id: strategyIdField
                                Layout.fillWidth: true
                                text: draftConfiguration.strategyId || "astock_quant_ui"
                                placeholderText: "默认 astock_quant_ui"
                                color: "#F8FAFC"
                                placeholderTextColor: "#64748B"
                                selectByMouse: true
                                background: Rectangle {
                                    radius: 12
                                    color: "#0F172A"
                                    border.color: strategyIdField.activeFocus ? "#38BDF8" : "#334155"
                                    border.width: 1
                                }
                            }
                        }

                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 8

                            Text {
                                text: "运行模式"
                                font.pixelSize: 14
                                color: "#E2E8F0"
                            }

                            ComboBox {
                                id: modeBox
                                Layout.fillWidth: true
                                model: [
                                    { label: "1 - 实盘", value: "1" },
                                    { label: "2 - 回测", value: "2" }
                                ]
                                textRole: "label"
                                property string selectedModeValue: model[currentIndex] ? model[currentIndex].value : "1"
                                onActivated: selectedModeValue = model[currentIndex].value
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
                        text: "说明：token 使用界面保存到本地配置文件后，后续行情连接会优先读取该文件。系统还会检测当前 Windows 进程里是否存在掘金客户端；未检测到时，即使 token 正确也无法使用对应接口。"
                        font.pixelSize: 13
                        color: "#94A3B8"
                    }
                }
            }
        }
    }
}