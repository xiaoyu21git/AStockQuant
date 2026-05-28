// ModeTitleBar.qml
// 模式标题栏组件 - 用于FactorWorkbench的顶部导航
import QtQuick 2.15
import QtQuick.Layouts 1.15
import QtQuick.Controls 2.15

/**
 * 模式标题栏组件
 * 显示当前模式标题，提供模式切换按钮
 */
Rectangle {
    id: root
    
    // ============ 属性 ============
    
    property string currentMode: "home"
    property bool showBackButton: currentMode !== "home"
    property var modeOptions: ["library", "debug", "analyze", "backtest"]
    property var modeTitleMap: ({})
    property var modeSubtitleMap: ({})
    property int modeButtonMinWidth: 80

    default property alias trailingActions: actionSlot.data
    
    signal modeSelected(string mode)
    signal backClicked()
    
    // ============ UI ============
    
    implicitHeight: 80
    color: "#1E293B"
    border.width: 0
    
    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: 24
        anchors.rightMargin: 24
        spacing: 16
        
        // 返回按钮
        Rectangle {
            Layout.preferredWidth: 36
            Layout.preferredHeight: 36
            radius: 8
            color: "#1E293B"
            visible: showBackButton
            
            Text {
                anchors.centerIn: parent
                text: "←"
                font.pixelSize: 14
                color: "#F1F5F9"
            }
            
            MouseArea {
                anchors.fill: parent
                cursorShape: Qt.PointingHandCursor
                onClicked: root.backClicked()
            }
        }
        
        // 标题区域
        ColumnLayout {
            spacing: 4
            
            Text {
                text: resolveModeTitle(currentMode)
                font.pixelSize: 20
                font.weight: Font.DemiBold
                color: "#F1F5F9"
            }
            
            Text {
                text: resolveModeSubtitle(currentMode)
                font.pixelSize: 12
                color: "#94A3B8"
                visible: text.length > 0
            }
        }
        
        Item { Layout.fillWidth: true }

        // 模式切换按钮
        Row {
            spacing: 8
            
            Repeater {
                model: root.modeOptions
                
                delegate: Rectangle {
                    readonly property string modeValue: root.resolveModeValue(modelData)
                    readonly property string modeLabel: root.resolveModeLabel(modelData)

                    width: Math.max(root.modeButtonMinWidth, modeLabelText.implicitWidth + 24)
                    height: 36
                    radius: 8
                    color: currentMode === modeValue ? "#3B82F6" : "transparent"
                    border.width: currentMode === modeValue ? 0 : 1
                    border.color: "#334155"
                    
                    Text {
                        id: modeLabelText
                        anchors.centerIn: parent
                        text: modeLabel
                        font.pixelSize: 14
                        color: currentMode === modeValue ? "white" : "#F1F5F9"
                    }
                    
                    MouseArea {
                        anchors.fill: parent
                        cursorShape: Qt.PointingHandCursor
                        onClicked: root.modeSelected(modeValue)
                    }
                }
            }
        }

        RowLayout {
            id: actionSlot
            spacing: 8
        }
    }
    
    // ============ 函数 ============
    
    // 获取模式标题
    function resolveModeTitle(mode) {
        var customTitle = modeTitleMap && modeTitleMap[mode] !== undefined ? modeTitleMap[mode] : ""
        if (String(customTitle || "").length > 0) {
            return String(customTitle)
        }

        switch(mode) {
            case "home": return "🏠 因子分析工作台"
            case "library": return "📚 因子库浏览"
            case "create": return "📝 因子创建"
            case "debug": return "🔧 因子调试"
            case "analyze": return "📊 因子分析"
            case "backtest": return "🧪 因子回测"
            default: return "因子分析"
        }
    }
    
    // 获取模式标签
    function resolveModeValue(modeItem) {
        if (modeItem && typeof modeItem === "object") {
            return String(modeItem.value || "")
        }
        return String(modeItem || "")
    }

    function resolveModeLabel(modeItem) {
        if (modeItem && typeof modeItem === "object") {
            var explicitLabel = String(modeItem.label || "")
            if (explicitLabel.length > 0) {
                return explicitLabel
            }
        }

        var mode = resolveModeValue(modeItem)
        switch(mode) {
            case "home": return "首页"
            case "library": return "因子库"
            case "create": return "创建"
            case "debug": return "调试"
            case "analyze": return "分析"
            case "backtest": return "回测"
            default: return mode
        }
    }

    function resolveModeSubtitle(mode) {
        var customSubtitle = modeSubtitleMap && modeSubtitleMap[mode] !== undefined ? modeSubtitleMap[mode] : ""
        if (String(customSubtitle || "").length > 0) {
            return String(customSubtitle)
        }

        if (mode === "home") {
            return "一站式因子创建、调试、分析和回测平台"
        }

        return ""
    }
}