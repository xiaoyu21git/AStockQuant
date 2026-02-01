import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Item {
    id: root
    width: 280
    height: 260

    // 当前选中的日期
    property date selectedDate: new Date()

    // 内部当前显示的年月
    property int currentYear: selectedDate.getFullYear()
    property int currentMonth: selectedDate.getMonth()    // 0-11

    signal dateChosen(date d)

    function daysInMonth(y, m) {
        // m: 0-11
        return new Date(y, m + 1, 0).getDate();
    }

    function firstDayOffset() {
        // 一个月第一天是星期几（0=周日）
        return new Date(currentYear, currentMonth, 1).getDay();
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 8
        spacing: 4

        RowLayout {
            Layout.fillWidth: true

            Button {
                text: "<"
                width: 32
                onClicked: {
                    if (root.currentMonth === 0) {
                        root.currentMonth = 11;
                        root.currentYear -= 1;
                    } else {
                        root.currentMonth -= 1;
                    }
                }
            }

            Label {
                Layout.fillWidth: true
                horizontalAlignment: Text.AlignHCenter
                font.bold: true
                text: root.currentYear + "-" + (root.currentMonth + 1).toString().padStart(2, "0")
            }

            Button {
                text: ">"
                width: 32
                onClicked: {
                    if (root.currentMonth === 11) {
                        root.currentMonth = 0;
                        root.currentYear += 1;
                    } else {
                        root.currentMonth += 1;
                    }
                }
            }
        }

        // 星期标题行
        RowLayout {
            Layout.fillWidth: true

            Repeater {
                model: ["日", "一", "二", "三", "四", "五", "六"]
                delegate: Label {
                    text: modelData
                    horizontalAlignment: Text.AlignHCenter
                    Layout.alignment: Qt.AlignHCenter
                    Layout.fillWidth: true
                }
            }
        }

        // 日期网格
        Grid {
            id: dateGrid
            columns: 7
            rowSpacing: 4
            columnSpacing: 4
            Layout.alignment: Qt.AlignHCenter | Qt.AlignTop

            Repeater {
                model: 42    // 最多 6 行
                delegate: DayCell {
                    readonly property int offset: root.firstDayOffset()
                    readonly property int dayNum: index - offset + 1

                    enabled: dayNum >= 1 && dayNum <= root.daysInMonth(root.currentYear, root.currentMonth)
                    visible: enabled

                    date: new Date(root.currentYear, root.currentMonth, dayNum)

                    isToday: {
                        var today = new Date()
                        return date.getFullYear() === today.getFullYear()
                               && date.getMonth() === today.getMonth()
                               && date.getDate() === today.getDate()
                    }

                    isSelected: {
                        return date.getFullYear() === root.selectedDate.getFullYear()
                               && date.getMonth() === root.selectedDate.getMonth()
                               && date.getDate() === root.selectedDate.getDate()
                    }

                    isCurrentMonth: true
                    isWeekend: (index % 7 === 0) || (index % 7 === 6)

                    onClicked: {
                        root.selectedDate = date
                        root.dateChosen(root.selectedDate)
                    }
                }
            }
        }
    }
}