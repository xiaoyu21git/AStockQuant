#pragma once
// StockNameResolver — 股票名称解析器 (纯工具类，零 Q_OBJECT，可安全包含于任何编译单元)

#include <QString>
#include <QHash>

class StockNameResolver {
public:
    /// @brief symbol → "平安银行"，线程安全，首次调用从 DB 加载
    static QString name(const QString& symbol);

    /// @brief symbol → "平安银行 000001.SZ"
    static QString displayName(const QString& symbol);

    StockNameResolver() = delete;
};
