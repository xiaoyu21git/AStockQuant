import QtQuick 2.15
import QtQuick.Controls 2.15

Item {
    id: themeSwitcher
    width: 120
    height: 40

    property bool darkMode: false

    Row {
        spacing: 8
        anchors.centerIn: parent
        Text {
            text: darkMode ? "暗色" : "亮色"
            font.pixelSize: 16
            color: Qt.application.palette.text
        }
        Switch {
            checked: darkMode
            onCheckedChanged: {
                themeSwitcher.darkMode = checked
                Qt.application.theme = checked ? "dark" : "light"
            }
        }
    }

    function setTheme(mode) {
        darkMode = (mode === "dark")
        Qt.application.theme = mode
    }
}
