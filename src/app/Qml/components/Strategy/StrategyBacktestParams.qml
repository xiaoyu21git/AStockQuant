// StrategyBacktestParams.qml
import QtQuick 2.15
import QtQuick.Controls 2.15
import AStock.Bridge 1.0
import "../" as Shared

Rectangle {
    id: root
    color: "#0F172A"

    property var strategyViewModel: null
    property string selectedStrategyId: ""
    property string selectedStrategyName: ""
    property string startDateP: "2020-01-01"
    property string endDateP: "2025-12-31"
    property string benchmarkIndex: "000300.SH"
    property string dataFrequency: "daily"
    property string priceAdjustment: "pre"
    property real initialCapital: 1000000
    property real commissionRate: 0.0003
    property real minCommission: 5.0
    property real slippageRate: 0.001
    property real stampTaxRate: 0.001
    property string executionTiming: "close"
    property real volumeLimitPercent: 0.10
    property string rebalanceFrequency: "daily"
    property int maxPositionCount: 20
    property real singlePositionWeight: 0.20
    property real stopLossPercent: 0.10
    property real takeProfitPercent: 0.30
    property string backtestMode: "in_sample"
    property string outSampleStart: "2024-01-01"
    property bool parametersValid: true
    property string validationMessage: ""

    signal parametersChanged(var params)
    signal startBacktestRequested()
    signal strategySwitched(string strategyId)

    readonly property var bmOpts: [{l:"沪深300",v:"000300.SH"},{l:"中证500",v:"000905.SH"},{l:"中证1000",v:"000852.SH"},{l:"创业板指",v:"399006.SZ"},{l:"上证50",v:"000016.SH"},{l:"科创50",v:"000688.SH"}]
    readonly property var fqOpts: [{l:"日线",v:"daily"},{l:"周线",v:"weekly"},{l:"月线",v:"monthly"}]
    readonly property var adjOpts: [{l:"前复权",v:"pre"},{l:"后复权",v:"post"},{l:"不复权",v:"none"}]
    readonly property var tmOpts: [{l:"当日收盘",v:"close"},{l:"次日开盘",v:"next_open"}]
    readonly property var rbOpts: [{l:"每日",v:"daily"},{l:"每周",v:"weekly"},{l:"每月",v:"monthly"},{l:"每季度",v:"quarterly"}]
    readonly property var mdOpts: [{l:"全样本回测",v:"full"},{l:"样本内训练",v:"in_sample"},{l:"样本外验证",v:"out_sample"}]

    function vp() { var e=[]; if(startDateP>=endDateP)e.push("开始<结束"); if(initialCapital<=0)e.push("初始>0");
        parametersValid=e.length===0; validationMessage=parametersValid?"":e.join(";"); return parametersValid }
    function ec() { vp(); parametersChanged({strategyId:selectedStrategyId,strategyName:selectedStrategyName,startDate:startDateP,endDate:endDateP,benchmarkIndex:benchmarkIndex,dataFrequency:dataFrequency,priceAdjustment:priceAdjustment,initialCapital:initialCapital,commissionRate:commissionRate,minCommission:minCommission,slippageRate:slippageRate,stampTaxRate:stampTaxRate,executionTiming:executionTiming,volumeLimitPercent:volumeLimitPercent,rebalanceFrequency:rebalanceFrequency,maxPositionCount:maxPositionCount,singlePositionWeight:singlePositionWeight,stopLossPercent:stopLossPercent,takeProfitPercent:takeProfitPercent,backtestMode:backtestMode,outSampleStart:outSampleStart}) }
    function rd() { startDateP="2020-01-01";endDateP="2025-12-31";benchmarkIndex="000300.SH";dataFrequency="daily";priceAdjustment="pre";initialCapital=1000000;commissionRate=0.0003;minCommission=5;slippageRate=0.001;stampTaxRate=0.001;executionTiming="close";volumeLimitPercent=0.10;rebalanceFrequency="daily";maxPositionCount=20;singlePositionWeight=0.20;stopLossPercent=0.10;takeProfitPercent=0.30;backtestMode="in_sample";outSampleStart="2024-01-01";ec() }
    function sc(cb,a,k,v){ for(var i=0;i<a.length;i++)if(a[i][k]===v){cb.currentIndex=i;return} }

    // ---- 策略列表 ----
    function initStrategyList() {
        if (!StrategyBridge) return
        StrategyBridge.initAsync()
        strategyViewModel = StrategyBridge.listModel
        rebuildStrategyCombo()
    }
    function rebuildStrategyCombo() {
        strategyComboModel.clear()
        if (!strategyViewModel) return
        for (var i = 0; i < strategyViewModel.count; i++) {
            var r = strategyViewModel.getRow(i)
            strategyComboModel.append({ text: r.strategyName || r.name || "未命名", id: r.strategyId || "" })
        }
    }
    function switchStrategy(sid, sname) {
        if (sid) { root.selectedStrategyId = sid; root.selectedStrategyName = sname || ""; root.ec(); root.strategySwitched(sid) }
    }
    ListModel { id: strategyComboModel }

    Flickable {
        anchors.fill: parent
        contentWidth: width
        contentHeight: mainCol.implicitHeight + 40
        clip: true
        boundsBehavior: Flickable.StopAtBounds

        Column {
            id: mainCol
            width: Math.min(parent.width - 40, 1100)
            anchors.horizontalCenter: parent.horizontalCenter
            y: 20
            spacing: 16

            // === 标题 ===
            Rectangle { width: parent.width; height: 36; color: "transparent"
                Text { text: "策略回测参数"; font.pixelSize: 18; font.weight: Font.Bold; color: "#F1F5F9"; anchors.verticalCenter: parent.verticalCenter }
                Rectangle { anchors.right: parent.right; anchors.verticalCenter: parent.verticalCenter; width: 120; height: 34; radius: 8; color: "#334155"; border.width: 1; border.color: "#475569"
                    Text { anchors.centerIn: parent; text: "恢复默认"; font.pixelSize: 13; color: "#F1F5F9" }
                    MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: root.rd() } } }

            // === 策略选择栏 ===
            Rectangle { width: parent.width; height: 56; radius: 10; color: "#0B1427"; border.width: 1; border.color: "#1E40AF"
                Row { anchors.verticalCenter: parent.verticalCenter; anchors.left: parent.left; anchors.leftMargin: 16; spacing: 10
                    Text { text: "📋 选择策略:"; font.pixelSize: 14; font.weight: Font.Medium; color: "#38BDF8"; anchors.verticalCenter: parent.verticalCenter }
                    ComboBox {
                        id: strategyComboBox; width: 320; height: 36; model: strategyComboModel; textRole: "text"; font.pixelSize: 13
                        currentIndex: {
                            for (var i=0;i<strategyComboModel.count;i++){if(strategyComboModel.get(i).id===root.selectedStrategyId)return i}
                            return -1
                        }
                        onCurrentIndexChanged: {
                            if (currentIndex>=0){var item=strategyComboModel.get(currentIndex);root.switchStrategy(item.id,item.text)}
                        }
                        background: Rectangle { radius: 6; color: "#0F172A"; border.color: "#334155"; border.width: 1 }
                        contentItem: Text { text: parent.displayText; font.pixelSize: 13; color: "#F1F5F9"; verticalAlignment: Text.AlignVCenter; leftPadding: 10 }
                    }
                }
            }

            // === 错误 ===
            Rectangle { width: parent.width; height: visible ? 44 : 0; visible: root.validationMessage !== ""; radius: 8; color: "#7F1D1D"; border.width: 1; border.color: "#EF4444"
                Text { anchors.fill: parent; anchors.margins: 10; text: root.validationMessage; font.pixelSize: 13; color: "#FCA5A5" } }

            // ============= SECTION 1: 回测周期 =============
            Rectangle { width: parent.width; height: 130; radius: 10; color: "#1E293B"; border.width: 1; border.color: "#334155"
                Text { x: 16; y: 14; text: "📅 回测周期与基准"; font.pixelSize: 15; font.weight: Font.DemiBold; color: "#F1F5F9" }
                Row { x: 16; y: 44; spacing: 12
                    Column { spacing: 4
                        Text { text: "开始日期"; font.pixelSize: 12; color: "#94A3B8" }
                        Shared.DatePicker { selectedDate: root.startDateP; onDateChanged: function(d) { root.startDateP = d; root.ec() } } }
                    Column { spacing: 4
                        Text { text: "结束日期"; font.pixelSize: 12; color: "#94A3B8" }
                        Shared.DatePicker { selectedDate: root.endDateP; onDateChanged: function(d) { root.endDateP = d; root.ec() } } }
                    Column { spacing: 4
                        Text { text: "回测模式"; font.pixelSize: 12; color: "#94A3B8" }
                        ComboBox { id: cbMode; width: 160; height: 34; model: root.mdOpts; textRole: "l"; font.pixelSize: 13
                            Component.onCompleted: root.sc(cbMode,root.mdOpts,"v",root.backtestMode)
                            onCurrentIndexChanged: { if(currentIndex>=0){root.backtestMode=root.mdOpts[currentIndex].v;root.ec()} }
                            background: Rectangle { radius: 6; color: "#0F172A"; border.color: "#334155"; border.width: 1 }
                            contentItem: Text { text: parent.displayText; font.pixelSize: 13; color: "#F1F5F9"; verticalAlignment: Text.AlignVCenter; leftPadding: 10 } } }
                    Column { spacing: 4
                        Text { text: "样本外起始"; font.pixelSize: 12; color: root.backtestMode==="out_sample"?"#94A3B8":"#64748B" }
                        Rectangle { width: 170; height: 34; radius: 6; color: root.backtestMode==="out_sample"?"#0F172A":"#0C1320"; border.width: 1; border.color: "#334155"
                            TextInput { anchors.fill: parent; anchors.leftMargin: 10; anchors.rightMargin: 10; enabled: root.backtestMode==="out_sample"
                                text: root.outSampleStart; font.pixelSize: 13; color: enabled?"#F1F5F9":"#64748B"; verticalAlignment: TextInput.AlignVCenter } } }
                    Column { spacing: 4
                        Text { text: "业绩基准"; font.pixelSize: 12; color: "#94A3B8" }
                        ComboBox { id: cbBM; width: 160; height: 34; model: root.bmOpts; textRole: "l"; font.pixelSize: 13
                            Component.onCompleted: root.sc(cbBM,root.bmOpts,"v",root.benchmarkIndex)
                            onCurrentIndexChanged: { if(currentIndex>=0){root.benchmarkIndex=root.bmOpts[currentIndex].v;root.ec()} }
                            background: Rectangle { radius: 6; color: "#0F172A"; border.color: "#334155"; border.width: 1 }
                            contentItem: Text { text: parent.displayText; font.pixelSize: 13; color: "#F1F5F9"; verticalAlignment: Text.AlignVCenter; leftPadding: 10 } } }
                }
            }

            // ============= SECTION 2: 两列 =============
            Row { spacing: 14
                Rectangle { width: (mainCol.width - 14) / 2; height: 130; radius: 10; color: "#1E293B"; border.width: 1; border.color: "#334155"
                    Text { x: 16; y: 14; text: "🗄 数据与执行"; font.pixelSize: 15; font.weight: Font.DemiBold; color: "#F1F5F9" }
                    Row { x: 16; y: 44; spacing: 12
                        Column { spacing: 4; width: (parent.parent.width - 44 - 24) / 3
                            Text { text: "数据频率"; font.pixelSize: 12; color: "#94A3B8" }
                            ComboBox { id: cbF; width: parent.width; height: 34; model: root.fqOpts; textRole: "l"; font.pixelSize: 13
                                Component.onCompleted: root.sc(cbF,root.fqOpts,"v",root.dataFrequency)
                                onCurrentIndexChanged: { if(currentIndex>=0){root.dataFrequency=root.fqOpts[currentIndex].v;root.ec()} }
                                background: Rectangle { radius: 6; color: "#0F172A"; border.color: "#334155"; border.width: 1 }
                                contentItem: Text { text: parent.displayText; font.pixelSize: 13; color: "#F1F5F9"; verticalAlignment: Text.AlignVCenter; leftPadding: 10 } } }
                        Column { spacing: 4; width: (parent.parent.width - 44 - 24) / 3
                            Text { text: "复权方式"; font.pixelSize: 12; color: "#94A3B8" }
                            ComboBox { id: cbA; width: parent.width; height: 34; model: root.adjOpts; textRole: "l"; font.pixelSize: 13
                                Component.onCompleted: root.sc(cbA,root.adjOpts,"v",root.priceAdjustment)
                                onCurrentIndexChanged: { if(currentIndex>=0){root.priceAdjustment=root.adjOpts[currentIndex].v;root.ec()} }
                                background: Rectangle { radius: 6; color: "#0F172A"; border.color: "#334155"; border.width: 1 }
                                contentItem: Text { text: parent.displayText; font.pixelSize: 13; color: "#F1F5F9"; verticalAlignment: Text.AlignVCenter; leftPadding: 10 } } }
                        Column { spacing: 4; width: (parent.parent.width - 44 - 24) / 3
                            Text { text: "执行时点"; font.pixelSize: 12; color: "#94A3B8" }
                            ComboBox { id: cbE; width: parent.width; height: 34; model: root.tmOpts; textRole: "l"; font.pixelSize: 13
                                Component.onCompleted: root.sc(cbE,root.tmOpts,"v",root.executionTiming)
                                onCurrentIndexChanged: { if(currentIndex>=0){root.executionTiming=root.tmOpts[currentIndex].v;root.ec()} }
                                background: Rectangle { radius: 6; color: "#0F172A"; border.color: "#334155"; border.width: 1 }
                                contentItem: Text { text: parent.displayText; font.pixelSize: 13; color: "#F1F5F9"; verticalAlignment: Text.AlignVCenter; leftPadding: 10 } } }
                    }
                }
                Rectangle { width: (mainCol.width - 14) / 2; height: 130; radius: 10; color: "#1E293B"; border.width: 1; border.color: "#334155"
                    Text { x: 16; y: 14; text: "📊 再平衡与持仓"; font.pixelSize: 15; font.weight: Font.DemiBold; color: "#F1F5F9" }
                    Row { x: 16; y: 44; spacing: 12
                        Column { spacing: 4; width: (parent.parent.width - 44 - 24) / 3
                            Text { text: "再平衡频率"; font.pixelSize: 12; color: "#94A3B8" }
                            ComboBox { id: cbR; width: parent.width; height: 34; model: root.rbOpts; textRole: "l"; font.pixelSize: 13
                                Component.onCompleted: root.sc(cbR,root.rbOpts,"v",root.rebalanceFrequency)
                                onCurrentIndexChanged: { if(currentIndex>=0){root.rebalanceFrequency=root.rbOpts[currentIndex].v;root.ec()} }
                                background: Rectangle { radius: 6; color: "#0F172A"; border.color: "#334155"; border.width: 1 }
                                contentItem: Text { text: parent.displayText; font.pixelSize: 13; color: "#F1F5F9"; verticalAlignment: Text.AlignVCenter; leftPadding: 10 } } }
                        Column { spacing: 4; width: 110
                            Text { text: "最大持仓数"; font.pixelSize: 12; color: "#94A3B8" }
                            SpinBox { width: parent.width; height: 34; from: 1; to: 200; value: root.maxPositionCount; editable: true
                                contentItem: TextInput { text: parent.value.toString(); font.pixelSize: 13; color: "#F1F5F9"; horizontalAlignment: Text.AlignHCenter; verticalAlignment: TextInput.AlignVCenter }
                                background: Rectangle { radius: 6; color: "#0F172A"; border.color: "#334155"; border.width: 1 }
                                onValueChanged: { root.maxPositionCount = value; root.ec() } } }
                        Column { spacing: 4; width: 140
                            Text { text: "单票仓位上限%"; font.pixelSize: 12; color: "#94A3B8" }
                            Row { spacing: 6
                                Slider { id: ws; width: 90; from: 1; to: 100; value: root.singlePositionWeight * 100
                                    onValueChanged: { root.singlePositionWeight = value / 100; root.ec() } }
                                Text { text: Math.round(ws.value) + "%"; font.pixelSize: 13; color: "#3B82F6"; anchors.verticalCenter: ws.verticalCenter } } }
                    }
                }
            }

            // ============= SECTION 3: 两列 =============
            Row { spacing: 14
                Rectangle { width: (mainCol.width - 14) / 2; height: 120; radius: 10; color: "#1E293B"; border.width: 1; border.color: "#334155"
                    Text { x: 16; y: 14; text: "💰 资金设置"; font.pixelSize: 15; font.weight: Font.DemiBold; color: "#F1F5F9" }
                    Row { x: 16; y: 44; spacing: 16
                        Column { spacing: 4; width: 180
                            Text { text: "初始资金(万)"; font.pixelSize: 12; color: "#94A3B8" }
                            Rectangle { width: parent.width; height: 34; radius: 6; color: "#0F172A"; border.width: 1; border.color: "#334155"
                                TextInput { anchors.fill: parent; anchors.leftMargin: 10; anchors.rightMargin: 10
                                    text: (root.initialCapital / 10000).toFixed(2); font.pixelSize: 13; color: "#F1F5F9"; verticalAlignment: TextInput.AlignVCenter
                                    validator: DoubleValidator { bottom: 0 }
                                    onEditingFinished: { var v = parseFloat(text) * 10000; if (!isNaN(v) && v >= 0) { root.initialCapital = v } else { text = (root.initialCapital / 10000).toFixed(2) }; root.ec() } } } }
                        Column { spacing: 4; width: 140
                            Text { text: "成交量上限%"; font.pixelSize: 12; color: "#94A3B8" }
                            Rectangle { width: parent.width; height: 34; radius: 6; color: "#0F172A"; border.width: 1; border.color: "#334155"
                                TextInput { anchors.fill: parent; anchors.leftMargin: 10; anchors.rightMargin: 10
                                    text: (root.volumeLimitPercent * 100).toFixed(1); font.pixelSize: 13; color: "#F1F5F9"; verticalAlignment: TextInput.AlignVCenter
                                    validator: DoubleValidator { bottom: 0; top: 100 }
                                    onEditingFinished: { var v = parseFloat(text); if (!isNaN(v) && v >= 0 && v <= 100) { root.volumeLimitPercent = v / 100 } else { text = (root.volumeLimitPercent * 100).toFixed(1) }; root.ec() } } } }
                    }
                }
                Rectangle { width: (mainCol.width - 14) / 2; height: 120; radius: 10; color: "#1E293B"; border.width: 1; border.color: "#334155"
                    Text { x: 16; y: 14; text: "💸 交易成本"; font.pixelSize: 15; font.weight: Font.DemiBold; color: "#F1F5F9" }
                    Row { x: 16; y: 44; spacing: 10
                        Column { spacing: 4; width: 100
                            Text { text: "手续费(‱)"; font.pixelSize: 12; color: "#94A3B8" }
                            Rectangle { width: parent.width; height: 34; radius: 6; color: "#0F172A"; border.width: 1; border.color: "#334155"
                                TextInput { anchors.fill: parent; anchors.leftMargin: 8; anchors.rightMargin: 8
                                    text: (root.commissionRate * 10000).toFixed(1); font.pixelSize: 13; color: "#F1F5F9"; verticalAlignment: TextInput.AlignVCenter
                                    onEditingFinished: { var v = parseFloat(text); if (!isNaN(v) && v >= 0) { root.commissionRate = v / 10000 } else { text = (root.commissionRate * 10000).toFixed(1) }; root.ec() } } } }
                        Column { spacing: 4; width: 90
                            Text { text: "最低手续费"; font.pixelSize: 12; color: "#94A3B8" }
                            Rectangle { width: parent.width; height: 34; radius: 6; color: "#0F172A"; border.width: 1; border.color: "#334155"
                                TextInput { anchors.fill: parent; anchors.leftMargin: 8; anchors.rightMargin: 8
                                    text: root.minCommission.toFixed(2); font.pixelSize: 13; color: "#F1F5F9"; verticalAlignment: TextInput.AlignVCenter
                                    validator: DoubleValidator { bottom: 0 }
                                    onEditingFinished: { var v = parseFloat(text); if (!isNaN(v) && v >= 0) { root.minCommission = v } else { text = root.minCommission.toFixed(2) }; root.ec() } } } }
                        Column { spacing: 4; width: 90
                            Text { text: "滑点率%"; font.pixelSize: 12; color: "#94A3B8" }
                            Rectangle { width: parent.width; height: 34; radius: 6; color: "#0F172A"; border.width: 1; border.color: "#334155"
                                TextInput { anchors.fill: parent; anchors.leftMargin: 8; anchors.rightMargin: 8
                                    text: (root.slippageRate * 100).toFixed(2); font.pixelSize: 13; color: "#F1F5F9"; verticalAlignment: TextInput.AlignVCenter
                                    onEditingFinished: { var v = parseFloat(text); if (!isNaN(v) && v >= 0) { root.slippageRate = v / 100 } else { text = (root.slippageRate * 100).toFixed(2) }; root.ec() } } } }
                        Column { spacing: 4; width: 90
                            Text { text: "印花税率%"; font.pixelSize: 12; color: "#94A3B8" }
                            Rectangle { width: parent.width; height: 34; radius: 6; color: "#0F172A"; border.width: 1; border.color: "#334155"
                                TextInput { anchors.fill: parent; anchors.leftMargin: 8; anchors.rightMargin: 8
                                    text: (root.stampTaxRate * 100).toFixed(2); font.pixelSize: 13; color: "#F1F5F9"; verticalAlignment: TextInput.AlignVCenter
                                    onEditingFinished: { var v = parseFloat(text); if (!isNaN(v) && v >= 0) { root.stampTaxRate = v / 100 } else { text = (root.stampTaxRate * 100).toFixed(2) }; root.ec() } } } }
                    }
                }
            }

            // ============= SECTION 4: 风控 =============
            Rectangle { width: parent.width; height: 120; radius: 10; color: "#1E293B"; border.width: 1; border.color: "#334155"
                Text { x: 16; y: 14; text: "🛡 风控设置"; font.pixelSize: 15; font.weight: Font.DemiBold; color: "#F1F5F9" }
                Row { x: 16; y: 44; spacing: 40
                    Column { spacing: 4; width: 320
                        Text { text: "止损比例%"; font.pixelSize: 12; color: "#94A3B8" }
                        Row { spacing: 8
                            Slider { id: sl; width: 240; from: 1; to: 50; value: root.stopLossPercent * 100
                                onValueChanged: { root.stopLossPercent = value / 100; root.ec() } }
                            Text { text: Math.round(sl.value) + "%"; font.pixelSize: 14; color: "#EF4444"; anchors.verticalCenter: sl.verticalCenter } } }
                    Column { spacing: 4; width: 320
                        Text { text: "止盈比例%"; font.pixelSize: 12; color: "#94A3B8" }
                        Row { spacing: 8
                            Slider { id: tp; width: 240; from: 5; to: 200; value: root.takeProfitPercent * 100
                                onValueChanged: { root.takeProfitPercent = value / 100; root.ec() } }
                            Text { text: Math.round(tp.value) + "%"; font.pixelSize: 14; color: "#10B981"; anchors.verticalCenter: tp.verticalCenter } } }
                }
            }

            // === 按钮 ===
            Rectangle { width: 200; height: 44; radius: 8; color: "#3B82F6"; anchors.right: parent.right
                Text { anchors.centerIn: parent; text: "▶ 开始回测"; font.pixelSize: 15; font.weight: Font.Medium; color: "white" }
                MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: { if (root.vp()) root.startBacktestRequested() } } }

            Item { width: 1; height: 20 }
        }
    }

    Component.onCompleted: { root.initStrategyList() }
}