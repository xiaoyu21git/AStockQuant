#pragma once

#include <QObject>
#include <QVariantMap>
#include <QString>

/// @brief 规则模板建议服务 — QML 规则编排面板的搜索后端
///
/// 从共享规则库 (compiled.json → RuleLibrary) 按关键词/阶段/动作过滤模板,
/// 通过 suggestionReady/suggestionFailed 信号异步返回结果。
class RuleTemplateSuggestionService : public QObject {
    Q_OBJECT
public:
    explicit RuleTemplateSuggestionService(QObject* parent = nullptr);

    /// 确保共享规则库已加载 (幂等, QML Component.onCompleted + 每次搜索前调用)
    Q_INVOKABLE void initialize();

    /// 发起模板建议请求
    /// @param requestId  QML 侧预先生成的请求 ID, 用于关联异步结果
    /// @param request   { text, phase?, action?, stageId?, groupRole?, groupId?,
    ///                    strategyProfile?, onlyReady?, limit? }
    Q_INVOKABLE void suggestTemplatesRequestAsync(const QString& requestId, const QVariantMap& request);

signals:
    /// 建议成功: { requestId, resolvedTermId, resolvedTermDisplayName, suggestions[] }
    void suggestionReady(const QVariantMap& result);
    /// 建议失败: { requestId, error }
    void suggestionFailed(const QVariantMap& error);

private:
    bool m_initialized{false};
};
