// components/BreadcrumbNavigation.qml
import QtQuick 2.15
import ConsoleUi 1.0 as Constants

Rectangle {
    id: breadcrumbNav
    implicitHeight: 24
    color: "transparent"
    
    Row {
        spacing: 8
        
        BreadcrumbItem {
            icon: "\uf015" // fa-home
            text: "首页"
            active: false
        }
        
        BreadcrumbSeparator {}
        
        BreadcrumbItem {
            icon: ""
            text: "策略交易"
            active: false
        }
        
        BreadcrumbSeparator {}
        
        BreadcrumbItem {
            icon: ""
            text: "策略库管理"
            active: true
        }
    }
}