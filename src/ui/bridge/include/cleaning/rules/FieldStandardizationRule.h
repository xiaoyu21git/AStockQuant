#pragma once

#include "../CleaningEngine.h"
#include "field_traits.h"

#include <QString>
#include <QHash>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QSqlError>
#include <QSet>
#include <QDebug>

namespace factor::bridge {

// 字段标准化规则
// 负责：1. 合成派生字段（adj_factor = post_adjust_factor）
//       2. 重命名不一致字段（effective_disclosure_date -> disclosure_date）
//       3. 格式化字段（trade_date 去时间后缀）
//       4. 删除数据库内部字段（id, created_at, updated_at, symbol_id, indicator_id）
//       5. 删除原始复权因子字段（pre_adjust_factor, post_adjust_factor -> 已合成为 adj_factor）
//       6. 从 symbol_info 表补充通用字段（name, exchange, asset_class, status, list_date, industry）
class FieldStandardizationRule final : public ICleaningRule {
public:
    FieldStandardizationRule() = default;

    // 允许外部注入数据库连接名（用于已初始化的 QSqlDatabase）
    void setDatabaseConnectionName(const QString& connName) {
        m_dbConnectionName = connName;
    }

    QString id() const override { return "field_standardization"; }
    QString displayName() const override { return QStringLiteral("字段标准化"); }
    int executionOrder() const override { return 20; }

    bool appliesTo(const QVariantMap&) const override { return true; }

    // 横截面：一次性加载所有 symbol_info
    void cleanCrossSectional(QVariantList& records) override {
        loadSymbolInfos(records);
        m_loaded = true;
    }

    bool clean(QVariantMap& record) override {
        // ========== 1. 合成 adj_factor（后复权因子）==========
        auto postAdj = record.value(QStringLiteral("post_adjust_factor"));
        if (postAdj.isValid() && !postAdj.isNull()) {
            bool ok = false;
            double adjVal = postAdj.toDouble(&ok);
            if (ok && std::isfinite(adjVal) && adjVal > 0.0) {
                Accessors::AdjFactor.set(record, adjVal);
            }
        }
        // 如果 post_adjust_factor 不存在，尝试 pre_adjust_factor 作兜底
        if (!Accessors::AdjFactor.get(record).has_value()) {
            auto preAdj = record.value(QStringLiteral("pre_adjust_factor"));
            if (preAdj.isValid() && !preAdj.isNull()) {
                bool ok = false;
                double adjVal = preAdj.toDouble(&ok);
                if (ok && std::isfinite(adjVal) && adjVal > 0.0) {
                    Accessors::AdjFactor.set(record, adjVal);
                }
            }
        }

        // ========== 2. 重命名字段 ==========
        // effective_disclosure_date -> disclosure_date
        auto effDisc = record.value(QStringLiteral("effective_disclosure_date"));
        if (effDisc.isValid() && !effDisc.isNull() && !record.contains(QStringLiteral("disclosure_date"))) {
            QString val = effDisc.toString().trimmed();
            if (!val.isEmpty()) {
                record[QStringLiteral("disclosure_date")] = val.left(10);
            }
        }

        // ========== 3. 格式化日期字段，去掉时间后缀 ==========
        auto fixDateField = [&](const QString& fieldName) {
            auto v = record.value(fieldName);
            if (v.isValid()) {
                QString s = v.toString().trimmed();
                if (s.contains(' ')) {
                    record[fieldName] = s.left(10);
                }
            }
        };
        fixDateField(QStringLiteral("trade_date"));
        fixDateField(QStringLiteral("report_date"));
        fixDateField(QStringLiteral("disclosure_date"));

        // ========== 4. 标准化 data_source / data_type ==========
        auto sourceVal = record.value(QStringLiteral("source"));
        if (sourceVal.isValid()) {
            QString src = sourceVal.toString().trimmed();
            if (!src.isEmpty()) {
                if (!record.contains(QStringLiteral("data_source")))
                    record[QStringLiteral("data_source")] = src;
            }
            record.remove(QStringLiteral("source"));
        }
        auto dtVal = record.value(QStringLiteral("dataType"));
        if (dtVal.isValid()) {
            if (!record.contains(QStringLiteral("data_type")))
                record[QStringLiteral("data_type")] = dtVal.toString();
            record.remove(QStringLiteral("dataType"));
        }

        // ========== 5. 删除不需要的内部字段 ==========
        static const QStringList internalFields = {
            QStringLiteral("id"),
            QStringLiteral("created_at"),
            QStringLiteral("updated_at"),
            QStringLiteral("symbol_id"),
            QStringLiteral("indicator_id"),
            QStringLiteral("pre_adjust_factor"),
            QStringLiteral("post_adjust_factor"),
            QStringLiteral("effective_disclosure_date"),
        };
        for (const auto& f : internalFields) {
            record.remove(f);
        }

        // ========== 6. 补充通用字段（从 symbol_info 缓存）==========
        if (m_loaded) {
            QString symbol = Accessors::Symbol.get(record).value_or(QString());
            if (!symbol.isEmpty()) {
                auto it = m_symbolInfoCache.constFind(symbol);
                if (it != m_symbolInfoCache.constEnd()) {
                    const auto& info = it.value();
                    // 仅补充行内缺失的字段
                    if (!Accessors::Name.has(record))
                        Accessors::Name.set(record, info.name);
                    if (!Accessors::Exchange.has(record))
                        Accessors::Exchange.set(record, info.exchange);
                    if (!Accessors::AssetClass.has(record))
                        Accessors::AssetClass.set(record, info.assetClass);
                    if (!Accessors::StatusVal.has(record))
                        Accessors::StatusVal.set(record, info.status);
                    if (!Accessors::ListDate.has(record))
                        Accessors::ListDate.set(record, info.listDate);
                    if (!Accessors::Industry.has(record) && !info.industry.isEmpty())
                        Accessors::Industry.set(record, info.industry);
                }
            }
        }

        return true;
    }

private:
    struct SymbolInfo {
        QString name;
        QString exchange;
        QString assetClass;
        QString status;
        QString listDate;
        QString industry;
    };

    void loadSymbolInfos(const QVariantList& records) {
        // ====== 收集所有需要查询的 symbol ======
        QSet<QString> symbolsNeeded;
        for (const QVariant& item : records) {
            if (!item.canConvert<QVariantMap>()) continue;
            QVariantMap row = item.toMap();
            auto sym = Accessors::Symbol.get(row);
            if (sym.has_value() && !sym->isEmpty()) {
                symbolsNeeded.insert(*sym);
            }
        }
        if (symbolsNeeded.isEmpty()) return;

        // ====== 优先从已有缓存查（复用上次加载的数据）======
        QSet<QString> toQuery;
        for (const auto& sym : symbolsNeeded) {
            if (!m_symbolInfoCache.contains(sym))
                toQuery.insert(sym);
        }
        if (toQuery.isEmpty()) return;

        // ====== 从数据库查询 ======
        QString connName = m_dbConnectionName.isEmpty()
            ? QStringLiteral("qt_sql_default_connection")
            : m_dbConnectionName;

        if (!QSqlDatabase::contains(connName)) {
            qWarning() << "FieldStandardizationRule: 数据库连接" << connName << "不存在，跳过 symbol_info 加载";
            return;
        }

        QSqlDatabase db = QSqlDatabase::database(connName);
        if (!db.isOpen()) {
            qWarning() << "FieldStandardizationRule: 数据库未打开";
            return;
        }

        // 分批查询，避免 SQL 过长
        const int batchSize = 200;
        QList<QString> symList = toQuery.values();
        for (int i = 0; i < symList.size(); i += batchSize) {
            QList<QString> batch = symList.mid(i, batchSize);
            QStringList quotedSymbols;
            for (const auto& s : batch) {
                QString escaped = s;
                escaped.replace("'", "''");
                quotedSymbols << QStringLiteral("'%1'").arg(escaped);
            }

            QString sql = QStringLiteral(
                "SELECT symbol, name, exchange, asset_class, "
                "       COALESCE(list_date, '') AS list_date, "
                "       COALESCE(status, '') AS status, "
                "       COALESCE(industry, '') AS industry "
                "FROM symbol_info "
                "WHERE symbol IN (%1)")
                .arg(quotedSymbols.join(QStringLiteral(",")));

            QSqlQuery query(db);
            if (!query.exec(sql)) {
                qWarning() << "FieldStandardizationRule: symbol_info 查询失败:" << query.lastError().text();
                continue;
            }

            while (query.next()) {
                SymbolInfo info;
                info.name = query.value(QStringLiteral("name")).toString().trimmed();
                info.exchange = query.value(QStringLiteral("exchange")).toString().trimmed();
                info.assetClass = query.value(QStringLiteral("asset_class")).toString().trimmed();
                info.status = query.value(QStringLiteral("status")).toString().trimmed();
                info.listDate = query.value(QStringLiteral("list_date")).toString().trimmed();
                info.industry = query.value(QStringLiteral("industry")).toString().trimmed();
                QString sym = query.value(QStringLiteral("symbol")).toString().trimmed();
                if (!sym.isEmpty())
                    m_symbolInfoCache[sym] = info;
            }
        }

        qDebug().noquote() << QStringLiteral("FieldStandardizationRule: 已加载 %1 条 symbol_info (查询 %2 条)")
                                .arg(m_symbolInfoCache.size()).arg(toQuery.size());
    }

    QString m_dbConnectionName;
    QHash<QString, SymbolInfo> m_symbolInfoCache;
    bool m_loaded = false;
};

} // namespace factor::bridge
