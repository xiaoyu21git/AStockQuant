import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import "../../utils/TradingConstants.js" as Const

Item {
    id: formRoot

    required property bool compactMode
    required property real scaleFactor
    required property string stockCode
    required property string stockDisplayName
    required property string stockShares
    required property string stockPriceType
    required property string stockPrice
    required property var quickButtonModel
    required property var equityQuickPriceButtonModel
    required property var equityDisplay
    required property var marketSnapshot
    required property var depthSnapshot
    required property var tradingFormHelper
    required property string currentReferenceText
    required property var symbolSearchModel

    // 暴露搜索字段供父组件 Popup 定位和更新文本
    property alias codeField: stockCodeField

    signal codeEdited(string newCode)
    signal displayNameEdited(string newDisplayName)
    signal searchRequested(string query)
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
            text: "📌 股票代码"
            color: Const.tradingLabelSecondary
            font.pixelSize: _sectionLabelFont
        }

        TextField {
            id: stockCodeField
            Layout.fillWidth: true
            Layout.preferredHeight: _inputHeight
            placeholderText: "输代码/名称搜索"
            color: Const.tradingTitleText
            font.pixelSize: _inputFont
            horizontalAlignment: TextInput.AlignHCenter
            verticalAlignment: TextInput.AlignVCenter
            topPadding: _inputVpad
            bottomPadding: _inputVpad
            leftPadding: _inputHpad
            rightPadding: _inputHpad
            property bool suppressTextChange: false
            // 显示"名称 代码"，用户输入时只回写代码避免双向绑定循环
            text: formRoot.stockDisplayName
                  ? (formRoot.stockCode ? formRoot.stockDisplayName + " " + formRoot.stockCode
                                        : formRoot.stockDisplayName)
                  : (formRoot.stockCode || "")
            onTextChanged: {
                if (suppressTextChange) return
                var t = text.trim()
                // 用户输入代码(数字)时回写stockCode，搜索名称时只触发search
                var codeMatch = t.match(/\b(\d{6})\b/)
                suppressTextChange = true
                if (codeMatch) {
                    formRoot.codeEdited(codeMatch[1])
                } else {
                    formRoot.codeEdited(t)
                }
                formRoot.searchRequested(t)
                suppressTextChange = false
            }
            background: Rectangle {
                radius: _inputRadius
                color: Const.tradingInputBg
                border.color: Const.tradingInputBorder
                border.width: 1
            }
        }

        // 搜索结果显示后由父组件回调更新 display text
        function refreshDisplayText() {
            stockCodeField.text = formRoot.stockDisplayName
                ? formRoot.stockDisplayName + " " + formRoot.stockCode
                : formRoot.stockCode
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
                text: formRoot.stockShares
                placeholderText: "股数(100倍数)"
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
                currentIndex: formRoot.stockPriceType === "market" ? 0 : 1
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
                text: formRoot.stockPrice
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
                        text: formRoot.tradingFormHelper.equityShortcutButtonText(modelData.code, modelData.label, "stock", formRoot.marketSnapshot || ({}), formRoot.depthSnapshot || ({}))
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
