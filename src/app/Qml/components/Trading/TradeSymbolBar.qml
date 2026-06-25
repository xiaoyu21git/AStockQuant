import QtQuick 2.15
import QtQuick.Controls 2.15
import "../Base/Constants.qml" as Const
import "TradeUtils.js" as Utils

// ── TradeSymbolBar — 代码输入 + 名称 + 现价 + 涨跌幅 ──
Rectangle {
    id: root

    // ── 输入 ──
    property string symbol: ""
    property var marketSnapshot: null   // {price, changePct, changeAmt, high, low, open, preClose}

    // ── 输出 ──
    signal symbolChanged(string newSymbol)

    // ── 内部 ──
    property string displayDensity: "full"
    property string displayName: ""      // 由 StockNameResolver 填充

    implicitHeight: densityHeight()
    color: "transparent"

    function densityHeight() {
        if (displayDensity === "mini") return 36
        return 56
    }

    RowLayout {
        anchors.fill: parent
        spacing: 8

        // ── 代码输入 ──
        TextField {
            id: codeInput
            Layout.preferredWidth: displayDensity === "mini" ? 80 : 110
            Layout.fillHeight: true
            text: root.symbol
            placeholderText: "代码"
            font.pixelSize: 13
            font.family: "Inter, Noto Sans SC"
            color: Const.Constants.textPrimary
            verticalAlignment: TextInput.AlignVCenter

            background: Rectangle {
                radius: 6
                color: Const.Constants.tertiaryBg
                border.color: codeInput.activeFocus ? Const.Constants.accentBlue : Const.Constants.borderColor
            }

            onEditingFinished: {
                var t = text.trim().toUpperCase()
                if (t !== root.symbol) root.symbolChanged(t)
            }
        }

        // ── 名称（compact/full 模式）──
        Text {
            visible: displayDensity !== "mini"
            Layout.fillWidth: true
            text: root.displayName || root.symbol || "--"
            font.pixelSize: 14
            font.family: "Inter, Noto Sans SC"
            font.weight: Font.Medium
            color: Const.Constants.textPrimary
            elide: Text.ElideRight
            verticalAlignment: Text.AlignVCenter
        }

        // ── 现价 ──
        Text {
            property double price: root.marketSnapshot ? root.marketSnapshot.price || 0 : 0
            text: Utils.formatPrice(price)
            font.pixelSize: displayDensity === "mini" ? 14 : 18
            font.family: "Inter, Noto Sans SC"
            font.weight: Font.Bold
            color: Utils.priceColor(root.marketSnapshot ? root.marketSnapshot.changePct || 0 : 0)
            verticalAlignment: Text.AlignVCenter
        }

        // ── 涨跌幅（compact/full 模式）──
        ColumnLayout {
            visible: displayDensity !== "mini"
            spacing: 0
            Layout.preferredWidth: 68

            Text {
                property double pct: root.marketSnapshot ? root.marketSnapshot.changePct || 0 : 0
                text: Utils.formatPercent(pct)
                font.pixelSize: 13
                font.family: "Inter, Noto Sans SC"
                font.weight: Font.Medium
                color: Utils.priceColor(pct)
                horizontalAlignment: Text.AlignRight
                Layout.fillWidth: true
            }
            Text {
                property double amt: root.marketSnapshot ? root.marketSnapshot.changeAmt || 0 : 0
                text: amt !== 0 ? Utils.formatPrice(amt) : ""
                font.pixelSize: 11
                font.family: "Inter, Noto Sans SC"
                color: Const.Constants.textTertiary
                horizontalAlignment: Text.AlignRight
                Layout.fillWidth: true
            }
        }
    }
}
