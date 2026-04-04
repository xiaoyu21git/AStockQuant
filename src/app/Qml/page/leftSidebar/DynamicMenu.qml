// DynamicMenu.qml - 修复显示问题
import QtQuick 2.15
import QtQuick.Layouts 1.15
import QtQuick.Controls 2.15



Item {
    id: dynamicMenu
    Layout.fillWidth: true
    Layout.fillHeight: true
    
    // 属性
    property var menuModel: null
    
    // 信号
    signal menuItemClicked(string menuCode, string menuText)
    
    ScrollView {
        anchors.fill: parent
        clip: true
        
        Column {
            width: parent.width
            spacing: 0
            
            // === 顶级标题 ===
            Text {
                text: menuModel ? menuModel.mainTitle : "交易"
                color: "#64748b"
                font.pixelSize: 12
                font.bold: true
                leftPadding: 20
                topPadding: 20
                bottomPadding: 16
                width: parent.width
            }
            
            // === 一级菜单区域 ===
            Repeater {
                id: primaryMenuRepeater
                model: menuModel ? menuModel.primaryMenus.length : 0
                
                Rectangle {
                    readonly property var menuItemData: menuModel ? (menuModel.primaryMenus[index] || ({})) : ({})
                    width: parent.width
                    height: 44
                    color: "transparent"
                    
                    // 可点击区域 - 覆盖整个矩形
                    MouseArea {
                        id: primaryMouseArea
                        anchors.fill: parent  // 覆盖整个菜单项矩形
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: {
                            console.log("点击一级菜单:", menuItemData.title, menuItemData.code)
                            menuModel.selectPrimaryMenu(menuItemData.code)
                        }
                    }
                    
                    // 背景矩形（在 MouseArea 下面）
                    Rectangle {
                        anchors.fill: parent
                        anchors.margins: 4
                        radius: 8
                        color: {
                            if (menuItemData.code === menuModel.currentPrimaryMenu) return "#1a2235"
                            return primaryMouseArea.containsMouse ? "#1a2235" : "transparent"
                        }
                        border.color: menuItemData.code === menuModel.currentPrimaryMenu ? "#3b82f6" : "transparent"
                        border.width: menuItemData.code === menuModel.currentPrimaryMenu ? 1 : 0
                    }
                    
                    // 内容（在背景矩形上面）
                    Row {
                        anchors.fill: parent  // 对齐到整个菜单项
                        anchors.leftMargin: 20
                        anchors.rightMargin: 20
                        spacing: 12
                        
                        // 图标
                        Item {
                            width: 20
                            height: 20
                            anchors.verticalCenter: parent.verticalCenter
                            Text { 
                                anchors.centerIn: parent
                                text: menuItemData.icon || "📈"
                                font.pixelSize: 16
                                color: menuItemData.code === menuModel.currentPrimaryMenu ? "#3b82f6" : "#94a3b8"
                            }
                        }
                        
                        // 标题
                        Text {
                            text: menuItemData.title || ""
                            color: menuItemData.code === menuModel.currentPrimaryMenu ? "#f1f5f9" : "#94a3b8"
                            font.pixelSize: 14
                            font.bold: menuItemData.code === menuModel.currentPrimaryMenu
                            anchors.verticalCenter: parent.verticalCenter
                        }
                        
                        // 占位空间
                        Item { 
                            width: parent.width - x - (badgeItem.visible ? badgeItem.width + 20 : 20)
                            height: 1
                        }
                        
                        // 角标
                        Item {
                            id: badgeItem
                            visible: menuItemData.badge && menuItemData.badge !== ""
                            width: visible ? Math.max(24, badgeText.width + 12) : 0
                            height: 20
                            anchors.verticalCenter: parent.verticalCenter
                            
                            Rectangle {
                                anchors.fill: parent
                                radius: 10
                                color: menuItemData.code === menuModel.currentPrimaryMenu ? "#3b82f620" : "#10b98120"
                                border.color: menuItemData.code === menuModel.currentPrimaryMenu ? "#3b82f6" : "#10b981"
                                border.width: 1
                                
                                Text {
                                    id: badgeText
                                    anchors.centerIn: parent
                                    text: menuItemData.badge || ""
                                    color: menuItemData.code === menuModel.currentPrimaryMenu ? "#3b82f6" : "#10b981"
                                    font.pixelSize: 11
                                }
                            }
                        }
                    }
                }
            }
            
            // === 二级菜单标题 ===
            Text {
                text: menuModel ? menuModel.getCurrentSecondaryTitle() : "菜单"
                color: "#64748b"
                font.pixelSize: 12
                font.bold: true
                leftPadding: 20
                topPadding: 24
                bottomPadding: 8
                width: parent.width
            }
            
            // === 二级菜单区域 ===
            Repeater {
                id: secondaryMenuRepeater
                model: menuModel ? menuModel.getCurrentSecondaryMenus().length : 0
                
                Rectangle {
                    readonly property var secondaryItemData: menuModel ? (menuModel.getCurrentSecondaryMenus()[index] || ({})) : ({})
                    width: parent.width
                    height: 44
                    color: "transparent"
                    
                    // 可点击区域 - 覆盖整个矩形
                    MouseArea {
                        id: secondaryMouseArea
                        anchors.fill: parent  // 覆盖整个菜单项矩形
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: {
                            console.log("点击二级菜单:", secondaryItemData.title, secondaryItemData.code)
                            menuModel.selectSecondaryMenu(secondaryItemData.code)
                            dynamicMenu.menuItemClicked(secondaryItemData.code, secondaryItemData.title)
                        }
                    }
                    
                    // 背景矩形（在 MouseArea 下面）
                    Rectangle {
                        anchors.fill: parent
                        anchors.margins: 4
                        radius: 8
                        color: {
                            if (secondaryItemData.code === menuModel.currentSecondaryMenu) return "#1a2235"
                            return secondaryMouseArea.containsMouse ? "#1a2235" : "transparent"
                        }
                        border.color: secondaryItemData.code === menuModel.currentSecondaryMenu ? "#3b82f6" : "transparent"
                        border.width: secondaryItemData.code === menuModel.currentSecondaryMenu ? 1 : 0
                    }
                    
                    // 内容（在背景矩形上面）
                    Row {
                        anchors.fill: parent  // 对齐到整个菜单项
                        anchors.leftMargin: 20
                        anchors.rightMargin: 20
                        spacing: 12
                        
                        // 图标
                        Item {
                            width: 20
                            height: 20
                            anchors.verticalCenter: parent.verticalCenter
                            Text { 
                                anchors.centerIn: parent
                                text: secondaryItemData.icon || "💰"
                                font.pixelSize: 16
                                color: secondaryItemData.code === menuModel.currentSecondaryMenu ? "#3b82f6" : "#94a3b8"
                            }
                        }
                        
                        // 标题
                        Text {
                            text: secondaryItemData.title || ""
                            color: secondaryItemData.code === menuModel.currentSecondaryMenu ? "#f1f5f9" : "#94a3b8"
                            font.pixelSize: 14
                            font.bold: secondaryItemData.code === menuModel.currentSecondaryMenu
                            anchors.verticalCenter: parent.verticalCenter
                        }
                        
                        // 占位空间
                        Item { 
                            width: parent.width - x - (badgeItem2.visible ? badgeItem2.width + 20 : 20)
                            height: 1
                        }
                        
                        // 角标
                        Item {
                            id: badgeItem2
                            visible: secondaryItemData.badge && secondaryItemData.badge !== ""
                            width: visible ? Math.max(24, badgeText2.width + 12) : 0
                            height: 20
                            anchors.verticalCenter: parent.verticalCenter
                            
                            Rectangle {
                                anchors.fill: parent
                                radius: 10
                                color: getBadgeColor(secondaryItemData.code === menuModel.currentSecondaryMenu, secondaryItemData.badge)
                                border.color: getBadgeBorderColor(secondaryItemData.code === menuModel.currentSecondaryMenu, secondaryItemData.badge)
                                border.width: 1
                                
                                Text {
                                    id: badgeText2
                                    anchors.centerIn: parent
                                    text: secondaryItemData.badge || ""
                                    color: getBadgeTextColor(secondaryItemData.code === menuModel.currentSecondaryMenu, secondaryItemData.badge)
                                    font.pixelSize: 11
                                }
                            }
                        }
                    }
                }
            }
            
            Item { 
                height: 1
                width: parent.width
            }
        }
    }
    
    // 角标颜色函数
    function getBadgeColor(isActive, badge) {
        if (isActive) return "#3b82f620"
        if (badge === "!" || badge === "警告") return "#ef444420"
        if (badge === "新" || badge === "New") return "#10b98120"
        if (badge === "Beta") return "#f59e0b20"
        return "#3b82f620"
    }
    
    function getBadgeBorderColor(isActive, badge) {
        if (isActive) return "#3b82f6"
        if (badge === "!" || badge === "警告") return "#ef4444"
        if (badge === "新" || badge === "New") return "#10b981"
        if (badge === "Beta") return "#f59e0b"
        return "#3b82f6"
    }
    
    function getBadgeTextColor(isActive, badge) {
        if (isActive) return "#3b82f6"
        if (badge === "!" || badge === "警告") return "#ef4444"
        if (badge === "新" || badge === "New") return "#10b981"
        if (badge === "Beta") return "#f59e0b"
        return "#3b82f6"
    }
}