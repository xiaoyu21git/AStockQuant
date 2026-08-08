import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import "../../utils/TradingConstants.js" as Const

Item {
    id: formRoot

    required property bool compactMode
    required property real scaleFactor
    required property string code
    required property string shares
    required property string priceType
    required property string price
    required property var quickButtonModel
    required property var equityQuickPriceButtonModel
    required property var equityDisplay
    required property var marketSnapshot
    required property var depthSnapshot
    required property var tradingFormHelper
    required property string currentReferenceText

    signal codeEdited(string newCode)
    signal sharesEdited(string newShares)
    signal priceTypeEdited(string newType)
    signal priceEdited(string newPrice)
    signal quickValueSelected(string value)
    signal priceAdjustRequested(int delta)
    signal equityPriceShortcutRequested(string shortcutCode)

    function _s(v) { return Math.max(1, Math.round(v * scaleFactor)) }
    readonly property int _sectionLabelFont: _s(compactMode ? 9 : 12)
    readonly property int _inputFont: _s(compactMode ? 9 : 12)
    readonly property int _inputHeight: _s(compactMode ? 26 : 38)
    readonly property int _inputRadius: _s(compactMode ? 8 : 12)
    readonly property int _inputVpad: _s(compactMode ? 0 : 1)
    readonly property int _inputHpad: _s(compactMode ? 8 : 12)
    readonly property int _quickBtnH: _s(compactMode ? 20 : 30)
    readonly property int _quickBtnFont: _s(compactMode ? 8 : 10)
    readonly property int _btnFont: _s(compactMode ? 9 : 13)
    readonly property int _metaFont: _s(compactMode ? 9 : 12)
    readonly property int _spacing: compactMode ? 8 : 12
    readonly property int _smallSpacing: compactMode ? 6 : 8

    ColumnLayout {
        anchors.fill: parent
        spacing: _spacing

        Text {
            text: "💳 融资买入"
            color: Const.tradingLabelSecondary
            font.pixelSize: _sectionLabelFont
        }

        TextField {
            Layout.fillWidth: true
            Layout.preferredHeight: _inputHeight
            text: formRoot.code
            placeholderText: "股票代码"
            color: Const.tradingTitleText
            font.pixelSize: _inputFont
            horizontalAlignment: TextInput.AlignHCenter
            verticalAlignment: TextInput.AlignVCenter
            topPadding: _inputVpad
            bottomPadding: _inputVpad
            leftPadding: _inputHpad
            rightPadding: _inputHpad
            onTextChanged: formRoot.codeEdited(text)
            background: Rectangle {
                radius: _inputRadius
                color: Const.tradingInputBg
                border.color: Const.tradingInputBorder
                border.width: 1
            }
        }

        Text {
            Layout.fillWidth: true
            text: String(formRoot.equityDisplay.identitySummary || "")
            color: String(formRoot.equityDisplay.identityColor || Const.tradingLabelTertiary)
            font.pixelSize: _metaFont
            horizontalAlignment: Text.AlignHCenter
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: _smallSpacing

            Repeater {
                model: formRoot.quickButtonModel

                Rectangle {
                    Layout.fillWidth: true
                    implicitHeight: _quickBtnH
                    radius: _inputRadius
                    color: Const.tradingButtonBg
                    border.color: Const.tradingInputActiveBorder
                    border.width: 1

                    Text {
                        anchors.centerIn: parent
                        text: modelData
                        color: Const.tradingLightBlue
                        font.pixelSize: _quickBtnFont
                    }

                    MouseArea {
                        anchors.fill: parent
                        cursorShape: Qt.PointingHandCursor
                        onClicked: formRoot.quickValueSelected(modelData)
                    }
                }
            }
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: _spacing

            TextField {
                Layout.fillWidth: true
                Layout.preferredHeight: _inputHeight
                text: formRoot.shares
                placeholderText: "股数"
                color: Const.tradingTitleText
                font.pixelSize: _inputFont
                horizontalAlignment: TextInput.AlignHCenter
                verticalAlignment: TextInput.AlignVCenter
                topPadding: _inputVpad
                bottomPadding: _inputVpad
                leftPadding: _inputHpad
                rightPadding: _inputHpad
                onTextChanged: formRoot.sharesEdited(text)
                background: Rectangle {
                    radius: _inputRadius
                    color: Const.tradingInputBg
                    border.color: Const.tradingInputBorder
                    border.width: 1
                }
            }

            ComboBox {
                Layout.preferredWidth: 120
                Layout.preferredHeight: _inputHeight
                font.pixelSize: _inputFont
                model: ["市价", "限价"]
                currentIndex: formRoot.priceType === "market" ? 0 : 1
                onActivated: formRoot.priceTypeEdited(currentIndex === 0 ? "market" : "limit")
            }
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: _smallSpacing

            Rectangle {
                Layout.preferredWidth: _inputHeight
                Layout.preferredHeight: _inputHeight
                radius: _inputRadius
                color: Const.tradingButtonBg
                border.color: Const.tradingInputActiveBorder
                border.width: 1

                Text {
                    anchors.centerIn: parent
                    text: "-"
                    color: Const.tradingLightBlue
                    font.pixelSize: _btnFont
                    font.weight: Font.DemiBold
                }

                MouseArea {
                    anchors.fill: parent
                    cursorShape: Qt.PointingHandCursor
                    onClicked: formRoot.priceAdjustRequested(-1)
                }
            }

            TextField {
                Layout.fillWidth: true
                Layout.preferredHeight: _inputHeight
                text: formRoot.price
                placeholderText: formRoot.currentReferenceText
                color: Const.tradingTitleText
                font.pixelSize: _inputFont
                horizontalAlignment: TextInput.AlignHCenter
                verticalAlignment: TextInput.AlignVCenter
                topPadding: _inputVpad
                bottomPadding: _inputVpad
                leftPadding: _inputHpad
                rightPadding: _inputHpad
                onTextChanged: formRoot.priceEdited(text)
                background: Rectangle {
                    radius: _inputRadius
                    color: Const.tradingInputBg
                    border.color: Const.tradingInputBorder
                    border.width: 1
                }
            }

            Rectangle {
                Layout.preferredWidth: _inputHeight
                Layout.preferredHeight: _inputHeight
                radius: _inputRadius
                color: Const.tradingButtonBg
                border.color: Const.tradingInputActiveBorder
                border.width: 1

                Text {
                    anchors.centerIn: parent
                    text: "+"
                    color: Const.tradingLightBlue
                    font.pixelSize: _btnFont
                    font.weight: Font.DemiBold
                }

                MouseArea {
                    anchors.fill: parent
                    cursorShape: Qt.PointingHandCursor
                    onClicked: formRoot.priceAdjustRequested(1)
                }
            }
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: _smallSpacing

            Repeater {
                model: formRoot.equityQuickPriceButtonModel

                Rectangle {
                    Layout.fillWidth: true
                    implicitHeight: _quickBtnH
                    radius: _inputRadius
                    color: Const.tradingButtonBg
                    border.color: Const.tradingInputActiveBorder
                    border.width: 1

                    Text {
                        anchors.centerIn: parent
                        text: formRoot.tradingFormHelper.equityShortcutButtonText(modelData.code, modelData.label, "margin_buy", formRoot.marketSnapshot || ({}), formRoot.depthSnapshot || ({}))
                        color: Const.tradingLightBlue
                        font.pixelSize: _quickBtnFont
                        horizontalAlignment: Text.AlignHCenter
                    }

                    MouseArea {
                        anchors.fill: parent
                        cursorShape: Qt.PointingHandCursor
                        onClicked: formRoot.equityPriceShortcutRequested(modelData.code)
                    }
                }
            }
        }

        Text {
            Layout.fillWidth: true
            text: String(formRoot.equityDisplay.priceSummary || "")
            color: Const.tradingLabelTertiary
            font.pixelSize: _metaFont
            horizontalAlignment: Text.AlignHCenter
        }

        Text {
            Layout.fillWidth: true
            text: String(formRoot.equityDisplay.amountSummary || "")
            color: Const.tradingLabelTertiary
            font.pixelSize: _metaFont
            horizontalAlignment: Text.AlignHCenter
        }
    }
}
