// DataTable.qml - 数据表格组件（已添加行点击事件）
import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import ConsoleUi 1.0 as Theme

Rectangle {
    id: dataTable
    width: parent.width
    height: 400
    radius: 10
    color: "#121c44"//Theme.darkCard
    border.color: Theme.darkBorder
    border.width: 1
    
    property string title: "历史数据统计"
    property string subtitle: "数据更新至: 2023-12-31"
    property var tableData: []
    
    // 行点击信号
    signal rowClicked(var rowData)
    
    // 标题区域
    Row {
        id: tableHeader
        width: parent.width - 60
        anchors.top: parent.top
        anchors.topMargin: 24
        anchors.horizontalCenter: parent.horizontalCenter
        spacing: 20
        
        Text {
            text: dataTable.title
            font.pixelSize: 28
            font.bold: true
            color: Theme.darkText
            anchors.verticalCenter: parent.verticalCenter
        }
        
        // 副标题
        Rectangle {
            width: 180
            height: 36
            radius: 6
            color: Qt.rgba(26/255, 35/255, 126/255, 0.3)
            
            Row {
                anchors.centerIn: parent
                spacing: 10
                
                Image {
                    source: "qrc:/resources/icons/chart-line.svg"
                    width: 20
                    height: 20
                    anchors.verticalCenter: parent.verticalCenter
                    //color: Theme.darkText
                }
                
                Text {
                    text: dataTable.subtitle
                    font.pixelSize: 14
                    color: Theme.darkText
                    anchors.verticalCenter: parent.verticalCenter
                }
            }
        }
    }
    
    // 表格
    ListView {
        id: tableView
        width: parent.width - 48
        height: parent.height - 80
        anchors.top: tableHeader.bottom
        anchors.topMargin: 20
        anchors.horizontalCenter: parent.horizontalCenter
        clip: true
        
        // 表头
        header: Row {
            width: tableView.width
            height: 50
            
            Repeater {
                model: ["数据类别", "覆盖标的", "时间范围", "数据频率", "完整度", "近期更新"]
                
                Rectangle {
                    width: tableView.width / 6
                    height: 50
                    color: Qt.rgba(26/255, 35/255, 126/255, 0.3)
                    
                    Text {
                        text: modelData
                        font.pixelSize: 14
                        font.bold: true
                        color: Theme.darkText
                        anchors.centerIn: parent
                    }
                    
                    Rectangle {
                        width: 2
                        height: parent.height
                        color: Theme.darkBorder
                        anchors.right: parent.right
                    }
                }
            }
        }
        
        // 表格内容
        model: dataTable.tableData
        
        delegate: Rectangle {
            id: rowDelegate
            width: tableView.width
            height: 50
            color: index % 2 === 0 ? "transparent" : Qt.rgba(57/255, 73/255, 171/255, 0.1)
            
            // 当前行的数据
            property var rowData: modelData
            
            MouseArea {
                anchors.fill: parent
                hoverEnabled: true
                cursorShape: Qt.PointingHandCursor  // 添加手型光标
                
                onEntered: {
                    parent.color = Qt.rgba(57/255, 73/255, 171/255, 0.2)
                    rowDelegate.scale = 1.01  // 悬停放大效果
                }
                
                onExited: {
                    parent.color = index % 2 === 0 ? "transparent" : Qt.rgba(57/255, 73/255, 171/255, 0.1)
                    rowDelegate.scale = 1.0
                }
                
                // 添加点击事件
                onClicked: {
                    console.log("表格行点击: " + rowData.category)
                    dataTable.rowClicked(rowData)  // 触发行点击信号
                    
                    // 添加点击反馈效果
                    rowDelegate.color = Qt.rgba(0, 188/255, 212/255, 0.3)  // 点击后变色
                    clickTimer.start()
                }
                
                // 添加按下效果
                onPressed: rowDelegate.opacity = 0.8
                onReleased: rowDelegate.opacity = 1.0
                
                // 添加右键菜单支持（可选）
                onPressAndHold: {
                    console.log("长按表格行: " + rowData.category)
                    showRowContextMenu(rowData)
                }
            }
            
            // 点击后恢复颜色的定时器
            Timer {
                id: clickTimer
                interval: 300
                onTriggered: {
                    rowDelegate.color = index % 2 === 0 ? "transparent" : Qt.rgba(57/255, 73/255, 171/255, 0.1)
                }
            }
            
            Row {
                anchors.fill: parent
                
                // 数据类别
                Rectangle {
                    width: tableView.width / 6
                    height: parent.height
                    color: "transparent"
                    
                    Row {
                        anchors.centerIn: parent
                        spacing: 10
                        
                        Rectangle {
                            width: 10
                            height: 10
                            radius: 5
                            color: modelData.status === "active" ? 
                                   Theme.successColor : Theme.warningColor
                            anchors.verticalCenter: parent.verticalCenter
                        }
                        
                        Text {
                            text: modelData.category
                            font.pixelSize: 14
                            color: Theme.darkText
                            anchors.verticalCenter: parent.verticalCenter
                        }
                    }
                }
                
                // 覆盖标的
                Rectangle {
                    width: tableView.width / 6
                    height: parent.height
                    color: "transparent"
                    
                    Text {
                        text: modelData.coverage
                        font.pixelSize: 14
                        color: Theme.darkText
                        anchors.centerIn: parent
                    }
                }
                
                // 时间范围
                Rectangle {
                    width: tableView.width / 6
                    height: parent.height
                    color: "transparent"
                    
                    Text {
                        text: modelData.timeRange
                        font.pixelSize: 14
                        color: Theme.darkText
                        anchors.centerIn: parent
                    }
                }
                
                // 数据频率
                Rectangle {
                    width: tableView.width / 6
                    height: parent.height
                    color: "transparent"
                    
                    Text {
                        text: modelData.frequency
                        font.pixelSize: 14
                        color: Theme.darkText
                        anchors.centerIn: parent
                    }
                }
                
                // 完整度
                Rectangle {
                    width: tableView.width / 6
                    height: parent.height
                    color: "transparent"
                    
                    Text {
                        text: modelData.completeness
                        font.pixelSize: 14
                        color: Theme.darkText
                        anchors.centerIn: parent
                    }
                }
                
                // 近期更新
                Rectangle {
                    width: tableView.width / 6
                    height: parent.height
                    color: "transparent"
                    
                    Row {
                        anchors.centerIn: parent
                        spacing: 5
                        
                        Image {
                            source: "qrc:/resources/icons/arrow-up.svg"
                            width: 16
                            height: 16
                            visible: modelData.recentUpdate === "updated"
                            anchors.verticalCenter: parent.verticalCenter
                        }
                        
                        Text {
                            text: modelData.recentUpdate === "updated" ? "今日已更新" : "部分延迟"
                            font.pixelSize: 14
                            color: modelData.recentUpdate === "updated" ? 
                                   Theme.successColor : Theme.warningColor
                            anchors.verticalCenter: parent.verticalCenter
                        }
                    }
                }
            }
            
            Rectangle {
                width: parent.width
                height: 1
                color: Theme.darkBorder
                anchors.bottom: parent.bottom
            }
        }
    }
    
    // 显示行上下文菜单（可选功能）
    function showRowContextMenu(rowData) {
        console.log("显示上下文菜单: " + rowData.category)
        // 这里可以显示一个上下文菜单
        // 例如：查看详情、刷新数据、导出数据等选项
    }
}