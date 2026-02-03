import QtQuick 2.15
import QtQuick.Controls 2.15

Snackbar {
    id: globalSnackbar
    anchors.horizontalCenter: parent.horizontalCenter
    anchors.bottom: parent.bottom
    width: parent.width * 0.5
    background: Rectangle {
        color: globalSnackbar.type === "error" ? "#d32f2f" : (globalSnackbar.type === "success" ? "#388e3c" : "#323232")
        radius: 8
    }
    contentItem: Text {
        text: globalSnackbar.text
        color: "#fff"
        font.pixelSize: 18
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
        wrapMode: Text.WordWrap
    }
    property string text: ""
    property string type: "info" // info, success, error
    function show(msg, msgType) {
        globalSnackbar.text = msg
        globalSnackbar.type = msgType || "info"
        globalSnackbar.open()
    }
}
