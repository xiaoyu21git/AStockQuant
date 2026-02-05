// MenuTitle.qml
import QtQuick 2.15

Text {
    // 属性
    property alias title: titleText.text
    property int topPaddingValue: 20
    
    id: titleText
    color: "#64748b"
    font.pixelSize: 12
    font.bold: true
    leftPadding: 20
    topPadding: topPaddingValue
    bottomPadding: 8
}