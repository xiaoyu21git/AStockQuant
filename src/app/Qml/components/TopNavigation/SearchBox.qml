// Qml/components/TopNavigation/SearchBox.qml
import QtQuick 2.15
import QtQuick.Controls 2.15
import ConsoleUi 1.0
import QtQuick.Layouts 1.15  // 必须导入 Layouts
Rectangle {
    id: root
    height: 36
    radius: 18
    color: Constants.secondaryBg
    border.width: 1
    border.color: Constants.borderColor
    
    property string placeholderText: "搜索..."
    property string text: ""
    signal searchTriggered(string text)
    
    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: 16
        anchors.rightMargin: 16
        spacing: 8
        
        // 搜索图标
        Text {
            text: "\uf002" // fa-search
            color: Constants.secondary
            font.pixelSize: 14
            // font.family: Constants.fontAwesome // 暂时注释，如果字体未加载
            Layout.alignment: Qt.AlignVCenter
        }
        
        // 输入框
        TextInput {
            id: inputField
            Layout.fillWidth: true
            Layout.alignment: Qt.AlignVCenter
            color: Constants.secondary
            font.pixelSize: Constants.fontSizeBase
            verticalAlignment: Text.AlignVCenter
            selectByMouse: true
            
            // 占位符文本
            Text {
                text: root.placeholderText
                color: Constants.secondary
                font: inputField.font
                visible: !inputField.text && !inputField.activeFocus
                anchors.verticalCenter: parent.verticalCenter
            }
            
            // 回车键搜索
            Keys.onReturnPressed: {
                searchTriggered(inputField.text);
            }
            Keys.onEnterPressed: {
                searchTriggered(inputField.text);
            }
        }
        
        // 清空按钮
        Text {
            visible: inputField.text.length > 0
            text: "\uf00d" // fa-times
            color: Constants.secondary
            font.pixelSize: 14
            // font.family: Constants.fontAwesome
            Layout.alignment: Qt.AlignVCenter
            
            MouseArea {
                anchors.fill: parent
                cursorShape: Qt.PointingHandCursor
                onClicked: {
                    inputField.text = "";
                    inputField.focus = true;
                }
            }
        }
    }
}