import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import QtCharts 2.15

Item {
    id: page
    anchors.fill: parent
    property string strategyId: ""
    property string strategyName: ""
    property var backtestResult: null
    signal backToWorkbench()

    // 比例小数 → 百分比显示 (0.0677 → "6.77%")
    function fp(v,d){var n=Number(v);return isNaN(n)?"--":(n*100).toFixed(d||2)+"%"}
    function fn(v,d){var n=Number(v);return isNaN(n)?"--":n.toFixed(d||2)}

    property var perf: (backtestResult && backtestResult.performance) ? backtestResult.performance : ({})
    property var trades: (backtestResult && backtestResult.trades) ? backtestResult.trades : ({})
    property var ts: (backtestResult && backtestResult.timeSeries) ? backtestResult.timeSeries : ({})
    property var params: (backtestResult && backtestResult.parameters) ? backtestResult.parameters : ({})
    property var risk: (backtestResult && backtestResult.risk) ? backtestResult.risk : ({})
    property var tradeRows: (backtestResult && backtestResult.tradeLog) ? backtestResult.tradeLog : []
    property var ddIndexByDate: ({})   // 交易日(YYYYMMDD) → timeSeries 下标, 成交行对齐当日回撤

    function fmtDate(v){var s=String(v||"");return s.length===8?s.substr(0,4)+"-"+s.substr(4,2)+"-"+s.substr(6,2):(s||"--")}
    // 卖出收益率 = 已实现盈亏 / 成本基数(卖出金额 − 盈亏)
    function tradeReturnRatio(row){var cost=(row.amount||0)-(row.realizedPnl||0);return cost>0?(row.realizedPnl||0)/cost:NaN}
    function dayDrawdownOf(d){var i=ddIndexByDate[d];return (i!==undefined&&ts.drawdowns)?ts.drawdowns[i]:NaN}

    function rejectionLabel(code) {
        switch(Number(code)) {
            case 1: return "缺少必填字段"; case 2: return "策略未绑定"; case 3: return "策略未激活"
            case 4: return "价格无效"; case 5: return "信号太弱"; case 6: return "持仓快照未就绪"
            case 7: return "非交易时段"; case 8: return "无可卖持仓"; case 9: return "卖出超量"
            case 10: return "订单金额超限"; case 11: return "滑点超限"; case 12: return "日成交额超限"
            case 13: return "止损触发"; case 14: return "止盈触发"
            case 15: return "一级熔断"; case 16: return "二级熔断"; case 17: return "三级熔断"
            case 18: return "最大回撤超限"; case 19: return "集中度超限"; case 20: return "总敞口超限"
            case 21: return "交易暂停"; case 22: return "缺委托数量"
            default: return "未知("+code+")"
        }
    }

    onBacktestResultChanged: {
        if (!backtestResult || !backtestResult.timeSeries) return
        var tss = backtestResult.timeSeries
        var pv=tss.portfolioValues||[]; var dd=tss.drawdowns||[]; var ret=tss.returns||[]
        var bv=tss.benchmarkValues||[]; var bdd=tss.benchmarkDrawdowns||[]
        equityS.clear(); drawdownS.clear(); returnS.clear()
        bmEquityS.clear(); bmDrawdownS.clear()

        // 标准化到同一起点（1.0），首日净值归一
        var stratBase = (pv.length > 0 && isFinite(pv[0]) && pv[0] > 0) ? pv[0] : 1.0
        var bmBase = (bv.length > 0 && isFinite(bv[0]) && bv[0] > 0) ? bv[0] : 1.0

        var pMin=1e18,pMax=-1e18,dMin=0,dMax=-1e18,rMin=1e18,rMax=-1e18
        for(var i=0;i<pv.length;i++){
            var sv = isFinite(pv[i]) && stratBase > 0 ? pv[i] / stratBase : 1.0
            equityS.append(i, sv); drawdownS.append(i, isFinite(dd[i]) ? dd[i] : 0)
            if(sv>pMax)pMax=sv;if(sv<pMin)pMin=sv
            if(dd[i]<dMin)dMin=dd[i];if(dd[i]>dMax)dMax=dd[i]
        }
        // 基准曲线（归一化到同一起点）
        for(var k=0;k<bv.length;k++){
            var bmv = isFinite(bv[k]) && bmBase > 0 ? bv[k] / bmBase : 1.0
            bmEquityS.append(k, bmv); bmDrawdownS.append(k, isFinite(bdd[k]) ? bdd[k] : 0)
            if(bmv>pMax)pMax=bmv;if(bmv<pMin)pMin=bmv
            if(bdd[k]<dMin)dMin=bdd[k];if(bdd[k]>dMax)dMax=bdd[k]
        }
        var eqN=Math.max(1,Math.max(pv.length,bv.length)-1)
        eqX.max=eqN; ddX.max=eqN; retX.max=Math.max(1,ret.length-1)
        eqY.min=pMin*0.95;eqY.max=pMax*1.05;ddY.min=dMin*1.1;ddY.max=dMax>0?dMax*1.1:0
        var cum=0; for(var j=0;j<ret.length;j++){cum+=ret[j];returnS.append(j,cum);if(cum>rMax)rMax=cum;if(cum<rMin)rMin=cum}
        retY.min=rMin*1.1;retY.max=rMax*1.1
        // 交易日 → 序列下标映射 (成交明细行对齐当日回撤)
        var dateIndexMap={}; var tsDates=tss.dates||[]
        for(var di=0;di<tsDates.length;di++) dateIndexMap[tsDates[di]]=di
        ddIndexByDate=dateIndexMap
        console.log("分析页: 策略"+pv.length+"点 基准"+bv.length+"点 回撤"+dd.length+"点 基准首值"+bmBase+" 策略首值"+stratBase)
    }

    Flickable { anchors.fill: parent; contentWidth: parent.width; contentHeight: content.implicitHeight+20; clip: true
        ColumnLayout { id: content; width: parent.width; spacing:10

            RowLayout { Layout.fillWidth: true
                Text{text:"回测分析 · "+(strategyName||strategyId||"");font.pixelSize:16;font.weight:Font.Bold;color:"#F1F5F9"}
                Item{Layout.fillWidth:true}
                Text{text:"基准:";font.pixelSize:11;color:"#94A3B8"}
                ComboBox { id: bmBox; width: 140; font.pixelSize: 11
                    model: ["沪深300","中证500","中证1000","创业板指","上证50","科创50"]
                    // 根据 backtest 参数设置当前基准
                    Component.onCompleted: {
                        var bm = params.benchmarkIndex || "000300.SH"
                        var map = {"000300.SH":0,"000905.SH":1,"000852.SH":2,"399006.SZ":3,"000016.SH":4,"000688.SH":5}
                        currentIndex = map[bm] !== undefined ? map[bm] : 0
                    }
                    onCurrentIndexChanged: {
                        // 切换基准需要重新跑回测 — 提示用户
                        console.log("切换基准到:", currentText, "— 需重新运行回测")
                    }
                    background: Rectangle { radius: 4; color: "#1E293B"; border.color: "#334155" }
                    contentItem: Text { text: parent.displayText; font.pixelSize: 11; color: "#F1F5F9"; verticalAlignment: Text.AlignVCenter; leftPadding: 6 } }
                Item{width:12}
                Rectangle{width:55;height:24;radius:4;color:"#475569";
                    Text{anchors.centerIn:parent;text:"返回";font.pixelSize:10;color:"#F1F5F9"}
                    MouseArea{anchors.fill:parent;onClicked:backToWorkbench()}} }

            // 参数摘要
            Rectangle { Layout.fillWidth: true; visible: Object.keys(params).length>0; radius: 8; color: "#1E293B"
                RowLayout { anchors.fill: parent; anchors.margins: 10; spacing: 12
                    Text{text:"参数: 初始"+(params.initialCapital||"--")+" | "+(params.startDate||"--")+"~"+(params.endDate||"--")+" | 基准"+(params.benchmarkIndex||"--")+" | "+(params.dataFrequency||"--")+" "+params.priceAdjustment;font.pixelSize:10;color:"#64748B"} } }

            // 绩效指标卡片
            Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 200; radius: 8; color: "#1E293B"
                GridLayout { anchors.fill: parent; anchors.margins: 10; columns: 5; columnSpacing: 8; rowSpacing: 6
                    Card{ Layout.fillWidth:true; Layout.preferredHeight:55; label:"总收益";   value:fp(perf.totalReturn);          accent:(perf.totalReturn||0)>=0?"#EF4444":"#10B981" }
                    Card{ Layout.fillWidth:true; Layout.preferredHeight:55; label:"年化收益"; value:fp(perf.annualizedReturn);     accent:(perf.annualizedReturn||0)>=0?"#EF4444":"#10B981" }
                    Card{ Layout.fillWidth:true; Layout.preferredHeight:55; label:"最大回撤"; value:fp(perf.maxDrawdown);          accent:"#F59E0B" }
                    Card{ Layout.fillWidth:true; Layout.preferredHeight:55; label:"夏普比率"; value:fn(perf.sharpeRatio,3);        accent:"#38BDF8" }
                    Card{ Layout.fillWidth:true; Layout.preferredHeight:55; label:"Sortino";  value:fn(perf.sortinoRatio,3);       accent:"#38BDF8" }
                    Card{ Layout.fillWidth:true; Layout.preferredHeight:55; label:"Calmar";   value:fn(perf.calmarRatio,3);        accent:"#38BDF8" }
                    Card{ Layout.fillWidth:true; Layout.preferredHeight:55; label:"胜率";     value:fp(perf.winRate);              accent:"#EF4444" }
                    Card{ Layout.fillWidth:true; Layout.preferredHeight:55; label:"利润因子"; value:fn(perf.profitFactor,2);       accent:"#EF4444" }
                    Card{ Layout.fillWidth:true; Layout.preferredHeight:55; label:"Alpha";    value:fn(perf.alpha,3);             accent:"#F1F5F9" }
                    Card{ Layout.fillWidth:true; Layout.preferredHeight:55; label:"Beta";     value:fn(perf.beta,2);              accent:"#F1F5F9" }
                    Card{ Layout.fillWidth:true; Layout.preferredHeight:55; label:"信息比率"; value:fn(perf.informationRatio,3);   accent:"#F1F5F9" }
                    Card{ Layout.fillWidth:true; Layout.preferredHeight:55; label:"跟踪误差"; value:fn(perf.trackingError,3);      accent:"#F1F5F9" }
                    Card{ Layout.fillWidth:true; Layout.preferredHeight:55; label:"总交易";   value:fn(trades.totalTrades,0);       accent:"#64748B" }
                    Card{ Layout.fillWidth:true; Layout.preferredHeight:55; label:"胜/负";    value:fn(trades.winningTrades,0)+"/"+fn(trades.losingTrades,0); accent:"#64748B" }
                    Card{ Layout.fillWidth:true; Layout.preferredHeight:55; label:"盈亏比";   value:trades.totalLoss&&trades.totalLoss!==0?fn(trades.totalProfit/Math.abs(trades.totalLoss),2):"--"; accent:"#64748B" }
                }
            }

            // 交易详情
            Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 90; radius: 8; color: "#1E293B"
                RowLayout { anchors.fill: parent; anchors.margins: 10; spacing: 14
                    Column { Layout.fillWidth: true; spacing: 2
                        Text{text:"总盈利: "+fn(trades.totalProfit,0);font.pixelSize:12;color:"#EF4444";font.weight:Font.Bold}
                        Text{text:"总亏损: "+fn(trades.totalLoss,0);font.pixelSize:12;color:"#10B981";font.weight:Font.Bold}
                        Text{text:"净值: "+(trades.totalProfit&&trades.totalLoss?fn(trades.totalProfit/Math.abs(trades.totalLoss),2):"--");font.pixelSize:10;color:"#64748B"} }
                    Column { Layout.fillWidth: true; spacing: 2
                        Text{text:"最大盈利: "+fn(trades.largestWin,0);font.pixelSize:12;color:"#EF4444"}
                        Text{text:"最大亏损: "+fn(trades.largestLoss,0);font.pixelSize:12;color:"#10B981"}
                        Text{text:"平均持期: "+fn(trades.averageHoldingPeriodDays,0)+"天";font.pixelSize:10;color:"#64748B"} }
                    Column { Layout.fillWidth: true; spacing: 2
                        Text{text:"总交易: "+fn(trades.totalTrades,0);font.pixelSize:12;color:"#F1F5F9"}
                        Text{text:"胜: "+fn(trades.winningTrades,0)+" | 负: "+fn(trades.losingTrades,0);font.pixelSize:10;color:"#94A3B8"}
                        Text{text:"胜率: "+fp(perf.winRate);font.pixelSize:10;color:"#EF4444"} }
                }
            }

            // 订单拒绝原因
            Rectangle { Layout.fillWidth: true; visible: (risk.totalRejected||0)>0; radius: 8; color: "#1E293B"
                implicitHeight: rejectCol.implicitHeight + 16
                ColumnLayout { id: rejectCol; anchors.fill: parent; anchors.margins: 10; spacing: 4
                    Text{text:"订单拒绝统计 (共 "+(risk.totalRejected||0)+" 笔)";font.pixelSize:11;color:"#94A3B8";font.weight:Font.Bold}
                    GridLayout { columns: 4; columnSpacing: 12; rowSpacing: 2; Layout.fillWidth: true
                        Repeater {
                            model: {
                                var list=[]; var d=risk.rejectionDetails||{}
                                for(var k in d){if(d.hasOwnProperty(k))list.push({code:k,count:d[k]})}
                                list.sort(function(a,b){return b.count-a.count})
                                return list
                            }
                            delegate: RowLayout { spacing: 4
                                Text{text:rejectionLabel(modelData.code);font.pixelSize:10;color:"#94A3B8";Layout.preferredWidth:80}
                                Text{text:modelData.count;font.pixelSize:10;color:"#F59E0B";font.weight:Font.Bold}
                            }
                        }
                    }
                }
            }

            // 净值曲线
            Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 250; radius: 8; color: "#1E293B"
                ColumnLayout { anchors.fill: parent; anchors.margins: 8; spacing: 2
                    Text{text:"净值曲线";font.pixelSize:11;color:"#94A3B8"}
                    ChartView{Layout.fillWidth:true;Layout.fillHeight:true;antialiasing:true;legend.visible:false;backgroundColor:"transparent";plotAreaColor:"transparent"
                        ValueAxis{id:eqX;min:0;labelsColor:"#64748B";gridLineColor:"#1E293B"}
                        ValueAxis{id:eqY;labelsColor:"#94A3B8";gridLineColor:"#1E293B";labelFormat:"%.2f"}
                        LineSeries{id:equityS;axisX:eqX;axisY:eqY;color:"#EF4444";width:2}
                        LineSeries{id:bmEquityS;axisX:eqX;axisY:eqY;color:"#94A3B8";width:1;style:Qt.DashLine}} } }

            // 回撤+收益
            RowLayout { Layout.fillWidth: true; Layout.preferredHeight: 220; spacing: 8
                Rectangle { Layout.fillWidth: true; Layout.fillHeight: true; radius: 8; color: "#1E293B"
                    ColumnLayout { anchors.fill: parent; anchors.margins: 8; spacing: 2
                        Text{text:"回撤曲线";font.pixelSize:11;color:"#94A3B8"}
                        ChartView{Layout.fillWidth:true;Layout.fillHeight:true;antialiasing:true;legend.visible:false;backgroundColor:"transparent";plotAreaColor:"transparent"
                            ValueAxis{id:ddX;min:0;labelsColor:"#64748B";gridLineColor:"#1E293B"}
                            ValueAxis{id:ddY;labelsColor:"#94A3B8";gridLineColor:"#1E293B";labelFormat:"%.2f"}
                            LineSeries{id:drawdownS;axisX:ddX;axisY:ddY;color:"#F59E0B";width:2}
                            LineSeries{id:bmDrawdownS;axisX:ddX;axisY:ddY;color:"#94A3B8";width:1;style:Qt.DashLine}} } }
                Rectangle { Layout.fillWidth: true; Layout.fillHeight: true; radius: 8; color: "#1E293B"
                    ColumnLayout { anchors.fill: parent; anchors.margins: 8; spacing: 2
                        Text{text:"累计收益";font.pixelSize:11;color:"#94A3B8"}
                        ChartView{Layout.fillWidth:true;Layout.fillHeight:true;antialiasing:true;legend.visible:false;backgroundColor:"transparent";plotAreaColor:"transparent"
                            ValueAxis{id:retX;min:0;labelsColor:"#64748B";gridLineColor:"#1E293B"}
                            ValueAxis{id:retY;labelsColor:"#94A3B8";gridLineColor:"#1E293B";labelFormat:"%.2f"}
                            LineSeries{id:returnS;axisX:retX;axisY:retY;color:"#38BDF8";width:2}} } }
            }

            // 成交明细 (逐笔订单: 收益/收益率/当日回撤)
            Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 240; radius: 8; color: "#1E293B"
                visible: tradeRows.length > 0
                ColumnLayout { anchors.fill: parent; anchors.margins: 8; spacing: 2
                    Text{text:"成交明细 ("+tradeRows.length+" 笔)";font.pixelSize:11;color:"#94A3B8"}
                    ListView { Layout.fillWidth: true; Layout.fillHeight: true; clip: true; model: tradeRows
                        header: Rectangle { width: parent.width; height: 20; color: "transparent"
                            Row { anchors.verticalCenter: parent.verticalCenter
                                Text{width:80;text:"日期";font.pixelSize:9;color:"#64748B";font.weight:Font.Bold}
                                Text{width:80;text:"标的";font.pixelSize:9;color:"#64748B";font.weight:Font.Bold}
                                Text{width:40;text:"方向";font.pixelSize:9;color:"#64748B";font.weight:Font.Bold}
                                Text{width:60;text:"数量";font.pixelSize:9;color:"#64748B";font.weight:Font.Bold}
                                Text{width:60;text:"价格";font.pixelSize:9;color:"#64748B";font.weight:Font.Bold}
                                Text{width:80;text:"金额";font.pixelSize:9;color:"#64748B";font.weight:Font.Bold}
                                Text{width:80;text:"收益";font.pixelSize:9;color:"#64748B";font.weight:Font.Bold}
                                Text{width:70;text:"收益率";font.pixelSize:9;color:"#64748B";font.weight:Font.Bold}
                                Text{width:70;text:"当日回撤";font.pixelSize:9;color:"#64748B";font.weight:Font.Bold} } }
                        delegate: Rectangle { width: ListView.view.width; height: 22; color: index%2?"transparent":"#0B1220"
                            Row { anchors.verticalCenter: parent.verticalCenter
                                Text{width:80;text:fmtDate(modelData.date);font.pixelSize:9;color:"#94A3B8"}
                                Text{width:80;text:modelData.symbol||"--";font.pixelSize:9;color:"#F1F5F9"}
                                Text{width:40;text:modelData.isBuy?"买入":"卖出";font.pixelSize:9;color:modelData.isBuy?"#EF4444":"#10B981"}
                                Text{width:60;text:fn(modelData.quantity,0);font.pixelSize:9;color:"#F1F5F9"}
                                Text{width:60;text:fn(modelData.price,2);font.pixelSize:9;color:"#F1F5F9"}
                                Text{width:80;text:fn(modelData.amount,0);font.pixelSize:9;color:"#94A3B8"}
                                Text{width:80;text:modelData.isBuy?"--":fn(modelData.realizedPnl,0);font.pixelSize:9;color:(modelData.realizedPnl||0)>=0?"#FCA5A5":"#86EFAC"}
                                Text{width:70;text:modelData.isBuy?"--":fp(tradeReturnRatio(modelData));font.pixelSize:9;color:(modelData.realizedPnl||0)>=0?"#FCA5A5":"#86EFAC"}
                                Text{width:70;text:fp(dayDrawdownOf(modelData.date));font.pixelSize:9;color:"#F59E0B"} } } } } }

            // 每日明细
            Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 200; radius: 8; color: "#1E293B"
                ListView { anchors.fill: parent; anchors.margins: 8; clip: true; model: ts.dates||[]
                    header: Rectangle { width: parent.width; height: 20; color: "transparent"
                        Row { anchors.verticalCenter: parent.verticalCenter
                            Text{width:90;text:"日期";font.pixelSize:9;color:"#64748B";font.weight:Font.Bold}
                            Text{width:70;text:"净值";font.pixelSize:9;color:"#64748B";font.weight:Font.Bold}
                            Text{width:70;text:"日收益";font.pixelSize:9;color:"#64748B";font.weight:Font.Bold}
                            Text{width:70;text:"回撤";font.pixelSize:9;color:"#64748B";font.weight:Font.Bold} } }
                    delegate: Rectangle { width: ListView.view.width; height: 22; color: index%2?"transparent":"#0B1220"
                        Row { anchors.verticalCenter: parent.verticalCenter
                            Text{width:90;text:modelData||"--";font.pixelSize:9;color:"#94A3B8"}
                            Text{width:70;text:ts.portfolioValues&&ts.portfolioValues[index]?ts.portfolioValues[index].toFixed(4):"--";font.pixelSize:9;color:"#F1F5F9"}
                            Text{width:70;text:ts.returns&&ts.returns[index]?fp(ts.returns[index]):"--";font.pixelSize:9;color:ts.returns&&ts.returns[index]>=0?"#FCA5A5":"#86EFAC"}
                            Text{width:70;text:ts.drawdowns&&ts.drawdowns[index]?fp(ts.drawdowns[index]):"--";font.pixelSize:9;color:"#F59E0B"} } } } }

            Item { Layout.preferredHeight: 10 }
        }
    }

    component Card: Rectangle {
        property string label: ""; property string value: ""; property color accent: "#F1F5F9"
        radius: 6; color: "#0B1220"
        Column { anchors.centerIn: parent; spacing: 1
            Text { anchors.horizontalCenter: parent.horizontalCenter; text: value; font.pixelSize: 16; font.weight: Font.Bold; color: accent }
            Text { anchors.horizontalCenter: parent.horizontalCenter; text: label; font.pixelSize: 9; color: "#64748B" } }
    }
}