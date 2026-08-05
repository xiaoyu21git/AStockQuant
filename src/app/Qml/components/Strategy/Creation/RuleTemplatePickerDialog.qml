import QtQuick 2.15
import QtQuick.Layouts 1.15
import QtQuick.Controls 2.15
import AStock.Bridge 1.0 as Bridge

// 规则模板浏览弹窗 — 替代旧的「引入规则」按钮 + QuickImport + SuggestionPanel 三入口
Popup {
    id: root

    property string stageId: ""
    property string groupId: ""
    property string groupRole: ""
    property var strategyProfile: ({})
    property int selectedStrategyTypeIndex: 0

    signal ruleAdded(string templateId)

    modal: true
    dim: true
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
    width: 680
    height: 520
    x: (parent.width - width) / 2
    y: (parent.height - height) / 2

    // ── 搜索状态 ──
    property string searchText: ""
    property string selectedPhase: "all"
    property var suggestionItems: []
    property var filteredItems: []

    // 阶段选项
    readonly property var phases: [
        {id: "all", name: "全部"},
        {id: "market", name: "市场闸门"},
        {id: "signal", name: "信号审核"},
        {id: "eligibility", name: "资格检查"},
        {id: "rebalance", name: "调仓管理"},
        {id: "portfolio", name: "组合构建"}
    ]

    function refreshFilter() {
        var items = suggestionItems
        if (selectedPhase !== "all") {
            items = items.filter(function(item) {
                return String(item.phase || "").toLowerCase() === selectedPhase
            })
        }
        if (searchText.trim() !== "") {
            var q = searchText.trim().toLowerCase()
            items = items.filter(function(item) {
                return (String(item.displayName || "").toLowerCase().indexOf(q) >= 0)
                    || (String(item.summary || "").toLowerCase().indexOf(q) >= 0)
                    || (String(item.templateId || "").toLowerCase().indexOf(q) >= 0)
            })
        }
        filteredItems = items
    }

    onOpened: {
        searchText = ""
        selectedPhase = stageId || "all"
        // 从 C++ 拉取全部模板
        var raw = Bridge.RuleTemplateSuggestionService.suggestAllTemplates()
        suggestionItems = Array.isArray(raw) ? raw : []
        refreshFilter()
    }

    onSearchTextChanged: refreshFilter()
    onSelectedPhaseChanged: refreshFilter()

    background: Rectangle {
        color: "#1a2332"
        radius: 12
        border.width: 1
        border.color: "#334155"
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 16
        spacing: 12

        // ── 标题栏 ──
        RowLayout {
            Text {
                text: "选择规则模板"
                font.pixelSize: 18
                font.weight: Font.DemiBold
                color: "#f1f5f9"
            }
            Item { Layout.fillWidth: true }
            Text {
                text: filteredItems.length + " 个模板"
                font.pixelSize: 13
                color: "#94a3b8"
            }
        }

        // ── 搜索 + 筛选 ──
        RowLayout {
            spacing: 8
            Rectangle {
                Layout.fillWidth: true
                height: 36; radius: 8
                color: "#0f172a"
                border.width: 1; border.color: "#334155"
                RowLayout {
                    anchors.fill: parent; anchors.margins: 8; spacing: 6
                    Text {
                        text: "🔍"; font.pixelSize: 14
                    }
                    TextInput {
                        id: searchInput
                        Layout.fillWidth: true
                        font.pixelSize: 13; color: "#f1f5f9"
                        Text { text: "搜索模板名称..."; color: "#64748b"; font.pixelSize: 13; visible: !searchInput.text }
                        onTextChanged: root.searchText = text
                    }
                }
            }
            Repeater {
                model: phases
                delegate: Rectangle {
                    height: 32; radius: 6
                    width: phaseText.implicitWidth + 20
                    color: root.selectedPhase === modelData.id ? "#1e40af" : "#1e293b"
                    border.width: 1
                    border.color: root.selectedPhase === modelData.id ? "#3b82f6" : "#334155"
                    Text {
                        id: phaseText
                        anchors.centerIn: parent
                        text: modelData.name
                        font.pixelSize: 11
                        color: root.selectedPhase === modelData.id ? "white" : "#94a3b8"
                    }
                    MouseArea {
                        anchors.fill: parent
                        cursorShape: Qt.PointingHandCursor
                        onClicked: root.selectedPhase = modelData.id
                    }
                }
            }
        }

        // ── 模板列表 ──
        ScrollView {
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            ScrollBar.vertical.policy: ScrollBar.AsNeeded

            ColumnLayout {
                width: parent.width
                spacing: 4

                Repeater {
                    model: root.filteredItems
                    delegate: Rectangle {
                        Layout.fillWidth: true
                        height: 58
                        radius: 8
                        color: mouseArea.containsMouse ? "#1e3a5f" : "transparent"
                        border.width: 1
                        border.color: mouseArea.containsMouse ? "#3b82f6" : "transparent"

                        RowLayout {
                            anchors.fill: parent
                            anchors.margins: 10
                            spacing: 10

                            // 阶段标签
                            Rectangle {
                                width: 60; height: 24; radius: 4
                                color: {
                                    var p = String(modelData.phase || "").toLowerCase()
                                    if (p === "market") return "#422006"
                                    if (p === "signal") return "#1a2f4f"
                                    if (p === "rebalance") return "#1f2a1a"
                                    return "#1e293b"
                                }
                                Text {
                                    anchors.centerIn: parent
                                    text: Bridge.StrategyBridge.phaseShortName(String(modelData.phase || ""))
                                    font.pixelSize: 10
                                    color: {
                                        var p = String(modelData.phase || "").toLowerCase()
                                        if (p === "market") return "#f59e0b"
                                        if (p === "signal") return "#3b82f6"
                                        if (p === "rebalance") return "#10b981"
                                        return "#94a3b8"
                                    }
                                }
                            }

                            ColumnLayout {
                                Layout.fillWidth: true
                                spacing: 2
                                Text {
                                    text: String(modelData.displayName || modelData.templateId || "")
                                    font.pixelSize: 13; font.weight: Font.Medium
                                    color: "#f1f5f9"
                                    elide: Text.ElideRight
                                }
                                Text {
                                    text: String(modelData.summary || "")
                                    font.pixelSize: 11
                                    color: "#94a3b8"
                                    elide: Text.ElideRight
                                    maximumLineCount: 1
                                }
                            }

                            // 添加按钮
                            Rectangle {
                                width: 56; height: 28; radius: 6
                                color: "#10b981"
                                opacity: mouseArea.containsMouse ? 0.9 : 0.7
                                Text {
                                    anchors.centerIn: parent
                                    text: "添加"
                                    font.pixelSize: 12; color: "white"
                                }
                                MouseArea {
                                    id: btnArea
                                    anchors.fill: parent
                                    cursorShape: Qt.PointingHandCursor
                                    onClicked: {
                                        root.ruleAdded(String(modelData.templateId || ""))
                                        root.close()
                                    }
                                }
                            }
                        }

                        MouseArea {
                            id: mouseArea
                            anchors.fill: parent
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            onClicked: {
                                root.ruleAdded(String(modelData.templateId || ""))
                                root.close()
                            }
                        }
                    }
                }
            }
        }

        // ── 空状态 ──
        Text {
            visible: root.filteredItems.length === 0 && suggestionItems.length > 0
            text: "没有匹配的模板"
            font.pixelSize: 14; color: "#64748b"
            Layout.alignment: Qt.AlignCenter
        }
        Text {
            visible: suggestionItems.length === 0
            text: "正在加载模板列表..."
            font.pixelSize: 14; color: "#64748b"
            Layout.alignment: Qt.AlignCenter
        }
    }
}
