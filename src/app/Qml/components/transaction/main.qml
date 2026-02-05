// main.qml
import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import QtQuick.Window 2.15
import QtCharts 2.15

ApplicationWindow {
    id: mainWindow
    width: 1600
    height: 1000
    visible: true
    title: "量化交易系统 - 个性化数据分析"
    color: "#0f172a"

    // 用户数据模型
    QtObject {
        id: userData
        property string name: "张伟"
        property string role: "交易员"
        property string level: "PRO"
        property int riskTolerance: 7
        property string investmentGoal: "growth"
        property int totalTrades: 842
        property var favoriteSectors: ["科技", "金融"]
        property var watchlist: ["AAPL", "MSFT", "GOOGL", "TSLA", "NVDA", "AMZN"]
        property var strategies: [
            {name: "成长型科技股策略", active: true, weight: 35, return: 24.5, winRate: 68.2, maxDrawdown: -12.3},
            {name: "价值投资策略", active: true, weight: 25, return: 15.8, winRate: 72.4, maxDrawdown: -8.7},
            {name: "短线动量策略", active: false, weight: 20, return: 18.3, winRate: 58.6, maxDrawdown: -15.2},
            {name: "股息收益策略", active: true, weight: 20, return: 9.5, winRate: 0, maxDrawdown: -6.8, dividend: 4.2}
        ]
        property var alerts: [
            {type: "股价突破", symbol: "AAPL", price: 175.00, enabled: true, color: "#3b82f6"},
            {type: "收益率目标", target: 5, enabled: true, color: "#10b981"},
            {type: "风险系数", threshold: 0.8, enabled: true, color: "#f59e0b"},
            {type: "交易频率", threshold: 10, enabled: true, color: "#ef4444"}
        ]
    }

    // 头部区域
    Rectangle {
        id: header
        width: parent.width
        height: 100
        color: "transparent"
        anchors.top: parent.top

        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: 30
            anchors.rightMargin: 30

            // Logo区域
            Row {
                spacing: 12
                Layout.alignment: Qt.AlignLeft | Qt.AlignVCenter

                Rectangle {
                    width: 40
                    height: 40
                    radius: 8
                    color: "#1e293b"
                    border.color: "#3b82f6"
                    border.width: 2

                    Text {
                        text: "Q"
                        color: "#3b82f6"
                        font.pointSize: 20
                        font.bold: true
                        anchors.centerIn: parent
                    }
                }

                Column {
                    spacing: 2
                    Text {
                        text: "量化交易系统"
                        font.pointSize: 18
                        font.bold: true
                        color: "#e2e8f0"
                    }
                    Text {
                        text: "个性化数据分析模块"
                        font.pointSize: 12
                        color: "#94a3b8"
                    }
                }
            }

            // 用户信息区域
            Row {
                spacing: 20
                Layout.alignment: Qt.AlignRight | Qt.AlignVCenter

                // 通知按钮
                Rectangle {
                    width: 50
                    height: 50
                    radius: 8
                    color: "#1e293b"
                    border.color: "#334155"
                    border.width: 1

                    MouseArea {
                        anchors.fill: parent
                        onClicked: notificationMenu.open()

                        Column {
                            anchors.centerIn: parent
                            spacing: 2
                            Text {
                                text: "🔔"
                                font.pointSize: 18
                                anchors.horizontalCenter: parent.horizontalCenter
                            }
                            Rectangle {
                                width: 18
                                height: 18
                                radius: 9
                                color: "#ef4444"
                                anchors.horizontalCenter: parent.horizontalCenter
                                visible: true

                                Text {
                                    text: "3"
                                    color: "white"
                                    font.pointSize: 10
                                    font.bold: true
                                    anchors.centerIn: parent
                                }
                            }
                        }
                    }

                    Menu {
                        id: notificationMenu
                        y: parent.height

                        MenuItem {
                            text: "AAPL 价格突破 $175.00"
                            onTriggered: console.log("查看AAPL预警")
                        }
                        MenuItem {
                            text: "本月收益率已达 5.2%"
                            onTriggered: console.log("查看收益率")
                        }
                        MenuItem {
                            text: "风险系数接近阈值"
                            onTriggered: console.log("查看风险")
                        }
                    }
                }

                // 用户信息
                Rectangle {
                    width: 180
                    height: 50
                    radius: 8
                    color: "#1e293b"
                    border.color: "#334155"
                    border.width: 1

                    MouseArea {
                        anchors.fill: parent
                        onClicked: personalizationModal.open()

                        Row {
                            anchors.fill: parent
                            anchors.leftMargin: 12
                            anchors.rightMargin: 12
                            spacing: 12

                            // 用户头像
                            Rectangle {
                                width: 36
                                height: 36
                                radius: 18
                                anchors.verticalCenter: parent.verticalCenter
                                gradient: Gradient {
                                    GradientStop { position: 0.0; color: "#3b82f6" }
                                    GradientStop { position: 1.0; color: "#8b5cf6" }
                                }

                                Text {
                                    text: userData.name.charAt(0)
                                    color: "white"
                                    font.pointSize: 16
                                    font.bold: true
                                    anchors.centerIn: parent
                                }
                            }

                            // 用户信息文本
                            Column {
                                spacing: 2
                                anchors.verticalCenter: parent.verticalCenter
                                width: parent.width - 48

                                Text {
                                    text: userData.name + " (" + userData.role + ")"
                                    color: "#e2e8f0"
                                    font.pointSize: 12
                                    font.bold: true
                                    elide: Text.ElideRight
                                    width: parent.width
                                }

                                Text {
                                    text: userData.level + " 会员"
                                    color: "#f59e0b"
                                    font.pointSize: 10
                                    width: parent.width
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    // 个性化标签区域
    Rectangle {
        id: tagsContainer
        width: parent.width
        height: 50
        color: "transparent"
        anchors.top: header.bottom
        anchors.left: parent.left
        anchors.leftMargin: 30

        Row {
            spacing: 10
            anchors.verticalCenter: parent.verticalCenter

            Repeater {
                model: ["我的关注", "成长型策略", "保守型配置", "短线交易", "科技板块", "个性化设置"]

                Rectangle {
                    id: tag
                    width: tagText.width + 28
                    height: 36
                    radius: 18
                    color: index === 0 ? "#3b82f6" : "#1e293b"
                    border.color: index === 0 ? "#3b82f6" : "#334155"
                    border.width: 1

                    property bool active: index === 0

                    MouseArea {
                        anchors.fill: parent
                        onClicked: {
                            // 激活当前标签，取消其他标签
                            for (var i = 0; i < tagsContainer.children[0].children.length; i++) {
                                var child = tagsContainer.children[0].children[i];
                                if (child.hasOwnProperty("active")) {
                                    child.active = (child === tag);
                                    child.color = child.active ? "#3b82f6" : "#1e293b";
                                    child.border.color = child.active ? "#3b82f6" : "#334155";
                                }
                            }
                            
                            if (modelData === "个性化设置") {
                                personalizationModal.open();
                            }
                        }
                    }

                    Row {
                        spacing: 6
                        anchors.centerIn: parent

                        Text {
                            text: {
                                var icons = ["★", "📈", "🛡️", "⚡", "🏭", "⚙️"];
                                return icons[index];
                            }
                            color: tag.active ? "white" : "#e2e8f0"
                            font.pointSize: 12
                        }

                        Text {
                            id: tagText
                            text: modelData
                            color: tag.active ? "white" : "#e2e8f0"
                            font.pointSize: 12
                        }
                    }
                }
            }
        }
    }

    // 选项卡区域
    Rectangle {
        id: tabsContainer
        width: parent.width - 60
        height: 60
        radius: 10
        color: "#1e293b"
        anchors.top: tagsContainer.bottom
        anchors.topMargin: 20
        anchors.horizontalCenter: parent.horizontalCenter

        Row {
            anchors.fill: parent
            spacing: 0

            Repeater {
                model: ["个性化看板", "策略偏好", "预警设置", "交易历史"]

                Rectangle {
                    id: tab
                    width: parent.width / 4
                    height: parent.height
                    color: index === 0 ? "#0f172a" : "transparent"
                    border.bottom: index === 0 ? 3 : 0
                    border.color: "#3b82f6"

                    property bool active: index === 0

                    MouseArea {
                        anchors.fill: parent
                        onClicked: {
                            // 激活当前选项卡
                            for (var i = 0; i < tabsContainer.children[0].children.length; i++) {
                                var child = tabsContainer.children[0].children[i];
                                if (child.hasOwnProperty("active")) {
                                    child.active = (child === tab);
                                    child.color = child.active ? "#0f172a" : "transparent";
                                    child.border.bottom = child.active ? 3 : 0;
                                }
                            }
                            
                            // 显示对应的内容
                            dashboardContent.visible = index === 0;
                            strategiesContent.visible = index === 1;
                            alertsContent.visible = index === 2;
                            historyContent.visible = index === 3;
                        }
                    }

                    Row {
                        spacing: 8
                        anchors.centerIn: parent

                        Text {
                            text: {
                                var icons = ["📊", "♛", "🔔", "📜"];
                                return icons[index];
                            }
                            color: tab.active ? "#3b82f6" : "#e2e8f0"
                            font.pointSize: 14
                        }

                        Text {
                            text: modelData
                            color: tab.active ? "#3b82f6" : "#e2e8f0"
                            font.pointSize: 14
                            font.bold: true
                        }
                    }
                }
            }
        }
    }

    // 主内容区域
    Rectangle {
        id: mainContent
        width: parent.width - 60
        height: parent.height - header.height - tagsContainer.height - tabsContainer.height - 80
        color: "transparent"
        anchors.top: tabsContainer.bottom
        anchors.topMargin: 20
        anchors.horizontalCenter: parent.horizontalCenter

        // 个性化看板内容
        DashboardContent {
            id: dashboardContent
            visible: true
            anchors.fill: parent
            userData: userData
        }

        // 策略偏好内容
        StrategiesContent {
            id: strategiesContent
            visible: false
            anchors.fill: parent
            userData: userData
        }

        // 预警设置内容
        AlertsContent {
            id: alertsContent
            visible: false
            anchors.fill: parent
            userData: userData
        }

        // 交易历史内容
        HistoryContent {
            id: historyContent
            visible: false
            anchors.fill: parent
            userData: userData
        }
    }

    // 个性化设置模态框
    PersonalizationModal {
        id: personalizationModal
        userData: userData
        onSettingsSaved: {
            console.log("个性化设置已保存");
            dashboardContent.updatePersonalizedData();
        }
    }
}