// FactorViewModel.h
// 因子视图模型 - 只负责视图更新，不包含业务逻辑
// 设计模式：像PreviewDataModel一样，通过Q_INVOKABLE方法更新数据
#pragma once

#include <QAbstractListModel>
#include <QVector>
#include <QString>
#include <QVariant>
#include <QStringList>

class FactorViewModel : public QAbstractListModel {
    Q_OBJECT
    
    Q_PROPERTY(int count READ rowCount NOTIFY countChanged)
    
public:
    // 角色枚举
    enum RoleNames {
        FactorIdRole = Qt::UserRole + 1,
        FactorNameRole,
        DisplayNameRole,
        MajorCategoryRole,
        SubCategoryRole,
        DescriptionRole,
        IcValueRole,
        IrValueRole,
        ValidityDaysRole,
        TurnoverRateRole,
        IsRecommendedRole,
        IsFavoriteRole,
        StatusRole,
        TagsRole,
        CreatorRole,
        CreateDateRole,
        GroupReturnsRole
    };
    Q_ENUM(RoleNames)
    
    explicit FactorViewModel(QObject* parent = nullptr);
    ~FactorViewModel();
    
    // QAbstractListModel接口
    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;
    
    // 数据操作方法 - 像PreviewDataModel一样，QML调用这些方法更新数据
    Q_INVOKABLE void updateData(const QVariantList& factors);
    Q_INVOKABLE void clearData();
    Q_INVOKABLE void appendData(const QVariantMap& factorData);
    Q_INVOKABLE void addDataBatch(const QVariantList& factors);
    
    // 数据访问方法
    Q_INVOKABLE QVariantMap getRow(int index) const;
    Q_INVOKABLE QVariantMap getFactorById(const QString& factorId) const;
    Q_INVOKABLE QVariantList getAllFactors() const;
    Q_INVOKABLE QVariantList searchFactors(const QString& keyword) const;
    Q_INVOKABLE QVariantList filterFactorsByCategory(const QString& category) const;
    Q_INVOKABLE QVariantList filterFactorsByTags(const QStringList& tags) const;
    
    // 单个因子操作
    Q_INVOKABLE void updateFactor(const QString& factorId, const QVariantMap& factorData);
    Q_INVOKABLE void removeFactor(const QString& factorId);
    
signals:
    void countChanged();
    void dataUpdated();
    
private:
    // 因子数据结构
    struct FactorViewData {
        QString factorId;
        QString factorName;
        QString displayName;
        QString majorCategory;
        QString subCategory;
        QString description;
        double icValue;
        double irValue;
        int validityDays;
        double turnoverRate;
        bool isRecommended;
        bool isFavorite;
        QString status;
        QStringList tags;
        QString creator;
        QString createDate;
        QVector<double> groupReturns;
        
        QVariantMap toVariantMap() const;
        static FactorViewData fromVariantMap(const QVariantMap& map);
        bool operator==(const FactorViewData& other) const;
    };
    
    // 数据查找
    int findIndexById(const QString& factorId) const;
    bool hasSameData(const QVariantList& factors) const;
    
private:
    QVector<FactorViewData> m_factors;
};
