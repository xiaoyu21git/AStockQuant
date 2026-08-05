import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import AStock.Bridge 1.0

Item {
    id: root
    anchors.fill: parent

    ListModel { id: cacheModel }   // 全部数据集(含清洗集)
    ListModel { id: rowsA }        // 数据集A 中该 symbol 的行
    ListModel { id: rowsB }        // 数据集B 中该 symbol 的行
    property int countA: 0
    property int countB: 0

    DataFetchController { id: dfc }

    function refreshAll() {
        var infos = dfc.allDataSetInfos()
        cacheModel.clear()
        for (var i = 0; i < infos.length; i++) {
            var f = infos[i]
            cacheModel.append({
                id: f.id,
                label: "#" + f.id + "  " + (f.sourceType === "cleaning" ? "[清洗] " : "[原始] ")
                       + (f.displayName || "") + "  (" + (f.rowCount || 0).toLocaleString() + ")",
                sourceType: f.sourceType || "",
                rowCount: f.rowCount || 0
            })
        }
    }

    function fillRows(dsId, sym, model) {
        model.clear()
        if (dsId <= 0 || sym === "") return 0
        var rows = dfc.loadCacheRowsBySymbol(dsId, sym)
        rows.sort(function(a, b) {
            var da = a.trade_date || "", db = b.trade_date || ""
            return da < db ? -1 : (da > db ? 1 : 0)
        })
        var prev = ""
        for (var i = 0; i < rows.length; i++) {
            var r = rows[i]
            var td = r.trade_date || ""
            model.append({
                trade_date: td,
                dup: (td !== "" && td === prev),
                open: r.open, high: r.high, low: r.low, close: r.close,
                volume: r.volume, pre_adjust_factor: r.pre_adjust_factor, post_adjust_factor: r.post_adjust_factor
            })
            prev = td
        }
        return rows.length
    }

    function doCompare() {
        var idA = comboA.currentIndex >= 0 ? cacheModel.get(comboA.currentIndex).id : -1
        var idB = comboB.currentIndex >= 0 ? cacheModel.get(comboB.currentIndex).id : -1
        var sym = symbolField.text.trim()
        countA = fillRows(idA, sym, rowsA)
        countB = fillRows(idB, sym, rowsB)
    }

    // 可复用数据表(内联组件)
    component DataTable: ColumnLayout {
        id: tableRoot
        property string title: ""
        property var tableModel
        property int total: 0
        spacing: 4
        Text { text: tableRoot.title + "  (" + tableRoot.total + " 行)"; color: "#e2e8f0"; font.pixelSize: 13; font.bold: true }
        Rectangle {
            Layout.fillWidth: true; Layout.fillHeight: true
            color: "#0f172a"; border.width: 1; border.color: "#334155"; radius: 4
            ColumnLayout {
                anchors.fill: parent; anchors.margins: 1; spacing: 0
                // 表头
                Rectangle {
                    Layout.fillWidth: true; height: 26; color: "#1e293b"
                    RowLayout {
                        anchors.fill: parent; anchors.leftMargin: 6; anchors.rightMargin: 6; spacing: 4
                        Text { text: "trade_date"; Layout.preferredWidth: 90; color: "#94a3b8"; font.pixelSize: 11; font.bold: true }
                        Text { text: "open"; Layout.fillWidth: true; color: "#94a3b8"; font.pixelSize: 11; font.bold: true }
                        Text { text: "high"; Layout.fillWidth: true; color: "#94a3b8"; font.pixelSize: 11; font.bold: true }
                        Text { text: "low"; Layout.fillWidth: true; color: "#94a3b8"; font.pixelSize: 11; font.bold: true }
                        Text { text: "close"; Layout.fillWidth: true; color: "#94a3b8"; font.pixelSize: 11; font.bold: true }
                        Text { text: "volume"; Layout.preferredWidth: 100; color: "#94a3b8"; font.pixelSize: 11; font.bold: true }
                    }
                }
                ListView {
                    Layout.fillWidth: true; Layout.fillHeight: true
                    clip: true; model: tableRoot.tableModel
                    ScrollBar.vertical: ScrollBar {}
                    delegate: Rectangle {
                        width: ListView.view.width; height: 22
                        color: model.dup ? "#5b2130" : (index % 2 === 0 ? "#111827" : "#0f172a")
                        RowLayout {
                            anchors.fill: parent; anchors.leftMargin: 6; anchors.rightMargin: 6; spacing: 4
                            Text { text: model.trade_date + (model.dup ? "  ✗重复" : ""); Layout.preferredWidth: 90
                                   color: model.dup ? "#fca5a5" : "#cbd5e1"; font.pixelSize: 11 }
                            Text { text: model.open !== undefined ? Number(model.open).toFixed(2) : "-"; Layout.fillWidth: true; color: "#94a3b8"; font.pixelSize: 11 }
                            Text { text: model.high !== undefined ? Number(model.high).toFixed(2) : "-"; Layout.fillWidth: true; color: "#94a3b8"; font.pixelSize: 11 }
                            Text { text: model.low !== undefined ? Number(model.low).toFixed(2) : "-"; Layout.fillWidth: true; color: "#94a3b8"; font.pixelSize: 11 }
                            Text { text: model.close !== undefined ? Number(model.close).toFixed(2) : "-"; Layout.fillWidth: true; color: "#e2e8f0"; font.pixelSize: 11 }
                            Text { text: model.volume !== undefined ? Number(model.volume).toLocaleString() : "-"; Layout.preferredWidth: 100; color: "#94a3b8"; font.pixelSize: 11 }
                        }
                    }
                }
            }
        }
    }

    ColumnLayout {
        anchors.fill: parent; anchors.margins: 16; spacing: 10

        RowLayout {
            Text { text: "缓存管理 · 清洗前后数据对比"; font.pixelSize: 18; font.bold: true; color: "white"; Layout.fillWidth: true }
            Button {
                text: "刷新数据集"
                onClicked: refreshAll()
                background: Rectangle { color: "#1e3a5f"; radius: 4 }
                contentItem: Text { text: "刷新数据集"; color: "white"; font.pixelSize: 12; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter }
            }
        }
        Rectangle { Layout.fillWidth: true; height: 1; color: "#4b5563" }

        // ── 删除操作行 ──
        RowLayout {
            spacing: 8
            Text { text: "删除:"; color: "#fca5a5"; font.pixelSize: 12; font.bold: true }

            // 删除数据集A
            Button {
                text: "删除数据集A"
                enabled: comboA.currentIndex >= 0
                onClicked: {
                    var entry = cacheModel.get(comboA.currentIndex)
                    if (entry && entry.id > 0) {
                        if (dfc.removeDataSet(entry.id)) {
                            refreshAll()
                            comboA.currentIndex = -1
                        }
                    }
                }
                background: Rectangle { color: parent.enabled ? "#991b1b" : "#374151"; radius: 4 }
                contentItem: Text { text: parent.text; color: parent.enabled ? "#fca5a5" : "#666"; font.pixelSize: 11; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter }
                padding: 6
            }

            // 删除数据集B
            Button {
                text: "删除数据集B"
                enabled: comboB.currentIndex >= 0
                onClicked: {
                    var entry = cacheModel.get(comboB.currentIndex)
                    if (entry && entry.id > 0) {
                        if (dfc.removeDataSet(entry.id)) {
                            refreshAll()
                            comboB.currentIndex = -1
                        }
                    }
                }
                background: Rectangle { color: parent.enabled ? "#991b1b" : "#374151"; radius: 4 }
                contentItem: Text { text: parent.text; color: parent.enabled ? "#fca5a5" : "#666"; font.pixelSize: 11; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter }
                padding: 6
            }

            Rectangle { width: 1; height: 20; color: "#4b5563" }

            // 删除所有原始缓存
            Button {
                text: "清空原始缓存"
                onClicked: {
                    var removed = 0
                    for (var i = cacheModel.count - 1; i >= 0; i--) {
                        var entry = cacheModel.get(i)
                        if (entry.sourceType !== "cleaning" && entry.id > 0) {
                            if (dfc.removeDataSet(entry.id)) removed++
                        }
                    }
                    refreshAll()
                    comboA.currentIndex = -1; comboB.currentIndex = -1
                }
                background: Rectangle { color: "#7f1d1d"; radius: 4 }
                contentItem: Text { text: parent.text; color: "#fca5a5"; font.pixelSize: 11; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter }
                padding: 6
            }

            // 删除所有清洗缓存
            Button {
                text: "清空清洗缓存"
                onClicked: {
                    var removed = 0
                    for (var i = cacheModel.count - 1; i >= 0; i--) {
                        var entry = cacheModel.get(i)
                        if (entry.sourceType === "cleaning" && entry.id > 0) {
                            if (dfc.removeDataSet(entry.id)) removed++
                        }
                    }
                    refreshAll()
                    comboA.currentIndex = -1; comboB.currentIndex = -1
                }
                background: Rectangle { color: "#7f1d1d"; radius: 4 }
                contentItem: Text { text: parent.text; color: "#fca5a5"; font.pixelSize: 11; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter }
                padding: 6
            }

            Item { Layout.fillWidth: true }
        }
        Rectangle { Layout.fillWidth: true; height: 1; color: "#4b5563" }

        // 对比控制行
        RowLayout {
            spacing: 8
            Text { text: "数据集A:"; color: "#94a3b8"; font.pixelSize: 12 }
            ComboBox { id: comboA; Layout.preferredWidth: 320; model: cacheModel; textRole: "label" }
            Text { text: "数据集B:"; color: "#94a3b8"; font.pixelSize: 12 }
            ComboBox { id: comboB; Layout.preferredWidth: 320; model: cacheModel; textRole: "label" }
            Text { text: "股票:"; color: "#94a3b8"; font.pixelSize: 12 }
            TextField {
                id: symbolField; Layout.preferredWidth: 130; placeholderText: "如 000001.SZ"
                color: "white"; text: "000001.SZ"
                background: Rectangle { color: "#374151"; radius: 4; border.width: 1; border.color: "#4b5563" }
                onAccepted: doCompare()
            }
            Button {
                text: "对比"
                onClicked: doCompare()
                background: Rectangle { color: "#7c3aed"; radius: 4 }
                contentItem: Text { text: "对比"; color: "white"; font.pixelSize: 12; font.bold: true; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter }
            }
            Item { Layout.fillWidth: true }
        }

        // 左右两个表
        RowLayout {
            Layout.fillWidth: true; Layout.fillHeight: true; spacing: 12
            DataTable { Layout.fillWidth: true; Layout.fillHeight: true; title: "A"; tableModel: rowsA; total: root.countA }
            DataTable { Layout.fillWidth: true; Layout.fillHeight: true; title: "B"; tableModel: rowsB; total: root.countB }
        }
    }

    Component.onCompleted: refreshAll()
}
