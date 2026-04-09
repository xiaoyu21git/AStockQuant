// components/StrategyFilter.qml
import QtQuick 2.15
import QtQuick.Layouts 1.15
import QtQuick.Controls 2.15
Rectangle {
    id: strategyFilter
    implicitWidth: 320
    implicitHeight: 420
    radius: 16  // borderRadiusXLarge
    color: "#1E293B"  // secondaryBg
    border.color: "#475569"  // borderColor
    
    // 属性
    property bool filterRunning: true
    property bool filterPaused: true
    property bool filterStopped: true
    property var filterCategories: []
    property var filterAssetTypes: []
    
    // 信号
    signal filterApplied(var filterData)
    signal filterReset()
    signal filterClosed()
    
    // 颜色常量
    readonly property color textPrimary: "#F1F5F9"
    readonly property color textSecondary: "#94A3B8"
    readonly property color textTertiary: "#64748B"
    readonly property color secondaryBg: "#1E293B"
    readonly property color tertiaryBg: "#334155"
    readonly property color accentBlue: "#3B82F6"
    readonly property color borderColor: "#475569"
    readonly property color borderLight: "#64748B"
    
    readonly property int fontSizeSmall: 12
    readonly property int fontSizeNormal: 14
    readonly property int fontSizeMedium: 16
    readonly property int fontSizeLarge: 18
    
    readonly property real spacingSmall: 4
    readonly property real spacingMedium: 8
    readonly property real spacingLarge: 16
    readonly property real spacingXLarge: 20
    
    readonly property real borderRadiusSmall: 4
    readonly property real borderRadiusMedium: 8
    readonly property real borderRadiusLarge: 12
    readonly property real borderRadiusXLarge: 16
    
    ColumnLayout {
        anchors.fill: parent
        spacing: 0
        
        // 标题栏
        Rectangle {
            Layout.fillWidth: true
            height: 60
            radius: borderRadiusXLarge
            color: tertiaryBg
            
            Text {
                anchors.centerIn: parent
                text: "筛选策略"
                font.pixelSize: fontSizeLarge
                font.weight: Font.DemiBold
                color: textPrimary
            }
            
            // 关闭按钮
            Rectangle {
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                anchors.rightMargin: spacingLarge
                width: 28
                height: 28
                radius: 14
                color: "transparent"
                
                Text {
                    anchors.centerIn: parent
                    text: "×"
                    font.pixelSize: 20
                    color: textTertiary
                }
                
                MouseArea {
                    anchors.fill: parent
                    cursorShape: Qt.PointingHandCursor
                    onClicked: filterClosed()
                }
            }
        }
        
        // 内容区域
        ScrollView {
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            
            ColumnLayout {
                width: parent.width
                spacing: spacingLarge
                //padding: spacingLarge
                
                // 策略状态筛选
                ColumnLayout {
                    spacing: spacingMedium
                    
                    Text {
                        text: "策略状态"
                        font.pixelSize: fontSizeMedium
                        font.weight: Font.DemiBold
                        color: textPrimary
                    }
                    
                    Row {
                        spacing: spacingLarge
                        
                        // 运行中
                        Rectangle {
                            id: runningFilter
                            width: 80
                            height: 36
                            radius: borderRadiusMedium
                            color: filterRunning ? Qt.rgba(59/255, 130/255, 246/255, 0.2) : tertiaryBg
                            border.color: filterRunning ? accentBlue : borderLight
                            border.width: filterRunning ? 2 : 1
                            
                            Text {
                                anchors.centerIn: parent
                                text: "运行中"
                                font.pixelSize: fontSizeNormal
                                color: filterRunning ? accentBlue : textSecondary
                            }
                            
                            MouseArea {
                                anchors.fill: parent
                                cursorShape: Qt.PointingHandCursor
                                onClicked: filterRunning = !filterRunning
                            }
                        }
                        
                        // 已暂停
                        Rectangle {
                            id: pausedFilter
                            width: 80
                            height: 36
                            radius: borderRadiusMedium
                            color: filterPaused ? Qt.rgba(59/255, 130/255, 246/255, 0.2) : tertiaryBg
                            border.color: filterPaused ? accentBlue : borderLight
                            border.width: filterPaused ? 2 : 1
                            
                            Text {
                                anchors.centerIn: parent
                                text: "已暂停"
                                font.pixelSize: fontSizeNormal
                                color: filterPaused ? accentBlue : textSecondary
                            }
                            
                            MouseArea {
                                anchors.fill: parent
                                cursorShape: Qt.PointingHandCursor
                                onClicked: filterPaused = !filterPaused
                            }
                        }
                        
                        // 已停止
                        Rectangle {
                            id: stoppedFilter
                            width: 80
                            height: 36
                            radius: borderRadiusMedium
                            color: filterStopped ? Qt.rgba(59/255, 130/255, 246/255, 0.2) : tertiaryBg
                            border.color: filterStopped ? accentBlue : borderLight
                            border.width: filterStopped ? 2 : 1
                            
                            Text {
                                anchors.centerIn: parent
                                text: "已停止"
                                font.pixelSize: fontSizeNormal
                                color: filterStopped ? accentBlue : textSecondary
                            }
                            
                            MouseArea {
                                anchors.fill: parent
                                cursorShape: Qt.PointingHandCursor
                                onClicked: filterStopped = !filterStopped
                            }
                        }
                    }
                }
                
                // 策略类型筛选
                ColumnLayout {
                    spacing: spacingMedium
                    
                    Text {
                        text: "策略类型"
                        font.pixelSize: fontSizeMedium
                        font.weight: Font.DemiBold
                        color: textPrimary
                    }
                    
                    Flow {
                        spacing: spacingSmall
                        
                        Repeater {
                            model: ["趋势跟踪", "均值回归", "动量策略", "套利策略", "机器学习", "高频交易"]
                            
                            delegate: Rectangle {
                                width: 85
                                height: 32
                                radius: borderRadiusMedium
                                color: filterCategories.includes(modelData) ? 
                                       Qt.rgba(59/255, 130/255, 246/255, 0.2) : tertiaryBg
                                border.color: filterCategories.includes(modelData) ? accentBlue : borderLight
                                border.width: filterCategories.includes(modelData) ? 2 : 1
                                
                                Text {
                                    anchors.centerIn: parent
                                    text: modelData
                                    font.pixelSize: fontSizeSmall
                                    color: filterCategories.includes(modelData) ? accentBlue : textSecondary
                                }
                                
                                MouseArea {
                                    anchors.fill: parent
                                    cursorShape: Qt.PointingHandCursor
                                    onClicked: toggleCategory(modelData)
                                }
                            }
                        }
                    }
                }
                
                // 资产类型筛选
                ColumnLayout {
                    spacing: spacingMedium
                    
                    Text {
                        text: "资产类型"
                        font.pixelSize: fontSizeMedium
                        font.weight: Font.DemiBold
                        color: textPrimary
                    }
                    
                    Flow {
                        spacing: spacingSmall
                        
                        Repeater {
                            model: ["股票", "期货", "加密货币", "外汇", "期权"]
                            
                            delegate: Rectangle {
                                width: 85
                                height: 32
                                radius: borderRadiusMedium
                                color: filterAssetTypes.includes(modelData) ? 
                                       Qt.rgba(59/255, 130/255, 246/255, 0.2) : tertiaryBg
                                border.color: filterAssetTypes.includes(modelData) ? accentBlue : borderLight
                                border.width: filterAssetTypes.includes(modelData) ? 2 : 1
                                
                                Text {
                                    anchors.centerIn: parent
                                    text: modelData
                                    font.pixelSize: fontSizeSmall
                                    color: filterAssetTypes.includes(modelData) ? accentBlue : textSecondary
                                }
                                
                                MouseArea {
                                    anchors.fill: parent
                                    cursorShape: Qt.PointingHandCursor
                                    onClicked: toggleAssetType(modelData)
                                }
                            }
                        }
                    }
                }
                
                Item { Layout.fillHeight: true }
                
                // 操作按钮
                Row {
                    Layout.alignment: Qt.AlignHCenter
                    spacing: spacingLarge
                    
                    // 重置按钮
                    Rectangle {
                        width: 100
                        height: 40
                        radius: borderRadiusMedium
                        color: tertiaryBg
                        border.color: borderLight
                        
                        Text {
                            anchors.centerIn: parent
                            text: "重置筛选"
                            font.pixelSize: fontSizeNormal
                            color: textSecondary
                        }
                        
                        MouseArea {
                            anchors.fill: parent
                            cursorShape: Qt.PointingHandCursor
                            onClicked: resetFilters()
                        }
                    }
                    
                    // 应用按钮
                    Rectangle {
                        width: 100
                        height: 40
                        radius: borderRadiusMedium
                        
                        gradient: Gradient {
                            GradientStop { position: 0.0; color: accentBlue }
                            GradientStop { position: 1.0; color: "#1D4ED8" }
                        }
                        
                        Text {
                            anchors.centerIn: parent
                            text: "应用筛选"
                            font.pixelSize: fontSizeNormal
                            color: "white"
                        }
                        
                        MouseArea {
                            anchors.fill: parent
                            cursorShape: Qt.PointingHandCursor
                            onClicked: applyFilters()
                        }
                    }
                }
            }
        }
    }
    
    // 工具函数
    function toggleCategory(category) {
        var index = filterCategories.indexOf(category);
        if (index === -1) {
            filterCategories.push(category);
        } else {
            filterCategories.splice(index, 1);
        }
        filterCategoriesChanged();
    }
    
    function toggleAssetType(assetType) {
        var index = filterAssetTypes.indexOf(assetType);
        if (index === -1) {
            filterAssetTypes.push(assetType);
        } else {
            filterAssetTypes.splice(index, 1);
        }
        filterAssetTypesChanged();
    }
    
    function resetFilters() {
        filterRunning = true;
        filterPaused = true;
        filterStopped = true;
        filterCategories = [];
        filterAssetTypes = [];
        filterReset();
    }
    
    function applyFilters() {
        var filterData = {
            status: {
                running: filterRunning,
                paused: filterPaused,
                stopped: filterStopped
            },
            categories: filterCategories,
            assetTypes: filterAssetTypes
        };
        filterApplied(filterData);
    }
}