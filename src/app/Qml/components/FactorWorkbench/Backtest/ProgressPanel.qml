import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15

ColumnLayout {
    id: panelRoot
        Layout.fillWidth: true
        spacing: 4
        visible: isBacktesting

        // 进度条
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 8
            radius: 4
            color: "#334155"

            Rectangle {
                width: parent.width * (backtestProgress / 100)
                height: parent.height
                radius: 4
                color: "#3B82F6"
            }
        }

        // 进度信息
        RowLayout {
            Layout.fillWidth: true

            Text {
                text: backtestStatus
                font.pixelSize: 12
                color: "#F59E0B"
            }

            Text {
                text: activeRunFactorIds.length > 0
                    ? ("本次回测: " + activeRunFactorIds.length + " 个因子")
                    : (selectedFactorIds.length > 0 ? ("待回测: " + selectedFactorIds.length + " 个因子") : "")
                font.pixelSize: 12
                color: "#38BDF8"
                visible: activeRunFactorIds.length > 0 || selectedFactorIds.length > 0
            }

            Text {
                text: activeRunFactorIds.length > 0 ? activeRunFactorDisplayText() : selectedFactorDisplayText()
                font.pixelSize: 11
                color: "#94A3B8"
                elide: Text.ElideRight
                Layout.fillWidth: true
                visible: activeRunFactorIds.length > 0 || selectedFactorIds.length > 0
            }

            Text {
                text: backtestProgress + "%"
                font.pixelSize: 12
                color: "#94A3B8"
            }

            Item { Layout.fillWidth: true }

            Text {
                text: currentGroup > 0 ? "批次: " + currentGroup + "/" + totalGroups : ""
                font.pixelSize: 12
                color: "#94A3B8"
                visible: currentGroup > 0
            }
        }
    }
