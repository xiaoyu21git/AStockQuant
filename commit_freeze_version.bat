@echo off
echo === AStockQuantEngine 接口冻结版本提交 ===
echo.

REM 检查git状态
echo 1. 检查git状态...
git status

echo.
echo 2. 添加变更文件...
git add .

echo.
echo 3. 提交变更...
git commit -m "feat: 接口冻结版本 v1.0.0

- 项目总结报告完成
- API接口冻结声明
- .gitignore更新排除临时文件
- 数据清洗流程完善
- 第三方API集成框架完成
- 文档完善和代码整理

详细变更：
1. 新增 PROJECT_SUMMARY_2026_02_12.md 项目总结报告
2. 新增 API_INTERFACE_FREEZE.md 接口冻结声明
3. 更新 .gitignore 排除测试文件和临时文件
4. 完善数据清洗引擎和UI界面
5. 完成掘金C++ API集成框架
6. 整理项目结构和文档

版本: v1.0.0 (接口冻结版)
日期: 2026年2月12日"

echo.
echo 4. 推送到远程仓库...
git push origin dev

echo.
echo === 提交完成 ===
echo.
echo 提交信息已记录，版本已冻结。
echo 项目状态：接口冻结准备完成，可以进入长假维护模式。
echo.
pause