import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import AStock.Bridge 1.0 as Bridge

Rectangle {
    id: panelRoot

    // ── 父级桥接属性 ──
    required property string strategyId
    required property var blacklistSymbols
    required property string blacklistInput
    signal blacklistSymbolsChanged(var newValue)
    signal blacklistInputChanged(string newValue)

    function addToBlacklist() {
        var sid = String(strategyId || "")
        if (!sid) return
        var raw = blacklistInput.trim().toUpperCase()
        if (!raw) return
        var ids = raw.split(/[\s,;，；]+/)
        var current = blacklistSymbols.slice()
        var changed = false
        for (var i = 0; i < ids.length; i++) {
            var id = ids[i].trim()
            if (!id || current.indexOf(id) >= 0) continue
            if (id.length === 6) id = id + ".SZ"
            if (id.indexOf(".") < 0) continue
            current.push(id)
            changed = true
        }
        if (changed) {
            Bridge.StrategyBridge.updateSymbolBlacklist(sid, current)
            blacklistSymbolsChanged(current)
        }
        blacklistInputChanged("")
    }

    function removeFromBlacklist(index) {
        var sid = String(strategyId || "")
        if (!sid) return
        var current = blacklistSymbols.slice()
        current.splice(index, 1)
        Bridge.StrategyBridge.updateSymbolBlacklist(sid, current)
        blacklistSymbolsChanged(current)
    }

    // 暴露输入字段供外部访问
    property alias inputField: blacklistInputField

    Layout.fillWidth: true
    Layout.alignment: Qt.AlignTop
    radius: 8
    color: "#1a2332"
    border.width: 1
    border.color: "#334155"
    implicitHeight: blacklistCardLayout.implicitHeight + 14

    ColumnLayout {
        id: blacklistCardLayout
        anchors.fill: parent
        anchors.margins: 10
        spacing: 6

        RowLayout {
            Layout.fillWidth: true
            Text {
                text: "标的黑名单"
                font.pixelSize: 14
                font.weight: Font.Medium
                color: "#f1f5f9"
            }
            Item { Layout.fillWidth: true }
            Text {
                text: blacklistSymbols.length + " 只"
                font.pixelSize: 11
                color: blacklistSymbols.length > 0 ? "#f59e0b" : "#64748b"
            }
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: 8
            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: 32
                radius: 6
                color: "#1e293b"
                border.width: 1
                border.color: "#334155"
                TextInput {
                    id: blacklistInputField
                    anchors.fill: parent
                    anchors.margins: 8
                    text: blacklistInput
                    font.pixelSize: 12
                    color: "#e2e8f0"
                    onTextChanged: blacklistInput = text
                    Text {
                        anchors.fill: parent
                        verticalAlignment: Text.AlignVCenter
                        text: "输入股票代码，如 600001 或 600001.SH"
                        font: parent.font
                        color: "#64748b"
                        visible: !parent.text && !parent.activeFocus
                    }
                }
            }
            Button {
                text: "添加"
                onClicked: panelRoot.addToBlacklist()
            }
        }

        ColumnLayout {
            Layout.fillWidth: true
            visible: blacklistSymbols.length > 0
            spacing: 4

            Repeater {
                model: blacklistSymbols
                delegate: Rectangle {
                    Layout.fillWidth: true
                    height: 28
                    radius: 4
                    color: index % 2 ? "#0b1220" : "#111827"
                    RowLayout {
                        anchors.fill: parent
                        anchors.margins: 6
                        spacing: 8
                        Text {
                            Layout.fillWidth: true
                            text: Bridge.StrategyBridge.stockDisplayName(modelData)
                            font.pixelSize: 11
                            color: "#e2e8f0"
                            elide: Text.ElideRight
                        }
                        Text {
                            text: modelData
                            font.pixelSize: 9
                            color: "#64748b"
                        }
                        Button {
                            text: "移除"
                            onClicked: panelRoot.removeFromBlacklist(index)
                        }
                    }
                }
            }
        }
    }
}
