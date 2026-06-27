import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import "../../utils/TradingConstants.js" as Const

// ── OrderActionBar — 交易按钮容器 ──
// 使用 default property 委托模式：调用方声明式注入按钮行
// 不包含任何业务逻辑或模式判断

Rectangle {
    id: root

    property bool compactMode: false
    readonly property int cActionHeight: compactMode ? 28 : 38
    readonly property int cActionRadius: compactMode ? 12 : 16

    implicitHeight: actionContent.implicitHeight + (compactMode ? 20 : 28)
    radius: compactMode ? 14 : 18
    color: Const.tradingOrderItemBg
    border.color: Const.tradingTabInactiveBorder
    border.width: 1

    default property alias content: actionContent.data

    ColumnLayout {
        id: actionContent
        anchors.fill: parent
        anchors.margins: compactMode ? 10 : 14
        spacing: compactMode ? 6 : 10
    }
}
