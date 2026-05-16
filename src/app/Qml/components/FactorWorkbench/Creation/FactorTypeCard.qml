// FactorTypeCard.qml
// 因子类型卡片组件

import QtQuick 2.15
import QtQuick.Layouts 1.15

Item {
    id: factorTypeCardRoot
    
    property int typeId: -1
    property string displayName: ""
    property string description: ""
    property string icon: ""
    property color color: "#94A3B8"
    property bool isSelected: false
    signal clicked()
    
    Layout.fillWidth: true
    Layout.minimumWidth: 100
    Layout.preferredHeight: 110
    
    Rectangle {
        anchors.fill: parent
        radius: 8
        color: factorTypeCardRoot.isSelected ? factorTypeCardRoot.color : "#1E293B"
        border.width: factorTypeCardRoot.isSelected ? 2 : 1
        border.color: factorTypeCardRoot.isSelected ? factorTypeCardRoot.color : "#334155"
        
        ColumnLayout {
            anchors.fill: parent
            anchors.margins: 12
            spacing: 8
            
            Text {
                text: factorTypeCardRoot.icon
                font.pixelSize: 24
                horizontalAlignment: Text.AlignHCenter
                Layout.fillWidth: true
            }
            
            Text {
                text: factorTypeCardRoot.displayName
                font.pixelSize: 14
                font.weight: Font.Medium
                color: factorTypeCardRoot.isSelected ? "white" : "#F1F5F9"
                horizontalAlignment: Text.AlignHCenter
                Layout.fillWidth: true
            }
            
            Text {
                text: factorTypeCardRoot.description
                font.pixelSize: 10
                color: factorTypeCardRoot.isSelected ? "#E2E8F0" : "#94A3B8"
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.WordWrap
                maximumLineCount: 2
                elide: Text.ElideRight
                Layout.fillWidth: true
                Layout.fillHeight: true
            }
        }
        
        MouseArea {
            anchors.fill: parent
            cursorShape: Qt.PointingHandCursor
            onClicked: factorTypeCardRoot.clicked()
        }
    }
}