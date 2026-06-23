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

    function fp(v,d){var n=Number(v);return isNaN(n)?"--":n.toFixed(d||2)+"%"}
    function fn(v,d){var n=Number(v);return isNaN(n)?"--":n.toFixed(d||2)}

    property var perf: (backtestResult && backtestResult.performance) ? backtestResult.performance : ({})
    property var trades: (backtestResult && backtestResult.trades) ? backtestResult.trades : ({})
    property var ts: (backtestResult && backtestResult.timeSeries) ? backtestResult.timeSeries : ({})
    property var params: (backtestResult && backtestResult.parameters) ? backtestResult.parameters : ({})

    onBacktestResultChanged: {
        if (!backtestResult || !backtestResult.timeSeries) return
        var pv=ts.portfolioValues||[]; var dd=ts.drawdowns||[]; var ret=ts.returns||[]
        equityS.clear(); drawdownS.clear(); returnS.clear()
        var peak=1.0; for(var i=0;i<pv.length;i++){equityS.append(i,pv[i]);drawdownS.append(i,dd[i]||0);if(pv[i]>peak)peak=pv[i]}
        var n=Math.max(1,pv.length-1); eqX.max=n; ddX.max=n; retX.max=Math.max(1,ret.length-1)
        var cum=0; for(var j=0;j<ret.length;j++){cum+=ret[j];returnS.append(j,cum)}
    }

    Flickable { anchors.fill: parent; contentWidth: parent.width; contentHeight: content.implicitHeight+20; clip: true
        ColumnLayout { id: content; width: parent.width; spacing:10

            RowLayout { Layout.fillWidth: true
                Text{text:"回测分析 · "+(strategyName||strategyId||"");font.pixelSize:16;font.weight:Font.Bold;color:"#F1F5F9"}
                Item{Layout.fillWidth:true}
                Text{text:"基准:";font.pixelSize:11;color:"#94A3B8"}
                ComboBox { id: bmBox; width: 110; font.pixelSize: 11; currentIndex: 0
                    model: ["全市场","沪深300","中证500","中证1000","创业板指","上证50","科创50"]
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

            // 净值曲线
            Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 250; radius: 8; color: "#1E293B"
                ColumnLayout { anchors.fill: parent; anchors.margins: 8; spacing: 2
                    Text{text:"净值曲线";font.pixelSize:11;color:"#94A3B8"}
                    ChartView{Layout.fillWidth:true;Layout.fillHeight:true;antialiasing:true;legend.visible:false;backgroundColor:"transparent";plotAreaColor:"transparent"
                        ValueAxis{id:eqX;min:0;labelsColor:"#64748B";gridLineColor:"#1E293B"}
                        ValueAxis{id:eqY;labelsColor:"#94A3B8";gridLineColor:"#1E293B";labelFormat:"%.2f"}
                        LineSeries{id:equityS;axisX:eqX;axisY:eqY;color:"#EF4444";width:2}} } }

            // 回撤+收益
            RowLayout { Layout.fillWidth: true; Layout.preferredHeight: 220; spacing: 8
                Rectangle { Layout.fillWidth: true; Layout.fillHeight: true; radius: 8; color: "#1E293B"
                    ColumnLayout { anchors.fill: parent; anchors.margins: 8; spacing: 2
                        Text{text:"回撤曲线";font.pixelSize:11;color:"#94A3B8"}
                        ChartView{Layout.fillWidth:true;Layout.fillHeight:true;antialiasing:true;legend.visible:false;backgroundColor:"transparent";plotAreaColor:"transparent"
                            ValueAxis{id:ddX;min:0;labelsColor:"#64748B";gridLineColor:"#1E293B"}
                            ValueAxis{id:ddY;labelsColor:"#94A3B8";gridLineColor:"#1E293B";labelFormat:"%.2f"}
                            LineSeries{id:drawdownS;axisX:ddX;axisY:ddY;color:"#F59E0B";width:2}} } }
                Rectangle { Layout.fillWidth: true; Layout.fillHeight: true; radius: 8; color: "#1E293B"
                    ColumnLayout { anchors.fill: parent; anchors.margins: 8; spacing: 2
                        Text{text:"累计收益";font.pixelSize:11;color:"#94A3B8"}
                        ChartView{Layout.fillWidth:true;Layout.fillHeight:true;antialiasing:true;legend.visible:false;backgroundColor:"transparent";plotAreaColor:"transparent"
                            ValueAxis{id:retX;min:0;labelsColor:"#64748B";gridLineColor:"#1E293B"}
                            ValueAxis{id:retY;labelsColor:"#94A3B8";gridLineColor:"#1E293B";labelFormat:"%.2f"}
                            LineSeries{id:returnS;axisX:retX;axisY:retY;color:"#38BDF8";width:2}} } }
            }

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