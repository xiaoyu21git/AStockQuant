import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import QtCharts 2.15
import AStock.Bridge 1.0
import "../Backtest" as BacktestComponents

Rectangle {
    id: root
    color: "#0F172A"
    property var cleanedDataController: null
    property var strategyViewModel: null
    property string selectedStrategyId: ""
    property string selectedStrategyName: ""
    property int selectedDatasetId: -1
    property string startDateP: "2020-01-01"; property string endDateP: "2025-12-31"
    property string benchmarkIndex: "000300.SH"; property string dataFrequency: "daily"
    property string priceAdjustment: "pre"; property real initialCapital: 1000000
    property real commissionRate: 0.0003; property real minCommission: 5.0
    property real slippageRate: 0.001; property real stampTaxRate: 0.001
    property string executionTiming: "close"; property real volumeLimitPercent: 0.10
    property string rebalanceFrequency: "daily"; property int maxPositionCount: 20
    property real singlePositionWeight: 0.20; property real stopLossPercent: 0.10
    property real takeProfitPercent: 0.30; property string backtestMode: "in_sample"
    property string outSampleStart: "2024-01-01"
    property bool parametersValid: true; property string validationMessage: ""

    property bool isBacktesting: false; property real backtestProgress: 0.0
    property string backtestStatus: ""; property string backtestError: ""
    property var backtestResult: null

    signal parametersChanged(var p); signal startBacktestRequested()
    signal strategySwitched(string s); signal backtestFinished(var r)

    readonly property var bmOpts: [{l:"沪深300",v:"000300.SH"},{l:"中证500",v:"000905.SH"},{l:"中证1000",v:"000852.SH"},{l:"创业板指",v:"399006.SZ"},{l:"上证50",v:"000016.SH"},{l:"科创50",v:"000688.SH"}]
    readonly property var fqOpts: [{l:"日线",v:"daily"},{l:"周线",v:"weekly"},{l:"月线",v:"monthly"}]
    readonly property var adjOpts: [{l:"前复权",v:"pre"},{l:"后复权",v:"post"},{l:"不复权",v:"none"}]
    readonly property var tmOpts: [{l:"当日收盘",v:"close"},{l:"次日开盘",v:"next_open"}]
    readonly property var rbOpts: [{l:"每日",v:"daily"},{l:"每周",v:"weekly"},{l:"每月",v:"monthly"},{l:"每季度",v:"quarterly"}]
    readonly property var mdOpts: [{l:"全样本回测",v:"full"},{l:"样本内训练",v:"in_sample"},{l:"样本外验证",v:"out_sample"}]

    function vp(){var e=[];if(startDateP>=endDateP)e.push("开始<结束");if(initialCapital<=0)e.push("初始>0");parametersValid=e.length===0;validationMessage=parametersValid?"":e.join(";");return parametersValid}
    function ec(){vp();parametersChanged({strategyId:selectedStrategyId,strategyName:selectedStrategyName,startDate:startDateP,endDate:endDateP,benchmarkIndex:benchmarkIndex,dataFrequency:dataFrequency,priceAdjustment:priceAdjustment,initialCapital:initialCapital,commissionRate:commissionRate,minCommission:minCommission,slippageRate:slippageRate,stampTaxRate:stampTaxRate,executionTiming:executionTiming,volumeLimitPercent:volumeLimitPercent,rebalanceFrequency:rebalanceFrequency,maxPositionCount:maxPositionCount,singlePositionWeight:singlePositionWeight,stopLossPercent:stopLossPercent,takeProfitPercent:takeProfitPercent,backtestMode:backtestMode,outSampleStart:outSampleStart,datasetCacheId:selectedDatasetId})}
    function rd(){startDateP="2020-01-01";endDateP="2025-12-31";benchmarkIndex="000300.SH";dataFrequency="daily";priceAdjustment="pre";initialCapital=1000000;commissionRate=0.0003;minCommission=5;slippageRate=0.001;stampTaxRate=0.001;executionTiming="close";volumeLimitPercent=0.10;rebalanceFrequency="daily";maxPositionCount=20;singlePositionWeight=0.20;stopLossPercent=0.10;takeProfitPercent=0.30;backtestMode="in_sample";outSampleStart="2024-01-01";ec()}
    function sc(cb,a,k,v){for(var i=0;i<a.length;i++)if(a[i][k]===v){cb.currentIndex=i;return}}

    function switchStrategy(sid,sname){if(sid){selectedStrategyId=sid;selectedStrategyName=sname||"";ec();strategySwitched(sid)}}

    // ── datasets ──
    ListModel { id: datasetComboModel; ListElement { text:"默认数据源";itemId:-1;dsStart:"";dsEnd:"" } }
    ListModel { id: strategyComboModel }
    function onDatasetIndexChanged(idx){if(idx>=0){selectedDatasetId=datasetComboModel.get(idx).itemId;ec()}}
    function loadDatasets(){if(!cleanedDataController)return;datasetComboModel.clear();datasetComboModel.append({text:"默认数据源",itemId:-1,dsStart:"",dsEnd:""});try{var dss=cleanedDataController.cleanDatasets||[];for(var i=0;i<dss.length;i++){var ds=dss[i];if(ds&&ds.itemId!==undefined){datasetComboModel.append({text:(ds.title||ds.name||"数据集"+i)+" [ID:"+ds.itemId+"]",itemId:ds.itemId,dsStart:ds.startDate||"",dsEnd:ds.endDate||""})}}}catch(e){console.log("load datasets err:",e.message)}}

    function initStrategyList(){if(StrategyBridge){StrategyBridge.initAsync();strategyViewModel=StrategyBridge.listModel;strategyViewModel&&strategyViewModel.count>0&&rebuildStrategyCombo()}}
    function rebuildStrategyCombo(){strategyComboModel.clear();if(strategyViewModel&&strategyViewModel.count>0){for(var i=0;i<strategyViewModel.count;i++){var r=strategyViewModel.getRow(i);strategyComboModel.append({text:r.strategyName||r.name||"未命名",sid:r.strategyId||""})}}else{if(StrategyBridge&&StrategyBridge.list){var L=StrategyBridge.list();if(L&&Array.isArray(L)&&L.length>0){for(var j=0;j<L.length;j++){var it=L[j];strategyComboModel.append({text:it.strategyName||it.name||"未命名",sid:it.strategyId||""})}}}}if(strategyComboModel.count===0)strategyComboModel.append({text:"暂无策略",sid:""})}
    function syncCB(){if(!selectedStrategyId)return;for(var i=0;i<strategyComboModel.count;i++){if(strategyComboModel.get(i).sid===selectedStrategyId){strategyComboBox.currentIndex=i;return}}}

    // ── CTL ──
    StrategyBacktestController { id: backtestController
        onIsRunningChanged: { isBacktesting=backtestController.isRunning }
        onProgressChanged: { backtestProgress=backtestController.progress/100.0 }
        onStatusChanged: { backtestStatus=backtestController.status }
        onBacktestCompleted: function(r){backtestResult=r;isBacktesting=false;backtestError="";backtestFinished(r)}
        onBacktestFailed: function(e){backtestError=e;isBacktesting=false}
        onBacktestCancelled: { isBacktesting=false;backtestStatus="已取消" }
    }
    function doStartBacktest(){
        backtestError="";backtestResult=null
        if(!selectedStrategyId||selectedStrategyId===""){backtestError="请先选择一个策略";return}
        if(!backtestController){backtestError="回测控制器未就绪";return}
        if(!backtestController.initialize()){backtestError="回测控制器初始化失败";return}
        var cfg={strategyId:selectedStrategyId,strategyName:selectedStrategyName,startDate:startDateP,endDate:endDateP,benchmarkIndex:benchmarkIndex,dataFrequency:dataFrequency,priceAdjustment:priceAdjustment,initialCapital:initialCapital,commissionRate:commissionRate,minCommission:minCommission,slippageRate:slippageRate,stampTaxRate:stampTaxRate,executionTiming:executionTiming,volumeLimitPercent:volumeLimitPercent,rebalanceFrequency:rebalanceFrequency,maxPositionCount:maxPositionCount,singlePositionWeight:singlePositionWeight,stopLossPercent:stopLossPercent,takeProfitPercent:takeProfitPercent,backtestMode:backtestMode,outSampleStart:outSampleStart,datasetCacheId:selectedDatasetId,numGroups:10}
        backtestController.runBacktest(selectedStrategyId,cfg);startBacktestRequested()
    }

    Timer { id: refreshTimer; interval:200; repeat:false; onTriggered: rebuildStrategyCombo() }

    onCleanedDataControllerChanged: loadDatasets()
    Component.onCompleted: { initStrategyList(); loadDatasets()
        var sig=StrategyBridge.strategiesChanged; if(sig) sig.connect(function(){rebuildStrategyCombo()})
        refreshTimer.start()
    }

    // ═══ UI ═══
    ScrollView {
        anchors.fill: parent; clip: true
        ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
        ColumnLayout {
            width: parent.width - 40; anchors.left: parent.left; anchors.leftMargin: 20
            spacing: 16; anchors.top: parent.top; anchors.topMargin: 16

            // 标题
            RowLayout { Layout.fillWidth: true
                Text { text:"策略回测参数"; font.pixelSize:18; font.weight:Font.Bold; color:"#F1F5F9" }
                Item { Layout.fillWidth:true }
                Rectangle { width:100; height:32; radius:8; color:"#334155"
                    Text { anchors.centerIn:parent; text:"恢复默认"; font.pixelSize:12; color:"#F1F5F9" }
                    MouseArea { anchors.fill:parent; cursorShape:Qt.PointingHandCursor; onClicked:rd() } }
            }

            // 策略 & 数据集
            RowLayout { Layout.fillWidth:true; spacing:16
                ColumnLayout { spacing:4; Layout.preferredWidth:280
                    Text { text:"选择策略"; font.pixelSize:12; color:"#38BDF8" }
                    ComboBox { id:strategyComboBox; Layout.fillWidth:true; model:strategyComboModel; textRole:"text"
                        currentIndex:{for(var i=0;i<strategyComboModel.count;i++){if(strategyComboModel.get(i).sid===selectedStrategyId)return i}return-1}
                        onCurrentIndexChanged:{if(currentIndex>=0){var it=strategyComboModel.get(currentIndex);switchStrategy(it.sid,it.text)}}
                        background:Rectangle{radius:6;color:"#0F172A";border.color:"#334155";border.width:1}
                        contentItem:Text{text:parent.displayText;font.pixelSize:13;color:"#F1F5F9";verticalAlignment:Text.AlignVCenter;leftPadding:10} } }
                ColumnLayout { spacing:4; Layout.preferredWidth:280
                    Text { text:"缓存数据集"; font.pixelSize:12; color:"#38BDF8" }
                    ComboBox { id:datasetComboBox; Layout.fillWidth:true; model:datasetComboModel; textRole:"text"
                        onCurrentIndexChanged:onDatasetIndexChanged(currentIndex)
                        background:Rectangle{radius:6;color:"#0F172A";border.color:"#334155";border.width:1}
                        contentItem:Text{text:parent.displayText;font.pixelSize:13;color:"#F1F5F9";verticalAlignment:Text.AlignVCenter;leftPadding:10} } }
            }

            // 错误
            Rectangle { visible:!parametersValid; Layout.fillWidth:true; height:36; radius:8; color:"#7F1D1D"
                Text { anchors.fill:parent; anchors.margins:8; text:validationMessage; font.pixelSize:12; color:"#FCA5A5" } }

            // ── SECTION: 日期/基准 ──
            Rectangle { Layout.fillWidth:true; height:130; radius:10; color:"#1E293B"; border.width:1; border.color:"#334155"
                Text { x:16;y:14; text:"📅 日期与基准"; font.pixelSize:14; font.weight:Font.DemiBold; color:"#F1F5F9" }
                RowLayout { x:16;y:46; spacing:12
                    ColumnLayout { spacing:4; Layout.preferredWidth:160
                        Text { text:"开始日期"; font.pixelSize:11; color:"#94A3B8" }
                        TextField { Layout.fillWidth:true; text:startDateP; font.pixelSize:12; color:"#F1F5F9"; onEditingFinished:{startDateP=text;ec()}
                            background:Rectangle{radius:6;color:"#0F172A";border.color:"#334155"} } }
                    ColumnLayout { spacing:4; Layout.preferredWidth:160
                        Text { text:"结束日期"; font.pixelSize:11; color:"#94A3B8" }
                        TextField { Layout.fillWidth:true; text:endDateP; font.pixelSize:12; color:"#F1F5F9"; onEditingFinished:{endDateP=text;ec()}
                            background:Rectangle{radius:6;color:"#0F172A";border.color:"#334155"} } }
                    ColumnLayout { spacing:4; Layout.preferredWidth:180
                        Text { text:"回测模式"; font.pixelSize:11; color:"#94A3B8" }
                        ComboBox { Layout.fillWidth:true; model:mdOpts; textRole:"l"
                            Component.onCompleted:sc(this,mdOpts,"v",backtestMode)
                            onCurrentIndexChanged:{if(currentIndex>=0){backtestMode=mdOpts[currentIndex].v;ec()}}
                            background:Rectangle{radius:6;color:"#0F172A";border.color:"#334155"}
                            contentItem:Text{text:parent.displayText;font.pixelSize:12;color:"#F1F5F9";verticalAlignment:Text.AlignVCenter;leftPadding:8} } }
                    ColumnLayout { spacing:4; Layout.preferredWidth:140
                        Text { text:"基准指数"; font.pixelSize:11; color:"#94A3B8" }
                        ComboBox { Layout.fillWidth:true; model:bmOpts; textRole:"l"
                            Component.onCompleted:sc(this,bmOpts,"v",benchmarkIndex)
                            onCurrentIndexChanged:{if(currentIndex>=0){benchmarkIndex=bmOpts[currentIndex].v;ec()}}
                            background:Rectangle{radius:6;color:"#0F172A";border.color:"#334155"}
                            contentItem:Text{text:parent.displayText;font.pixelSize:12;color:"#F1F5F9";verticalAlignment:Text.AlignVCenter;leftPadding:8} } }
                }
                RowLayout { x:16;y:88; spacing:12
                    ColumnLayout { spacing:4; Layout.preferredWidth:120
                        Text { text:"频率"; font.pixelSize:11; color:"#94A3B8" }
                        ComboBox { Layout.fillWidth:true; model:fqOpts; textRole:"l"
                            Component.onCompleted:sc(this,fqOpts,"v",dataFrequency)
                            onCurrentIndexChanged:{if(currentIndex>=0){dataFrequency=fqOpts[currentIndex].v;ec()}}
                            background:Rectangle{radius:6;color:"#0F172A";border.color:"#334155"}
                            contentItem:Text{text:parent.displayText;font.pixelSize:12;color:"#F1F5F9";verticalAlignment:Text.AlignVCenter;leftPadding:8} } }
                    ColumnLayout { spacing:4; Layout.preferredWidth:120
                        Text { text:"复权"; font.pixelSize:11; color:"#94A3B8" }
                        ComboBox { Layout.fillWidth:true; model:adjOpts; textRole:"l"
                            Component.onCompleted:sc(this,adjOpts,"v",priceAdjustment)
                            onCurrentIndexChanged:{if(currentIndex>=0){priceAdjustment=adjOpts[currentIndex].v;ec()}}
                            background:Rectangle{radius:6;color:"#0F172A";border.color:"#334155"}
                            contentItem:Text{text:parent.displayText;font.pixelSize:12;color:"#F1F5F9";verticalAlignment:Text.AlignVCenter;leftPadding:8} } }
                    ColumnLayout { spacing:4; Layout.preferredWidth:120
                        Text { text:"执行时点"; font.pixelSize:11; color:"#94A3B8" }
                        ComboBox { Layout.fillWidth:true; model:tmOpts; textRole:"l"
                            Component.onCompleted:sc(this,tmOpts,"v",executionTiming)
                            onCurrentIndexChanged:{if(currentIndex>=0){executionTiming=tmOpts[currentIndex].v;ec()}}
                            background:Rectangle{radius:6;color:"#0F172A";border.color:"#334155"}
                            contentItem:Text{text:parent.displayText;font.pixelSize:12;color:"#F1F5F9";verticalAlignment:Text.AlignVCenter;leftPadding:8} } }
                    Item { Layout.fillWidth:true }
                }
            }

            // ── SECTION: 资金 + 成本 ──
            RowLayout { Layout.fillWidth:true; spacing:14
                Rectangle { Layout.fillWidth:true; height:130; radius:10; color:"#1E293B"; border.width:1; border.color:"#334155"
                    Text { x:16;y:14; text:"💰 资金设置"; font.pixelSize:14; font.weight:Font.DemiBold; color:"#F1F5F9" }
                    RowLayout { x:16;y:48; spacing:16
                        ColumnLayout { spacing:4; Layout.preferredWidth:160
                            Text { text:"初始资金(万)"; font.pixelSize:11; color:"#94A3B8" }
                            TextField { Layout.fillWidth:true; text:(initialCapital/10000).toFixed(2); font.pixelSize:13; color:"#F1F5F9"
                                onEditingFinished:{var v=parseFloat(text)*10000;if(!isNaN(v)&&v>=0)initialCapital=v;else text=(initialCapital/10000).toFixed(2);ec()}
                                background:Rectangle{radius:6;color:"#0F172A";border.color:"#334155"} } }
                        ColumnLayout { spacing:4; Layout.preferredWidth:140
                            Text { text:"成交量上限%"; font.pixelSize:11; color:"#94A3B8" }
                            TextField { Layout.fillWidth:true; text:(volumeLimitPercent*100).toFixed(1); font.pixelSize:13; color:"#F1F5F9"
                                onEditingFinished:{var v=parseFloat(text);if(!isNaN(v)&&v>=0&&v<=100)volumeLimitPercent=v/100;else text=(volumeLimitPercent*100).toFixed(1);ec()}
                                background:Rectangle{radius:6;color:"#0F172A";border.color:"#334155"} } }
                        Item { Layout.fillWidth:true }
                    }
                }
                Rectangle { Layout.fillWidth:true; height:130; radius:10; color:"#1E293B"; border.width:1; border.color:"#334155"
                    Text { x:16;y:14; text:"💸 交易成本"; font.pixelSize:14; font.weight:Font.DemiBold; color:"#F1F5F9" }
                    RowLayout { x:16;y:48; spacing:10
                        ColumnLayout { spacing:4; Layout.preferredWidth:90
                            Text { text:"手续费‱"; font.pixelSize:11; color:"#94A3B8" }
                            TextField { Layout.fillWidth:true; text:(commissionRate*10000).toFixed(1); font.pixelSize:13; color:"#F1F5F9"
                                onEditingFinished:{var v=parseFloat(text);if(!isNaN(v)&&v>=0)commissionRate=v/10000;else text=(commissionRate*10000).toFixed(1);ec()}
                                background:Rectangle{radius:6;color:"#0F172A";border.color:"#334155"} } }
                        ColumnLayout { spacing:4; Layout.preferredWidth:80
                            Text { text:"最低"; font.pixelSize:11; color:"#94A3B8" }
                            TextField { Layout.fillWidth:true; text:minCommission.toFixed(2); font.pixelSize:13; color:"#F1F5F9"
                                onEditingFinished:{var v=parseFloat(text);if(!isNaN(v)&&v>=0)minCommission=v;else text=minCommission.toFixed(2);ec()}
                                background:Rectangle{radius:6;color:"#0F172A";border.color:"#334155"} } }
                        ColumnLayout { spacing:4; Layout.preferredWidth:80
                            Text { text:"滑点%"; font.pixelSize:11; color:"#94A3B8" }
                            TextField { Layout.fillWidth:true; text:(slippageRate*100).toFixed(2); font.pixelSize:13; color:"#F1F5F9"
                                onEditingFinished:{var v=parseFloat(text);if(!isNaN(v)&&v>=0)slippageRate=v/100;else text=(slippageRate*100).toFixed(2);ec()}
                                background:Rectangle{radius:6;color:"#0F172A";border.color:"#334155"} } }
                        ColumnLayout { spacing:4; Layout.preferredWidth:80
                            Text { text:"印花税%"; font.pixelSize:11; color:"#94A3B8" }
                            TextField { Layout.fillWidth:true; text:(stampTaxRate*100).toFixed(2); font.pixelSize:13; color:"#F1F5F9"
                                onEditingFinished:{var v=parseFloat(text);if(!isNaN(v)&&v>=0)stampTaxRate=v/100;else text=(stampTaxRate*100).toFixed(2);ec()}
                                background:Rectangle{radius:6;color:"#0F172A";border.color:"#334155"} } }
                        Item { Layout.fillWidth:true }
                    }
                }
            }

            // ── SECTION: 再平衡 + 风控 ──
            RowLayout { Layout.fillWidth:true; spacing:14
                Rectangle { Layout.fillWidth:true; height:120; radius:10; color:"#1E293B"; border.width:1; border.color:"#334155"
                    Text { x:16;y:14; text:"⚖ 再平衡与持仓"; font.pixelSize:14; font.weight:Font.DemiBold; color:"#F1F5F9" }
                    RowLayout { x:16;y:48; spacing:12
                        ColumnLayout { spacing:4; Layout.preferredWidth:130
                            Text { text:"再平衡频率"; font.pixelSize:11; color:"#94A3B8" }
                            ComboBox { Layout.fillWidth:true; model:rbOpts; textRole:"l"
                                Component.onCompleted:sc(this,rbOpts,"v",rebalanceFrequency)
                                onCurrentIndexChanged:{if(currentIndex>=0){rebalanceFrequency=rbOpts[currentIndex].v;ec()}}
                                background:Rectangle{radius:6;color:"#0F172A";border.color:"#334155"}
                                contentItem:Text{text:parent.displayText;font.pixelSize:12;color:"#F1F5F9";verticalAlignment:Text.AlignVCenter;leftPadding:8} } }
                        ColumnLayout { spacing:4; Layout.preferredWidth:100
                            Text { text:"最大持仓"; font.pixelSize:11; color:"#94A3B8" }
                            SpinBox { Layout.fillWidth:true; from:1;to:200;value:maxPositionCount;editable:true
                                onValueChanged:{maxPositionCount=value;ec()}
                                background:Rectangle{radius:6;color:"#0F172A";border.color:"#334155"}
                                contentItem:TextInput{text:parent.value.toString();font.pixelSize:12;color:"#F1F5F9";horizontalAlignment:Text.AlignHCenter;verticalAlignment:TextInput.AlignVCenter} } }
                        ColumnLayout { spacing:4; Layout.preferredWidth:150
                            Text { text:"单票仓位:"+Math.round(singlePositionWeight*100)+"%"; font.pixelSize:11; color:"#94A3B8" }
                            Slider { Layout.fillWidth:true; from:1;to:100;value:singlePositionWeight*100
                                onValueChanged:{singlePositionWeight=value/100;ec()} } }
                        Item { Layout.fillWidth:true }
                    }
                }
                Rectangle { Layout.fillWidth:true; height:120; radius:10; color:"#1E293B"; border.width:1; border.color:"#334155"
                    Text { x:16;y:14; text:"🛡 风控设置"; font.pixelSize:14; font.weight:Font.DemiBold; color:"#F1F5F9" }
                    RowLayout { x:16;y:48; spacing:30
                        ColumnLayout { spacing:4; Layout.preferredWidth:220
                            Text { text:"止损 "+Math.round(stopLossPercent*100)+"%"; font.pixelSize:11; color:"#94A3B8" }
                            Slider { Layout.fillWidth:true; from:1;to:50;value:stopLossPercent*100
                                onValueChanged:{stopLossPercent=value/100;ec()} } }
                        ColumnLayout { spacing:4; Layout.preferredWidth:220
                            Text { text:"止盈 "+Math.round(takeProfitPercent*100)+"%"; font.pixelSize:11; color:"#94A3B8" }
                            Slider { Layout.fillWidth:true; from:5;to:200;value:takeProfitPercent*100
                                onValueChanged:{takeProfitPercent=value/100;ec()} } }
                        Item { Layout.fillWidth:true }
                    }
                }
            }

            // ── BUTTONS ──
            RowLayout { Layout.fillWidth:true; visible:!isBacktesting
                Rectangle { width:180; height:40; radius:8; color:"#3B82F6"
                    Text { anchors.centerIn:parent; text:"▶ 开始回测"; font.pixelSize:14; font.weight:Font.Medium; color:"white" }
                    MouseArea { anchors.fill:parent; cursorShape:Qt.PointingHandCursor; onClicked:{if(vp())doStartBacktest()} } }
                Item { Layout.fillWidth:true }
            }
            RowLayout { Layout.fillWidth:true; visible:isBacktesting; height:34
                Rectangle { Layout.fillWidth:true; height:8; radius:4; color:"#334155"
                    Rectangle { width:Math.max(2,parent.width*Math.min(1,Math.max(0,backtestProgress)));height:8;radius:4;color:"#3B82F6" } }
                Text { text:Math.round(backtestProgress*100)+"%"; font.pixelSize:12; color:"#3B82F6"; Layout.preferredWidth:40 }
                Rectangle { width:60;height:30;radius:6;color:"#475569"; visible:isBacktesting
                    Text { anchors.centerIn:parent;text:"取消";font.pixelSize:12;color:"#F1F5F9" }
                    MouseArea { anchors.fill:parent;cursorShape:Qt.PointingHandCursor;onClicked:{backtestController.cancelBacktest();isBacktesting=false} } }
            }
            Text { visible:isBacktesting&&backtestStatus!==""; text:backtestStatus; font.pixelSize:12; color:"#94A3B8"; Layout.fillWidth:true }

            Rectangle { visible:backtestError!==""; Layout.fillWidth:true; height:40; radius:8; color:"#7F1D1D"; border.width:1; border.color:"#EF4444"
                Text { anchors.fill:parent; anchors.margins:10; text:backtestError; font.pixelSize:12; color:"#FCA5A5" } }

            // ── RESULT ──
            Loader {
                id: resultLoader
                active: root.backtestResult !== null
                visible: active
                Layout.fillWidth: true
                sourceComponent: backtestResultComponent
                onLoaded: { if (item) item.backtestResult = root.backtestResult }
            }
            Component {
                id: backtestResultComponent
                BacktestComponents.BacktestResultPanel { backtestResult: root.backtestResult }
            }
            Connections {
                target: root
                function onBacktestResultChanged() { if (resultLoader.item) resultLoader.item.backtestResult = root.backtestResult }
            }

            Item { Layout.preferredHeight:20 }
        }
    }
}