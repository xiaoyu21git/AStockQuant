import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import AStock.Engine 1.0
Calendar {
    id: calendar
    
    Component.onCompleted: {
        // 导航方法
        calendar.showNextMonth()      // 下一月
        calendar.showPreviousMonth()  // 上一月
        calendar.showNextYear()       // 下一年
        calendar.showPreviousYear()   // 上一年
        
        // 跳转到特定日期
        calendar.showDate(new Date(2023, 5, 15))
        
        // 重置到今天
        calendar.selectedDate = new Date()
    }
}