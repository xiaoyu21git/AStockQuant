// WorkflowSteps.qml - 工作流程步骤组件（单文件版）
import QtQuick 2.15
import QtQuick.Controls 2.15
import "." as Theme

Item {
    id: workflowContainer
    width: parent.width
    height: 150
    
    property string title: "标准工作流程"
    property string subtitle: "点击任意步骤开始"
    property int activeStep: 0
    
    signal stepClicked(int index)
    
    // 单个步骤组件的定义
    Component {
        id: stepComponent
        
        Item {
            id: stepItem
            width: 80
            height: 100
            
            property int stepIndex: 0
            property string iconSource: ""
            property string stepTitle: ""
            property string stepDescription: ""
            property bool stepActive: false
            
            signal stepClickedInternal(int index)
            
            MouseArea {
                anchors.fill: parent
                cursorShape: Qt.PointingHandCursor
                onClicked: stepItem.stepClickedInternal(stepItem.stepIndex)
            }
            
            // 步骤图标
            Rectangle {
                id: stepIcon
                width: 50
                height: 50
                radius: 25
                anchors.horizontalCenter: parent.horizontalCenter
                color: stepActive ? Theme.accentColor : Theme.darkCard
                border.color: Theme.darkBg
                border.width: 3
                
                Image {
                    source: stepItem.iconSource
                    width: 24
                    height: 24
                    anchors.centerIn: parent
                    //color: stepActive ? Theme.whiteColor : Theme.accentColor
                }
            }
            
            // 步骤标题
            Text {
                text: stepItem.stepTitle
                anchors.top: stepIcon.bottom
                anchors.topMargin: 10
                anchors.horizontalCenter: parent.horizontalCenter
                font.pixelSize: 12
                font.bold: true
                color: Theme.darkText
                horizontalAlignment: Text.AlignHCenter
                width: parent.width
                wrapMode: Text.WordWrap
            }
            
            // 步骤描述
            Text {
                text: stepItem.stepDescription
                anchors.top: stepTitle.bottom
                anchors.topMargin: 5
                anchors.horizontalCenter: parent.horizontalCenter
                font.pixelSize: 10
                color: "#aaaaaa"
                horizontalAlignment: Text.AlignHCenter
                width: parent.width
                wrapMode: Text.WordWrap
            }
        }
    }
    
    // 标题区域
    Row {
        id: titleRow
        width: parent.width
        height: 40
        spacing: 20
        
        Text {
            text: workflowContainer.title
            font.pixelSize: 28
            font.bold: true
            color: Theme.darkText
            anchors.verticalCenter: parent.verticalCenter
        }
        
        // 日期范围指示器
        Rectangle {
            width: 200
            height: 36
            radius: 6
            color: Theme.darkCard
            border.color: Theme.darkBorder
            border.width: 1
            
            Row {
                anchors.centerIn: parent
                spacing: 10
                
                Image {
                    source: "qrc:/icons/project.svg"
                    width: 20
                    height: 20
                    anchors.verticalCenter: parent.verticalCenter
                }
                
                Text {
                    text: workflowContainer.subtitle
                    font.pixelSize: 14
                    font.weight: Font.Medium
                    color: Theme.darkText
                    anchors.verticalCenter: parent.verticalCenter
                }
            }
        }
    }
    
    // 步骤连接线
    Rectangle {
        id: connectionLine
        width: parent.width * 0.8
        height: 3
        color: Theme.darkBorder
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.top: titleRow.bottom
        anchors.topMargin: 55
        z: 1
    }
    
    // 步骤容器
    Row {
        id: stepsRow
        width: parent.width
        height: 80
        anchors.top: titleRow.bottom
        anchors.topMargin: 20
        spacing: (parent.width - (80 * 5)) / 4
        
        // 使用Loader动态创建步骤
        Loader {
            id: step1Loader
            width: 80
            height: 100
            sourceComponent: stepComponent
            
            onLoaded: {
                item.stepIndex = 0
                item.iconSource = "qrc:/icons/database.svg"
                item.stepTitle = "数据整合与清洗"
                item.stepDescription = "导入并准备分析数据"
                item.stepActive = workflowContainer.activeStep === 0
                item.stepClickedInternal.connect(workflowContainer.stepClicked)
            }
        }
        
        Loader {
            id: step2Loader
            width: 80
            height: 100
            sourceComponent: stepComponent
            
            onLoaded: {
                item.stepIndex = 1
                item.iconSource = "qrc:/icons/chart-bar.svg"
                item.stepTitle = "因子分析与特征工程"
                item.stepDescription = "开发量化因子和特征"
                item.stepActive = workflowContainer.activeStep === 1
                item.stepClickedInternal.connect(workflowContainer.stepClicked)
            }
        }
        
        Loader {
            id: step3Loader
            width: 80
            height: 100
            sourceComponent: stepComponent
            
            onLoaded: {
                item.stepIndex = 2
                item.iconSource = "qrc:/icons/history.svg"
                item.stepTitle = "策略回测与验证"
                item.stepDescription = "历史测试策略表现"
                item.stepActive = workflowContainer.activeStep === 2
                item.stepClickedInternal.connect(workflowContainer.stepClicked)
            }
        }
        
        Loader {
            id: step4Loader
            width: 80
            height: 100
            sourceComponent: stepComponent
            
            onLoaded: {
                item.stepIndex = 3
                item.iconSource = "qrc:/icons/shield.svg"
                item.stepTitle = "风险管理与优化"
                item.stepDescription = "调整风险与优化组合"
                item.stepActive = workflowContainer.activeStep === 3
                item.stepClickedInternal.connect(workflowContainer.stepClicked)
            }
        }
        
        Loader {
            id: step5Loader
            width: 80
            height: 100
            sourceComponent: stepComponent
            
            onLoaded: {
                item.stepIndex = 4
                item.iconSource = "qrc:/icons/file.svg"
                item.stepTitle = "报告与部署"
                item.stepDescription = "生成报告并部署策略"
                item.stepActive = workflowContainer.activeStep === 4
                item.stepClickedInternal.connect(workflowContainer.stepClicked)
            }
        }
    }
}