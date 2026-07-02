// SectorHeatWidget.qml — 热门板块排名 (密度自适应)
import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import "../../Trading"
import "../../utils/TradingConstants.js" as Const
import "../../utils/TradingWidgetBase.js" as Base
import AStock.Bridge 1.0 as Bridge

Item {
    id: root

    property var widgetConfig: ({})
    readonly property int optimalHeight: 400
    readonly property real scaleFactor: Base.computeScaleFactor(height, optimalHeight)
    readonly property string densityMode: Base.computeDensityMode(height, optimalHeight, _densityCache)
    property string _densityCache: "normal"

    function _sf(v) { return Math.max(1, Math.round(v * scaleFactor)) }
    readonly property int rowH: Math.max(18, Math.round((height - headerH) / Math.max(1, _sortedModel.length)))
    readonly property int headerH: _sf(28)
    readonly property int col1W: width * 0.35   // 板块名
    readonly property int col2W: width * 0.17   // 涨跌幅
    readonly property int col3W: width * 0.26   // 领涨股
    readonly property int col4W: width * 0.22   // 领涨股涨幅
    readonly property int fs: _sf(densityMode==="compact"?9:(densityMode==="expanded"?13:11))
    readonly property int fsSmall: _sf(densityMode==="compact"?8:10)

    readonly property color upC: Const.tradingBuyRed
    readonly property color downC: Const.depthLimitDownGreen

    // 数据源 — 后续接入 gmsdk stk_get_industry_category + history_bars 替换 mock
    property var sectorData: [
        { name:"半导体",       chg:+3.52, lead:"中芯国际", leadChg:+5.1 },
        { name:"人工智能",     chg:+2.87, lead:"科大讯飞", leadChg:+4.3 },
        { name:"新能源车",     chg:+2.15, lead:"比亚迪",   leadChg:+3.2 },
        { name:"光伏",         chg:+1.93, lead:"隆基绿能", leadChg:+2.8 },
        { name:"消费电子",     chg:+1.64, lead:"立讯精密", leadChg:+2.5 },
        { name:"医疗器械",     chg:+1.42, lead:"迈瑞医疗", leadChg:+2.1 },
        { name:"军工",         chg:+1.28, lead:"中航沈飞", leadChg:+1.9 },
        { name:"白酒",         chg:-0.35, lead:"贵州茅台", leadChg:-0.5 },
        { name:"银行",         chg:-0.62, lead:"招商银行", leadChg:-0.8 },
        { name:"房地产",       chg:-1.15, lead:"万科A",    leadChg:-1.6 },
        { name:"煤炭",         chg:-1.48, lead:"中国神华", leadChg:-2.0 },
        { name:"电力",         chg:-0.88, lead:"长江电力", leadChg:-1.1 }
    ]

    readonly property var _sortedModel: {
        var arr = sectorData.slice()
        arr.sort(function(a,b) { return Math.abs(b.chg) - Math.abs(a.chg) })
        return arr
    }

    // header
    Rectangle {
        id: header; anchors { top: parent.top; left: parent.left; right: parent.right }
        height: headerH; color: "#1a1a2e"
        RowLayout {
            anchors.fill: parent; anchors.margins: 4; spacing: 2
            Text { text: "板块"; color: "#888"; font.pixelSize: fsSmall; Layout.preferredWidth: col1W; horizontalAlignment: Text.AlignHCenter }
            Text { text: "涨幅%"; color: "#888"; font.pixelSize: fsSmall; Layout.preferredWidth: col2W; horizontalAlignment: Text.AlignHCenter }
            Text { text: "领涨股"; color: "#888"; font.pixelSize: fsSmall; Layout.preferredWidth: col3W; horizontalAlignment: Text.AlignHCenter }
            Text { text: "涨幅%"; color: "#888"; font.pixelSize: fsSmall; Layout.preferredWidth: col4W; horizontalAlignment: Text.AlignHCenter }
        }
    }

    // rows
    ListView {
        id: listView
        anchors { top: header.bottom; bottom: parent.bottom; left: parent.left; right: parent.right }
        clip: true; model: _sortedModel
        delegate: Rectangle {
            width: listView.width; height: rowH
            color: index % 2 === 0 ? "#1a1a2e" : "#1f1f3a"
            RowLayout {
                anchors.fill: parent; anchors.margins: 2; spacing: 0
                Text { text: modelData.name; color: "#d0d0e0"; font.pixelSize: fs; elide: Text.ElideRight; Layout.preferredWidth: col1W; horizontalAlignment: Text.AlignLeft; leftPadding: 4 }
                Text { text: (modelData.chg>0?"+":"")+modelData.chg.toFixed(2); color: modelData.chg>=0?upC:downC; font.pixelSize: fs; font.weight: Font.DemiBold; Layout.preferredWidth: col2W; horizontalAlignment: Text.AlignHCenter }
                Text { text: modelData.lead; color: "#d0d0e0"; font.pixelSize: fs; elide: Text.ElideRight; Layout.preferredWidth: col3W; horizontalAlignment: Text.AlignHCenter }
                Text { text: (modelData.leadChg>0?"+":"")+modelData.leadChg.toFixed(1); color: modelData.leadChg>=0?upC:downC; font.pixelSize: fs; Layout.preferredWidth: col4W; horizontalAlignment: Text.AlignHCenter }
            }
        }
    }

    // density mode tracking
    onDensityModeChanged: { _densityCache = densityMode }
}
