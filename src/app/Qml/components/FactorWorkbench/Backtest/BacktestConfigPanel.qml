import QtQuick 2.15
import QtQuick.Controls 2.15

import QtQuick.Layouts 1.15

RowLayout {

    id: panelRoot

    property alias groupComboBox: groupComboBox

    property alias datasetComboBox: datasetComboBox

    Layout.fillWidth: true

    spacing: 12

    Layout.alignment: Qt.AlignTop

    // 分组数量

    ColumnLayout {

        spacing: 4

        Layout.alignment: Qt.AlignTop

        Layout.preferredWidth: 96

        Layout.minimumWidth: 96

        Text {

            text: "分组数量"

            font.pixelSize: 12

            color: "#94A3B8"

        }

        ComboBox {

            id: groupComboBox

            Layout.preferredWidth: 96

            Layout.preferredHeight: 36

            model: ["5组", "10组", "20组"]

            currentIndex: 1

            background: Rectangle {

                radius: 6

                color: "#0F172A"

                border.width: 1

                border.color: "#334155"

            }

            contentItem: Text {

                text: parent.displayText

                font.pixelSize: 12

                color: "#F1F5F9"

                horizontalAlignment: Text.AlignLeft

                verticalAlignment: Text.AlignVCenter

            }

        }

        Text {

            text: "用于分层统计"

            font.pixelSize: 10

            color: "#64748B"

        }

    }

    ColumnLayout {

        spacing: 4

        Layout.alignment: Qt.AlignTop

        Layout.fillWidth: true

        Text {

            text: "缓存集"

            font.pixelSize: 12

            color: "#94A3B8"

        }

        ComboBox {

            id: datasetComboBox

            Layout.fillWidth: true

            Layout.preferredHeight: 36

            model: cacheDatasetOptions

            textRole: "text"

            enabled: !isBacktesting

            opacity: enabled ? 1.0 : 0.45

            background: Rectangle {

                radius: 6

                color: "#0F172A"

                border.width: 1

                border.color: enabled ? "#334155" : "#1E293B"

            }

            contentItem: Text {

                text: selectedCacheDatasetText()

                font.pixelSize: 12

                color: "#F1F5F9"

                leftPadding: 8

                verticalAlignment: Text.AlignVCenter

                elide: Text.ElideRight

            }

            indicator: Item {

                x: datasetComboBox.width - width - 12

                y: datasetComboBox.topPadding + (datasetComboBox.availableHeight - height) / 2

                width: 16

                height: 16

                Text {

                    anchors.centerIn: parent

                    text: datasetComboBox.popup.visible ? "▲" : "▼"

                    font.pixelSize: 10

                    color: "#94A3B8"

                }

            }

            popup: Popup {

                y: datasetComboBox.height + 4

                width: datasetComboBox.width

                implicitHeight: Math.min(contentItem.implicitHeight + 8, 280)

                padding: 4

                background: Rectangle {

                    radius: 8

                    color: "#1E293B"

                    border.width: 1

                    border.color: "#334155"

                }

                contentItem: ListView {

                    clip: true

                    implicitHeight: contentHeight

                    model: datasetComboBox.popup.visible ? datasetComboBox.delegateModel : null

                    currentIndex: datasetComboBox.highlightedIndex

                    ScrollIndicator.vertical: ScrollIndicator {}

                }

            }

            delegate: ItemDelegate {

                width: datasetComboBox.width - 8

                height: 40

                highlighted: datasetComboBox.highlightedIndex === index

                background: Rectangle {

                    radius: 6

                    color: parent.highlighted ? "#334155"

                    : parent.hovered ? "#2D3748" : "transparent"

                }

                contentItem: Text {

                    text: cacheDatasetOptionText(index)

                    font.pixelSize: 12

                    color: "#F1F5F9"

                    leftPadding: 8

                    rightPadding: 8

                    verticalAlignment: Text.AlignVCenter

                    elide: Text.ElideRight

                }

                onClicked: {

                    if (!isBacktesting) {

                        selectCacheDatasetAt(index)

                        datasetComboBox.popup.close()

                    }

                }

            }

            onActivated: function(index) {

                if (!isBacktesting) {

                    selectCacheDatasetAt(index)

                }

            }

        }

        Text {

            text: resolvedSelectedDatasetId() > 0 ? ("当前清洗缓存集: " + selectedCacheDatasetText()) : "请选择清洗缓存集"

            font.pixelSize: 10

            color: resolvedSelectedDatasetId() > 0 ? "#93C5FD" : "#64748B"

            wrapMode: Text.WordWrap

        }

        Text {

            text: "因子回测仅支持清洗缓存集模式"

            font.pixelSize: 10

            color: "#64748B"

        }

    }

    ColumnLayout {

        spacing: 4

        Layout.alignment: Qt.AlignTop

        Layout.preferredWidth: 180

        Layout.minimumWidth: 180

        Text {

            text: "回测参数"

            font.pixelSize: 13

            font.weight: Font.DemiBold

            color: "#E2E8F0"

        }

        Rectangle {

            Layout.preferredWidth: 112

            Layout.preferredHeight: 36

            radius: 6

            color: "#111827"

            border.width: 1

            border.color: "#334155"

            Text {

                anchors.centerIn: parent

                text: "设置参数"

                font.pixelSize: 11

                color: "#E2E8F0"

            }

            MouseArea {

                anchors.fill: parent

                cursorShape: Qt.PointingHandCursor

                onClicked: openRuntimeParamsDialog()

            }

        }

        Text {

            text: "持仓 / 调仓 / 费用 / 复权"

            font.pixelSize: 10

            color: "#94A3B8"

        }

    }

}

