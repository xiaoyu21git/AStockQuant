// RiskCategoryCard.qml - 风险分类卡片组件
import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import "../Factor" as FactorComponents

Rectangle {
    id: riskCategoryCard
    
    // 属性
    property string title: "风险分类"
    property string icon: "qrc:/icons/shield.svg"
    property string description: "风险分类描述"
    property color borderColor: FactorComponents.FactorDesignSystem.darkBorder
    property color backgroundColor: FactorComponents.FactorDesignSystem.darkCard
    
    // 信号
    signal clicked()
    
    // 外观
    radius: 8
    color: backgroundColor
    border.color: borderColor
    border.width: 1
    
    // 鼠标交互
    MouseArea {
        anchors.fill: parent
        hoverEnabled: true
        cursorShape: Qt.PointingHandCursor
        
        onEntered: {
            riskCategoryCard.borderColor = FactorComponents.FactorDesignSystem.accentColor
            riskCategoryCard.backgroundColor = Qt.lighter(FactorComponents.FactorDesignSystem.darkCard, 1.1)
        }
        
        onExited: {
            riskCategoryCard.borderColor = FactorComponents.FactorDesignSystem.darkBorder
            riskCategoryCard.backgroundColor = FactorComponents.FactorDesignSystem.darkCard
        }
        
        onClicked: {
            riskCategoryCard.clicked()
        }
    }
    
    // 内容布局
    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 16
        spacing: 12
        
        // 标题行
        RowLayout {
            Layout.fillWidth: true
            spacing: 12
            
            // 图标
            Image {
                source: icon
                width: 24
                height: 24
                Layout.alignment: Qt.AlignVCenter
            }
            
            // 标题
            Text {
                text: title
                font.pixelSize: 18
                font.bold: true
                color: FactorComponents.FactorDesignSystem.darkText
                Layout.fillWidth: true
                Layout.alignment: Qt.AlignVCenter
            }
        }
        
        // 描述
        Text {
            text: description
            font.pixelSize: 14
            color: FactorComponents.FactorDesignSystem.darkTextSecondary
            wrapMode: Text.WordWrap
            Layout.fillWidth: true
        }
        
        // 内容占位符
        Item {
            id: contentPlaceholder
            Layout.fillWidth: true
            Layout.fillHeight: true
        }
    }
}
