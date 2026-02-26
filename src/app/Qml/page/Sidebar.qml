// Sidebar.qml - 更新版
import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import ConsoleUi 1.0

Item {
    id: sidebar
    width: 280
    
    // === 菜单模型 ===
    property alias menuModel: internalMenuModel
    
    MenuModel {
        id: internalMenuModel
        // MenuModel.qml已经定义了完整的功能
    }
    
    // === 对外信号 ===
    signal menuClicked(string menuCode, string menuTitle)
    signal depositClicked()
    signal withdrawClicked()
    signal accountRefreshClicked()
    signal userProfileClicked()
    
    Rectangle {
        anchors.fill: parent
        color: "#121828"
        border.color: "#2d3748"
        border.width: 1
        
        ColumnLayout {
            anchors.fill: parent
            spacing: 0
            
            // === 动态菜单组件 ===
            DynamicMenu {
                Layout.fillWidth: true
                Layout.fillHeight: true
                menuModel: internalMenuModel
                onMenuItemClicked: function(menuCode, menuText) {
                    sidebar.menuClicked(menuCode, menuText)
                }
            }
            
            // === 账户面板 ===
            FundOperationPanel {
                accountValue: 1234567.89
                accountChange: 12345.67
                accountChangePercent: 1.23
                
                onDepositClicked: sidebar.depositClicked()
                onWithdrawClicked: sidebar.withdrawClicked()
                onRefreshClicked: sidebar.accountRefreshClicked()
            }
            
            // === 用户信息面板 ===
            UserInfoPanel {
                userName: "量化交易员"
                userStatus: "专业版 · 在线"
                userInitials: "QT"
                
                onProfileClicked: sidebar.userProfileClicked()
            }
        }
    }
    
    // === 公开API ===
    
    // 设置当前菜单
    function setCurrentMenu(menuCode) {
        internalMenuModel.setCurrentMenu(menuCode)
    }
    // Sidebar.qml - 添加这个方法
    function getCurrentMenu() {
        if (menuModel && menuModel.getCurrentMenu) {
            return menuModel.getCurrentMenu()
        }
        return { primary: "", secondary: "" }
    }
    // 更新账户数据
    function updateAccountData(value, change, changePercent) {
        // 这里需要找到FundOperationPanel实例并更新
        for (var i = 0; i < children.length; i++) {
            var child = children[i]
            if (child.objectName === "fundPanel") {
                child.accountValue = value
                child.accountChange = change
                child.accountChangePercent = changePercent
                break
            }
        }
    }
    
    // 更新用户信息
    function updateUserInfo(name, status, initials) {
        // 这里需要找到UserInfoPanel实例并更新
        for (var i = 0; i < children.length; i++) {
            var child = children[i]
            if (child.objectName === "userPanel") {
                child.userName = name
                child.userStatus = status
                child.userInitials = initials
                break
            }
        }
    }
    
    // 初始化
    Component.onCompleted: {
    }
}