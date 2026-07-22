import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import QtCharts 2.15
import AStock.Bridge 1.0

Item {
    id: page
    anchors.fill: parent
    property string strategyId: ""
    property string strategyName: ""
    property var backtestResult: null
    signal backToWorkbench()

    property string selSymbol: ""
    property double selPnLTotal: 0.0

    StrategyPerformanceModel { id: histModel; strategyId: page.strategyId }
    Component.onCompleted: { if (page.strategyId) histModel.refresh() }

    property var tradeRows: {
        if (backtestResult && backtestResult.tradeLog && backtestResult.tradeLog.length > 0)
            return backtestResult.tradeLog
        if (histModel.count > 0 && page.strategyId) {
            var detail = histModel.loadResultDetail(0)
            if (detail && detail.id) {
                var tlist = histModel.loadTrades(detail.id, 0, 10000)
                var a = []
                for (var j = 0; j < tlist.length; j++) {
                    var t = tlist[j]
                    a.push({date:parseInt((t.date||"").replace(/-/g,""))||0,symbol:t.symbol||"",isBuy:(t.side||"")==="B",quantity:t.qty||0,price:t.price||0,amount:(t.qty||0)*(t.price||0),realizedPnl:t.pnl||0})
                }
                return a
            }
        }
        return backtestResult&&backtestResult.tradeLog?backtestResult.tradeLog:[]
    }

    property var stockSummary: {
        var m={},a=[]
        for(var i=0;i<tradeRows.length;i++){var t=tradeRows[i],s=t.symbol
        if(!m[s])m[s]={symbol:s,buys:0,sells:0,wins:0,totalPnl:0,maxWin:0,maxLoss:0}
        if(t.isBuy)m[s].buys++;else{m[s].sells++;var p=t.realizedPnl||0;m[s].totalPnl+=p
        if(p>m[s].maxWin)m[s].maxWin=p;if(p<m[s].maxLoss)m[s].maxLoss=p;if(p>=0)m[s].wins++}}
        for(var k in m){var x=m[k];x.winRate=x.sells>0?(x.wins/x.sells*100).toFixed(1)+"%":"--";a.push(x)}
        a.sort(function(a,b){return b.totalPnl-a.totalPnl});return a}

    // 买卖点选中对比
    property var selBuyInfo: null; property var selSellInfo: null
    property var selPairStats: {
        if(!selBuyInfo||!selSellInfo)return{days:"--",pnl:0,ratio:"--"}
        var bd=selBuyInfo.dateD,sd=selSellInfo.dateD
        var days=Math.round((sd-bd)/86400000)
        var pnl=selSellInfo.pnl||0
        var ratio=selBuyInfo.price>0?(pnl/selBuyInfo.price*100).toFixed(2)+"%":"--"
        return{days:days,pnl:pnl,ratio:ratio}
    }
    function fmtTradeDate(d){var s=String(d||"");return s.length===8?s.substr(0,4)+"-"+s.substr(4,2)+"-"+s.substr(6,2):s}
    function findTradeInfo(pt,isBuy){
        for(var i=0;i<tradeRows.length;i++){var t=tradeRows[i]
        if(selSymbol&&t.symbol!==selSymbol)continue
        if(t.isBuy!==isBuy)continue
        var d2=new Date(parseInt(t.date.toString().substr(0,4)),parseInt(t.date.toString().substr(4,2))-1,parseInt(t.date.toString().substr(6,2)))
        if(Math.abs(d2.getTime()-pt.x)<86400000&&Math.abs(t.price-pt.y)<t.price*0.02)
            return{date:fmtTradeDate(t.date),dateD:d2,price:t.price,pnl:t.realizedPnl||0,isBuy:t.isBuy}}
        return null
    }

    function buildSymbolChart(){
        selBuyInfo=null;selSellInfo=null
        buyPoints.clear();sellWin.clear();sellLoss.clear();priceLine.clear();selPnLTotal=0.0
        var tMin=Infinity,tMax=-Infinity,pMin=Infinity,pMax=-Infinity
        // 1. 日线收盘价走势
        var sp=backtestResult&&backtestResult.symbolPrices?backtestResult.symbolPrices[selSymbol]:null
        if(sp&&sp.dates) for(var di=0;di<sp.dates.length;di++){
            var dd=sp.dates[di],dc=sp.closes[di]
            var dObj=new Date(parseInt(String(dd).substr(0,4)),parseInt(String(dd).substr(4,2))-1,parseInt(String(dd).substr(6,2)))
            var ms=dObj.getTime();if(ms<tMin)tMin=ms;if(ms>tMax)tMax=ms;if(dc<pMin)pMin=dc;if(dc>pMax)pMax=dc
            priceLine.append(ms,dc)
        }
        // 2. 买卖点叠加
        var pts=[];for(var i=0;i<tradeRows.length;i++){var t=tradeRows[i]
        if(selSymbol&&t.symbol!==selSymbol)continue;pts.push(t)}
        pts.sort(function(a,b){return a.date-b.date})
        for(var j=0;j<pts.length;j++){var t=pts[j]
        var d2=new Date(parseInt(t.date.toString().substr(0,4)),parseInt(t.date.toString().substr(4,2))-1,parseInt(t.date.toString().substr(6,2)))
        var ms2=d2.getTime();if(ms2<tMin)tMin=ms2;if(ms2>tMax)tMax=ms2;if(t.price<pMin)pMin=t.price;if(t.price>pMax)pMax=t.price
        if(t.isBuy){buyPoints.append(ms2,t.price)}
        else{var pp=t.realizedPnl||0;selPnLTotal+=pp
        if(pp>=0){sellWin.append(ms2,t.price)}else{sellLoss.append(ms2,t.price)}}}
        if(isFinite(tMin)){symAxisX.min=new Date(tMin-86400000*30);symAxisX.max=new Date(tMax+86400000*30)}
        if(isFinite(pMin)){var pad=(pMax-pMin)*0.1||pMax*0.02;symAxisY.min=pMin-pad;symAxisY.max=pMax+pad}
    }

    // 比例显示
    function fp(v,d){var n=Number(v);return isNaN(n)?"--":(n*100).toFixed(d||2)+"%"}
    function fn(v,d){var n=Number(v);return isNaN(n)?"--":n.toFixed(d||2)}

    property var perf: (backtestResult&&backtestResult.performance)?backtestResult.performance:({})
    property var trades: (backtestResult&&backtestResult.trades)?backtestResult.trades:({})
    property var ts: (backtestResult&&backtestResult.timeSeries)?backtestResult.timeSeries:({})
    property var params: (backtestResult&&backtestResult.parameters)?backtestResult.parameters:({})
    property var risk: (backtestResult&&backtestResult.risk)?backtestResult.risk:({})
    property var ddIndexByDate: ({})

    // 空仓统计: 按标的汇总所有持仓日期, 统计空仓天数
    property var positionDaysSet: {
        var s=new Set()
        for(var i=0;i<tradeRows.length;i++){var t=tradeRows[i];s.add(t.date)}
        // 简化: 用首笔买入到末笔卖出之间的天数估算
        var sorted=Array.from(s).sort()
        if(sorted.length<2)return{totalDays:0,positionDays:0,emptyDays:0}
        var first=parseInt(sorted[0]),last=parseInt(sorted[sorted.length-1])
        var total=Math.ceil((new Date(parseInt(last.toString().substr(0,4)),parseInt(last.toString().substr(4,2))-1,parseInt(last.toString().substr(6,2)))
                              -new Date(parseInt(first.toString().substr(0,4)),parseInt(first.toString().substr(4,2))-1,parseInt(first.toString().substr(6,2))))/86400000)+1
        return{totalDays:total,positionDays:s.size,emptyDays:total-s.size}
    }
    property var tradeMeta: {
        var tsd=ts.dates||[]
        return{tradingDays:tsd.length,totalTrades:tradeRows.length}
    }

    function fmtDate(v){var s=String(v||"");return s.length===8?s.substr(0,4)+"-"+s.substr(4,2)+"-"+s.substr(6,2):(s||"--")}
    function tradeReturnRatio(row){var c=(row.amount||0)-(row.realizedPnl||0);return c>0?(row.realizedPnl||0)/c:NaN}
    function dayDrawdownOf(d){var i=ddIndexByDate[d];return(i!==undefined&&ts.drawdowns)?ts.drawdowns[i]:NaN}
    function rejectionLabel(code){switch(Number(code)){case 1:return"缺少必填字段";case 2:return"策略未绑定";case 3:return"策略未激活";case 4:return"价格无效";case 5:return"信号太弱";case 6:return"持仓快照未就绪";case 7:return"非交易时段";case 8:return"无可卖持仓";case 9:return"卖出超量";case 10:return"订单金额超限";case 11:return"滑点超限";case 12:return"日成交额超限";case 13:return"止损触发";case 14:return"止盈触发";case 15:return"一级熔断";case 16:return"二级熔断";case 17:return"三级熔断";case 18:return"最大回撤超限";case 19:return"集中度超限";case 20:return"总敞口超限";case 21:return"交易暂停";case 22:return"缺委托数量";default:return"未知("+code+")"}}

    onBacktestResultChanged: {
        if(!backtestResult||!backtestResult.timeSeries)return
        // 自动选第一个标的
        if(tradeRows.length>0){selSymbol=tradeRows[0].symbol;buildSymbolChart()}
        // 填充图表 — 使用真实日期作为 X 轴
        var tss=backtestResult.timeSeries
        var pv=tss.portfolioValues||[],dd=tss.drawdowns||[],ret=tss.returns||[]
        var bv=tss.benchmarkValues||[],bdd=tss.benchmarkDrawdowns||[]
        var dates=tss.dates||[]
        equityS.clear();drawdownS.clear();returnS.clear();bmEquityS.clear();bmDrawdownS.clear()
        var bB=bv.length>0&&isFinite(bv[0])&&bv[0]>0?bv[0]:1.0
        var pM=-1e18,pm=1e18
        for(var i=0;i<pv.length;i++){
            var ms=i<dates.length?(new Date(parseInt(String(dates[i]).substr(0,4)),parseInt(String(dates[i]).substr(4,2))-1,parseInt(String(dates[i]).substr(6,2)))).getTime():i*86400000;
            var sv=isFinite(pv[i])?pv[i]:pv[0]||1;equityS.append(ms,sv);drawdownS.append(ms,isFinite(dd[i])?dd[i]:0);if(sv>pM)pM=sv;if(sv<pm)pm=sv}
        for(var k=0;k<bv.length;k++){
            var ms2=k<dates.length?(new Date(parseInt(String(dates[k]).substr(0,4)),parseInt(String(dates[k]).substr(4,2))-1,parseInt(String(dates[k]).substr(6,2)))).getTime():k*86400000;
            var bmv=isFinite(bv[k])?bv[k]:bv[0]||1;bmEquityS.append(ms2,bmv);bmDrawdownS.append(ms2,isFinite(bdd[k])?bdd[k]:0);if(bmv>pM)pM=bmv;if(bmv<pm)pm=bmv}
        eqY.min=pm*0.95;eqY.max=pM*1.05
        var ddMinAll=0;for(var di2=0;di2<dd.length;di2++){var dv2=isFinite(dd[di2])?dd[di2]:0;if(dv2<ddMinAll)ddMinAll=dv2}
        for(var dk2=0;dk2<bdd.length;dk2++){var bv2=isFinite(bdd[dk2])?bdd[dk2]:0;if(bv2<ddMinAll)ddMinAll=bv2}
        ddY.min=Math.min(0,ddMinAll*1.05);ddY.max=0
        var cum=0,rM=-1e18;for(var j=0;j<ret.length;j++){
            var ms3=j<dates.length?(new Date(parseInt(String(dates[j]).substr(0,4)),parseInt(String(dates[j]).substr(4,2))-1,parseInt(String(dates[j]).substr(6,2)))).getTime():j*86400000;
            cum+=ret[j];returnS.append(ms3,cum);if(cum>rM)rM=cum}
        retY.min=-0.5;retY.max=rM*1.1
        var m={},td=dates;for(var di=0;di<td.length;di++)m[td[di]]=di;ddIndexByDate=m
    }

    Flickable { anchors.fill: parent; contentWidth: parent.width; contentHeight: content.implicitHeight+20; clip: true
    ColumnLayout { id: content; width: parent.width; spacing:10

        RowLayout { Layout.fillWidth: true
            Text{text:"回测分析 · "+(strategyName||strategyId||"");font.pixelSize:16;font.weight:Font.Bold;color:"#F1F5F9"}
            Item{Layout.fillWidth:true}
            Rectangle{width:55;height:24;radius:4;color:"#475569";Text{anchors.centerIn:parent;text:"返回";font.pixelSize:10;color:"#F1F5F9"}MouseArea{anchors.fill:parent;onClicked:backToWorkbench()}}}

        Rectangle { Layout.fillWidth: true; visible: Object.keys(params).length>0; radius: 8; color: "#1E293B"
            RowLayout { anchors.fill: parent; anchors.margins: 10; spacing: 12
                Text{text:"参数: 初始"+(params.initialCapital||"--")+" | "+(params.startDate||"--")+"~"+(params.endDate||"--")+" | 基准"+(params.benchmarkIndex||"--");font.pixelSize:10;color:"#64748B"}}}

        Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 160; radius: 8; color: "#1E293B"
            GridLayout { anchors.fill: parent; anchors.margins: 10; columns: 5; columnSpacing: 8; rowSpacing: 6
                Card{Layout.fillWidth:true;Layout.preferredHeight:45;label:"总收益";value:fp(perf.totalReturn);accent:(perf.totalReturn||0)>=0?"#EF4444":"#10B981"}
                Card{Layout.fillWidth:true;Layout.preferredHeight:45;label:"年化收益";value:fp(perf.annualizedReturn);accent:(perf.annualizedReturn||0)>=0?"#EF4444":"#10B981"}
                Card{Layout.fillWidth:true;Layout.preferredHeight:45;label:"最大回撤";value:fp(perf.maxDrawdown);accent:"#F59E0B"}
                Card{Layout.fillWidth:true;Layout.preferredHeight:45;label:"夏普比率";value:fn(perf.sharpeRatio,3);accent:"#38BDF8"}
                Card{Layout.fillWidth:true;Layout.preferredHeight:45;label:"Sortino";value:fn(perf.sortinoRatio,3);accent:"#38BDF8"}
                Card{Layout.fillWidth:true;Layout.preferredHeight:45;label:"胜率";value:fp(perf.winRate);accent:"#EF4444"}
                Card{Layout.fillWidth:true;Layout.preferredHeight:45;label:"利润因子";value:fn(perf.profitFactor,2);accent:"#EF4444"}
                Card{Layout.fillWidth:true;Layout.preferredHeight:45;label:"Alpha";value:fn(perf.alpha,3);accent:"#F1F5F9"}
                Card{Layout.fillWidth:true;Layout.preferredHeight:45;label:"Beta";value:fn(perf.beta,2);accent:"#F1F5F9"}
                Card{Layout.fillWidth:true;Layout.preferredHeight:45;label:"总交易";value:fn(trades.totalTrades,0);accent:"#64748B"}
                Card{Layout.fillWidth:true;Layout.preferredHeight:45;label:"交易日";value:fn(tradeMeta.tradingDays,0);accent:"#F1F5F9"}
                Card{Layout.fillWidth:true;Layout.preferredHeight:45;label:"持仓日";value:fn(tradeMeta.tradingDays-positionDaysSet.emptyDays,0);accent:"#F59E0B"}
                Card{Layout.fillWidth:true;Layout.preferredHeight:45;label:"空仓日";value:fn(positionDaysSet.emptyDays,0);accent:"#64748B"}
                Card{Layout.fillWidth:true;Layout.preferredHeight:45;label:"持仓比";value: tradeMeta.tradingDays>0?fn((tradeMeta.tradingDays-positionDaysSet.emptyDays)/tradeMeta.tradingDays*100,0)+"%":"--";accent:"#38BDF8"}}}

        Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 300; radius: 8; color: "#1E293B"
            ColumnLayout { anchors.fill: parent; anchors.margins: 8; spacing: 2
                Text{text:"净值曲线";font.pixelSize:11;color:"#94A3B8"}
                ChartView{Layout.fillWidth:true;Layout.fillHeight:true;antialiasing:true;legend.visible:false;backgroundColor:"transparent";plotAreaColor:"transparent"
                    DateTimeAxis{id:eqX;format:"yy/MM";labelsColor:"#64748B";gridLineColor:"#1E293B";labelsFont.pixelSize:8}
                    ValueAxis{id:eqY;labelsColor:"#94A3B8";gridLineColor:"#1E293B";labelFormat:"%.0f"}
                    LineSeries{id:equityS;axisX:eqX;axisY:eqY;color:"#EF4444";width:2}
                    LineSeries{id:bmEquityS;axisX:eqX;axisY:eqY;color:"#94A3B8";width:1;style:Qt.DashLine}}}}

        RowLayout { Layout.fillWidth: true; Layout.preferredHeight: 240; spacing: 8
            Rectangle { Layout.fillWidth: true; Layout.fillHeight: true; radius: 8; color: "#1E293B"
                ColumnLayout { anchors.fill: parent; anchors.margins: 8; spacing: 2
                    Text{text:"回撤曲线";font.pixelSize:11;color:"#94A3B8"}
                    ChartView{Layout.fillWidth:true;Layout.fillHeight:true;antialiasing:true;legend.visible:false;backgroundColor:"transparent";plotAreaColor:"transparent"
                        DateTimeAxis{id:ddX;format:"yy/MM";labelsColor:"#64748B";gridLineColor:"#1E293B";labelsFont.pixelSize:8}
                        ValueAxis{id:ddY;labelsColor:"#94A3B8";gridLineColor:"#1E293B";labelFormat:"%.2f"}
                        LineSeries{id:drawdownS;axisX:ddX;axisY:ddY;color:"#F59E0B";width:2}
                        LineSeries{id:bmDrawdownS;axisX:ddX;axisY:ddY;color:"#94A3B8";width:1;style:Qt.DashLine}}}}
            Rectangle { Layout.fillWidth: true; Layout.fillHeight: true; radius: 8; color: "#1E293B"
                ColumnLayout { anchors.fill: parent; anchors.margins: 8; spacing: 2
                    Text{text:"累计收益";font.pixelSize:11;color:"#94A3B8"}
                    ChartView{Layout.fillWidth:true;Layout.fillHeight:true;antialiasing:true;legend.visible:false;backgroundColor:"transparent";plotAreaColor:"transparent"
                        DateTimeAxis{id:retX;format:"yy/MM";labelsColor:"#64748B";gridLineColor:"#1E293B";labelsFont.pixelSize:8}
                        ValueAxis{id:retY;labelsColor:"#94A3B8";gridLineColor:"#1E293B";labelFormat:"%.2f"}
                        LineSeries{id:returnS;axisX:retX;axisY:retY;color:"#38BDF8";width:2}}}}}

        // 个股统计 + 买卖点图表
        Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 420; radius: 8; color: "#1E293B"
            RowLayout { anchors.fill: parent; anchors.margins: 8; spacing: 12
                ColumnLayout { Layout.fillWidth: true; Layout.fillHeight: true; spacing: 2
                    Text{text:"持仓标的统计";font.pixelSize:11;color:"#94A3B8"}
                    Rectangle { Layout.fillWidth: true; height: 20; color: "transparent"
                        Row { anchors.verticalCenter: parent.verticalCenter
                            Text{width:120;text:"标的";font.pixelSize:9;color:"#64748B";font.weight:Font.Bold}
                            Text{width:36;text:"买";font.pixelSize:9;color:"#64748B";font.weight:Font.Bold}
                            Text{width:36;text:"卖";font.pixelSize:9;color:"#64748B";font.weight:Font.Bold}
                            Text{width:40;text:"胜率";font.pixelSize:9;color:"#64748B";font.weight:Font.Bold}
                            Text{width:68;text:"盈亏";font.pixelSize:9;color:"#64748B";font.weight:Font.Bold}}}
                    ListView { Layout.fillWidth: true; Layout.fillHeight: true; clip: true; model: stockSummary
                        delegate: Rectangle { width: ListView.view.width; height: 22; color: index%2?"transparent":"#0B1220"
                            MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor
                                onClicked: { selSymbol=modelData.symbol; buildSymbolChart() }}
                            Row { anchors.verticalCenter: parent.verticalCenter
                                Text{width:120;text:StrategyBridge.stockDisplayName(modelData.symbol);font.pixelSize:9;color:selSymbol===modelData.symbol?"#F59E0B":"#F1F5F9";elide:Text.ElideRight}
                                Text{width:36;text:modelData.buys;font.pixelSize:9;color:"#94A3B8"}
                                Text{width:36;text:modelData.sells;font.pixelSize:9;color:"#94A3B8"}
                                Text{width:40;text:modelData.winRate;font.pixelSize:9;color:parseFloat(modelData.winRate)>=50?"#EF4444":"#10B981"}
                                Text{width:68;text:fn(modelData.totalPnl,0);font.pixelSize:9;color:modelData.totalPnl>=0?"#EF4444":"#10B981"}}}}}
                ColumnLayout { Layout.fillWidth: true; Layout.fillHeight: true; spacing: 4
                    RowLayout {
                        Text{text:selSymbol?"买卖点 · "+StrategyBridge.stockDisplayName(selSymbol):"← 点击标的查看";font.pixelSize:11;color:"#94A3B8";elide:Text.ElideRight}
                        Item{Layout.fillWidth:true}
                        Text{text:selPnLTotal>=0?"盈":"亏";font.pixelSize:11;color:selPnLTotal>=0?"#EF4444":"#10B981"}
                        Text{text:selPnLTotal?fn(selPnLTotal,0):"";font.pixelSize:11;color:selPnLTotal>=0?"#EF4444":"#10B981"}}
                    // 买卖点选中对比
                    Loader { Layout.fillWidth: true; active: selBuyInfo&&selSellInfo; sourceComponent: Rectangle { width:parent.width; height:44; radius:6; color:"#0F172A"; border.color:"#334155"; border.width:1
                        RowLayout { anchors.fill:parent; anchors.margins:8; spacing:16
                            Text{text:selBuyInfo?"买 "+selBuyInfo.date+" · "+fn(selBuyInfo.price,2):"";font.pixelSize:10;color:"#F59E0B"}
                            Text{text:"→";font.pixelSize:14;color:"#64748B"}
                            Text{text:selSellInfo?"卖 "+selSellInfo.date+" · "+fn(selSellInfo.price,2):"";font.pixelSize:10;color:selSellInfo&&selSellInfo.pnl>=0?"#EF4444":"#10B981"}
                            Item{Layout.fillWidth:true}
                            Text{text:selSymbol?StrategyBridge.stockDisplayName(selSymbol):"";font.pixelSize:9;color:"#64748B"}
                            Text{text:"持仓 "+selPairStats.days+"天";font.pixelSize:11;color:"#F1F5F9";font.weight:Font.Bold}
                            Text{text:"盈亏 "+fn(selPairStats.pnl,0);font.pixelSize:11;color:selPairStats.pnl>=0?"#EF4444":"#10B981";font.weight:Font.Bold}
                            Text{text:selPairStats.ratio;font.pixelSize:10;color:"#94A3B8"}}}}
                    ChartView { Layout.fillWidth: true; Layout.fillHeight: true; antialiasing: true; legend.visible: false
                        backgroundColor: "#0B1220"; plotAreaColor: "#0B1220"
                        DateTimeAxis { id: symAxisX; format: "yy/MM"; labelsColor: "#64748B"; gridVisible: true; gridLineColor: "#1F2937"; labelsFont.pixelSize: 8 }
                        ValueAxis { id: symAxisY; labelsColor: "#64748B"; gridVisible: true; gridLineColor: "#1F2937"; labelsFont.pixelSize: 8; labelFormat: "%.2f" }
                        LineSeries { id: priceLine; axisX: symAxisX; axisY: symAxisY; color: "#94A3B8"; width: 2 }
                        ScatterSeries { id: buyPoints; axisX: symAxisX; axisY: symAxisY; color: "#F59E0B"; markerSize: 9; borderColor: "#F59E0B"
                            onClicked: function(point) { selBuyInfo=findTradeInfo(point,true); if(selBuyInfo)selSellInfo=null } }
                        ScatterSeries { id: sellWin;  axisX: symAxisX; axisY: symAxisY; color: "#EF4444"; markerSize: 10
                            onClicked: function(point) { if(selBuyInfo)selSellInfo=findTradeInfo(point,false) } }
                        ScatterSeries { id: sellLoss; axisX: symAxisX; axisY: symAxisY; color: "#10B981"; markerSize: 10
                            onClicked: function(point) { if(selBuyInfo)selSellInfo=findTradeInfo(point,false) } }}}}}

        Item { Layout.preferredHeight: 10 }}}

    component Card: Rectangle {
        property string label: ""; property string value: ""; property color accent: "#F1F5F9"
        radius: 6; color: "#0B1220"
        Column { anchors.centerIn: parent; spacing: 1
            Text { anchors.horizontalCenter: parent.horizontalCenter; text: value; font.pixelSize: 16; font.weight: Font.Bold; color: accent }
            Text { anchors.horizontalCenter: parent.horizontalCenter; text: label; font.pixelSize: 9; color: "#64748B" }}}
}
