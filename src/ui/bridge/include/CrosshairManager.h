// CrosshairManager.h — 十字光标状态管理 (单例)
// 职责: 存储当前选中的数据索引, 通知所有 Canvas 同步绘制十字线
// QML 鼠标移动 → setSelectedIndex → selectedIndexChanged → 3 Canvas 各自重绘
#pragma once

#include <QObject>

namespace bridge {

class CrosshairManager : public QObject {
    Q_OBJECT
    Q_PROPERTY(int selectedIndex READ selectedIndex NOTIFY selectedIndexChanged)
    Q_PROPERTY(bool visible READ isVisible NOTIFY visibleChanged)

public:
    static CrosshairManager& instance();

    int selectedIndex() const { return m_index; }
    bool isVisible() const { return m_visible; }

public slots:
    void setSelectedIndex(int idx);
    void setVisible(bool v);
    void hide();

signals:
    void selectedIndexChanged();
    void visibleChanged();

private:
    explicit CrosshairManager(QObject* parent = nullptr);
    int m_index = -1;
    bool m_visible = false;
};

} // namespace bridge
