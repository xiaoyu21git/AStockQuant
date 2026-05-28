import QtQuick 2.15
import QtQuick.Layouts 1.15
import QtQuick.Controls 2.15
import AStock.Bridge 1.0

Rectangle {
    id: root

    property string stageId: ""
    property var groupData: ({})
    property bool isSelected: false
    property string issueLevel: ""
    property var issueMessages: []

    signal addRuleRequested(string stageId, string groupId)
    signal groupSelected(string stageId, string groupId)
    signal groupEdited(string stageId, string groupId, var patch)
    signal removeRuleRequested(string stageId, string groupId, string instanceId)
    signal moveRuleRequested(string stageId, string groupId, string instanceId, int direction)

    function roleOptions() {
        return [
            { value: "must_pass", label: "必须满足" },
            { value: "any_pass", label: "任一满足" },
            { value: "veto", label: "否决条件" },
            { value: "score_boost", label: "评分增强" },
            { value: "position_management", label: "仓位管理" },
            { value: "execution_constraint", label: "执行限制" },
            { value: "account_guard", label: "账户保护" }
        ]
    }

    function operatorOptions() {
        return [
            { value: "all", label: "全部满足" },
            { value: "any", label: "任一满足" },
            { value: "at_least", label: "至少命中" },
            { value: "score_sum", label: "累计评分" },
            { value: "first_match", label: "首个命中" }
        ]
    }

    function roleIndex(role) {
        var options = roleOptions()
        for (var index = 0; index < options.length; ++index) {
            if (options[index].value === role) {
                return index
            }
        }
        return 0
    }

    function operatorIndex(operatorValue) {
        var options = operatorOptions()
        for (var index = 0; index < options.length; ++index) {
            if (options[index].value === operatorValue) {
                return index
            }
        }
        return 0
    }

    function emitGroupPatch(patch) {
        root.groupEdited(root.stageId, (groupData && groupData.groupId) || "", patch || ({}))
    }

    function roleDisplayName(role) {
        var mapping = {
            must_pass: "必须满足",
            any_pass: "任一满足",
            veto: "否决条件",
            score_boost: "评分增强",
            position_management: "仓位管理",
            execution_constraint: "执行限制",
            account_guard: "账户保护"
        }
        return mapping[role] || role || "规则组"
    }

    function operatorDisplayName(operatorValue) {
        var mapping = {
            all: "全部满足",
            any: "任一满足",
            at_least: "至少命中",
            score_sum: "累计评分",
            first_match: "首个命中"
        }
        return mapping[operatorValue] || operatorValue || "未设置"
    }

    radius: 10
    color: "#111827"
    border.width: 1
    border.color: root.isSelected ? "#2563eb" : "#1f2937"
    implicitHeight: groupLayout.implicitHeight + 20

    ColumnLayout {
        id: groupLayout
        anchors.fill: parent
        anchors.margins: 10
        spacing: 8

        RowLayout {
            id: groupHeaderRow
            Layout.fillWidth: true
            spacing: 8

            Text {
                Layout.fillWidth: true
                text: (groupData && groupData.title) || "未命名规则组"
                font.pixelSize: 13
                font.weight: Font.DemiBold
                color: "#f8fafc"
                wrapMode: Text.WordWrap
            }

            Rectangle {
                radius: 9
                color: "#1e3a8a"
                implicitWidth: roleText.implicitWidth + 14
                implicitHeight: 22

                Text {
                    id: roleText
                    anchors.centerIn: parent
                    text: root.roleDisplayName(groupData && groupData.role)
                    font.pixelSize: 10
                    font.weight: Font.Medium
                    color: "#dbeafe"
                }
            }
        }

        Text {
            id: groupSummaryText
            Layout.fillWidth: true
            text: root.operatorDisplayName(groupData && groupData.operator) + " · " + ((groupData && groupData.description) || "用于组合当前阶段规则。")
            font.pixelSize: 11
            color: "#94a3b8"
            wrapMode: Text.WordWrap
        }

        Rectangle {
            Layout.fillWidth: true
            visible: Array.isArray(root.issueMessages) && root.issueMessages.length > 0
            radius: 8
            color: root.issueLevel === "error" ? "#3f1d1d" : "#2b2114"
            border.width: 1
            border.color: root.issueLevel === "error" ? "#ef4444" : "#f59e0b"
            implicitHeight: issueMessageText.implicitHeight + 14

            Text {
                id: issueMessageText
                anchors.fill: parent
                anchors.margins: 7
                text: (root.issueLevel === "error" ? "阻断提示：" : "组合提示：")
                      + (root.issueMessages[0] || "")
                      + (root.issueMessages.length > 1 ? (" 等 " + root.issueMessages.length + " 条") : "")
                font.pixelSize: 11
                color: "#f8fafc"
                wrapMode: Text.WordWrap
            }
        }

        Rectangle {
            Layout.fillWidth: true
            radius: 8
            color: "#0b1220"
            border.width: 1
            border.color: "#1f2937"
            implicitHeight: editorColumn.implicitHeight + 16

            ColumnLayout {
                id: editorColumn
                anchors.fill: parent
                anchors.margins: 8
                spacing: 8

                Text {
                    text: "规则组设置"
                    font.pixelSize: 11
                    font.weight: Font.DemiBold
                    color: "#cbd5e1"
                }

                TextField {
                    Layout.fillWidth: true
                    text: (groupData && groupData.title) || ""
                    placeholderText: "规则组标题"
                    color: "#f8fafc"
                    selectByMouse: true
                    background: Rectangle {
                        radius: 6
                        color: "#111827"
                        border.width: 1
                        border.color: "#334155"
                    }
                    onEditingFinished: {
                        if (text !== ((groupData && groupData.title) || "")) {
                            root.emitGroupPatch({ title: text })
                        }
                    }
                }

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 8

                    ComboBox {
                        id: roleCombo
                        Layout.fillWidth: true
                        model: root.roleOptions()
                        textRole: "label"
                        valueRole: "value"
                        currentIndex: root.roleIndex((groupData && groupData.role) || "")
                        onActivated: function(index) {
                            var option = model[index]
                            if (option) {
                                root.emitGroupPatch({ role: option.value })
                            }
                        }
                    }

                    ComboBox {
                        id: operatorCombo
                        Layout.fillWidth: true
                        model: root.operatorOptions()
                        textRole: "label"
                        valueRole: "value"
                        currentIndex: root.operatorIndex((groupData && groupData.operator) || "")
                        onActivated: function(index) {
                            var option = model[index]
                            if (!option) {
                                return
                            }
                            var patch = { operator: option.value }
                            if (option.value === "at_least" && !((groupData && groupData.matchThreshold) > 0)) {
                                patch.matchThreshold = 1
                            }
                            root.emitGroupPatch(patch)
                        }
                    }
                }

                RowLayout {
                    Layout.fillWidth: true
                    visible: ((groupData && groupData.operator) || "") === "at_least"
                    spacing: 8

                    Text {
                        text: "最少命中数"
                        font.pixelSize: 11
                        color: "#94a3b8"
                    }

                    SpinBox {
                        from: 1
                        to: Math.max(1, Array.isArray(groupData && groupData.rules) ? groupData.rules.length : 1)
                        value: Math.max(1, Number((groupData && (groupData.groupMinMatchCount || groupData.matchThreshold)) || 1))
                        editable: true
                        onValueModified: root.emitGroupPatch({ matchThreshold: value })
                    }

                    Item { Layout.fillWidth: true }
                }

                TextArea {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 54
                    text: (groupData && groupData.description) || ""
                    placeholderText: "规则组说明"
                    wrapMode: Text.WordWrap
                    color: "#e2e8f0"
                    selectByMouse: true
                    background: Rectangle {
                        radius: 6
                        color: "#111827"
                        border.width: 1
                        border.color: "#334155"
                    }
                    onActiveFocusChanged: {
                        if (!activeFocus && text !== ((groupData && groupData.description) || "")) {
                            root.emitGroupPatch({ description: text })
                        }
                    }
                }
            }
        }

        ColumnLayout {
            Layout.fillWidth: true
            spacing: 6
            visible: Array.isArray(groupData && groupData.rules) && groupData.rules.length > 0

            Repeater {
                model: Array.isArray(groupData && groupData.rules) ? groupData.rules : []

                delegate: Rectangle {
                    id: ruleCard
                    required property var modelData
                    readonly property int ruleIndex: index
                    readonly property var bindingData: ({
                        template_id: modelData.templateId || "",
                        template_display_name: modelData.templateName || "",
                        file_name: modelData.fileName || "",
                        file_path: modelData.filePath || "",
                        phase: modelData.phase || root.stageId,
                        summary: modelData.summary || "",
                        term_id: modelData.termId || "",
                        term_display_name: modelData.termName || "",
                        default_injected: !!modelData.defaultInjected
                    })
                    Layout.fillWidth: true
                    radius: 9
                    color: "#0b1220"
                    border.width: 1
                    border.color: modelData.ready ? "#16a34a" : "#475569"
                    implicitHeight: ruleColumn.implicitHeight + 12

                    ColumnLayout {
                        id: ruleColumn
                        anchors.fill: parent
                        anchors.margins: 6
                        spacing: 6

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 6

                            Rectangle {
                                radius: 8
                                color: "#111827"
                                border.width: 1
                                border.color: modelData.ready ? "#16a34a" : "#475569"
                                Layout.fillWidth: true
                                implicitHeight: chipText.implicitHeight + 10

                                Text {
                                    id: chipText
                                    anchors.fill: parent
                                    anchors.margins: 8
                                    text: modelData.templateName || modelData.templateId || "未命名模板"
                                    font.pixelSize: 10
                                    color: "#e2e8f0"
                                    verticalAlignment: Text.AlignVCenter
                                    wrapMode: Text.WordWrap
                                }
                            }

                            Rectangle {
                                visible: !!modelData.defaultInjected
                                radius: 8
                                color: "#3f2d16"
                                border.width: 1
                                border.color: "#f59e0b"
                                implicitWidth: presetChipText.implicitWidth + 14
                                implicitHeight: presetChipText.implicitHeight + 10

                                Text {
                                    id: presetChipText
                                    anchors.centerIn: parent
                                    text: "系统预置"
                                    font.pixelSize: 10
                                    font.weight: Font.Medium
                                    color: "#fde68a"
                                }
                            }

                            Button {
                                text: "上移"
                                enabled: ruleIndex > 0
                                onClicked: root.moveRuleRequested(root.stageId, (groupData && groupData.groupId) || "", modelData.instanceId || "", -1)
                            }

                            Button {
                                text: "下移"
                                enabled: Array.isArray(groupData && groupData.rules) && ruleIndex < groupData.rules.length - 1
                                onClicked: root.moveRuleRequested(root.stageId, (groupData && groupData.groupId) || "", modelData.instanceId || "", 1)
                            }

                            Button {
                                text: "移除"
                                onClicked: root.removeRuleRequested(root.stageId, (groupData && groupData.groupId) || "", modelData.instanceId || "")
                            }
                        }

                        Text {
                            Layout.fillWidth: true
                            visible: !!(modelData.summary || modelData.termName || modelData.termId)
                            text: (modelData.summary || "")
                                  + ((modelData.summary && (modelData.termName || modelData.termId)) ? "\n" : "")
                                  + ((modelData.termName || modelData.termId) ? ("绑定术语: " + (modelData.termName || modelData.termId)) : "")
                            font.pixelSize: 11
                            color: "#94a3b8"
                            wrapMode: Text.WordWrap
                        }

                        RuleTemplateStructureView {
                            Layout.fillWidth: true
                            bindingData: ruleCard.bindingData
                            compact: true
                        }
                    }
                }
            }
        }

        Rectangle {
            Layout.fillWidth: true
            visible: !Array.isArray(groupData && groupData.rules) || groupData.rules.length === 0
            radius: 8
            color: "#0b1220"
            border.width: 1
            border.color: "#1f2937"
            implicitHeight: 44

            Text {
                anchors.fill: parent
                anchors.margins: 10
                text: "当前组还没有放入规则。优先从上方默认规则包快捷引入，右侧建议栏只用于补充非默认模板。"
                font.pixelSize: 11
                color: "#64748b"
                verticalAlignment: Text.AlignVCenter
                wrapMode: Text.WordWrap
            }
        }

        Button {
            text: "引入规则"
            onClicked: root.addRuleRequested(root.stageId, (groupData && groupData.groupId) || "")
        }
    }

    MouseArea {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        height: groupHeaderRow.height + groupSummaryText.height + groupLayout.spacing + 8
        acceptedButtons: Qt.LeftButton
        propagateComposedEvents: true
        onClicked: {
            root.groupSelected(root.stageId, (root.groupData && root.groupData.groupId) || "")
        }
    }
}