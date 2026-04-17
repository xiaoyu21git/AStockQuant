import QtQuick 2.15
import QtQuick.Layouts 1.15

Rectangle {
    id: root

    property var stages: []
    property var strategyProfile: ({})
    property var validationSummary: ({})

    function validationMessages() {
        var messages = []
        var summary = validationSummary || ({})
        var errors = Array.isArray(summary.errors) ? summary.errors : []
        var warnings = Array.isArray(summary.warnings) ? summary.warnings : []
        for (var errorIndex = 0; errorIndex < errors.length && messages.length < 2; ++errorIndex) {
            messages.push(errors[errorIndex].message || "")
        }
        for (var warningIndex = 0; warningIndex < warnings.length && messages.length < 3; ++warningIndex) {
            messages.push(warnings[warningIndex].message || "")
        }
        return messages.filter(function(message) {
            return !!String(message || "").trim()
        })
    }

    implicitHeight: summaryColumn.implicitHeight + 24

    function stageCount() {
        return Array.isArray(stages) ? stages.length : 0
    }

    function groupCount() {
        var total = 0
        var list = Array.isArray(stages) ? stages : []
        for (var stageIndex = 0; stageIndex < list.length; ++stageIndex) {
            total += Array.isArray(list[stageIndex].groups) ? list[stageIndex].groups.length : 0
        }
        return total
    }

    function ruleCount() {
        var total = 0
        var list = Array.isArray(stages) ? stages : []
        for (var stageIndex = 0; stageIndex < list.length; ++stageIndex) {
            var groups = Array.isArray(list[stageIndex].groups) ? list[stageIndex].groups : []
            for (var groupIndex = 0; groupIndex < groups.length; ++groupIndex) {
                total += Array.isArray(groups[groupIndex].rules) ? groups[groupIndex].rules.length : 0
            }
        }
        return total
    }

    radius: 12
    color: "#0f172a"
    border.width: 1
    border.color: "#334155"

    ColumnLayout {
        id: summaryColumn
        anchors.fill: parent
        anchors.margins: 12
        spacing: 8

        GridLayout {
            Layout.fillWidth: true
            columns: width >= 560 ? 4 : 2
            rowSpacing: 8
            columnSpacing: 8

            Repeater {
                model: [
                    { title: "阶段", value: root.stageCount(), tone: "#60a5fa" },
                    { title: "规则组", value: root.groupCount(), tone: "#34d399" },
                    { title: "已放入规则", value: root.ruleCount(), tone: "#f59e0b" },
                    { title: "画像周期", value: String((root.strategyProfile || {}).horizon || "-").replace("_", "-"), tone: "#f472b6" }
                ]

                delegate: Rectangle {
                    Layout.fillWidth: true
                    implicitHeight: 64
                    radius: 10
                    color: "#111827"
                    border.width: 1
                    border.color: "#1f2937"

                    ColumnLayout {
                        anchors.fill: parent
                        anchors.margins: 10
                        spacing: 2

                        Text {
                            text: modelData.title
                            font.pixelSize: 11
                            color: "#94a3b8"
                        }

                        Text {
                            text: modelData.value
                            font.pixelSize: 16
                            font.weight: Font.DemiBold
                            color: modelData.tone
                        }
                    }
                }
            }
        }

        Rectangle {
            Layout.fillWidth: true
            visible: (validationSummary.errorCount || 0) > 0 || (validationSummary.warningCount || 0) > 0
            radius: 10
            color: (validationSummary.errorCount || 0) > 0 ? "#3f1d1d" : "#2b2114"
            border.width: 1
            border.color: (validationSummary.errorCount || 0) > 0 ? "#ef4444" : "#f59e0b"
            implicitHeight: validationColumn.implicitHeight + 16

            ColumnLayout {
                id: validationColumn
                anchors.fill: parent
                anchors.margins: 8
                spacing: 4

                Text {
                    Layout.fillWidth: true
                    text: validationSummary.summaryText || "规则组合存在待处理提示"
                    font.pixelSize: 12
                    font.weight: Font.DemiBold
                    color: "#f8fafc"
                    wrapMode: Text.WordWrap
                }

                Repeater {
                    model: root.validationMessages()

                    delegate: Text {
                        Layout.fillWidth: true
                        text: "• " + modelData
                        font.pixelSize: 11
                        color: "#e5e7eb"
                        wrapMode: Text.WordWrap
                    }
                }
            }
        }
    }
}