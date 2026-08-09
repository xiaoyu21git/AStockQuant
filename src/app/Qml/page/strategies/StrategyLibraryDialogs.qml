import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import "../../components/Strategy" as StrategyComponents

Item {
    id: panelRoot
    // -- Interface --
    required property QtObject page
    property alias actionFeedbackDialog: actionFeedbackDialog
    property alias deleteConfirmDialog: deleteConfirmDialog

    // 新建策略对话框
    StrategyComponents.CreateStrategyDialog {
        id: createDialog
        anchors.centerIn: parent
        visible: isOpen
        
        onStrategyCreated: function(strategyData) {
            console.log("创建策略:", strategyData);
            // 添加到策略模型
            panelRoot.page.strategyModel.append({
                name: strategyData.name,
                description: strategyData.description,
                status: strategyData.status,
                returns: strategyData.returns,
                maxDrawdown: strategyData.maxDrawdown,
                sharpeRatio: strategyData.sharpeRatio,
                winRate: strategyData.winRate,
                tags: strategyData.tags,
                runningDays: 0,
                tradesCount: 0,
                position: 0,
                dailyPnL: 0
            });
        }
        
        onClosed: {
            // 关闭对话框
        }
    }
    
    // 筛选弹窗
    StrategyComponents.StrategyFilter {
        id: filterComponent
        anchors.centerIn: parent
        visible: panelRoot.page.showFilter
        
        onFilterApplied: function(filterData) {
            console.log("应用筛选:", filterData);
            panelRoot.page.showFilter = false;
        }
        
        onFilterReset: function() {
            console.log("重置筛选");
        }
        
        onFilterClosed: function() {
            panelRoot.page.showFilter = false;
        }
    }
    
    // 排序弹窗
    StrategyComponents.StrategySorter {
        id: sorterComponent
        anchors.centerIn: parent
        visible: panelRoot.page.showSorter
        
        onSortApplied: function(sortType) {
            console.log("应用排序:", sortType);
            panelRoot.page.showSorter = false;
        }
        
        onSortClosed: function() {
            panelRoot.page.showSorter = false;
        }
    }

    Dialog {
        id: actionFeedbackDialog
        anchors.centerIn: parent
        modal: true
        width: 440

        background: Rectangle {
            radius: panelRoot.page.borderRadiusMedium
            color: panelRoot.page.secondaryBg
            border.color: panelRoot.page.actionFeedbackError ? riseRed : panelRoot.page.accentBlue
            border.width: 1
        }

        contentItem: ColumnLayout {
            spacing: panelRoot.page.spacingLarge

            Text {
                text: panelRoot.page.actionFeedbackError ? "策略操作失败" : "策略操作结果"
                font.pixelSize: panelRoot.page.fontSizeLarge
                font.weight: Font.DemiBold
                color: panelRoot.page.textPrimary
            }

            Text {
                text: panelRoot.page.actionFeedbackMessage
                color: panelRoot.page.actionFeedbackError ? "#FCA5A5" : panelRoot.page.textSecondary
                font.pixelSize: panelRoot.page.fontSizeNormal
                wrapMode: Text.WordWrap
                Layout.fillWidth: true
            }

            RowLayout {
                Layout.fillWidth: true

                Item { Layout.fillWidth: true }

                Button {
                    text: "知道了"
                    onClicked: actionFeedbackDialog.close()
                }
            }
        }
    }

    Dialog {
        id: deleteConfirmDialog
        anchors.centerIn: parent
        modal: true
        width: 420
        property string strategyId: ""
        property string strategyName: ""

        background: Rectangle {
            radius: panelRoot.page.borderRadiusMedium
            color: panelRoot.page.secondaryBg
            border.color: panelRoot.page.borderColor
            border.width: 1
        }

        contentItem: ColumnLayout {
            spacing: panelRoot.page.spacingLarge

            Text {
                text: "删除策略"
                font.pixelSize: panelRoot.page.fontSizeLarge
                font.weight: Font.DemiBold
                color: panelRoot.page.textPrimary
            }

            Text {
                text: "确认删除策略“" + deleteConfirmDialog.strategyName + "”？此操作不可撤销。"
                color: panelRoot.page.textSecondary
                font.pixelSize: panelRoot.page.fontSizeNormal
                wrapMode: Text.WordWrap
                Layout.fillWidth: true
            }

            RowLayout {
                Layout.fillWidth: true

                Item { Layout.fillWidth: true }

                Button {
                    text: "取消"
                    onClicked: deleteConfirmDialog.close()
                }

                Button {
                    text: "确认删除"
                    enabled: !panelRoot.page.deleteInProgress
                    onClicked: {
                        if (panelRoot.page.deleteInProgress) {
                            return
                        }

                        panelRoot.page.deleteInProgress = true
                        if (panelRoot.page.strategyService && deleteConfirmDialog.strategyId) {
                            var deletedStrategyId = deleteConfirmDialog.strategyId
                            var ok = panelRoot.page.strategyService.remove(deletedStrategyId)
                            if (ok) {
                                if (panelRoot.page.selectedStrategyIndex >= 0 && panelRoot.page.strategyViewModel && panelRoot.page.selectedStrategyIndex >= panelRoot.page.strategyViewModel.count - 1) {
                                    panelRoot.page.selectedStrategyIndex = Math.max(0, panelRoot.page.strategyViewModel.count - 2)
                                }
                                console.log("策略删除成功:", deletedStrategyId)
                            } else {
                                console.error("策略删除失败:", deletedStrategyId)
                            }
                        }
                        panelRoot.page.deleteInProgress = false
                        deleteConfirmDialog.close()
                    }
                }
            }
        }
    }
    
    // 遮罩层
    Rectangle {
        anchors.fill: parent
        color: "#00000060"
        visible: panelRoot.page.showFilter || panelRoot.page.showSorter || createDialog.isOpen || deleteConfirmDialog.visible || actionFeedbackDialog.visible
        
        MouseArea {
            anchors.fill: parent
            onClicked: {
                panelRoot.page.showFilter = false;
                panelRoot.page.showSorter = false;
                createDialog.closeDialog();
                if (actionFeedbackDialog.visible) {
                    actionFeedbackDialog.close()
                }
                if (deleteConfirmDialog.visible) {
                    deleteConfirmDialog.close()
                }
            }
        }
    }
}
