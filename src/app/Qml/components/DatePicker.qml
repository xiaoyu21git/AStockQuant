import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import Qt5Compat.GraphicalEffects 
// 日期选择器组件
Item {
    id: root
    width: 200
    height: 40
    
    // 属性
    property alias selectedDate: dateField.text
    property string placeholder: "选择日期"
    property bool required: false
    property string validationError: ""
    property bool isValid: true
    property var minDate: null
    property var maxDate: null
    property bool restrictWeekends: false
    property string weekendAdjustment: "previous"
    property bool syncingTextValue: false
    
    // 信号
    signal dateChanged(string date)
    signal dateSelected(var dateObject)
    
    // 获取日期对象
    function getDate() {
        if (!dateField.text) return null
        var parts = dateField.text.split('-')
        if (parts.length !== 3) return null
        return new Date(parts[0], parts[1] - 1, parts[2])
    }

    function isWeekend(date) {
        if (!date) return false
        var dayOfWeek = date.getDay()
        return dayOfWeek === 0 || dayOfWeek === 6
    }

    function normalizeDate(date) {
        if (!date) return null

        var normalized = new Date(date.getFullYear(), date.getMonth(), date.getDate())
        if (!restrictWeekends || weekendAdjustment === "none") {
            return normalized
        }

        var direction = weekendAdjustment === "next" ? 1 : -1
        while (isWeekend(normalized)) {
            normalized.setDate(normalized.getDate() + direction)
        }
        return normalized
    }

    function applyDate(date, emitSignals) {
        var normalized = normalizeDate(date)
        if (!normalized) return

        syncingTextValue = true
        dateField.text = formatDate(normalized)
        syncingTextValue = false
        calendar.goToDate(normalized)
        validate()

        if (emitSignals) {
            root.dateSelected(normalized)
            root.dateChanged(dateField.text)
        }
    }
    
    // 设置日期
    function setDate(year, month, day) {
        var dateStr = year + '-' + 
                     (month < 10 ? '0' + month : month) + '-' + 
                     (day < 10 ? '0' + day : day)
        dateField.text = dateStr
    }
    
    // 设置为今天
    function setToday() {
        var today = new Date()
        root.setDate(today.getFullYear(), today.getMonth() + 1, today.getDate())
    }
    
    // 验证日期
    function validate() {
        if (required && !dateField.text) {
            validationError = "请选择日期"
            isValid = false
            return false
        }
        
        if (!dateField.text) {
            validationError = ""
            isValid = true
            return true
        }
        
        // 验证格式
        var dateRegex = /^\d{4}-\d{2}-\d{2}$/
        if (!dateRegex.test(dateField.text)) {
            validationError = "日期格式应为YYYY-MM-DD"
            isValid = false
            return false
        }
        
        // 验证日期有效性
        var parts = dateField.text.split('-')
        var year = parseInt(parts[0])
        var month = parseInt(parts[1])
        var day = parseInt(parts[2])
        
        if (month < 1 || month > 12) {
            validationError = "月份必须在1-12之间"
            isValid = false
            return false
        }
        
        var daysInMonth = new Date(year, month, 0).getDate()
        if (day < 1 || day > daysInMonth) {
            validationError = "日期超出范围"
            isValid = false
            return false
        }
        
        var dateObj = new Date(year, month - 1, day)

        if (restrictWeekends && weekendAdjustment === "none" && isWeekend(dateObj)) {
            validationError = "当前日期控件不支持周末日期"
            isValid = false
            return false
        }
        
        // 验证最小日期
        if (minDate && dateObj < minDate) {
            validationError = "日期不能早于" + formatDate(minDate)
            isValid = false
            return false
        }
        
        // 验证最大日期
        if (maxDate && dateObj > maxDate) {
            validationError = "日期不能晚于" + formatDate(maxDate)
            isValid = false
            return false
        }
        
        validationError = ""
        isValid = true
        return true
    }
    
    // 格式化日期
    function formatDate(date) {
        var year = date.getFullYear()
        var month = date.getMonth() + 1
        var day = date.getDate()
        
        month = month < 10 ? '0' + month : month
        day = day < 10 ? '0' + day : day
        
        return year + '-' + month + '-' + day
    }
    
    // 主控件
    Rectangle {
        id: datePickerBox
        anchors.fill: parent
        radius: 6
        border.width: 1
        border.color: root.validationError ? "#ef4444" : 
                     (calendarPopup.visible || dateField.activeFocus) ? "#3b82f6" : "#d1d5db"
        color: "white"
        
        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: 8
            anchors.rightMargin: 8
            spacing: 4
            
            // 日期输入框
            TextField {
                id: dateField
                Layout.fillWidth: true
                placeholderText: root.placeholder
                verticalAlignment: Text.AlignVCenter
                font.pixelSize: 13
                color: "#1f2937"
                background: Rectangle { color: "transparent" }
                selectByMouse: true
                
                onTextChanged: {
                    if (root.syncingTextValue) {
                        return
                    }

                    root.validate()
                    if (root.isValid) {
                        var currentDate = root.getDate()
                        if (currentDate) {
                            var normalizedDate = root.normalizeDate(currentDate)
                            var normalizedText = root.formatDate(normalizedDate)
                            if (normalizedText !== text) {
                                root.syncingTextValue = true
                                text = normalizedText
                                root.syncingTextValue = false
                            }
                            calendar.goToDate(normalizedDate)
                        }
                        root.dateChanged(text)
                    }
                }
                
                MouseArea {
                    anchors.fill: parent
                    cursorShape: Qt.PointingHandCursor
                    onClicked: calendarPopup.open()
                }
            }
            
            // 日历按钮
            Rectangle {
                width: 28
                height: 28
                radius: 4
                color: calendarBtn.containsMouse ? "#f3f4f6" : "transparent"
                
                Text {
                    text: "📅"
                    font.pixelSize: 14
                    anchors.centerIn: parent
                }
                
                MouseArea {
                    id: calendarBtn
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: calendarPopup.open()
                }
            }
        }
        
        // 错误提示
        Text {
            visible: root.validationError
            text: root.validationError
            color: "#ef4444"
            font.pixelSize: 10
            anchors {
                left: parent.left
                right: parent.right
                top: parent.bottom
                topMargin: 4
            }
        }
    }
    
    // 日历弹窗
    Popup {
        id: calendarPopup
        width: 320
        height: 380
        modal: true
        focus: true
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
        
        x: Math.max(0, Math.min(root.width - width, (root.width - width) / 2))
        y: root.height + 4
        
        background: Rectangle {
            radius: 8
            color: "white"
            border.width: 1
            border.color: "#e5e7eb"
            layer.enabled: true
            layer.effect: DropShadow {
                transparentBorder: true
                radius: 12
                spread: 0.1
                color: "#30000000"
            }
        }
        
        contentItem: ColumnLayout {
            spacing: 0
            
            // 日历标题栏
            Rectangle {
                Layout.fillWidth: true
                height: 48
                color: "#1a2980"
                radius: 8
                
                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: 16
                    anchors.rightMargin: 16
                    
                    Text {
                        text: "选择日期"
                        color: "white"
                        font.pixelSize: 14
                        font.bold: true
                    }
                    
                    Item { Layout.fillWidth: true }
                    
                    // 快速操作按钮
                    Row {
                        spacing: 8
                        
                        Button {
                            text: "今天"
                            height: 28
                            width: 48
                            font.pixelSize: 11
                            onClicked: {
                                calendar.setToday()
                                root.applyDate(new Date(), true)
                                calendarPopup.close()
                            }
                            
                            background: Rectangle {
                                radius: 4
                                color: parent.down ? "#2d3748" : 
                                       parent.hovered ? "#4a5568" : "#ffffff20"
                                border.width: 1
                                border.color: "#ffffff40"
                            }
                            
                            contentItem: Text {
                                text: parent.text
                                color: "white"
                                font.pixelSize: 11
                                horizontalAlignment: Text.AlignHCenter
                                verticalAlignment: Text.AlignVCenter
                            }
                        }
                        
                        Button {
                            text: "清空"
                            height: 28
                            width: 48
                            font.pixelSize: 11
                            onClicked: {
                                dateField.text = ""
                                calendarPopup.close()
                            }
                            
                            background: Rectangle {
                                radius: 4
                                color: parent.down ? "#2d3748" : 
                                       parent.hovered ? "#4a5568" : "#ffffff20"
                                border.width: 1
                                border.color: "#ffffff40"
                            }
                            
                            contentItem: Text {
                                text: parent.text
                                color: "white"
                                font.pixelSize: 11
                                horizontalAlignment: Text.AlignHCenter
                                verticalAlignment: Text.AlignVCenter
                            }
                        }
                        
                        Rectangle {
                            width: 28
                            height: 28
                            radius: 14
                            color: "transparent"
                            
                            MouseArea {
                                anchors.fill: parent
                                hoverEnabled: true
                                cursorShape: Qt.PointingHandCursor
                                onClicked: calendarPopup.close()
                                
                                Text {
                                    text: "×"
                                    color: "white"
                                    font.pixelSize: 18
                                    anchors.centerIn: parent
                                }
                                
                                Rectangle {
                                    anchors.fill: parent
                                    radius: 14
                                    color: parent.containsMouse ? "#ffffff20" : "transparent"
                                }
                            }
                        }
                    }
                }
            }
            
            // 年份和月份选择
            RowLayout {
                Layout.fillWidth: true
                Layout.topMargin: 12
                Layout.leftMargin: 16
                Layout.rightMargin: 16
                spacing: 8
                
                // 年份选择器
                Rectangle {
                    Layout.fillWidth: true
                    height: 36
                    radius: 6
                    border.width: 1
                    border.color: "#d1d5db"
                    
                    RowLayout {
                        anchors.fill: parent
                        spacing: 0
                        
                        // 上一年按钮
                        Rectangle {
                            width: 32
                            height: 36
                            color: prevYearBtn.containsMouse ? "#f3f4f6" : "transparent"
                            radius: 6
                            
                            Text {
                                text: "◀"
                                color: "#4b5563"
                                font.pixelSize: 12
                                anchors.centerIn: parent
                            }
                            
                            MouseArea {
                                id: prevYearBtn
                                anchors.fill: parent
                                hoverEnabled: true
                                cursorShape: Qt.PointingHandCursor
                                onClicked: calendar.previousYear()
                            }
                        }
                        
                        // 年份显示
                        Text {
                            text: calendar.currentYear + "年"
                            color: "#1f2937"
                            font.pixelSize: 14
                            font.bold: true
                            Layout.fillWidth: true
                            horizontalAlignment: Text.AlignHCenter
                        }
                        
                        // 下一年按钮
                        Rectangle {
                            width: 32
                            height: 36
                            color: nextYearBtn.containsMouse ? "#f3f4f6" : "transparent"
                            radius: 6
                            
                            Text {
                                text: "▶"
                                color: "#4b5563"
                                font.pixelSize: 12
                                anchors.centerIn: parent
                            }
                            
                            MouseArea {
                                id: nextYearBtn
                                anchors.fill: parent
                                hoverEnabled: true
                                cursorShape: Qt.PointingHandCursor
                                onClicked: calendar.nextYear()
                            }
                        }
                    }
                }
                
                // 月份选择器
                Rectangle {
                    Layout.fillWidth: true
                    height: 36
                    radius: 6
                    border.width: 1
                    border.color: "#d1d5db"
                    
                    RowLayout {
                        anchors.fill: parent
                        spacing: 0
                        
                        // 上一月按钮
                        Rectangle {
                            width: 32
                            height: 36
                            color: prevMonthBtn.containsMouse ? "#f3f4f6" : "transparent"
                            radius: 6
                            
                            Text {
                                text: "◀"
                                color: "#4b5563"
                                font.pixelSize: 12
                                anchors.centerIn: parent
                            }
                            
                            MouseArea {
                                id: prevMonthBtn
                                anchors.fill: parent
                                hoverEnabled: true
                                cursorShape: Qt.PointingHandCursor
                                onClicked: calendar.previousMonth()
                            }
                        }
                        
                        // 月份显示
                        Text {
                            text: calendar.currentMonth + "月"
                            color: "#1f2937"
                            font.pixelSize: 14
                            font.bold: true
                            Layout.fillWidth: true
                            horizontalAlignment: Text.AlignHCenter
                        }
                        
                        // 下一月按钮
                        Rectangle {
                            width: 32
                            height: 36
                            color: nextMonthBtn.containsMouse ? "#f3f4f6" : "transparent"
                            radius: 6
                            
                            Text {
                                text: "▶"
                                color: "#4b5563"
                                font.pixelSize: 12
                                anchors.centerIn: parent
                            }
                            
                            MouseArea {
                                id: nextMonthBtn
                                anchors.fill: parent
                                hoverEnabled: true
                                cursorShape: Qt.PointingHandCursor
                                onClicked: calendar.nextMonth()
                            }
                        }
                    }
                }
            }
            
            // 星期标题行
            GridLayout {
                Layout.fillWidth: true
                Layout.topMargin: 8
                Layout.leftMargin: 16
                Layout.rightMargin: 16
                columns: 7
                
                Repeater {
                    model: ["日", "一", "二", "三", "四", "五", "六"]
                    
                    Text {
                        text: modelData
                        color: index === 0 || index === 6 ? "#ef4444" : "#6b7280"
                        font.pixelSize: 12
                        font.bold: true
                        Layout.fillWidth: true
                        horizontalAlignment: Text.AlignHCenter
                    }
                }
            }
            
            // 日期网格
            GridLayout {
                id: dateGrid
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.topMargin: 4
                Layout.leftMargin: 16
                Layout.rightMargin: 16
                Layout.bottomMargin: 16
                columns: 7
                
                Repeater {
                    id: dateRepeater
                    model: calendar.daysInMonth + calendar.firstDayOfMonth
                    
                    Rectangle {
                        property int cellIndex: index
                        property bool isPlaceholder: cellIndex < calendar.firstDayOfMonth
                        property int dayNumber: cellIndex - calendar.firstDayOfMonth + 1
                        property var currentDate: isPlaceholder ? null : calendar.getDate(dayNumber)
                        property bool isWeekendDate: currentDate ? calendar.isWeekend(currentDate) : false

                        Layout.fillWidth: true
                        Layout.preferredHeight: 36
                        radius: 6
                        
                        // 背景色
                        color: {
                            if (isPlaceholder) {
                                return "transparent"
                            }

                            // 当天
                            if (calendar.isToday(currentDate)) {
                                return calendar.isSelected(dayNumber) ? "#3b82f6" : "#f3f4f6"
                            }

                            // 选中日期
                            if (calendar.isSelected(dayNumber)) {
                                return "#3b82f6"
                            }

                            // 周末
                            if (root.restrictWeekends && isWeekendDate) {
                                return "#f3f4f6"
                            }
                            return isWeekendDate ? "#f9fafb" : "white"
                        }
                        
                        // 边框
                        border.width: {
                            if (isPlaceholder) return 0
                            return calendar.isSelected(dayNumber) ? 2 : 0
                        }
                        border.color: "#3b82f6"
                        
                        // 日期文本
                        Text {
                            anchors.centerIn: parent
                            text: isPlaceholder ? "" : dayNumber
                            color: {
                                if (isPlaceholder) {
                                    return "transparent"
                                }

                                // 当天
                                if (calendar.isToday(currentDate)) {
                                    return calendar.isSelected(dayNumber) ? "white" : "#3b82f6"
                                }

                                // 选中日期
                                if (calendar.isSelected(dayNumber)) {
                                    return "white"
                                }

                                // 周末
                                if (root.restrictWeekends && isWeekendDate) {
                                    return "#9ca3af"
                                }
                                return isWeekendDate ? "#ef4444" : "#1f2937"
                            }
                            font.pixelSize: 13
                            font.bold: !isPlaceholder && calendar.isSelected(dayNumber)
                        }
                        
                        MouseArea {
                            anchors.fill: parent
                            enabled: !isPlaceholder && !(root.restrictWeekends && isWeekendDate)
                            cursorShape: enabled ? Qt.PointingHandCursor : Qt.ForbiddenCursor
                            onClicked: {
                                var selectedDate = currentDate
                                root.applyDate(selectedDate, true)
                                
                                calendarPopup.close()
                            }
                        }
                    }
                }
            }
        }
    }
    
    // 日历逻辑对象
    QtObject {
        id: calendar
        
        property int currentYear: new Date().getFullYear()
        property int currentMonth: new Date().getMonth() + 1
        property int selectedDay: new Date().getDate()
        
        // 计算当前月份的天数
        property int daysInMonth: new Date(currentYear, currentMonth, 0).getDate()
        
        // 计算当前月份第一天是星期几 (0=周日, 6=周六)
        property int firstDayOfMonth: new Date(currentYear, currentMonth - 1, 1).getDay()
        
        // 跳转到指定日期
        function goToDate(date) {
            currentYear = date.getFullYear()
            currentMonth = date.getMonth() + 1
            selectedDay = date.getDate()
            
            // 更新输入框
            dateField.text = root.formatDate(date)
        }
        
        // 获取日期对象
        function getDate(day) {
            return new Date(currentYear, currentMonth - 1, day)
        }

        function isWeekend(date) {
            return root.isWeekend(date)
        }
        
        // 检查是否是今天
        function isToday(date) {
            var today = new Date()
            return date.getFullYear() === today.getFullYear() &&
                   date.getMonth() === today.getMonth() &&
                   date.getDate() === today.getDate()
        }
        
        // 检查是否被选中
        function isSelected(day) {
            return selectedDay === day
        }
        
        // 选择日期
        function selectDate(day) {
            selectedDay = day
        }
        
        // 设置为今天
        function setToday() {
            var today = new Date()
            goToDate(root.normalizeDate(today))
        }
        
        // 上一年
        function previousYear() {
            currentYear--
        }
        
        // 下一年
        function nextYear() {
            currentYear++
        }
        
        // 上一月
        function previousMonth() {
            if (currentMonth === 1) {
                currentMonth = 12
                currentYear--
            } else {
                currentMonth--
            }
            
            // 确保选中日期不会超出新月份的范围
            var maxDays = new Date(currentYear, currentMonth, 0).getDate()
            if (selectedDay > maxDays) {
                selectedDay = maxDays
            }
        }
        
        // 下一月
        function nextMonth() {
            if (currentMonth === 12) {
                currentMonth = 1
                currentYear++
            } else {
                currentMonth++
            }
            
            // 确保选中日期不会超出新月份的范围
            var maxDays = new Date(currentYear, currentMonth, 0).getDate()
            if (selectedDay > maxDays) {
                selectedDay = maxDays
            }
        }
    }
    
    // 初始化
    Component.onCompleted: {
        var initialDate = root.getDate()
        if (initialDate) {
            root.applyDate(initialDate, false)
        } else {
            root.applyDate(new Date(), false)
        }
    }
}