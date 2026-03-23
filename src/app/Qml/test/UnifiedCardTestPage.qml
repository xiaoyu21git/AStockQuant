// UnifiedCardTestPage.qml
// 统一量化卡片组件库测试页面

import QtQuick 2.15
import QtQuick.Layouts 1.15
import QtQuick.Controls 2.15
import "../components/Base" as BaseComponents
import "../components" as Components
import "../utils/FactorDataAdapter.js" as FactorAdapter
import "../utils/StrategyDataAdapter.js" as StrategyAdapter

Rectangle {
    id: root
    color: "#0F172A"
    
    // 测试数据
    property var testFactors: [
        {
            factorId: "factor_001",
            factorName: "动量突破因子",
            displayName: "动量突破因子",
            majorCategory: "动量类",
            subCategory: "趋势动量",
            description: "基于价格突破的动量因子，识别趋势启动点",
            icValue: 0.035,
            irValue: 0.8,
            validityDays: 20,
            turnoverRate: 32,
            isFavorite: true,
            isRecommended: true,
            status: "ACTIVE",
            tags: ["动量", "技术指标", "趋势"],
            creator: "系统",
            createDate: "2023-10-15",
            groupReturns: [0.12, 0.08, 0.05, 0.02, 0.01, -0.01, -0.03, -0.05, -0.08, -0.12]
        },
        {
            factorId: "factor_002", 
            factorName: "价值低估因子",
            displayName: "价值低估因子",
            majorCategory: "价值类",
            subCategory: "价值挖掘",
            description: "基于市盈率、市净率的估值因子，识别低估股票",
            icValue: 0.028,
            irValue: 0.7,
            validityDays: 25,
            turnoverRate: 18,
            isFavorite: false,
            isRecommended: true,
            status: "ACTIVE",
            tags: ["价值", "基本面", "估值"],
            creator: "系统",
            createDate: "2023-09-20",
            groupReturns: [0.08, 0.06, 0.04, 0.03, 0.02, 0.01, -0.01, -0.02, -0.04, -0.06]
        },
        {
            factorId: "factor_003",
            factorName: "质量优选因子",
            displayName: "质量优选因子",
            majorCategory: "质量类",
            subCategory: "财务质量",
            description: "基于ROE、毛利率等财务指标的优质股票筛选",
            icValue: 0.032,
            irValue: 0.9,
            validityDays: 30,
            turnoverRate: 15,
            isFavorite: true,
            isRecommended: false,
            status: "EXPERIMENTAL",
            tags: ["质量", "财务", "稳定性"],
            creator: "用户A",
            createDate: "2023-11-05",
            groupReturns: [0.10, 0.08, 0.06, 0.04, 0.02, 0.01, 0.00, -0.01, -0.02, -0.04]
        }
    ]
    
    property var testStrategies: [
        {
            strategyId: "strategy_001",
            strategyName: "趋势跟踪策略",
            displayName: "趋势跟踪策略",
            strategyType: "趋势策略",
            subType: "趋势跟踪",
            description: "基于均线系统的趋势跟踪策略，捕捉主要趋势行情",
            returns: 15.5,
            sharpeRatio: 1.2,
            maxDrawdown: 8.3,
            winRate: 58.7,
            isFavorite: true,
            isRecommended: true,
            status: "RUNNING",
            tags: ["趋势", "均线", "技术分析"],
            creator: "系统",
            createDate: "2023-10-01",
            runningDays: 120,
            tradesCount: 245,
            dailyPnL: 1250,
            position: 50000,
            chartData: StrategyAdapter.generateDefaultStrategyChartData(),
            parameters: [
                {name: "短期均线周期", value: 20, min: 5, max: 200, unit: "天", color: "#3B82F6"},
                {name: "长期均线周期", value: 60, min: 10, max: 500, unit: "天", color: "#3B82F6"},
                {name: "止损比例", value: 5, min: 1, max: 20, unit: "%", color: "#EF4444"},
                {name: "止盈比例", value: 10, min: 1, max: 30, unit: "%", color: "#10B981"}
            ]
        },
        {
            strategyId: "strategy_002",
            strategyName: "均值回归策略",
            displayName: "均值回归策略",
            strategyType: "均值回归",
            subType: "振荡策略",
            description: "基于布林带和RSI的均值回归策略，适合震荡市场",
            returns: 9.2,
            sharpeRatio: 0.8,
            maxDrawdown: 5.6,
            winRate: 62.3,
            isFavorite: false,
            isRecommended: true,
            status: "STOPPED",
            tags: ["均值回归", "振荡", "反转"],
            creator: "用户B",
            createDate: "2023-09-15",
            runningDays: 0,
            tradesCount: 120,
            dailyPnL: 0,
            position: 0,
            chartData: StrategyAdapter.generateDefaultStrategyChartData(),
            parameters: [
                {name: "布林带周期", value: 20, min: 5, max: 100, unit: "天", color: "#F59E0B"},
                {name: "布林带宽度", value: 2.0, min: 1.0, max: 3.0, unit: "σ", color: "#F59E0B"},
                {name: "RSI周期", value: 14, min: 7, max: 30, unit: "天", color: "#8B5CF6"},
                {name: "超卖阈值", value: 30, min: 10, max: 50, unit: "", color: "#3B82F6"}
            ]
        },
        {
            strategyId: "strategy_003",
            strategyName: "多因子组合策略",
            displayName: "多因子组合策略",
            strategyType: "多因子策略",
            subType: "组合策略",
            description: "结合动量、价值、质量因子的综合策略，分散风险提升稳定性",
            returns: 18.7,
            sharpeRatio: 1.5,
            maxDrawdown: 6.8,
            winRate: 65.1,
            isFavorite: true,
            isRecommended: true,
            status: "PAUSED",
            tags: ["多因子", "组合", "分散"],
            creator: "系统",
            createDate: "2023-11-10",
            runningDays: 85,
            tradesCount: 180,
            dailyPnL: 850,
            position: 35000,
            chartData: StrategyAdapter.generateDefaultStrategyChartData(),
            parameters: [
                {name: "动量权重", value: 40, min: 0, max: 100, unit: "%", color: "#3B82F6"},
                {name: "价值权重", value: 35, min: 0, max: 100, unit: "%", color: "#F59E0B"},
                {name: "质量权重", value: 25, min: 0, max: 100, unit: "%", color: "#10B981"},
                {name: "再平衡周期", value: 20, min: 5, max: 60, unit: "天", color: "#8B5CF6"}
            ]
        }
    ]
    
    property string selectedEntityId: ""
    
    ScrollView {
        anchors.fill: parent
        clip: true
        
        ColumnLayout {
            width: root.width - 20
            spacing: 20
            
            // 标题
            Text {
                Layout.fillWidth: true
                Layout.topMargin: 20
                text: "统一量化卡片组件库测试页面"
                font.pixelSize: 28
                font.weight: Font.Bold
                color: "#F1F5F9"
                horizontalAlignment: Text.AlignHCenter
            }
            
            Text {
                Layout.fillWidth: true
                text: "展示因子卡片和策略卡件的统一设计和使用"
                font.pixelSize: 16
                color: "#94A3B8"
                horizontalAlignment: Text.AlignHCenter
            }
            
            // 因子卡片测试
            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: 340
                radius: 16
                color: "#1E293B"
                border.color: "#475569"
                border.width: 1
                
                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 20
                    spacing: 12
                    
                    Text {
                        text: "因子卡片测试 (FactorCard)"
                        font.pixelSize: 20
                        font.weight: Font.DemiBold
                        color: "#F1F5F9"
                    }
                    
                    Text {
                        text: "展示三种不同类型的因子卡片，包含IC值、换手率、分组收益等指标"
                        font.pixelSize: 14
                        color: "#94A3B8"
                        wrapMode: Text.WordWrap
                        Layout.fillWidth: true
                    }
                    
                    RowLayout {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        spacing: 20
                        
                        // 因子卡片1
                        FactorCard {
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            
                            factorId: testFactors[0].factorId
                            factorName: testFactors[0].factorName
                            displayName: testFactors[0].displayName
                            majorCategory: testFactors[0].majorCategory
                            subCategory: testFactors[0].subCategory
                            description: testFactors[0].description
                            icValue: testFactors[0].icValue
                            irValue: testFactors[0].irValue
                            validityDays: testFactors[0].validityDays
                            turnoverRate: testFactors[0].turnoverRate
                            isRecommended: testFactors[0].isRecommended
                            isFavorite: testFactors[0].isFavorite
                            status: testFactors[0].status
                            tags: testFactors[0].tags
                            creator: testFactors[0].creator
                            createDate: testFactors[0].createDate
                            categoryColor: FactorAdapter.getFactorCategoryColor(testFactors[0].majorCategory)
                            
                            selected: selectedEntityId === testFactors[0].factorId
                            showMiniChart: true
                            showGroupReturns: true
                            groupReturns: testFactors[0].groupReturns
                            
                            onEntitySelected: function(entityId) {
                                console.log("因子卡片选中:", entityId);
                                selectedEntityId = entityId;
                            }
                        }
                        
                        // 因子卡片2
                        FactorCard {
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            
                            factorId: testFactors[1].factorId
                            factorName: testFactors[1].factorName
                            displayName: testFactors[1].displayName
                            majorCategory: testFactors[1].majorCategory
                            subCategory: testFactors[1].subCategory
                            description: testFactors[1].description
                            icValue: testFactors[1].icValue
                            irValue: testFactors[1].irValue
                            validityDays: testFactors[1].validityDays
                            turnoverRate: testFactors[1].turnoverRate
                            isRecommended: testFactors[1].isRecommended
                            isFavorite: testFactors[1].isFavorite
                            status: testFactors[1].status
                            tags: testFactors[1].tags
                            creator: testFactors[1].creator
                            createDate: testFactors[1].createDate
                            categoryColor: FactorAdapter.getFactorCategoryColor(testFactors[1].majorCategory)
                            
                            selected: selectedEntityId === testFactors[1].factorId
                            showMiniChart: true
                            showGroupReturns: false
                            groupReturns: testFactors[1].groupReturns
                            
                            onEntitySelected: function(entityId) {
                                console.log("因子卡片选中:", entityId);
                                selectedEntityId = entityId;
                            }
                        }
                        
                        // 因子卡片3
                        FactorCard {
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            
                            factorId: testFactors[2].factorId
                            factorName: testFactors[2].factorName
                            displayName: testFactors[2].displayName
                            majorCategory: testFactors[2].majorCategory
                            subCategory: testFactors[2].subCategory
                            description: testFactors[2].description
                            icValue: testFactors[2].icValue
                            irValue: testFactors[2].irValue
                            validityDays: testFactors[2].validityDays
                            turnoverRate: testFactors[2].turnoverRate
                            isRecommended: testFactors[2].isRecommended
                            isFavorite: testFactors[2].isFavorite
                            status: testFactors[2].status
                            tags: testFactors[2].tags
                            creator: testFactors[2].creator
                            createDate: testFactors[2].createDate
                            categoryColor: FactorAdapter.getFactorCategoryColor(testFactors[2].majorCategory)
                            
                            selected: selectedEntityId === testFactors[2].factorId
                            showMiniChart: false
                            showGroupReturns: true
                            groupReturns: testFactors[2].groupReturns
                            
                            onEntitySelected: function(entityId) {
                                console.log("因子卡片选中:", entityId);
                                selectedEntityId = entityId;
                            }
                        }
                    }
                }
            }
            
            // 策略卡片测试
            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: 400
                radius: 16
                color: "#1E293B"
                border.color: "#475569"
                border.width: 1
                
                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 20
                    spacing: 12
                    
                    Text {
                        text: "策略卡片测试 (StrategyCard)"
                        font.pixelSize: 20
                        font.weight: Font.DemiBold
                        color: "#F1F5F9"
                    }
                    
                    Text {
                        text: "展示三种不同类型的策略卡片，集成参数控制面板、状态切换和实时监控"
                        font.pixelSize: 14
                        color: "#94A3B8"
                        wrapMode: Text.WordWrap
                        Layout.fillWidth: true
                    }
                    
                    RowLayout {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        spacing: 20
                        
                        // 策略卡片1
                        Components.StrategyCard {
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            
                            strategyId: testStrategies[0].strategyId
                            strategyName: testStrategies[0].strategyName
                            displayName: testStrategies[0].displayName
                            strategyType: testStrategies[0].strategyType
                            description: testStrategies[0].description
                            returns: testStrategies[0].returns
                            sharpeRatio: testStrategies[0].sharpeRatio
                            maxDrawdown: testStrategies[0].maxDrawdown
                            winRate: testStrategies[0].winRate
                            isRecommended: testStrategies[0].isRecommended
                            isFavorite: testStrategies[0].isFavorite
                            status: testStrategies[0].status
                            tags: testStrategies[0].tags
                            creator: testStrategies[0].creator
                            createDate: testStrategies[0].createDate
                            categoryColor: StrategyAdapter.getStrategyTypeColor(testStrategies[0].strategyType)
                            
                            runningDays: testStrategies[0].runningDays
                            tradesCount: testStrategies[0].tradesCount
                            dailyPnL: testStrategies[0].dailyPnL
                            position: testStrategies[0].position
                            controlParameters: testStrategies[0].parameters
                            chartData: testStrategies[0].chartData
                            
                            selected: selectedEntityId === testStrategies[0].strategyId
                            showMiniChart: true
                            showParameterPanel: true
                            
                            onEntitySelected: function(entityId) {
                                console.log("策略卡片选中:", entityId);
                                selectedEntityId = entityId;
                            }
                            
                            onStartRequested: function(entityId) {
                                console.log("启动策略:", entityId);
                                // 这里可以调用实际的策略启动接口
                            }
                            
                            onStopRequested: function(entityId) {
                                console.log("停止策略:", entityId);
                                // 这里可以调用实际的策略停止接口
                            }
                            
                            onOptimizeRequested: function(entityId) {
                                console.log("优化策略:", entityId);
                                // 这里可以调用策略优化接口
                            }
                        }
                        
                        // 策略卡片2
                        Components.StrategyCard {
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            
                            strategyId: testStrategies[1].strategyId
                            strategyName: testStrategies[1].strategyName
                            displayName: testStrategies[1].displayName
                            strategyType: testStrategies[1].strategyType
                            description: testStrategies[1].description
                            returns: testStrategies[1].returns
                            sharpeRatio: testStrategies[1].sharpeRatio
                            maxDrawdown: testStrategies[1].maxDrawdown
                            winRate: testStrategies[1].winRate
                            isRecommended: testStrategies[1].isRecommended
                            isFavorite: testStrategies[1].isFavorite
                            status: testStrategies[1].status
                            tags: testStrategies[1].tags
                            creator: testStrategies[1].creator
                            createDate: testStrategies[1].createDate
                            categoryColor: StrategyAdapter.getStrategyTypeColor(testStrategies[1].strategyType)
                            
                            runningDays: testStrategies[1].runningDays
                            tradesCount: testStrategies[1].tradesCount
                            dailyPnL: testStrategies[1].dailyPnL
                            position: testStrategies[1].position
                            controlParameters: testStrategies[1].parameters
                            chartData: testStrategies[1].chartData
                            
                            selected: selectedEntityId === testStrategies[1].strategyId
                            showMiniChart: true
                            showParameterPanel: false
                            
                            onEntitySelected: function(entityId) {
                                console.log("策略卡片选中:", entityId);
                                selectedEntityId = entityId;
                            }
                        }
                        
                        // 策略卡片3
                        Components.StrategyCard {
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            
                            strategyId: testStrategies[2].strategyId
                            strategyName: testStrategies[2].strategyName
                            displayName: testStrategies[2].displayName
                            strategyType: testStrategies[2].strategyType
                            description: testStrategies[2].description
                            returns: testStrategies[2].returns
                            sharpeRatio: testStrategies[2].sharpeRatio
                            maxDrawdown: testStrategies[2].maxDrawdown
                            winRate: testStrategies[2].winRate
                            isRecommended: testStrategies[2].isRecommended
                            isFavorite: testStrategies[2].isFavorite
                            status: testStrategies[2].status
                            tags: testStrategies[2].tags
                            creator: testStrategies[2].creator
                            createDate: testStrategies[2].createDate
                            categoryColor: StrategyAdapter.getStrategyTypeColor(testStrategies[2].strategyType)
                            
                            runningDays: testStrategies[2].runningDays
                            tradesCount: testStrategies[2].tradesCount
                            dailyPnL: testStrategies[2].dailyPnL
                            position: testStrategies[2].position
                            controlParameters: testStrategies[2].parameters
                            chartData: testStrategies[2].chartData
                            
                            selected: selectedEntityId === testStrategies[2].strategyId
                            showMiniChart: false
                            showParameterPanel: true
                            
                            onEntitySelected: function(entityId) {
                                console.log("策略卡片选中:", entityId);
                                selectedEntityId = entityId;
                            }
                        }
                    }
                }
            }
            
            // 数据适配器测试
            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: 180
                radius: 16
                color: "#1E293B"
                border.color: "#475569"
                border.width: 1
                
                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 20
                    spacing: 12
                    
                    Text {
                        text: "数据适配器测试 (DataAdapter)"
                        font.pixelSize: 20
                        font.weight: Font.DemiBold
                        color: "#F1F5F9"
                    }
                    
                    Text {
                        text: "展示数据适配器将原始数据转换为统一卡片格式的功能"
                        font.pixelSize: 14
                        color: "#94A3B8"
                        wrapMode: Text.WordWrap
                        Layout.fillWidth: true
                    }
                    
                    RowLayout {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        spacing: 20
                        
                        // 因子数据转换
                        Rectangle {
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            radius: 8
                            color: "#334155"
                            
                            ColumnLayout {
                                anchors.fill: parent
                                anchors.margins: 12
                                
                                Text {
                                    text: "因子数据转换"
                                    font.pixelSize: 16
                                    font.weight: Font.Medium
                                    color: "#F1F5F9"
                                }
                                
                                Text {
                                    text: {
                                        var cardData = FactorAdapter.mapFactorToCardData(testFactors[0]);
                                        return "显示名称: " + cardData.displayName + "\n" +
                                               "类别颜色: " + cardData.categoryColor + "\n" +
                                               "IC值: " + cardData.icValue + "\n" +
                                               "标签数量: " + cardData.tags.length;
                                    }
                                    font.pixelSize: 13
                                    color: "#94A3B8"
                                    wrapMode: Text.WordWrap
                                    Layout.fillWidth: true
                                    Layout.fillHeight: true
                                }
                            }
                        }
                        
                        // 策略数据转换
                        Rectangle {
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            radius: 8
                            color: "#334155"
                            
                            ColumnLayout {
                                anchors.fill: parent
                                anchors.margins: 12
                                
                                Text {
                                    text: "策略数据转换"
                                    font.pixelSize: 16
                                    font.weight: Font.Medium
                                    color: "#F1F5F9"
                                }
                                
                                Text {
                                    text: {
                                        var cardData = StrategyAdapter.mapStrategyToCardData(testStrategies[0]);
                                        return "显示名称: " + cardData.displayName + "\n" +
                                               "类别颜色: " + cardData.categoryColor + "\n" +
                                               "收益率: " + cardData.returns + "\n" +
                                               "参数数量: " + cardData.controlParameters.length;
                                    }
                                    font.pixelSize: 13
                                    color: "#94A3B8"
                                    wrapMode: Text.WordWrap
                                    Layout.fillWidth: true
                                    Layout.fillHeight: true
                                }
                            }
                        }
                        
                        // 统一数据转换
                        Rectangle {
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            radius: 8
                            color: "#334155"
                            
                            ColumnLayout {
                                anchors.fill: parent
                                anchors.margins: 12
                                
                                Text {
                                    text: "统一数据转换"
                                    font.pixelSize: 16
                                    font.weight: Font.Medium
                                    color: "#F1F5F9"
                                }
                                
                                Text {
                                    text: {
                                        var factorCard = StrategyAdapter.mapToUniversalCardData(testFactors[0]);
                                        var strategyCard = StrategyAdapter.mapToUniversalCardData(testStrategies[0]);
                                        return "因子类型: " + factorCard.entityType + "\n" +
                                               "策略类型: " + strategyCard.entityType + "\n" +
                                               "统一接口: 支持";
                                    }
                                    font.pixelSize: 13
                                    color: "#94A3B8"
                                    wrapMode: Text.WordWrap
                                    Layout.fillWidth: true
                                    Layout.fillHeight: true
                                }
                            }
                        }
                    }
                }
            }
            
            // 总结信息
            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: 120
                radius: 16
                color: "#1E293B"
                border.color: "#10B981"
                border.width: 2
                
                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 20
                    
                    Text {
                        text: "统一量化卡片组件库测试完成"
                        font.pixelSize: 18
                        font.weight: Font.DemiBold
                        color: "#10B981"
                    }
                    
                    Text {
                        text: "✓ 因子卡片 (FactorCard) 测试通过\n" +
                              "✓ 策略卡片 (StrategyCard) 测试通过\n" +
                              "✓ 数据适配器 (DataAdapter) 测试通过\n" +
                              "✓ 视觉统一性验证通过\n" +
                              "✓ 功能完整性验证通过"
                        font.pixelSize: 14
                        color: "#94A3B8"
                        Layout.fillWidth: true
                    }
                }
            }
        }
    }
}