// components/StrategySorter.qml
import QtQuick 2.15
import QtQuick.Layouts 1.15

Rectangle {
    id: strategySorter
    implicitWidth: 280
    implicitHeight: 320
    radius: 16  // borderRadiusXLarge
    color: "#1E293B"  // secondaryBg
    border.color: "#475569"  // borderColor
    
    // 属性
    property int selectedSortIndex: 0
    
    // 信号
    signal sortApplied(string sortType)
    signal sortClosed()
    
    // 颜色常量
    readonly property color textPrimary: "#F1F5F9"
    readonly property color textSecondary: "#94A3B8"
    readonly property color textTertiary: "#64748B"
    readonly property color secondaryBg: "#1E293B"
    readonly property color tertiaryBg: "#334155"
    readonly property color accentBlue: "#3B82F6"
    readonly property color borderColor: "#475569"
    
    readonly property int fontSizeNormal: 14
    readonly property int fontSizeLarge: 18
    
    readonly property real spacingMedium: 8
    readonly property real spacingLarge: 16
    readonly property real spacingXLarge: 20
    
    readonly property real borderRadiusMedium: 8
    readonly property real borderRadiusLarge: 12
    readonly property real borderRadiusXLarge: 16
    
    ColumnLayout {
        anchors.fill: parent
        spacing: 0
        
        // 标题栏
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 60
            radius: borderRadiusXLarge
            color: tertiaryBg
            
            Text {
                anchors.centerIn: parent
                text: "排序方式"
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
                    onClicked: sortClosed()
                }
            }
        }
        
        // 排序选项
        ColumnLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: spacingMedium
           // padding: spacingLarge
            
            // 排序选项列表
            Repeater {
                model: [
                    { text: "按收益率降序", value: "returns_desc" },
                    { text: "按收益率升序", value: "returns_asc" },
                    { text: "按夏普比率", value: "sharpe_desc" },
                    { text: "按胜率", value: "winrate_desc" },
                    { text: "按创建时间", value: "created_desc" },
                    { text: "按回撤幅度", value: "drawdown_asc" }
                ]
                
                delegate: Rectangle {
                    Layout.fillWidth: true
                    height: 48
                    radius: borderRadiusMedium
                    color: selectedSortIndex === index ? 
                           Qt.rgba(59/255, 130/255, 246/255, 0.2) : "transparent"
                    border.color: selectedSortIndex === index ? accentBlue : "transparent"
                    border.width: selectedSortIndex === index ? 2 : 0
                    
                    RowLayout {
                        anchors.fill: parent
                        anchors.margins: spacingLarge
                        
                        Text {
                            text: modelData.text
                            font.pixelSize: fontSizeNormal
                            color: selectedSortIndex === index ? accentBlue : textSecondary
                        }
                        
                        Item { Layout.fillWidth: true }
                        
                        // 选中标记
                        Rectangle {
                            width: 20
                            height: 20
                            radius: 10
                            color: selectedSortIndex === index ? accentBlue : "transparent"
                            border.color: selectedSortIndex === index ? accentBlue : textTertiary
                            border.width: 2
                            
                            Text {
                                anchors.centerIn: parent
                                text: "✓"
                                font.pixelSize: 12
                                color: "white"
                                visible: selectedSortIndex === index
                            }
                        }
                    }
                    
                    MouseArea {
                        anchors.fill: parent
                        cursorShape: Qt.PointingHandCursor
                        onClicked: selectSort(index)
                    }
                }
            }
            
            Item { Layout.fillHeight: true }
            
            // 应用按钮
            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: 44
                radius: borderRadiusMedium
                
                gradient: Gradient {
                    GradientStop { position: 0.0; color: accentBlue }
                    GradientStop { position: 1.0; color: "#1D4ED8" }
                }
                
                Text {
                    anchors.centerIn: parent
                    text: "应用排序"
                    font.pixelSize: fontSizeNormal
                    font.weight: Font.Medium
                    color: "white"
                }
                
                MouseArea {
                    anchors.fill: parent
                    cursorShape: Qt.PointingHandCursor
                    onClicked: applySort()
                }
            }
        }
    }
    
    // 工具函数
    function selectSort(index) {
        selectedSortIndex = index;
    }
    
    function applySort() {
        var sortTypes = [
            "returns_desc", "returns_asc", "sharpe_desc", 
            "winrate_desc", "created_desc", "drawdown_asc"
        ];
        var sortType = sortTypes[selectedSortIndex];
        sortApplied(sortType);
    }
}