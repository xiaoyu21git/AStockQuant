import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import AStock.Engine 1.0

Page {
    id: liveDashboardPage

    // 是否开启实盘监控（定时刷新快照）
    property bool liveMonitoringEnabled: false

    // 定时从快照文件刷新资金、持仓与动作日志
    Timer {
        id: liveRefreshTimer
        interval: 10000        // 每 10 秒刷新一次，可根据需要调整
        repeat: true
        running: liveDashboardPage.liveMonitoringEnabled
        onTriggered: {
            if (GlobalLiveAccount && GlobalLiveAccount.refresh)
                GlobalLiveAccount.refresh()

            if (GlobalLiveActions && GlobalLiveActions.refresh)
                GlobalLiveActions.refresh()

            if (GlobalLivePositions && GlobalLivePositions.refresh)
                GlobalLivePositions.refresh()
        }
    }

    header: ToolBar {
        height: 50

        RowLayout {
            anchors.fill: parent

            ToolButton {
                icon.source: "qrc:/icons/arrow_back.svg"
                onClicked: {
                    if (StackView.view) {
                        StackView.view.pop()
                    }
                }
            }

            Label {
                text: "📡 实盘仪表盘"
                font.bold: true
                font.pixelSize: 16
                Layout.fillWidth: true
            }

            // 开启/关闭实盘监控（仅控制 UI 定时刷新，不直接下单）
            Button {
                text: liveDashboardPage.liveMonitoringEnabled ? "停止实盘刷新" : "开启实盘刷新"
                Layout.rightMargin: 12
                onClicked: liveDashboardPage.liveMonitoringEnabled = !liveDashboardPage.liveMonitoringEnabled
            }
        }
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 20
        spacing: 20

        ColumnLayout {
            spacing: 4

            Text {
                text: "实盘账户监控（掘金）"
                font.pixelSize: 24
                font.bold: true
            }

            Text {
                text: "当前页面预留用于展示掘金账户的资金、持仓和当日盈亏。暂未接入真实账户数据。"
                font.pixelSize: 11
                color: "#888888"
                wrapMode: Text.WordWrap
            }
        }

        GridLayout {
            columns: 2
            columnSpacing: 20
            rowSpacing: 20
            Layout.fillWidth: true
            Layout.fillHeight: true

            Rectangle {
                Layout.fillWidth: true
                height: 120
                radius: 10
                color: "#2980b9"

                Column {
                    anchors.centerIn: parent
                    spacing: 10

                    Text {
                        text: "账户总资产"
                        color: "white"
                        font.pixelSize: 14
                    }

                    Text {
                        text: GlobalLiveAccount.totalAsset.toFixed(2)
                        color: "white"
                        font.pixelSize: 32
                        font.bold: true
                    }
                }
            }

            Rectangle {
                Layout.fillWidth: true
                height: 120
                radius: 10
                color: "#27ae60"

                Column {
                    anchors.centerIn: parent
                    spacing: 10

                    Text {
                        text: "可用资金"
                        color: "white"
                        font.pixelSize: 14
                    }

                    Text {
                        text: GlobalLiveAccount.availableCash.toFixed(2)
                        color: "white"
                        font.pixelSize: 32
                        font.bold: true
                    }
                }
            }

            Rectangle {
                Layout.fillWidth: true
                height: 120
                radius: 10
                color: "#8e44ad"

                Column {
                    anchors.centerIn: parent
                    spacing: 10

                    Text {
                        text: "当前持仓市值"
                        color: "white"
                        font.pixelSize: 14
                    }

                    Text {
                        text: GlobalLiveAccount.positionMarketValue.toFixed(2)
                        color: "white"
                        font.pixelSize: 32
                        font.bold: true
                    }
                }
            }

            Rectangle {
                Layout.fillWidth: true
                height: 120
                radius: 10
                color: "#c0392b"

                Column {
                    anchors.centerIn: parent
                    spacing: 10

                    Text {
                        text: "当日盈亏"
                        color: "white"
                        font.pixelSize: 14
                    }

                    Text {
                        text: GlobalLiveAccount.todayPnl.toFixed(2)
                        color: "white"
                        font.pixelSize: 32
                        font.bold: true
                    }
                }
            }
        }

        // 当前持仓明细
        GroupBox {
            title: "当前持仓明细"
            Layout.fillWidth: true
            Layout.fillHeight: true

            ListView {
                id: positionList
                anchors.fill: parent
                model: GlobalLivePositions

                header: RowLayout {
                    width: parent.width
                    spacing: 10

                    Text { text: "代码"; font.pixelSize: 12; font.bold: true; Layout.preferredWidth: 80 }
                    Text { text: "方向"; font.pixelSize: 12; font.bold: true; Layout.preferredWidth: 60 }
                    Text { text: "数量"; font.pixelSize: 12; font.bold: true; Layout.preferredWidth: 80 }
                    Text { text: "价格"; font.pixelSize: 12; font.bold: true; Layout.preferredWidth: 80 }
                    Text { text: "市值"; font.pixelSize: 12; font.bold: true; Layout.fillWidth: true }
                }

                delegate: RowLayout {
                    width: parent.width
                    spacing: 10

                    Text { text: model.symbol; font.pixelSize: 11; Layout.preferredWidth: 80 }
                    Text { text: model.direction; font.pixelSize: 11; Layout.preferredWidth: 60 }
                    Text { text: model.quantity.toFixed(0); font.pixelSize: 11; Layout.preferredWidth: 80 }
                    Text { text: model.price.toFixed(3); font.pixelSize: 11; Layout.preferredWidth: 80 }
                    Text { text: model.marketValue.toFixed(2); font.pixelSize: 11; Layout.fillWidth: true }
                }
            }
        }

        // 实盘动作日志：展示最近的下单/撤单等动作
        GroupBox {
            title: "实盘动作日志（最近记录）"
            Layout.fillWidth: true
            Layout.fillHeight: true

            ListView {
                id: actionList
                anchors.fill: parent
                model: GlobalLiveActions

                delegate: RowLayout {
                    width: parent.width
                    spacing: 10

                    Text { text: model.time; font.pixelSize: 11; color: "#bdc3c7"; Layout.preferredWidth: 150 }
                    Text { text: model.type; font.pixelSize: 11; Layout.preferredWidth: 90 }
                    Text { text: model.symbol; font.pixelSize: 11; Layout.preferredWidth: 80 }
                    Text { text: model.side; font.pixelSize: 11; Layout.preferredWidth: 60 }
                    Text { text: model.quantity.toFixed(0); font.pixelSize: 11; Layout.preferredWidth: 70 }
                    Text { text: model.price.toFixed(2); font.pixelSize: 11; Layout.preferredWidth: 80 }
                    Text { text: model.status; font.pixelSize: 11; Layout.fillWidth: true }
                }
            }
        }
    }

    Component.onCompleted: {
        // 进入页面时尝试刷新一次实盘账户快照
        if (GlobalLiveAccount && GlobalLiveAccount.refresh)
            GlobalLiveAccount.refresh()

        if (GlobalLiveActions && GlobalLiveActions.refresh)
            GlobalLiveActions.refresh()

        if (GlobalLivePositions && GlobalLivePositions.refresh)
            GlobalLivePositions.refresh()
    }
}
