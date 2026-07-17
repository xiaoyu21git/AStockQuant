import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import AStock.Bridge 1.0

Item {
    id: root
    property var connSvc: TradingConnectionConfigService
    property var connCfg: ({})

    readonly property color bg: "#0F172A"
    readonly property color cardBg: "#1E293B"
    readonly property color border: "#334155"
    readonly property color txt: "#E2E8F0"
    readonly property color sub: "#94A3B8"
    readonly property color accent: "#3B82F6"

    function ld() {
        connCfg = connSvc.loadConfiguration()
        // 已保存的时间字符串回填到各时间选择器 (否则永远显示硬编码默认值)
        applyTime(eodT, connCfg.eodTriggerTime, 15, 0)
        applyTime(syT,  connCfg.syncTriggerTime, 15, 1)
        applyTime(blS,  connCfg.syncBlockStart,  9, 25)
    }
    function applyTime(tp, str, defHour, defMinute) {
        var parts = String(str || "").split(":")
        var h = parseInt(parts[0], 10)
        var m = parseInt(parts[1], 10)
        tp.hour   = (isFinite(h) && h >= 0 && h <= 23) ? h : defHour
        tp.minute = (isFinite(m) && m >= 0 && m <= 59) ? m : defMinute
    }
    function sv() { connSvc.saveConfiguration(connCfg); toast.visible = true; toastTimer.start() }
    function n(v,d) { var x=Number(v); return isFinite(x)?x:(d||0) }
    function s(v,d) { return (v!==undefined&&v!==null&&String(v).length>0)?String(v):(d||"") }

    ScrollView {
        anchors.fill: parent; anchors.margins: 16
        ScrollBar.vertical.policy: ScrollBar.AsNeeded
        contentWidth: availableWidth

        ColumnLayout {
            width: parent.width; spacing: 12

            RowLayout {
                Text { text: "全局设置"; font.pixelSize: 20; font.bold: true; color: txt; Layout.fillWidth: true }
                Button {
                    text: "保存全部"
                    background: Rectangle { color: accent; radius: 6 }
                    contentItem: Text { text: "保存全部"; color: "white"; font.pixelSize: 13; font.bold: true; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter }
                    onClicked: sv()
                }
                Text { id: toast; text: "✓ 已保存"; color: "#10B981"; font.pixelSize: 12; visible: false }
                Timer { id: toastTimer; interval: 2000; onTriggered: toast.visible = false }
            }

            // ── 第一行：交易执行 | 盘前下单窗口 ──
            RowLayout {
                Layout.fillWidth: true; spacing: 12
                Card { title: "交易执行"
                    Layout.fillWidth: true
                    ColumnLayout { spacing: 8
                        RowLayout { spacing: 8
                            Lbl { text: "默认委托类型" }
                            ComboBox {
                                Layout.preferredWidth: 110; model: ["限价","市价"]
                                currentIndex: { var v=s(connCfg.defaultOrderType,"Limit"); return v==="Market"?1:0 }
                                onActivated: connCfg.defaultOrderType = (currentIndex===1?"Market":"Limit")
                            }
                        }
                        RowLayout { spacing: 8
                            Lbl { text: "整手(100股)" }
                            Switch { checked: connCfg.useBoardLot!==false; onToggled: connCfg.useBoardLot = checked }
                        }
                        RowLayout { spacing: 8
                            Lbl { text: "涨跌停容差(%)" }
                            SpinBox { Layout.preferredWidth: 80; from:5; to:30; value: n(connCfg.limitPriceTolerance,9.9); editable:true; onValueChanged: connCfg.limitPriceTolerance=value }
                        }
                    }
                }
                Card { title: "EOD 触发时间"
                    Layout.fillWidth: true
                    RowLayout { spacing: 8
                        Lbl { text: "触发时间" }
                        TimePick { id: eodT; hour:15; minute:0; onTimeChanged: connCfg.eodTriggerTime = eodT.hh+":"+eodT.mm }
                        Text { text: "日频策略每天此时触发评估下单"; color: sub; font.pixelSize: 10 }
                    }
                }
            }

            // ── 第二行：同步调度 | 交易时段 ──
            RowLayout {
                Layout.fillWidth: true; spacing: 12
                Card { title: "同步调度"
                    Layout.fillWidth: true
                    GridLayout { columns: 4; columnSpacing: 16; rowSpacing: 10
                        Lbl { text: "触发时间" }      TimePick { id: syT;  hour:15; minute:1;  onTimeChanged: connCfg.syncTriggerTime = syT.hh+":"+syT.mm }
                        Lbl { text: "覆盖阈值(%)" }   Nf { id: cov;  v: n(connCfg.syncCoverageRatio,0.9)*100;  onC: function(x){ connCfg.syncCoverageRatio = x/100 } }
                        Lbl { text: "封锁开始" }      TimePick { id: blS;  hour:9;  minute:25; onTimeChanged: connCfg.syncBlockStart = blS.hh+":"+blS.mm }
                        Lbl { text: "封锁结束" }      Text { text: "= EOD 触发时间（下单前禁止同步）"; color: sub; font.pixelSize: 11 }
                        Lbl { text: "日线批大小" }    Nf { id: sdbs; v: n(connCfg.syncDailyBatchSize,100);  intOnly:true; onC: function(x){ connCfg.syncDailyBatchSize = Math.round(x) } }
                        Lbl { text: "分钟批大小" }    Nf { id: smbs; v: n(connCfg.syncMinuteBatchSize,50);  intOnly:true; onC: function(x){ connCfg.syncMinuteBatchSize = Math.round(x) } }
                        Lbl { text: "DB写入批大小" }  Nf { id: sdb;  v: n(connCfg.syncDbFlushSize,500);    intOnly:true; onC: function(x){ connCfg.syncDbFlushSize = Math.round(x) } }
                        Lbl { text: "复权重试次数" }  Nf { id: sard; v: n(connCfg.syncAdjRetryCount,2);     intOnly:true; onC: function(x){ connCfg.syncAdjRetryCount = Math.round(x) } }
                    }
                }
                Card { title: "交易时段（只读）"
                    Layout.fillWidth: true
                    GridLayout { columns: 2; columnSpacing: 24; rowSpacing: 4
                        Lbl { text: "连续竞价" }    Text { text: "上午 9:30-11:30    下午 13:00-15:00"; color: txt; font.pixelSize: 12 }
                        Lbl { text: "集合竞价" }    Text { text: "早盘 9:15-9:25    尾盘 14:57-15:00"; color: txt; font.pixelSize: 12 }
                        Lbl { text: "收盘/盘后" }   Text { text: "锁定期 15:00-15:05    盘后定价 15:05-15:30"; color: txt; font.pixelSize: 12 }
                    }
                }
            }
        }
    }

    component Card: Rectangle {
        property string title
        default property alias content: inner.data
        Layout.fillWidth: true; Layout.preferredHeight: inner.implicitHeight + 20
        color: cardBg; radius: 8; border.width: 1; border.color: border
        ColumnLayout {
            id: inner; anchors { left: parent.left; right: parent.right; top: parent.top; margins: 12 }
            spacing: 6
            Text { text: parent.parent.title; font.pixelSize: 13; font.bold: true; color: "#60A5FA" }
            Rectangle { Layout.fillWidth: true; height: 1; color: border }
        }
    }

    component Lbl: Text { color: sub; font.pixelSize: 12; Layout.preferredWidth: 100 }

    component Nf: Rectangle {
        Layout.preferredWidth: 80; Layout.preferredHeight: 28; color: bg; radius: 4; border.width: 1; border.color: border
        property double v: 0; property bool intOnly: false; property var onC: null
        TextInput {
            anchors.fill: parent; anchors.margins: 4; color: txt; font.pixelSize: 12
            text: intOnly ? Math.round(parent.v) : Number(parent.v).toFixed(3)
            onTextChanged: { var x=Number(text); if(isFinite(x)&&parent.onC) parent.onC(x) }
        }
    }

    component TimePick: RowLayout {
        id: tp
        property int hour: 0; property int minute: 0
        property string hh: String(hour).padStart(2,'0')
        property string mm: String(minute).padStart(2,'0')
        signal timeChanged()
        Layout.preferredHeight: 30; spacing: 2
        SpinBox {
            from: 0; to: 23; value: tp.hour; editable: true; Layout.preferredWidth: 64
            onValueChanged: { tp.hour = value; tp.timeChanged() }
        }
        Text { text: ":"; color: root.sub; font.pixelSize: 14; font.bold: true }
        SpinBox {
            from: 0; to: 59; value: tp.minute; editable: true; Layout.preferredWidth: 64
            onValueChanged: { tp.minute = value; tp.timeChanged() }
        }
    }

    Component.onCompleted: ld()
}
