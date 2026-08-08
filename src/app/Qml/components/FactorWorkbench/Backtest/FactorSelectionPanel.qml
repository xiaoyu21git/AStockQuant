import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15

Rectangle {
    id: panelRoot
    // -- Interface: data in --
    required property int entryMode
    required property var selectedFactorIds
    required property bool isBacktesting

    // -- Interface: events out --
    signal entryModeSelected(int mode)
    signal factorSelectorRequested()
    signal factorRemoved(string factorId)
    signal compositeChildRemoved(string instanceId)
    signal compositeChildWeightUpdated(string instanceId, real weight)
    signal compositeChildDirectionToggled(string instanceId, bool ascending)
    signal compositeChildNormalizeModeChanged(string instanceId, int mode)

    color: "transparent"
    implicitHeight: contentColumn.implicitHeight

    ColumnLayout {
        id: contentColumn
        anchors.fill: parent
        spacing: 12

        Rectangle {
            Layout.fillWidth: true
            radius: 10
            color: "#111827"
            border.width: 1
            border.color: "#243041"
            implicitHeight: entryModeRow.implicitHeight + 20

            RowLayout {
                id: entryModeRow
                anchors.fill: parent
                anchors.margins: 10
                spacing: 10

                Rectangle {
                    Layout.preferredWidth: 180
                    Layout.preferredHeight: 34
                    radius: 8
                    color: panelRoot.entryMode === 0 ? "#2563EB" : "#172033"
                    border.width: 1
                    border.color: panelRoot.entryMode === 0 ? "#60A5FA" : "#334155"

                    Text {
                        anchors.centerIn: parent
                        text: "单因子 / 批量回测"
                        font.pixelSize: 12
                        font.weight: Font.Medium
                        color: panelRoot.entryMode === 0 ? "white" : "#CBD5E1"
                    }

                    MouseArea {
                        anchors.fill: parent
                        cursorShape: Qt.PointingHandCursor
                        onClicked: panelRoot.entryModeSelected(0)
                    }
                }

                Rectangle {
                    Layout.preferredWidth: 180
                    Layout.preferredHeight: 34
                    radius: 8
                    color: panelRoot.entryMode === 1 ? "#0F766E" : "#172033"
                    border.width: 1
                    border.color: panelRoot.entryMode === 1 ? "#2DD4BF" : "#334155"

                    Text {
                        anchors.centerIn: parent
                        text: "组合因子回测"
                        font.pixelSize: 12
                        font.weight: Font.Medium
                        color: panelRoot.entryMode === 1 ? "white" : "#CBD5E1"
                    }

                    MouseArea {
                        anchors.fill: parent
                        cursorShape: Qt.PointingHandCursor
                        onClicked: panelRoot.entryModeSelected(1)
                    }
                }

                Item { Layout.fillWidth: true }

                Text {
                    text: panelRoot.entryMode === 1
                        ? "模式: 单个组合因子实例回测"
                        : "模式: 多个单因子独立批量回测"
                    font.pixelSize: 11
                    color: "#94A3B8"
                }
            }
        }

        // 因子选择区域
        RowLayout {
            Layout.fillWidth: true
            spacing: 12

            Rectangle {
                Layout.preferredWidth: 160
                Layout.preferredHeight: 40
                radius: 8
                color: panelRoot.entryMode === 1 ? "#0F766E" : "#3B82F6"

                Row {
                    anchors.centerIn: parent
                    spacing: 8

                    Text {
                        text: panelRoot.entryMode === 1 ? "🧩" : "📊"
                        font.pixelSize: 14
                        color: "white"
                    }

                    Text {
                        text: panelRoot.entryMode === 1 ? "选择子因子" : "选择因子"
                        font.pixelSize: 14
                        font.weight: Font.Medium
                        color: "white"
                    }
                }

                MouseArea {
                    anchors.fill: parent
                    cursorShape: Qt.PointingHandCursor
                    onClicked: panelRoot.factorSelectorRequested()
                }
            }

            Rectangle {
                Layout.preferredWidth: 96
                Layout.preferredHeight: 40
                radius: 8
                color: "#1F2937"
                border.width: 1
                border.color: "#334155"
                visible: panelRoot.entryMode === 1

                Text {
                    anchors.centerIn: parent
                    text: "一键均权"
                    font.pixelSize: 12
                    color: (compositeChildAllocations && compositeChildAllocations.length > 0) ? "#E2E8F0" : "#64748B"
                }

                MouseArea {
                    anchors.fill: parent
                    cursorShape: Qt.PointingHandCursor
                    enabled: compositeChildAllocations && compositeChildAllocations.length > 0
                    onClicked: rebalanceCompositeChildWeights()
                }
            }

            Text {
                Layout.fillWidth: true
                text: panelRoot.entryMode === 1
                    ? (compositeChildAllocations.length > 0
                       ? ("已选 " + compositeChildAllocations.length + " 个子因子")
                       : "请选择要组合回测的子因子")
                    : (selectedFactorIds.length > 0
                       ? ("已选 " + selectedFactorIds.length + " 个因子")
                       : "请选择要回测的因子")
                font.pixelSize: 12
                color: (panelRoot.entryMode === 1
                        ? compositeChildAllocations.length > 0
                        : selectedFactorIds.length > 0) ? "#38BDF8" : "#94A3B8"
                elide: Text.ElideRight
                verticalAlignment: Text.AlignVCenter
            }
        }

        Rectangle {
            Layout.fillWidth: true
            implicitHeight: selectedFactorsPanelContent.implicitHeight + 24
            Layout.preferredHeight: implicitHeight
            radius: 10
            color: "#0F172A"
            border.width: 1
            border.color: "#1E293B"

            ColumnLayout {
                id: selectedFactorsPanelContent
                anchors.fill: parent
                anchors.margins: 12
                spacing: 8

                RowLayout {
                    Layout.fillWidth: true

                    Text {
                        text: panelRoot.entryMode === 1 ? "组合草稿与验证" : "验证状态"
                        font.pixelSize: 12
                        font.weight: Font.Medium
                        color: "#F1F5F9"
                    }

                    Item { Layout.fillWidth: true }

                    Text {
                        text: panelRoot.entryMode === 1
                            ? "流程: child 可执行性校验 -> 组合合同校验 -> 组合回测"
                            : "流程: 可执行性校验 -> 回测效果校验"
                        font.pixelSize: 10
                        color: "#64748B"
                    }
                }

                Text {
                    Layout.fillWidth: true
                    text: panelRoot.entryMode === 1
                        ? (compositeChildAllocations.length > 0
                           ? ("当前组合草稿已选择 " + compositeChildAllocations.length + " 个子因子")
                           : "当前组合草稿未选择子因子")
                        : (selectedFactorIds.length > 0
                           ? ("当前已选择 " + selectedFactorIds.length + " 个因子")
                           : "当前未选择因子")
                    font.pixelSize: 11
                    color: ((panelRoot.entryMode === 1 ? compositeChildAllocations.length : selectedFactorIds.length) > 0) ? "#38BDF8" : "#64748B"
                    wrapMode: Text.WordWrap
                }

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 10
                    visible: panelRoot.entryMode === 0

                    Item {
                        Layout.fillWidth: true
                        implicitHeight: selectedFactorsFlow.childrenRect.height
                        visible: selectedFactorIds.length > 0

                        Flow {
                            id: selectedFactorsFlow
                            width: parent.width
                            spacing: compactCardSpacing

                            Repeater {
                                model: selectedFactorIds

                                delegate: Rectangle {
                                    width: compactCardWidth(
                                               selectedFactorsFlow.width,
                                               selectedFactorCardMinWidth,
                                               selectedFactorCardMaxWidth)
                                    radius: 8
                                    color: "#111827"
                                    border.width: 1
                                    border.color: validationState.accentColor
                                    implicitHeight: selectedFactorCardColumn.implicitHeight + 20

                                    property var validationState: factorValidationState(modelData)

                                    ColumnLayout {
                                        id: selectedFactorCardColumn
                                        anchors.fill: parent
                                        anchors.margins: 10
                                        anchors.rightMargin: 34
                                        spacing: 5

                                        RowLayout {
                                            Layout.fillWidth: true
                                            spacing: 6

                                            Text {
                                                Layout.fillWidth: true
                                                text: resolveFactorDisplayName(modelData)
                                                font.pixelSize: 12
                                                font.weight: Font.Medium
                                                color: "#F1F5F9"
                                                wrapMode: Text.WordWrap
                                                maximumLineCount: 2
                                            }

                                            Text {
                                                text: validationState.statusText
                                                font.pixelSize: 10
                                                color: validationState.accentColor
                                                horizontalAlignment: Text.AlignRight
                                            }
                                        }

                                        Text {
                                            Layout.fillWidth: true
                                            text: "因子ID: " + String(modelData)
                                            font.pixelSize: 10
                                            color: "#94A3B8"
                                            elide: Text.ElideRight
                                        }

                                        Text {
                                            Layout.fillWidth: true
                                            text: validationState.reason
                                            font.pixelSize: 10
                                            color: "#94A3B8"
                                            wrapMode: Text.WordWrap
                                            maximumLineCount: 3
                                        }
                                    }

                                    Rectangle {
                                        anchors.top: parent.top
                                        anchors.right: parent.right
                                        anchors.topMargin: 8
                                        anchors.rightMargin: 8
                                        width: 20
                                        height: 20
                                        radius: 10
                                        color: "#1F2937"
                                        border.width: 1
                                        border.color: "#334155"

                                        Text {
                                            anchors.centerIn: parent
                                            text: "×"
                                            font.pixelSize: 13
                                            font.weight: Font.DemiBold
                                            color: "#94A3B8"
                                        }

                                        MouseArea {
                                            anchors.fill: parent
                                            cursorShape: Qt.PointingHandCursor
                                            onClicked: panelRoot.factorRemoved(modelData)
                                        }
                                    }
                                }
                            }
                        }
                    }

                    Text {
                        text: "选择后卡片会展示名称、状态和失败原因"
                        font.pixelSize: 11
                        color: "#64748B"
                        visible: selectedFactorIds.length === 0
                    }
                }

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 10
                    visible: panelRoot.entryMode === 1

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 10

                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 4

                            Text {
                                text: "组合名称"
                                font.pixelSize: 11
                                color: "#94A3B8"
                            }

                            TextField {
                                Layout.fillWidth: true
                                text: compositeDraftName
                                placeholderText: "例如 quality_value_composite"
                                color: "#F8FAFC"
                                placeholderTextColor: "#64748B"
                                selectByMouse: true
                                background: Rectangle {
                                    radius: 6
                                    color: "#111827"
                                    border.width: 1
                                    border.color: "#334155"
                                }
                                onTextChanged: {
                                    compositeDraftName = text
                                    compositeDraftDirty = true
                                }
                            }
                        }

                        ColumnLayout {
                            Layout.preferredWidth: 120
                            spacing: 4

                            Text {
                                text: "组合模式"
                                font.pixelSize: 11
                                color: "#94A3B8"
                            }

                            ComboBox {
                                Layout.fillWidth: true
                                model: compositeCombineModeOptions
                                textRole: "label"
                                currentIndex: compositeCombineMode
                                onActivated: function(index) {
                                    compositeCombineMode = index
                                    compositeDraftDirty = true
                                }
                            }
                        }

                        ColumnLayout {
                            Layout.preferredWidth: 140
                            spacing: 4

                            Text {
                                text: "缺失策略"
                                font.pixelSize: 11
                                color: "#94A3B8"
                            }

                            ComboBox {
                                Layout.fillWidth: true
                                model: compositeMissingPolicyOptions
                                textRole: "label"
                                currentIndex: compositeMissingPolicy
                                onActivated: function(index) {
                                    compositeMissingPolicy = index
                                    compositeDraftDirty = true
                                }
                            }
                        }

                        ColumnLayout {
                            Layout.preferredWidth: 120
                            spacing: 4

                            Text {
                                text: "最小覆盖率"
                                font.pixelSize: 11
                                color: "#94A3B8"
                            }

                            TextField {
                                Layout.fillWidth: true
                                text: String(compositeMinimumCoverageRatio)
                                color: "#F8FAFC"
                                selectByMouse: true
                                background: Rectangle {
                                    radius: 6
                                    color: "#111827"
                                    border.width: 1
                                    border.color: "#334155"
                                }
                                onEditingFinished: {
                                    var parsedCoverage = Number(text)
                                    compositeMinimumCoverageRatio = isFinite(parsedCoverage) ? parsedCoverage : 0.5
                                    text = String(compositeMinimumCoverageRatio)
                                    compositeDraftDirty = true
                                }
                            }
                        }
                    }

                    Item {
                        Layout.fillWidth: true
                        implicitHeight: compositeChildFlow.childrenRect.height
                        visible: compositeChildAllocations.length > 0

                        Flow {
                            id: compositeChildFlow
                            width: parent.width
                            spacing: compactCardSpacing

                            Repeater {
                                model: compositeChildAllocations

                                delegate: Rectangle {
                                    width: compactCardWidth(
                                               compositeChildFlow.width,
                                               compositeChildCardMinWidth,
                                               compositeChildCardMaxWidth)
                                    radius: 8
                                    color: "#111827"
                                    border.width: 1
                                    border.color: childSupport.supported === false ? "#DC2626" : "#0EA5E9"
                                    implicitHeight: compositeChildColumn.implicitHeight + 18

                                    property string childInstanceId: String((modelData || {}).instanceId || "")
                                    property var childSupport: currentCacheFactorSupportMap()[childInstanceId] || ({})

                                    ColumnLayout {
                                        id: compositeChildColumn
                                        anchors.fill: parent
                                        anchors.margins: 10
                                        spacing: 6

                                        RowLayout {
                                            Layout.fillWidth: true

                                            Text {
                                                Layout.fillWidth: true
                                                text: String((modelData || {}).displayName || resolveFactorDisplayName(childInstanceId))
                                                font.pixelSize: 12
                                                font.weight: Font.Medium
                                                color: "#F8FAFC"
                                                wrapMode: Text.WordWrap
                                                maximumLineCount: 2
                                            }

                                            Text {
                                                text: childSupport.supported === false ? "不可回测" : "已校验"
                                                font.pixelSize: 10
                                                color: childSupport.supported === false ? "#FCA5A5" : "#67E8F9"
                                            }

                                            Rectangle {
                                                width: 20
                                                height: 20
                                                radius: 10
                                                color: "#1F2937"
                                                border.width: 1
                                                border.color: "#334155"

                                                Text {
                                                    anchors.centerIn: parent
                                                    text: "×"
                                                    font.pixelSize: 13
                                                    color: "#94A3B8"
                                                }

                                                MouseArea {
                                                    anchors.fill: parent
                                                    cursorShape: Qt.PointingHandCursor
                                                    onClicked: panelRoot.compositeChildRemoved(childInstanceId)
                                                }
                                            }
                                        }

                                        Text {
                                            Layout.fillWidth: true
                                            text: "因子ID: " + childInstanceId
                                            font.pixelSize: 10
                                            color: "#94A3B8"
                                            elide: Text.ElideRight
                                        }

                                        GridLayout {
                                            Layout.fillWidth: true
                                            columns: 2
                                            columnSpacing: 8
                                            rowSpacing: 6

                                            ColumnLayout {
                                                Layout.fillWidth: true
                                                spacing: 4

                                                Text {
                                                    text: "权重"
                                                    font.pixelSize: 10
                                                    color: "#94A3B8"
                                                }

                                                TextField {
                                                    Layout.fillWidth: true
                                                    text: String(Number((modelData || {}).weight || 0))
                                                    color: "#F8FAFC"
                                                    selectByMouse: true
                                                    background: Rectangle {
                                                        radius: 6
                                                        color: "#0F172A"
                                                        border.width: 1
                                                        border.color: "#334155"
                                                    }
                                                    onEditingFinished: panelRoot.compositeChildWeightUpdated(childInstanceId, text)
                                                }
                                            }

                                            ColumnLayout {
                                                Layout.fillWidth: true
                                                spacing: 4

                                                Text {
                                                    text: "方向"
                                                    font.pixelSize: 10
                                                    color: "#94A3B8"
                                                }

                                                ComboBox {
                                                    Layout.fillWidth: true
                                                    model: ["升序", "降序"]
                                                    currentIndex: (modelData || {}).ascending === false ? 1 : 0
                                                    onActivated: function(index) {
                                                        panelRoot.compositeChildDirectionToggled(childInstanceId, index === 0)
                                                    }
                                                }
                                            }

                                            ColumnLayout {
                                                Layout.fillWidth: true
                                                Layout.columnSpan: 2
                                                spacing: 4

                                                Text {
                                                    text: "标准化"
                                                    font.pixelSize: 10
                                                    color: "#94A3B8"
                                                }

                                                ComboBox {
                                                    Layout.fillWidth: true
                                                    model: compositeNormalizeModeOptions
                                                    textRole: "label"
                                                    currentIndex: Number((modelData || {}).normalizeMode || 0)
                                                    onActivated: function(index) {
                                                        panelRoot.compositeChildNormalizeModeChanged(childInstanceId, index)
                                                    }
                                                }
                                            }
                                        }

                                        Text {
                                            Layout.fillWidth: true
                                            text: childSupport.reason ? String(childSupport.reason) : "子因子已加入组合草稿"
                                            font.pixelSize: 10
                                            color: childSupport.supported === false ? "#FCA5A5" : "#94A3B8"
                                            wrapMode: Text.WordWrap
                                            maximumLineCount: 3
                                        }
                                    }
                                }
                            }
                        }
                    }

                    Text {
                        text: "组合模式下，每个 child 必须显式设置 weight / ascending / normalizeMode。"
                        font.pixelSize: 11
                        color: "#64748B"
                        visible: compositeChildAllocations.length === 0
                    }
                }

                Text {
                    Layout.fillWidth: true
                    text: panelRoot.entryMode === 1
                        ? "组合模式要求所有 child 在同一缓存集和同一交易窗口上通过支持性检查。"
                        : "目标阈值: 数据覆盖率 >= 90%, |IC| >= 0.02, IR >= 0.30, IC正率 >= 50%, 多空收益差 > 0"
                    font.pixelSize: 10
                    color: "#64748B"
                    wrapMode: Text.WordWrap
                }
            }

            // 保存为实例：持久化组合因子配置，以后可在因子列表中直接选用
            RowLayout {
                Layout.fillWidth: true
                spacing: 8
                visible: panelRoot.entryMode === 1 && compositeChildAllocations.length > 0

                Button {
                    id: saveCompositeButton
                    text: "💾 保存为因子实例"
                    Layout.preferredHeight: 32
                    enabled: !panelRoot.isBacktesting && compositeDraftName.length > 0
                    background: Rectangle { color: parent.enabled ? (saveCompositeButton.hovered ? "#059669" : "#047857") : "#374151"; radius: 6 }
                    contentItem: Text { text: parent.text; color: parent.enabled ? "white" : "#666"; font.pixelSize: 12; font.bold: true; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter }
                    onClicked: {
                        if (!factorService || typeof factorService.addFactor !== "function") {
                            console.log("FactorService 不可用，无法保存组合因子实例")
                            return
                        }
                        var draft = buildCompositeDraft()
                        // 纠正并补充类型标记（组合因子在 JSON 里要显式标 FACTOR_TYPE）
                        draft.factorType = "COMPOSITE"
                        var result = factorService.addFactor(draft)
                        if (result && String(result).length > 0) {
                            console.log("组合因子实例已保存:", result)
                            handlePanelStatusRequested("✓ 组合因子实例已保存: " + String(result), "success")
                        } else {
                            console.log("组合因子实例保存失败")
                            handlePanelStatusRequested("❌ 保存失败", "error")
                        }
                    }
                }

                Text {
                    text: compositeDraftName.length > 0
                        ? ("保存后将在因子列表中可见，随时可选用或再次编辑")
                        : "请输入组合名称后才可保存"
                    font.pixelSize: 10
                    color: compositeDraftName.length > 0 ? "#10B981" : "#FCA5A5"
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                }
            }
        }
    }
}
