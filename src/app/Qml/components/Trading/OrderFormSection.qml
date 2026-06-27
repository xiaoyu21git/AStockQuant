import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import "../../utils/TradingConstants.js" as Const

// ── OrderFormSection — 可复用的下单表单区 ──
// 封装：品种标签 + 代码输入 + 快捷按钮行 + 数量/价格类型行 + 价格±微调行
// 使用 default property 让调用方注入品种特有的额外内容（如期权参数、快捷价格按钮等）

Rectangle {
    id: root

    // ── 输入属性 ──
    property string sectionLabel: ""
    property string codeText: ""
    property string codePlaceholder: ""
    property string quantityText: ""
    property string quantityPlaceholder: ""
    property string priceText: ""
    property string pricePlaceholder: ""
    property int priceTypeIndex: 0              // 0=市价, 1=限价
    property var quickButtonModel: []
    property bool compactMode: false
    property string identitySummary: ""
    property color identityColor: Const.tradingLabelTertiary
    property string priceSummary: ""
    property string amountSummary: ""

    // ── 紧凑模式尺寸 ──
    readonly property int cInputFont: compactMode ? 10 : 12
    readonly property int cInputHeight: compactMode ? 30 : 38
    readonly property int cInputRadius: compactMode ? 10 : 12
    readonly property int cInputHPad: compactMode ? 10 : 12
    readonly property int cInputVPad: compactMode ? 0 : 1
    readonly property int cQuickBtnH: compactMode ? 24 : 30
    readonly property int cQuickBtnFont: compactMode ? 9 : 10
    readonly property int cSectionFont: compactMode ? 10 : 12
    readonly property int cBtnFont: compactMode ? 10 : 13
    readonly property int cMetaFont: compactMode ? 10 : 12

    // ── 信号 ──
    signal codeChanged(string text)
    signal quantityChanged(string text)
    signal priceTypeChanged(int index)
    signal priceChanged(string text)
    signal quickValueSelected(string value)
    signal priceAdjusted(int delta)
    signal shortcutPriceSelected(string targetMode, string shortcut)

    implicitHeight: sectionColumn.implicitHeight + (compactMode ? 16 : 24)
    color: "transparent"

    default property alias extraContent: extraContainer.data

    ColumnLayout {
        id: sectionColumn
        anchors.fill: parent
        anchors.margins: compactMode ? 8 : 12
        spacing: compactMode ? 8 : 12

        // 品种标签
        Text {
            text: root.sectionLabel
            color: Const.tradingLabelSecondary
            font.pixelSize: cSectionFont
        }

        // 代码输入
        TextField {
            id: codeField
            Layout.fillWidth: true
            Layout.preferredHeight: cInputHeight
            text: root.codeText
            placeholderText: root.codePlaceholder
            color: Const.tradingTitleText
            font.pixelSize: cInputFont
            horizontalAlignment: TextInput.AlignHCenter
            verticalAlignment: TextInput.AlignVCenter
            topPadding: cInputVPad
            bottomPadding: cInputVPad
            leftPadding: cInputHPad
            rightPadding: cInputHPad
            onTextChanged: root.codeChanged(text)
            background: Rectangle {
                radius: cInputRadius
                color: Const.tradingInputBg
                border.color: Const.tradingInputBorder
                border.width: 1
            }
        }

        // 品种身份摘要
        Text {
            Layout.fillWidth: true
            text: root.identitySummary
            color: root.identityColor
            font.pixelSize: cMetaFont
            horizontalAlignment: Text.AlignHCenter
            visible: root.identitySummary.length > 0
        }

        // 快捷填单按钮行
        RowLayout {
            Layout.fillWidth: true
            spacing: compactMode ? 6 : 8

            Repeater {
                model: root.quickButtonModel

                Rectangle {
                    Layout.fillWidth: true
                    implicitHeight: cQuickBtnH
                    radius: cInputRadius
                    color: Const.tradingButtonBg
                    border.color: Const.tradingInputActiveBorder
                    border.width: 1

                    Text {
                        anchors.centerIn: parent
                        text: modelData
                        color: Const.tradingLightBlue
                        font.pixelSize: cQuickBtnFont
                    }

                    MouseArea {
                        anchors.fill: parent
                        cursorShape: Qt.PointingHandCursor
                        onClicked: root.quickValueSelected(modelData)
                    }
                }
            }
        }

        // 数量输入 + 价格类型
        RowLayout {
            Layout.fillWidth: true
            spacing: compactMode ? 8 : 12

            TextField {
                Layout.fillWidth: true
                Layout.preferredHeight: cInputHeight
                text: root.quantityText
                placeholderText: root.quantityPlaceholder
                color: Const.tradingTitleText
                font.pixelSize: cInputFont
                horizontalAlignment: TextInput.AlignHCenter
                verticalAlignment: TextInput.AlignVCenter
                topPadding: cInputVPad
                bottomPadding: cInputVPad
                leftPadding: cInputHPad
                rightPadding: cInputHPad
                onTextChanged: root.quantityChanged(text)
                background: Rectangle {
                    radius: cInputRadius
                    color: Const.tradingInputBg
                    border.color: Const.tradingInputBorder
                    border.width: 1
                }
            }

            ComboBox {
                Layout.preferredWidth: 120
                Layout.preferredHeight: cInputHeight
                font.pixelSize: cInputFont
                model: ["市价", "限价"]
                currentIndex: root.priceTypeIndex
                onActivated: root.priceTypeChanged(currentIndex)
            }
        }

        // 价格 ± 微调行
        RowLayout {
            Layout.fillWidth: true
            spacing: compactMode ? 6 : 8

            Rectangle {
                Layout.preferredWidth: cInputHeight
                Layout.preferredHeight: cInputHeight
                radius: cInputRadius
                color: Const.tradingButtonBg
                border.color: Const.tradingInputActiveBorder
                border.width: 1

                Text {
                    anchors.centerIn: parent
                    text: "-"
                    color: Const.tradingLightBlue
                    font.pixelSize: cBtnFont
                    font.weight: Font.DemiBold
                }

                MouseArea {
                    anchors.fill: parent
                    cursorShape: Qt.PointingHandCursor
                    onClicked: root.priceAdjusted(-1)
                }
            }

            TextField {
                Layout.fillWidth: true
                Layout.preferredHeight: cInputHeight
                text: root.priceText
                placeholderText: root.pricePlaceholder
                color: Const.tradingTitleText
                font.pixelSize: cInputFont
                horizontalAlignment: TextInput.AlignHCenter
                verticalAlignment: TextInput.AlignVCenter
                topPadding: cInputVPad
                bottomPadding: cInputVPad
                leftPadding: cInputHPad
                rightPadding: cInputHPad
                onTextChanged: root.priceChanged(text)
                background: Rectangle {
                    radius: cInputRadius
                    color: Const.tradingInputBg
                    border.color: Const.tradingInputBorder
                    border.width: 1
                }
            }

            Rectangle {
                Layout.preferredWidth: cInputHeight
                Layout.preferredHeight: cInputHeight
                radius: cInputRadius
                color: Const.tradingButtonBg
                border.color: Const.tradingInputActiveBorder
                border.width: 1

                Text {
                    anchors.centerIn: parent
                    text: "+"
                    color: Const.tradingLightBlue
                    font.pixelSize: cBtnFont
                    font.weight: Font.DemiBold
                }

                MouseArea {
                    anchors.fill: parent
                    cursorShape: Qt.PointingHandCursor
                    onClicked: root.priceAdjusted(1)
                }
            }
        }

        // 扩展区（各品种特有内容，如快捷价格按钮行、期权参数等）
        Item {
            id: extraContainer
            Layout.fillWidth: true
            implicitHeight: extraContainer.children.length > 0 ? extraContainer.childrenRect.height : 0
        }

        // 价格摘要
        Text {
            Layout.fillWidth: true
            text: root.priceSummary
            color: Const.tradingLabelTertiary
            font.pixelSize: cMetaFont
            horizontalAlignment: Text.AlignHCenter
            visible: root.priceSummary.length > 0
        }

        // 金额摘要
        Text {
            Layout.fillWidth: true
            text: root.amountSummary
            color: Const.tradingLabelTertiary
            font.pixelSize: cMetaFont
            horizontalAlignment: Text.AlignHCenter
            visible: root.amountSummary.length > 0
        }
    }
}
