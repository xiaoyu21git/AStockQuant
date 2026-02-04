// components/BreadcrumbItem.qml
import QtQuick 2.15
import ConsoleUi 1.0 as Constants

Rectangle {
    id: breadcrumbItem
    implicitHeight: 24
    color: "transparent"
    
    property string icon: ""
    property string text: ""
    property bool active: false
    
    Row {
        spacing: 4
        anchors.verticalCenter: parent.verticalCenter
        
        Text {
            visible: icon !== ""
            text: icon
            font.family: fontAwesome.name
            color: active ? Constants.textPrimary : Constants.textSecondary
            font.pixelSize: 12
        }
        
        Text {
            text: breadcrumbItem.text
            font.pixelSize: Constants.fontSizeMedium
            color: active ? Constants.textPrimary : Constants.textSecondary
            font.weight: active ? Font.Medium : Font.Normal
        }
    }
}