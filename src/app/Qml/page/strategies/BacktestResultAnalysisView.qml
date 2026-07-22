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

    function buildSymbolChart(){
        buyPoints.clear();sellWin.clear();sellLoss.clear();selPnLTotal=0.0
        for(var i=0;i<tradeRows.length;i++){var t=tradeRows[i]
        if(selSymbol&&t.symbol!==selSymbol)continue
        var d=new Date(parseInt(t.date.toString().substr(0,4)),parseInt(t.date.toString().substr(4,2))-1,parseInt(t.date.toString().substr(6,2)))
        if(t.isBuy){buyPoints.append(d.getTime(),t.price)}
        else{var p=t.realizedPnl||0;selPnLTotal+=p
        if(p>=0)sellWin.append(d.getTime(),t.price);else sellLoss.append(d.getTime(),t.price)}}}

    // 比例显示
    function fp(v,d){var n=Number(v);return isNaN(n)?"--":(n*100).toFixed(d||2)+"%"}
    function fn(v,d){var n=Number(v);return isNaN(n)?"--":n.toFixed(d||2)}

    property var perf: (backtestResult&&backtestResult.performance)?backtestResult.performance:({})
    property var trades: (backtestResult&&backtestResult.trades)?backtestResult.trades:({})
    property var ts: (backtestResult&&backtestResult.timeSeries)?backtestResult.timeSeries:({})
    property var params: (backtestResult&&backtestResult.parameters)?backtestResult.parameters:({})
    property var risk: (backtestResult&&backtestResult.risk)?backtestResult.risk:({})
    property var ddIndexByDate: ({})

    function fmtDate(v){var s=String(v||"");return s.length===8?s.substr(0,4)+"-"+s.substr(4,2)+"-"+s.substr(6,2):(s||"--")}
    function tradeReturnRatio(row){var c=(row.amount||0)-(row.realizedPnl||0);return c>0?(row.realizedPnl||0)/c:NaN}
    function dayDrawdownOf(d){var i=ddIndexByDate[d];return(i!==undefined&&ts.drawdowns)?ts.drawdowns[i]:NaN}
    function rejectionLabel(code){switch(Number(code)){case 1:return"缺少必填字段";case 2:return"策略未绑定";case 3:return"策略未激活";case 4:return"价格无效";case 5:return"信号太弱";case 6:return"持仓快照未就绪";case 7:return"非交易时段";case 8:return"无可卖持仓";case 9:return"卖出超量";case 10:return"订单金额超限";case 11:return"滑点超限";case 12:return"日成交额超限";case 13:return"止损触发";case 14:return"止盈触发";case 15:return"一级熔断";case 16:return"二级熔断";case 17:return"三级熔断";case 18:return"最大回撤超限";case 19:return"集中度超限";case 20:return"总敞口超限";case 21:return"交易暂停";case 22:return"缺委托数量";default:return"未知("+code+")"}}

    onBacktestResultChanged: {
        if(!backtestResult||!backtestResult.timeSeries)return
        // 自动选第一个标的
        if(tradeRows.length>0){selSymbol=tradeRows[0].symbol;buildSymbolChart()}
        // 填充图表
        var tss=backtestResult.timeSeries
        var pv=tss.portfolioValues||[],dd=tss.drawdowns||[],ret=tss.returns||[]
        var bv=tss.benchmarkValues||[],bdd=tss.benchmarkDrawdowns||[]
        equityS.clear();drawdownS.clear();returnS.clear();bmEquityS.clear();bmDrawdownS.clear()
        var sB=pv.length>0&&isFinite(pv[0])&&pv[0]>0?pv[0]:1.0
        var bB=bv.length>0&&isFinite(bv[0])&&bv[0]>0?bv[0]:1.0
        var pM=-1e18,pm=1e18;for(var i=0;i<pv.length;i++){var sv=isFinite(pv[i])&&sB>0?pv[i]/sB:1.0;equityS.append(i,sv);drawdownS.append(i,isFinite(dd[i])?dd[i]:0);if(sv>pM)pM=sv;if(sv<pm)pm=sv}
        for(var k=0;k<bv.length;k++){var bmv=isFinite(bv[k])&&bB>0?bv[k]/bB:1.0;bmEquityS.append(k,bmv);bmDrawdownS.append(k,isFinite(bdd[k])?bdd[k]:0);if(bmv>pM)pM=bmv;if(bmv<pm)pm=bmv}
        var eN=Math.max(1,Math.max(pv.length,bv.length)-1);eqX.max=eN;ddX.max=eN;retX.max=Math.max(1,ret.length-1)
        eqY.min=pm*0.95;eqY.max=pM*1.05;ddY.min=0;ddY.max=1
        var cum=0,rM=-1e18;for(var j=0;j<ret.length;j++){cum+=ret[j];returnS.append(j,cum);if(cum>rM)rM=cum}
        retY.min=-0.5;retY.max=rM*1.1
        var m={},td=tss.dates||[];for(var di=0;di<td.length;di++)m[td[di]]=di;ddIndexByDate=m
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
                Card{Layout.fillWidth:true;Layout.preferredHeight:45;label:"总交易";value:fn(trades.totalTrades,0);accent:"#64748B"}}}

        Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 250; radius: 8; color: "#1E293B"
            ColumnLayout { anchors.fill: parent; anchors.margins: 8; spacing: 2
                Text{text:"净值曲线";font.pixelSize:11;color:"#94A3B8"}
                ChartView{Layout.fillWidth:true;Layout.fillHeight:true;antialiasing:true;legend.visible:false;backgroundColor:"transparent";plotAreaColor:"transparent"
                    ValueAxis{id:eqX;min:0;labelsColor:"#64748B";gridLineColor:"#1E293B"}
                    ValueAxis{id:eqY;labelsColor:"#94A3B8";gridLineColor:"#1E293B";labelFormat:"%.2f"}
                    LineSeries{id:equityS;axisX:eqX;axisY:eqY;color:"#EF4444";width:2}
                    LineSeries{id:bmEquityS;axisX:eqX;axisY:eqY;color:"#94A3B8";width:1;style:Qt.DashLine}}}}

        RowLayout { Layout.fillWidth: true; Layout.preferredHeight: 200; spacing: 8
            Rectangle { Layout.fillWidth: true; Layout.fillHeight: true; radius: 8; color: "#1E293B"
                ColumnLayout { anchors.fill: parent; anchors.margins: 8; spacing: 2
                    Text{text:"回撤曲线";font.pixelSize:11;color:"#94A3B8"}
                    ChartView{Layout.fillWidth:true;Layout.fillHeight:true;antialiasing:true;legend.visible:false;backgroundColor:"transparent";plotAreaColor:"transparent"
                        ValueAxis{id:ddX;min:0;labelsColor:"#64748B";gridLineColor:"#1E293B"}
                        ValueAxis{id:ddY;labelsColor:"#94A3B8";gridLineColor:"#1E293B";labelFormat:"%.2f"}
                        LineSeries{id:drawdownS;axisX:ddX;axisY:ddY;color:"#F59E0B";width:2}
                        LineSeries{id:bmDrawdownS;axisX:ddX;axisY:ddY;color:"#94A3B8";width:1;style:Qt.DashLine}}}}
            Rectangle { Layout.fillWidth: true; Layout.fillHeight: true; radius: 8; color: "#1E293B"
                ColumnLayout { anchors.fill: parent; anchors.margins: 8; spacing: 2
                    Text{text:"累计收益";font.pixelSize:11;color:"#94A3B8"}
                    ChartView{Layout.fillWidth:true;Layout.fillHeight:true;antialiasing:true;legend.visible:false;backgroundColor:"transparent";plotAreaColor:"transparent"
                        ValueAxis{id:retX;min:0;labelsColor:"#64748B";gridLineColor:"#1E293B"}
                        ValueAxis{id:retY;labelsColor:"#94A3B8";gridLineColor:"#1E293B";labelFormat:"%.2f"}
                        LineSeries{id:returnS;axisX:retX;axisY:retY;color:"#38BDF8";width:2}}}}}

        // 个股统计 + 买卖点图表
        Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 320; radius: 8; color: "#1E293B"
            RowLayout { anchors.fill: parent; anchors.margins: 8; spacing: 12
                ColumnLayout { Layout.fillWidth: true; Layout.fillHeight: true; spacing: 2
                    Text{text:"持仓标的统计";font.pixelSize:11;color:"#94A3B8"}
                    Rectangle { Layout.fillWidth: true; height: 20; color: "transparent"
                        Row { anchors.verticalCenter: parent.verticalCenter
                            Text{width:72;text:"标的";font.pixelSize:9;color:"#64748B";font.weight:Font.Bold}
                            Text{width:44;text:"买";font.pixelSize:9;color:"#64748B";font.weight:Font.Bold}
                            Text{width:44;text:"卖";font.pixelSize:9;color:"#64748B";font.weight:Font.Bold}
                            Text{width:48;text:"胜率";font.pixelSize:9;color:"#64748B";font.weight:Font.Bold}
                            Text{width:72;text:"盈亏";font.pixelSize:9;color:"#64748B";font.weight:Font.Bold}}}
                    ListView { Layout.fillWidth: true; Layout.fillHeight: true; clip: true; model: stockSummary
                        delegate: Rectangle { width: ListView.view.width; height: 22; color: index%2?"transparent":"#0B1220"
                            MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor
                                onClicked: { selSymbol=modelData.symbol; buildSymbolChart() }}
                            Row { anchors.verticalCenter: parent.verticalCenter
                                Text{width:72;text:modelData.symbol;font.pixelSize:9;color:selSymbol===modelData.symbol?"#F59E0B":"#F1F5F9"}
                                Text{width:44;text:modelData.buys;font.pixelSize:9;color:"#94A3B8"}
                                Text{width:44;text:modelData.sells;font.pixelSize:9;color:"#94A3B8"}
                                Text{width:48;text:modelData.winRate;font.pixelSize:9;color:modelData.winRate>=50?"#FCA5A5":"#86EFAC"}
                                Text{width:72;text:fn(modelData.totalPnl,0);font.pixelSize:9;color:modelData.totalPnl>=0?"#FCA5A5":"#86EFAC"}}}}}
                ColumnLayout { Layout.fillWidth: true; Layout.fillHeight: true; spacing: 4
                    RowLayout {
                        Text{text:selSymbol?"买卖点 · "+selSymbol:"← 点击标的查看";font.pixelSize:11;color:"#94A3B8"}
                        Item{Layout.fillWidth:true}
                        Text{text:selPnLTotal>=0?"盈":"亏";font.pixelSize:11;color:selPnLTotal>=0?"#FCA5A5":"#86EFAC"}
                        Text{text:selPnLTotal?fn(selPnLTotal,0):"";font.pixelSize:11;color:selPnLTotal>=0?"#FCA5A5":"#86EFAC"}}
                    ChartView { Layout.fillWidth: true; Layout.fillHeight: true; antialiasing: true; legend.visible: false
                        backgroundColor: "#0B1220"; plotAreaColor: "#0B1220"
                        DateTimeAxis { id: symAxisX; format: "yy/MM"; labelsColor: "#64748B"; gridVisible: false; labelsFont.pixelSize: 8 }
                        ValueAxis { id: symAxisY; labelsColor: "#64748B"; gridLineColor: "#1F2937"; labelsFont.pixelSize: 8 }
                        ScatterSeries { id: buyPoints; color: "#EF4444"; markerSize: 8 }
                        ScatterSeries { id: sellWin; color: "#FCA5A5"; markerSize: 10 }
                        ScatterSeries { id: sellLoss; color: "#86EFAC"; markerSize: 10 }}}}}

        Item { Layout.preferredHeight: 10 }}}

    component Card: Rectangle {
        property string label: ""; property string value: ""; property color accent: "#F1F5F9"
        radius: 6; color: "#0B1220"
        Column { anchors.centerIn: parent; spacing: 1
            Text { anchors.horizontalCenter: parent.horizontalCenter; text: value; font.pixelSize: 16; font.weight: Font.Bold; color: accent }
            Text { anchors.horizontalCenter: parent.horizontalCenter; text: label; font.pixelSize: 9; color: "#64748B" }}}
}
