#!/usr/bin/env python3
"""StrategyRepository.cpp / FactorRepository.cpp 中 QSqlQuery → ISqlDatabase 模式替换"""
import re, sys

def convert_file(path):
    with open(path, 'r', encoding='utf-8') as f:
        content = f.read()

    # 1. conn.get() → conn.db()
    content = re.sub(r'conn\.get\(\)', 'conn.db()', content)

    # 2. QSqlDatabase& db = conn.db() → auto& db = conn.db()
    content = re.sub(r'QSqlDatabase\s*&\s*db\s*=\s*conn\.db\(\)', 'auto& db = conn.db()', content)

    # 3. Replace: QSqlQuery query(db); → use conn.exec() pattern
    #    This is the complex one — can't fully automate
    #    But we can fix the common patterns

    # 4. query.value("col") → row.getString("col") [only in row mapping functions]
    #    Leave for manual fix

    # 5. QSqlDatabase& db parameter → std::shared_ptr<ISqlDatabase>& db
    content = re.sub(r'QSqlDatabase\s*&\s*db', 'std::shared_ptr<ISqlDatabase>& db', content)

    # 6. const QSqlQuery& query → const SqlQueryResultRow& row
    content = re.sub(r'const QSqlQuery\s*&\s*query', 'const SqlQueryResultRow& row', content)
    content = re.sub(r'const QSqlQuery\s*&\s*q', 'const SqlQueryResultRow& row', content)

    # 7. query.value( → row.getString( [for string columns]
    content = re.sub(r'query\.value\(k(\w+)Key\)', r'QString::fromStdString(row.getString("\1"))', content)
    content = re.sub(r'query\.value\("(\w+)"\)', r'QString::fromStdString(row.getString("\1"))', content)
    content = re.sub(r'query\.value\("([^"]+)"\)\.toString\(\)', r'QString::fromStdString(row.getString("\1"))', content)
    content = re.sub(r'query\.value\("([^"]+)"\)\.toInt\(\)', r'row.getInt("\1")', content)
    content = re.sub(r'query\.value\("([^"]+)"\)\.toDouble\(\)', r'row.getDouble("\1")', content)
    content = re.sub(r'query\.value\("([^"]+)"\)\.toBool\(\)', r'(row.getString("\1") == "1" || row.getString("\1") == "true")', content)

    # 8. db.transaction() → db->beginTransaction()
    content = content.replace('db.transaction()', 'db->beginTransaction()')
    content = content.replace('db.commit()', 'db->commitTransaction()')
    content = content.replace('db.rollback()', 'db->rollbackTransaction()')

    # 9. query.lastError().text() → db->lastError()
    content = content.replace('query.lastError().text()', 'db->lastError()')

    # 10. QSqlQuery query(db); query.prepare(sql); query.addBindValue(v); if(!query.exec())
    #     → auto result = conn.exec(sql, {SqlParam{v}}); if(result.isEmpty())
    content = re.sub(
        r'QSqlQuery query\(([^)]+)\);\s*query\.prepare\("([^"]+)"\);\s*query\.addBindValue\(([^)]+)\);\s*if\s*\(!query\.exec\(\)\)',
        r'auto result = conn.exec("\2", {SqlParam{\3}}); if(result.isEmpty())',
        content
    )

    with open(path, 'w', encoding='utf-8') as f:
        f.write(content)

    print(f'{path}: converted')

if __name__ == '__main__':
    for p in sys.argv[1:]:
        convert_file(p)
