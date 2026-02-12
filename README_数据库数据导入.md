# 数据库数据导入指南

## 问题描述

当您在AStockQuantEngine应用程序中看到"数据库中没有找到匹配的数据"错误时，这是因为MySQL数据库中没有数据。应用程序只能从数据库中查询数据，而不能直接从掘金API获取数据。

## 解决方案

您需要使用数据导入工具将掘金数据导入到MySQL数据库中。

## 数据导入步骤

### 1. 检查数据库连接配置

确保MySQL数据库正在运行，并且配置正确：
- 主机：localhost
- 端口：3306
- 数据库：astock_quant
- 用户名：root
- 密码：123456a

### 2. 使用数据导入工具

#### 方法一：测试模式（不保存到数据库）
```bash
# 测试数据获取功能
python import_juejin_data.py --test-only

# 测试特定股票
python import_juejin_data.py --test-only --symbols "600000.SH,000001.SZ"

# 测试特定时间范围
python import_juejin_data.py --test-only --start-date "2024-01-01" --end-date "2024-01-31"
```

#### 方法二：实际导入数据到数据库
```bash
# 导入默认股票数据
python import_juejin_data.py --save-to-db

# 导入特定股票数据
python import_juejin_data.py --save-to-db --symbols "600000.SH,000001.SZ,000002.SZ"

# 导入特定时间范围的数据
python import_juejin_data.py --save-to-db --start-date "2024-01-01" --end-date "2024-06-30"
```

### 3. 验证数据导入

导入完成后，您可以在应用程序中：
1. 重新启动应用程序
2. 在数据源选择界面选择"MySQL数据库"
3. 选择股票代码和时间范围
4. 点击"加载数据"按钮

## 常用命令示例

### 导入A股主要股票数据
```bash
python import_juejin_data.py --save-to-db --symbols "600000.SH,000001.SZ,000002.SZ,600036.SH,600519.SH"
```

### 导入最近一年的数据
```bash
python import_juejin_data.py --save-to-db --start-date "2024-01-01" --end-date "2024-12-31"
```

### 批量导入多个股票
```bash
# 创建股票列表文件
echo "600000.SH
000001.SZ
000002.SZ
600036.SH
600519.SH" > stock_list.txt

# 导入所有股票
python import_juejin_data.py --save-to-db --symbols "$(cat stock_list.txt | tr '\n' ',')"
```

## 故障排除

### 1. 数据库连接失败
- 检查MySQL服务是否运行
- 检查数据库配置是否正确
- 检查防火墙设置

### 2. 数据导入失败
- 检查网络连接
- 检查掘金API配置
- 查看错误日志

### 3. 应用程序无法加载数据
- 确保数据已成功导入数据库
- 检查应用程序的数据库配置
- 重启应用程序

## 高级用法

### 定时自动导入
您可以创建定时任务（如cron job或Windows任务计划程序）定期导入数据：

```bash
# 每天凌晨1点导入数据
0 1 * * * cd /path/to/AStockQuantEngine && python import_juejin_data.py --save-to-db
```

### 增量导入
```bash
# 导入最近30天的数据
python import_juejin_data.py --save-to-db --start-date "$(date -d '30 days ago' +%Y-%m-%d)" --end-date "$(date +%Y-%m-%d)"
```

## 注意事项

1. **数据量**：导入大量数据可能需要较长时间
2. **网络连接**：确保稳定的网络连接
3. **数据库空间**：确保数据库有足够的存储空间
4. **权限**：确保有数据库写入权限

## 技术支持

如果遇到问题，请：
1. 查看错误日志
2. 检查数据库状态
3. 联系技术支持团队

---

**重要提示**：数据导入是使用应用程序的前提条件。没有数据，应用程序无法进行数据分析和策略回测。