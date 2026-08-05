// RuleParamEditor.qml — 规则模板参数编辑器
import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15

ColumnLayout {
    id: root
    spacing: 6

    property var params: []
    property bool editable: false
    property string reasonDisabled: ""
    signal paramChanged(string key, var value)

    Text {
        Layout.fillWidth: true
        text: qsTr("可调参数")
        font.pixelSize: 13; font.weight: Font.DemiBold
        color: "#F8FAFC"
    }

    Text {
        Layout.fillWidth: true
        visible: root.reasonDisabled !== ""
        text: root.reasonDisabled
        font.pixelSize: 11; color: "#F59E0B"; font.italic: true
    }

    Text {
        Layout.fillWidth: true
        visible: editable
        text: qsTr("修改数值自动保存到文件")
        font.pixelSize: 10; color: "#10B981"
    }

    Repeater {
        model: root.params
        delegate: RowLayout {
            Layout.fillWidth: true
            spacing: 4

            // 数值输入（左侧）
            TextField {
                id: valueInput
                Layout.preferredWidth: 70
                text: {
                    var v = modelData.currentValue
                    if (v === undefined || v === null) return "—"
                    var n = Number(v)
                    if (Math.abs(n - Math.round(n)) < 0.0001) return Math.round(n).toString()
                    return n.toFixed(4)
                }
                color: root.editable ? "#F8FAFC" : "#94A3B8"
                font.pixelSize: 11
                horizontalAlignment: TextInput.AlignRight
                readOnly: !root.editable
                validator: DoubleValidator {}
                background: Rectangle { color: "#0F172A"; radius: 3; border.width: 1; border.color: root.editable ? "#334155" : "#334155" }
                property string _lastSaved: ""
                function trySave() {
                    if (root.editable && modelData && modelData.key && acceptableInput && text !== _lastSaved) {
                        _lastSaved = text
                        root.paramChanged(modelData.key, parseFloat(text))
                    }
                }
                onEditingFinished: trySave()
                onActiveFocusChanged: if (!activeFocus) trySave()
                Binding on text {
                    when: !valueInput.activeFocus
                    value: {
                        var v = modelData.currentValue
                        if (v === undefined || v === null) return "—"
                        var n = Number(v)
                        if (Math.abs(n - Math.round(n)) < 0.0001) return Math.round(n).toString()
                        return n.toFixed(4)
                    }
                }
            }

            // 操作符
            Text {
                text: modelData.op || ""; font.pixelSize: 11; color: "#F59E0B"
                Layout.preferredWidth: 14; horizontalAlignment: Text.AlignHCenter
            }

            // 参数名 + 提示
            Text {
                Layout.fillWidth: true
                text: {
                    var label = modelData.displayLabel || modelData.key || ""
                    var v = modelData.currentValue
                    var op = modelData._rawOp || ""
                    var hint = ""
                    if (v !== undefined && v !== null && op !== "") {
                        if (op === "ge") hint = "  ≥" + Number(v).toFixed(2)
                        else if (op === "le") hint = "  ≤" + Number(v).toFixed(2)
                        else if (op === "gt") hint = "  >" + Number(v).toFixed(2)
                        else if (op === "lt") hint = "  <" + Number(v).toFixed(2)
                    }
                    return label + hint
                }
                font.pixelSize: 11; color: "#94A3B8"
                elide: Text.ElideRight
                ToolTip {
                    visible: labelHover.containsMouse
                    text: (modelData.key || "") + "  阈值: " + (modelData.currentValue || "")
                    delay: 300
                }
                MouseArea { id: labelHover; anchors.fill: parent; hoverEnabled: true }
            }
        }
    }

    // 空态
    Text {
        Layout.fillWidth: true
        visible: !root.params || root.params.length === 0
        text: qsTr("暂无可调参数")
        font.pixelSize: 12; color: "#64748B"
    }
}
