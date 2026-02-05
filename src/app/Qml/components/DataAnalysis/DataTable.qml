// DataTable.qml - 数据表格组件
import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15

Rectangle {
    id: dataTable
    width: parent.width
    height: 400
    radius: 10
    color: Theme.darkCard
    border.color: Theme.darkBorder
    border.width: 1
    
    property string title: "历史数据统计"
    property string subtitle: "数据更新至: 2023-12-31"
    property var tableData: []
    
    // 标题区域
    Row {
        id: tableHeader
        width: parent.width - 48
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
            width: 200
            height: 36
            radius: 6
            color: Qt.rgba(26/255, 35/255, 126/255, 0.3)
            
            Row {
                anchors.centerIn: parent
                spacing: 10
                
                Image {
                    source: "qrc:/icons/chart-line.svg"
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
            width: tableView.width
            height: 50
            color: index % 2 === 0 ? "transparent" : Qt.rgba(57/255, 73/255, 171/255, 0.1)
            
            MouseArea {
                anchors.fill: parent
                hoverEnabled: true
                onEntered: parent.color = Qt.rgba(57/255, 73/255, 171/255, 0.2)
                onExited: parent.color = index % 2 === 0 ? "transparent" : Qt.rgba(57/255, 73/255, 171/255, 0.1)
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
                            source: "qrc:/icons/arrow-up.svg"
                            width: 16
                            height: 16
                           // color: Theme.successColor
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
}