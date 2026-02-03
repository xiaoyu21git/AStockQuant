import QtQuick 2.15
import QtQuick.Controls 2.15

Dialog {
    id: syncDataDialog
    title: "数据未更新"
    modal: true
    standardButtons: Dialog.Ok | Dialog.Cancel
    visible: false
    width: 400
    onAccepted: {
        // 调用C++/Python接口执行数据同步
        if (typeof syncData === 'function') {
            syncData();
        }
    }
    Text {
        anchors.centerIn: parent
        text: "检测到行情数据未更新，是否立即同步？"
        font.pixelSize: 16
    }
}
