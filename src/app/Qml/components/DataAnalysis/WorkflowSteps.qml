// WorkflowSteps.qml - 优化间距版本
import QtQuick 2.15
import QtQuick.Controls 2.15
import ConsoleUi 1.0 as Theme

Item {
    id: workflowContainer
    width: parent.width
    height: 120  // 减小总高度
    
    property string title: "标准工作流程"
    property string subtitle: "点击任意步骤开始"
    property int activeStep: 0
    
    signal stepClicked(int index)
    
    // 单个步骤组件的定义
    Component {
        id: stepComponent

        Item {
            id: stepItem
            width: 65
            height: 90  // 减小步骤高度
            
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
                width: 44  // 减小图标大小
                height: 44
                radius: 22
                anchors.horizontalCenter: parent.horizontalCenter
                color: stepActive ? Theme.accentColor : Theme.darkCard
                border.color: Theme.darkBg
                border.width: 2
                
                Image {
                    source: stepItem.iconSource
                    width: 20  // 减小图标内图像大小
                    height: 20
                    anchors.centerIn: parent
                }
            }
            
            // 步骤标题
            Text {
                text: stepItem.stepTitle
                anchors.top: stepIcon.bottom
                anchors.topMargin: 6  // 减小间距
                anchors.horizontalCenter: parent.horizontalCenter
                font.pixelSize: 10  // 减小字体
                font.bold: true
                color: Theme.darkText
                horizontalAlignment: Text.AlignHCenter
                width: parent.width
                wrapMode: Text.WordWrap
                maximumLineCount: 2  // 限制最多2行
            }
            
            // 步骤描述
            Text {
                text: stepItem.stepDescription
                anchors.top: stepTitle.bottom
                anchors.topMargin: 3  // 减小间距
                anchors.horizontalCenter: parent.horizontalCenter
                font.pixelSize: 8  // 减小字体
                color: "#aaaaaa"
                horizontalAlignment: Text.AlignHCenter
                width: parent.width
                wrapMode: Text.WordWrap
                maximumLineCount: 2  // 限制最多2行
            }
        }
    }
    
    // 标题区域
    Row {
        id: titleRow
        width: Math.min(parent.width, 800)  // 限制最大宽度
        anchors.horizontalCenter: parent.horizontalCenter
        height: 30  // 减小标题高度
        spacing: 15  // 减小间距
        
        Text {
            text: workflowContainer.title
            font.pixelSize: 20  // 减小标题字体
            font.bold: true
            color: Theme.darkText
            anchors.verticalCenter: parent.verticalCenter
            elide: Text.ElideRight
        }
        
        // 日期范围指示器
        Rectangle {
            width: 180  // 减小宽度
            height: 28  // 减小高度
            radius: 5
            color: Theme.darkCard
            border.color: Theme.darkBorder
            border.width: 1
            
            Row {
                anchors.centerIn: parent
                spacing: 8
                
                Image {
                    source: "qrc:/icons/project.svg"
                    width: 16
                    height: 16
                    anchors.verticalCenter: parent.verticalCenter
                }
                
                Text {
                    text: workflowContainer.subtitle
                    font.pixelSize: 12  // 减小字体
                    font.weight: Font.Medium
                    color: Theme.darkText
                    anchors.verticalCenter: parent.verticalCenter
                    elide: Text.ElideRight
                }
            }
        }
    }
    
    // 步骤容器（不使用连接线，更简洁）
    Row {
        id: stepsRow
        width: Math.min(parent.width, 800)  // 限制最大宽度
        height: 90  // 减小高度
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.top: titleRow.bottom
        anchors.topMargin: 10  // 减小顶部间距
        spacing: (width - (5 * 65)) / 4  // 动态计算间距
        
        // 使用Loader动态创建步骤
        Loader {
            id: step1Loader
            width: 65
            height: 90
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
            width: 65
            height: 90
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
            width: 65
            height: 90
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
            width: 65
            height: 90
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
            width: 65
            height: 90
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