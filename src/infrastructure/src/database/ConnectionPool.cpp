#include "../../include/database/ConnectionPool.h"
#include <QSqlError>
#include <QSqlQuery>

namespace {

bool verboseDatabasePoolLogging()
{
    return qEnvironmentVariableIntValue("ASTOCK_VERBOSE_DB_POOL") > 0;
}

}

namespace astock {
namespace database {

ConnectionPool::ConnectionPool()
    : m_hostName("127.0.0.1")
    , m_databaseName("astock_quant")
    , m_username("root")
    , m_password("123456a")
    , m_port(3306)
    , m_maxConnectionCount(10)      // 最多10个连接
    , m_connectionTimeout(3000)      // 获取连接超时3秒
    , m_cleanupInterval(60000)       // 每分钟清理一次
    , m_isDestroyed(false)
{
    // 娣诲姞MySQL鏁版嵁搴撻┍鍔?
    if (!QSqlDatabase::isDriverAvailable("QMYSQL")) {
        qWarning() << "MySQL driver is not available!";
    }
    
    // 娉ㄦ剰锛氱敱浜庣Щ闄や簡QObject缁ф壙锛屽畾鏃跺櫒鍔熻兘琚鐢?
    // 濡傛灉闇€瑕佸畾鏃舵竻鐞嗗姛鑳斤紝闇€瑕侀噸鏂拌璁″畾鏃跺櫒瀹炵幇
    // connect(&m_cleanupTimer, &QTimer::timeout, this, &ConnectionPool::cleanIdleConnections);
    // m_cleanupTimer.start(m_cleanupInterval);
    
    qDebug() << "Connection pool initialized, max connections:" << m_maxConnectionCount;
    qWarning() << "Warning: Timer-based cleanup is disabled due to removal of QObject inheritance";
}

ConnectionPool::~ConnectionPool()
{
    destroy();
}

ConnectionPool& ConnectionPool::instance()
{
    // 进程生命周期单例，避免在进程退出时触发 Qt SQL 静态析构顺序问题。
    static ConnectionPool* pool = new ConnectionPool();
    return *pool;
}

void ConnectionPool::configure(const QString& hostName, const QString& databaseName,
                              const QString& username, const QString& password,
                              int port, int maxConnections)
{
    QMutexLocker locker(&m_mutex);
    
    m_hostName = hostName;
    m_databaseName = databaseName;
    m_username = username;
    m_password = password;
    m_port = port;
    m_maxConnectionCount = maxConnections;
    
    qDebug() << "Connection pool configured:"
             << "host:" << m_hostName
             << "database:" << m_databaseName
             << "username:" << m_username
             << "port:" << m_port
             << "max connections:" << m_maxConnectionCount;
}

void ConnectionPool::destroy()
{
    QMutexLocker locker(&m_mutex);
    
    if (m_isDestroyed) {
        return;
    }
    
    m_isDestroyed = true;
    m_cleanupTimer.stop();
    
    // 鍏抽棴鎵€鏈夎繛鎺?
    for (const QString& name : m_idleConnectionNames) {
        QSqlDatabase::removeDatabase(name);
    }
    
    for (const QString& name : m_usedConnectionNames) {
        QSqlDatabase::removeDatabase(name);
    }
    
    m_idleConnectionNames.clear();
    m_usedConnectionNames.clear();
    
    qDebug() << "Connection pool destroyed";
}

QSqlDatabase ConnectionPool::createConnection()
{
    static int connectionCount = 0;
    
    // 检查MySQL驱动是否可用
    if (!QSqlDatabase::isDriverAvailable("QMYSQL")) {
        qCritical() << "MySQL driver (QMYSQL) is not available!";
        qCritical() << "Available drivers:" << QSqlDatabase::drivers();
        return QSqlDatabase();
    }
    
    QString connectionName = QString("connection_%1_%2")
                              .arg(QString::number((quint64)QThread::currentThreadId()))
                              .arg(++connectionCount);
    
    qDebug() << "Creating connection:" << connectionName;
    qDebug() << "  Host:" << m_hostName;
    qDebug() << "  Database:" << m_databaseName;
    qDebug() << "  Username:" << m_username;
    qDebug() << "  Port:" << m_port;
    
    QSqlDatabase db = QSqlDatabase::addDatabase("QMYSQL", connectionName);
    db.setHostName(m_hostName);
    db.setDatabaseName(m_databaseName);
    db.setUserName(m_username);
    db.setPassword(m_password);
    db.setPort(m_port);
    
    // 设置连接选项
    db.setConnectOptions("MYSQL_OPT_RECONNECT=1");  // 自动重连
    
    if (!db.open()) {
        qCritical() << "Failed to create connection:" << db.lastError().text();
        qCritical() << "Error details:" << db.lastError().databaseText();
        QSqlDatabase::removeDatabase(connectionName);
        return QSqlDatabase();
    }
    
    qDebug() << "✅ Created new connection:" << connectionName;
    return db;
}

QSqlDatabase ConnectionPool::getConnection()
{
    QMutexLocker locker(&m_mutex);
    
    if (m_isDestroyed) {
        qWarning() << "Connection pool is destroyed, returning invalid database";
        return QSqlDatabase();
    }
    
    // 濡傛灉鏈夌┖闂茶繛鎺ワ紝鐩存帴鍙栧嚭涓€涓?
    if (!m_idleConnectionNames.isEmpty()) {
        QString connectionName = m_idleConnectionNames.dequeue();
        m_usedConnectionNames.enqueue(connectionName);
        
        QSqlDatabase db = QSqlDatabase::database(connectionName);
        if (db.isOpen() && !db.isOpenError()) {
            if (verboseDatabasePoolLogging()) {
                qDebug() << "Reusing idle connection:" << connectionName;
            }
            return db;
        } else {
            // 杩炴帴宸叉柇寮€锛岄噸鏂板垱寤?
            qDebug() << "Connection is dead, creating new one:" << connectionName;
            QSqlDatabase::removeDatabase(connectionName);
            
            // 浠庡凡浣跨敤闃熷垪涓Щ闄?
            m_usedConnectionNames.removeOne(connectionName);
            
            // 鍒涘缓鏂拌繛鎺?
            db = createConnection();
            if (db.isValid()) {
                m_usedConnectionNames.enqueue(db.connectionName());
                return db;
            }
        }
    }
    
    // 娌℃湁绌洪棽杩炴帴锛屾鏌ユ槸鍚﹀彲浠ュ垱寤烘柊杩炴帴
    int totalCount = m_idleConnectionNames.size() + m_usedConnectionNames.size();
    if (totalCount < m_maxConnectionCount) {
        QSqlDatabase db = createConnection();
        if (db.isValid()) {
            m_usedConnectionNames.enqueue(db.connectionName());
            return db;
        }
    }
    
    // 杈惧埌鏈€澶ц繛鎺ユ暟锛岀瓑寰呮湁杩炴帴琚噴鏀?
    qDebug() << "No available connection, waiting...";
    
    if (!m_waitCondition.wait(&m_mutex, m_connectionTimeout)) {
        qWarning() << "Get connection timeout after" << m_connectionTimeout << "ms";
        return QSqlDatabase();
    }
    
    // 琚敜閱掑悗閲嶆柊灏濊瘯鑾峰彇
    if (!m_idleConnectionNames.isEmpty()) {
        QString connectionName = m_idleConnectionNames.dequeue();
        m_usedConnectionNames.enqueue(connectionName);
        
        QSqlDatabase db = QSqlDatabase::database(connectionName);
        if (db.isOpen()) {
            qDebug() << "Got connection after waiting:" << connectionName;
            return db;
        }
    }
    
    qWarning() << "Failed to get connection";
    return QSqlDatabase();
}

void ConnectionPool::releaseConnection(const QSqlDatabase& db)
{
    QMutexLocker locker(&m_mutex);
    
    QString connectionName = db.connectionName();
    
    // 浠庢鍦ㄤ娇鐢ㄩ槦鍒楃Щ鍒扮┖闂查槦鍒?
    if (m_usedConnectionNames.contains(connectionName)) {
        m_usedConnectionNames.removeOne(connectionName);
        m_idleConnectionNames.enqueue(connectionName);
        
        // 鍞ら啋鍙兘姝ｅ湪绛夊緟鐨勭嚎绋?
        m_waitCondition.wakeOne();
        
        if (verboseDatabasePoolLogging()) {
            qDebug() << "Released connection:" << connectionName;
        }
    } else {
        qWarning() << "Trying to release a connection not from this pool:" << connectionName;
    }
}

void ConnectionPool::cleanIdleConnections()
{
    QMutexLocker locker(&m_mutex);
    
    if (m_isDestroyed) {
        return;
    }
    
    // 濡傛灉绌洪棽杩炴帴澶锛屽叧闂竴閮ㄥ垎锛堜繚鐣欒嚦灏?涓級
    int idleCount = m_idleConnectionNames.size();
    int keepCount = qMin(2, idleCount);  // 鑷冲皯淇濈暀2涓┖闂茶繛鎺?
    
    qDebug() << "Cleaning idle connections, current idle:" << idleCount 
             << "used:" << m_usedConnectionNames.size();
    
    for (int i = idleCount - 1; i >= keepCount; --i) {
        QString connectionName = m_idleConnectionNames.at(i);
        
        // 鍏抽棴杩炴帴
        {
            QSqlDatabase db = QSqlDatabase::database(connectionName);
            if (db.isOpen()) {
                db.close();
            }
        } // db 鍦ㄨ繖閲岃閿€姣?
        
        QSqlDatabase::removeDatabase(connectionName);
        m_idleConnectionNames.removeAt(i);
        
        qDebug() << "Closed idle connection:" << connectionName;
    }
}

} // namespace database
} // namespace astock}

