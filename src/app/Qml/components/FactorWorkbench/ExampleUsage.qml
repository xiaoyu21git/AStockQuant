// ExampleUsage.qml
// 展示如何使用新的因子库类结构
// FactorViewModel + FactorService 分离架构示例

import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import AStock.Bridge 1.0 as Bridge

/**
 * 新的因子库类结构使用示例：
 * 1. FactorViewModel - 只负责视图更新
 * 2. FactorService - 负责业务逻辑（数据库操作、缓存等）
 * 3. 两者通过信号/槽机制协作
 */

Item {
    id: root
    width: 800
    height: 600
    
    // ============ 数据绑定 ============
    
    // 因子服务（业务逻辑层）
    Bridge.FactorService {
        id: factorService
        onFactorsLoaded: function(factors) {
            console.log("✅ 因子数据加载完成，数量:", factors.length)
            // 将数据传递给视图模型
            factorViewModel.importFactorsToView(factors)
        }
        onFactorAdded: function(factorId, factorData) {
            console.log("✅ 因子添加完成:", factorId)
        }
        onFactorUpdated: function(factorId, factorData) {
            console.log("✅ 因子更新完成:", factorId)
        }
        onFactorDeleted: function(factorId) {
            console.log("✅ 因子删除完成:", factorId)
        }
        onErrorOccurred: function(errorMessage) {
            console.error("❌ 因子服务错误:", errorMessage)
        }
    }
    
    // 因子视图模型（视图层）
    Bridge.FactorViewModel {
        id: factorViewModel
        Component.onCompleted: {
            // 设置服务层引用
            setFactorService(factorService)
            // 初始化加载数据
            factorService.loadAllFactors()
        }
    }
    
    // ============ UI布局 ============
    
    ColumnLayout {
        anchors.fill: parent
        spacing: 10
        
        // 标题
        Text {
            text: "因子库重构示例 - FactorViewModel + FactorService"
            font.pixelSize: 20
            font.bold: true
            color: "#333"
        }
        
        // 操作按钮区域
        RowLayout {
            Layout.fillWidth: true
            spacing: 10
            
            Button {
                text: "加载所有因子"
                onClicked: {
                    console.log("点击：加载所有因子")
                    factorService.loadAllFactors()
                }
            }
            
            Button {
                text: "添加测试因子"
                onClicked: {
                    console.log("点击：添加测试因子")
                    var testFactor = {
                        factorName: "test_factor_" + Date.now(),
                        displayName: "测试因子",
                        majorCategory: "技术指标",
                        subCategory: "动量类",
                        description: "这是一个测试因子",
                        icValue: 0.15,
                        irValue: 1.2,
                        validityDays: 30,
                        turnoverRate: 0.25,
                        isRecommended: true,
                        isFavorite: false,
                        status: "active",
                        tags: ["测试", "动量"],
                        creator: "系统",
                        createDate: new Date().toISOString()
                    }
                    
                    var factorId = factorService.addFactor(testFactor)
                    console.log("添加因子结果，ID:", factorId)
                }
            }
            
            Button {
                text: "搜索因子"
                onClicked: {
                    console.log("点击：搜索因子")
                    var results = factorViewModel.searchFactorsInView("测试")
                    console.log("搜索结果数量:", results.length)
                }
            }
            
            Button {
                text: "清空缓存"
                onClicked: {
                    console.log("点击：清空缓存")
                    factorService.clearCache()
                }
            }
        }
        
        // 因子列表显示
        Rectangle {
            Layout.fillWidth: true
            Layout.fillHeight: true
            border.color: "#ccc"
            border.width: 1
            radius: 5
            
            ListView {
                id: factorListView
                anchors.fill: parent
                anchors.margins: 5
                model: factorViewModel
                clip: true
                
                delegate: Rectangle {
                    width: factorListView.width
                    height: 80
                    color: index % 2 === 0 ? "#f8f9fa" : "#ffffff"
                    border.color: "#e9ecef"
                    border.width: 1
                    radius: 3
                    
                    RowLayout {
                        anchors.fill: parent
                        anchors.margins: 10
                        spacing: 15
                        
                        // 因子图标
                        Rectangle {
                            Layout.preferredWidth: 40
                            Layout.preferredHeight: 40
                            radius: 20
                            color: "#007bff"
                            
                            Text {
                                anchors.centerIn: parent
                                text: model.displayName.charAt(0).toUpperCase()
                                font.pixelSize: 16
                                color: "white"
                                font.bold: true
                            }
                        }
                        
                        // 因子信息
                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 5
                            
                            Text {
                                text: model.displayName
                                font.pixelSize: 16
                                font.bold: true
                                color: "#333"
                            }
                            
                            Text {
                                text: model.description || "无描述"
                                font.pixelSize: 12
                                color: "#666"
                                elide: Text.ElideRight
                                maximumLineCount: 2
                            }
                            
                            RowLayout {
                                spacing: 10
                                
                                Text {
                                    text: "类别: " + model.majorCategory + " / " + model.subCategory
                                    font.pixelSize: 11
                                    color: "#888"
                                }
                                
                                Text {
                                    text: "IC: " + model.icValue.toFixed(3)
                                    font.pixelSize: 11
                                    color: model.icValue > 0 ? "#28a745" : "#dc3545"
                                }
                                
                                Text {
                                    text: "IR: " + model.irValue.toFixed(2)
                                    font.pixelSize: 11
                                    color: model.irValue > 1 ? "#28a745" : "#ffc107"
                                }
                            }
                        }
                        
                        // 操作按钮
                        ColumnLayout {
                            spacing: 5
                            
                            Button {
                                text: "详情"
                                onClicked: {
                                    console.log("查看因子详情:", model.factorId)
                                    var factorData = factorViewModel.getFactorByIdFromView(model.factorId)
                                    console.log("因子数据:", factorData)
                                }
                            }
                            
                            Button {
                                text: model.isFavorite ? "取消收藏" : "收藏"
                                onClicked: {
                                    console.log("切换收藏状态:", model.factorId)
                                    factorService.toggleFavorite(model.factorId)
                                }
                            }
                        }
                    }
                }
                
                // 空状态提示
                Text {
                    anchors.centerIn: parent
                    text: "暂无因子数据"
                    font.pixelSize: 16
                    color: "#999"
                    visible: factorListView.count === 0
                }
            }
        }
        
        // 状态信息
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 40
            color: "#f8f9fa"
            border.color: "#e9ecef"
            border.width: 1
            radius: 3
            
            RowLayout {
                anchors.fill: parent
                anchors.margins: 5
                
                Text {
                    text: "因子总数: " + factorListView.count
                    font.pixelSize: 12
                    color: "#333"
                }
                
                Text {
                    text: " | "
                    color: "#ccc"
                }
                
                Text {
                    text: "视图模型状态: " + (factorViewModel ? "已初始化" : "未初始化")
                    font.pixelSize: 12
                    color: "#333"
                }
                
                Text {
                    text: " | "
                    color: "#ccc"
                }
                
                Text {
                    text: "服务状态: " + (factorService ? "已连接" : "未连接")
                    font.pixelSize: 12
                    color: "#333"
                }
            }
        }
    }
    
    // ============ 使用说明 ============
    
    Component.onCompleted: {
        console.log("=== 因子库重构示例 ===")
        console.log("1. FactorViewModel - 只负责视图更新")
        console.log("2. FactorService - 负责业务逻辑")
        console.log("3. 两者通过信号/槽机制协作")
        console.log("4. 视图模型自动监听服务层的数据变更")
        console.log("5. 服务层负责数据库操作、缓存管理")
        console.log("6. 视图层只负责显示和用户交互")
    }
}