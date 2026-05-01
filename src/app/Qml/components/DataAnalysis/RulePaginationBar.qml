import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15

Item {
    id: root

    property int currentPage: 1
    property int totalPages: 1
    property int totalCount: 0

    signal pageChanged(int page)

    implicitHeight: paginationRow.implicitHeight
    visible: totalPages > 1

    function visiblePages() {
        var pages = []
        var start = Math.max(1, currentPage - 2)
        var end = Math.min(totalPages, currentPage + 2)
        for (var i = start; i <= end; i++) {
            pages.push(i)
        }
        return pages
    }

    RowLayout {
        id: paginationRow
        width: parent.width
        spacing: 8

        Text {
            text: "共 " + totalCount + " 项规则，第 " + currentPage + "/" + totalPages + " 页"
            font.pixelSize: 12
            color: "#94a3b8"
        }

        Item {
            Layout.fillWidth: true
        }

        Row {
            spacing: 4

            Rectangle {
                width: 32
                height: 32
                radius: 6
                color: currentPage === 1 ? "#334155" : "#475569"
                border.width: 1
                border.color: "#64748b"

                Text {
                    anchors.centerIn: parent
                    text: "◀"
                    font.pixelSize: 12
                    color: currentPage === 1 ? "#64748b" : "white"
                }

                MouseArea {
                    anchors.fill: parent
                    enabled: currentPage > 1
                    cursorShape: Qt.PointingHandCursor
                    onClicked: root.pageChanged(currentPage - 1)
                }
            }

            Repeater {
                model: root.visiblePages()

                delegate: Rectangle {
                    width: 32
                    height: 32
                    radius: 6
                    color: modelData === currentPage ? "#0ea5e9" : "#475569"
                    border.width: 1
                    border.color: modelData === currentPage ? "#38bdf8" : "#64748b"

                    Text {
                        anchors.centerIn: parent
                        text: modelData
                        font.pixelSize: 12
                        color: "white"
                        font.bold: modelData === currentPage
                    }

                    MouseArea {
                        anchors.fill: parent
                        cursorShape: Qt.PointingHandCursor
                        onClicked: {
                            if (modelData !== currentPage) {
                                root.pageChanged(modelData)
                            }
                        }
                    }
                }
            }

            Rectangle {
                width: 32
                height: 32
                radius: 6
                color: currentPage === totalPages ? "#334155" : "#475569"
                border.width: 1
                border.color: "#64748b"

                Text {
                    anchors.centerIn: parent
                    text: "▶"
                    font.pixelSize: 12
                    color: currentPage === totalPages ? "#64748b" : "white"
                }

                MouseArea {
                    anchors.fill: parent
                    enabled: currentPage < totalPages
                    cursorShape: Qt.PointingHandCursor
                    onClicked: root.pageChanged(currentPage + 1)
                }
            }
        }
    }
}