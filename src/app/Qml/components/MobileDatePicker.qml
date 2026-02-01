import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Item {
    id: root
    width: 240
    height: 180

    // 选中的日期
    property date selectedDate: new Date()
    property int startYear: 2000
    property int endYear: 2030

    signal dateChanged(date d)

    function daysInMonth(y, m) { // m: 0-11
        return new Date(y, m + 1, 0).getDate()
    }

    function syncFromSelected() {
        var y = selectedDate.getFullYear()
        var m = selectedDate.getMonth() // 0-11
        var d = selectedDate.getDate()

        if (y < startYear) y = startYear
        if (y > endYear) y = endYear

        yearView.currentIndex = y - startYear
        monthView.currentIndex = m

        var maxD = daysInMonth(y, m)
        if (d > maxD) d = maxD
        dayView.currentIndex = d - 1
    }

    function syncToSelected() {
        var y = startYear + yearView.currentIndex
        var m = monthView.currentIndex       // 0-11
        var maxD = daysInMonth(y, m)
        if (dayView.currentIndex > maxD - 1)
            dayView.currentIndex = maxD - 1
        var d = dayView.currentIndex + 1
        selectedDate = new Date(y, m, d)
        dateChanged(selectedDate)
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 8
        spacing: 4

        RowLayout {
            Layout.fillWidth: true

            Label {
                text: "年"
                Layout.alignment: Qt.AlignHCenter
                Layout.fillWidth: true
            }
            Label {
                text: "月"
                Layout.alignment: Qt.AlignHCenter
                Layout.fillWidth: true
            }
            Label {
                text: "日"
                Layout.alignment: Qt.AlignHCenter
                Layout.fillWidth: true
            }
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 4

            ListView {
                id: yearView
                Layout.fillWidth: true
                Layout.fillHeight: true
                model: endYear - startYear + 1
                clip: true
                boundsBehavior: Flickable.StopAtBounds
                snapMode: ListView.SnapOneItem
                highlightRangeMode: ListView.StrictlyEnforceRange
                preferredHighlightBegin: height / 2 - 14
                preferredHighlightEnd: height / 2 + 14

                delegate: Label {
                    required property int index
                    horizontalAlignment: Text.AlignHCenter
                    width: ListView.view ? ListView.view.width : implicitWidth
                    text: (startYear + index).toString()
                }

                onCurrentIndexChanged: root.syncToSelected()
            }

            ListView {
                id: monthView
                Layout.fillWidth: true
                Layout.fillHeight: true
                model: 12
                clip: true
                boundsBehavior: Flickable.StopAtBounds
                snapMode: ListView.SnapOneItem
                highlightRangeMode: ListView.StrictlyEnforceRange
                preferredHighlightBegin: height / 2 - 14
                preferredHighlightEnd: height / 2 + 14

                delegate: Label {
                    required property int index
                    horizontalAlignment: Text.AlignHCenter
                    width: ListView.view ? ListView.view.width : implicitWidth
                    text: (index + 1).toString().padStart(2, "0")
                }

                onCurrentIndexChanged: root.syncToSelected()
            }

            ListView {
                id: dayView
                Layout.fillWidth: true
                Layout.fillHeight: true
                model: root.daysInMonth(startYear + yearView.currentIndex, monthView.currentIndex)
                clip: true
                boundsBehavior: Flickable.StopAtBounds
                snapMode: ListView.SnapOneItem
                highlightRangeMode: ListView.StrictlyEnforceRange
                preferredHighlightBegin: height / 2 - 14
                preferredHighlightEnd: height / 2 + 14

                delegate: Label {
                    required property int index
                    horizontalAlignment: Text.AlignHCenter
                    width: ListView.view ? ListView.view.width : implicitWidth
                    text: (index + 1).toString().padStart(2, "0")
                }

                onCurrentIndexChanged: root.syncToSelected()
            }
        }
    }

    Component.onCompleted: {
        syncFromSelected()
    }
}
