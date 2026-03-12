#pragma once

#include <QObject>
#include <QString>
#include <QVariant>
#include <QVector>
#include <QJsonObject>
#include <QJsonArray>
#include <QMap>
#include <QColor>

namespace AStockQuantEngine::UI {

// 字段类型枚举
enum class FieldType {
    Text,       // 文本
    Number,     // 数字
    Boolean,    // 布尔
    Date,       // 日期
    DateTime,   // 日期时间
    Select,     // 下拉选择
    Tags,       // 标签数组
    Progress,   // 进度条
    Badge,      // 徽章（带颜色）
    Link,       // 链接
    Action      // 操作按钮
};

QString fieldTypeToString(FieldType type);
FieldType stringToFieldType(const QString& typeStr);

// 字段定义（用于表格、表单、卡片）
class MetaField : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString name READ name WRITE setName NOTIFY nameChanged)
    Q_PROPERTY(QString label READ label WRITE setLabel NOTIFY labelChanged)
    Q_PROPERTY(FieldType type READ type WRITE setType NOTIFY typeChanged)
    Q_PROPERTY(bool visible READ visible WRITE setVisible NOTIFY visibleChanged)
    Q_PROPERTY(bool sortable READ sortable WRITE setSortable NOTIFY sortableChanged)
    Q_PROPERTY(bool filterable READ filterable WRITE setFilterable NOTIFY filterableChanged)
    Q_PROPERTY(bool editable READ editable WRITE setEditable NOTIFY editableChanged)
    Q_PROPERTY(bool required READ required WRITE setRequired NOTIFY requiredChanged)
    Q_PROPERTY(QString format READ format WRITE setFormat NOTIFY formatChanged)
    Q_PROPERTY(QString unit READ unit WRITE setUnit NOTIFY unitChanged)
    Q_PROPERTY(int width READ width WRITE setWidth NOTIFY widthChanged)
    Q_PROPERTY(int precision READ precision WRITE setPrecision NOTIFY precisionChanged)
    Q_PROPERTY(QString placeholder READ placeholder WRITE setPlaceholder NOTIFY placeholderChanged)
    Q_PROPERTY(QString description READ description WRITE setDescription NOTIFY descriptionChanged)
    Q_PROPERTY(QVariant defaultValue READ defaultValue WRITE setDefaultValue NOTIFY defaultValueChanged)
    
public:
    explicit MetaField(QObject* parent = nullptr);
    MetaField(const MetaField& other);
    MetaField& operator=(const MetaField& other);
    
    // Getters
    QString name() const { return name_; }
    QString label() const { return label_; }
    FieldType type() const { return type_; }
    bool visible() const { return visible_; }
    bool sortable() const { return sortable_; }
    bool filterable() const { return filterable_; }
    bool editable() const { return editable_; }
    bool required() const { return required_; }
    QString format() const { return format_; }
    QString unit() const { return unit_; }
    int width() const { return width_; }
    int precision() const { return precision_; }
    QString placeholder() const { return placeholder_; }
    QString description() const { return description_; }
    QVariant defaultValue() const { return defaultValue_; }
    
    // Setters
    void setName(const QString& name);
    void setLabel(const QString& label);
    void setType(FieldType type);
    void setVisible(bool visible);
    void setSortable(bool sortable);
    void setFilterable(bool filterable);
    void setEditable(bool editable);
    void setRequired(bool required);
    void setFormat(const QString& format);
    void setUnit(const QString& unit);
    void setWidth(int width);
    void setPrecision(int precision);
    void setPlaceholder(const QString& placeholder);
    void setDescription(const QString& description);
    void setDefaultValue(const QVariant& value);
    
    // 选项管理
    Q_INVOKABLE void addOption(const QString& label, const QVariant& value);
    Q_INVOKABLE QVariantList getOptions() const;
    Q_INVOKABLE void clearOptions();
    
    // 值映射管理
    Q_INVOKABLE void addValueMapping(const QVariant& value, const QString& label, const QString& color);
    Q_INVOKABLE QVariantMap getValueMappings() const;
    Q_INVOKABLE void clearValueMappings();
    
    // 序列化
    QJsonObject toJson() const;
    static MetaField* fromJson(const QJsonObject& json, QObject* parent = nullptr);
    
signals:
    void nameChanged();
    void labelChanged();
    void typeChanged();
    void visibleChanged();
    void sortableChanged();
    void filterableChanged();
    void editableChanged();
    void requiredChanged();
    void formatChanged();
    void unitChanged();
    void widthChanged();
    void precisionChanged();
    void placeholderChanged();
    void descriptionChanged();
    void defaultValueChanged();
    
private:
    QString name_;
    QString label_;
    FieldType type_ = FieldType::Text;
    bool visible_ = true;
    bool sortable_ = false;
    bool filterable_ = false;
    bool editable_ = false;
    bool required_ = false;
    QString format_;
    QString unit_;
    int width_ = -1; // -1表示自动
    int precision_ = 2;
    QString placeholder_;
    QString description_;
    QVariant defaultValue_;
    
    // 选项（用于Select类型）
    struct Option {
        QString label;
        QVariant value;
    };
    QVector<Option> options_;
    
    // 值映射（用于Badge类型）- 使用QString作为键，因为QVariant不能直接用作QHash键
    struct ValueMapping {
        QString label;
        QColor color;
    };
    QHash<QString, ValueMapping> valueMappings_;
};

// 操作定义
class MetaAction : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString name READ name WRITE setName NOTIFY nameChanged)
    Q_PROPERTY(QString label READ label WRITE setLabel NOTIFY labelChanged)
    Q_PROPERTY(QString icon READ icon WRITE setIcon NOTIFY iconChanged)
    Q_PROPERTY(QString type READ type WRITE setType NOTIFY typeChanged)
    Q_PROPERTY(bool confirm READ confirm WRITE setConfirm NOTIFY confirmChanged)
    Q_PROPERTY(QString confirmText READ confirmText WRITE setConfirmText NOTIFY confirmTextChanged)
    
public:
    explicit MetaAction(QObject* parent = nullptr);
    
    // Getters
    QString name() const { return name_; }
    QString label() const { return label_; }
    QString icon() const { return icon_; }
    QString type() const { return type_; }
    bool confirm() const { return confirm_; }
    QString confirmText() const { return confirmText_; }
    
    // Setters
    void setName(const QString& name);
    void setLabel(const QString& label);
    void setIcon(const QString& icon);
    void setType(const QString& type);
    void setConfirm(bool confirm);
    void setConfirmText(const QString& text);
    
    // 序列化
    QJsonObject toJson() const;
    static MetaAction* fromJson(const QJsonObject& json, QObject* parent = nullptr);
    
signals:
    void nameChanged();
    void labelChanged();
    void iconChanged();
    void typeChanged();
    void confirmChanged();
    void confirmTextChanged();
    
private:
    QString name_;
    QString label_;
    QString icon_;
    QString type_ = "default"; // primary/danger/default
    bool confirm_ = false;
    QString confirmText_;
};

// 视图配置（一个页面就是一个View）
class MetaView : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString id READ id WRITE setId NOTIFY idChanged)
    Q_PROPERTY(QString title READ title WRITE setTitle NOTIFY titleChanged)
    Q_PROPERTY(QString type READ type WRITE setType NOTIFY typeChanged)
    Q_PROPERTY(QString model READ model WRITE setModel NOTIFY modelChanged)
    Q_PROPERTY(QString filterModel READ filterModel WRITE setFilterModel NOTIFY filterModelChanged)
    Q_PROPERTY(QVariantMap layout READ layout WRITE setLayout NOTIFY layoutChanged)
    
public:
    explicit MetaView(QObject* parent = nullptr);
    
    // Getters
    QString id() const { return id_; }
    QString title() const { return title_; }
    QString type() const { return type_; }
    QString model() const { return model_; }
    QString filterModel() const { return filterModel_; }
    QVariantMap layout() const { return layout_; }
    
    // Setters
    void setId(const QString& id);
    void setTitle(const QString& title);
    void setType(const QString& type);
    void setModel(const QString& model);
    void setFilterModel(const QString& filterModel);
    void setLayout(const QVariantMap& layout);
    
    // 字段管理
    Q_INVOKABLE void addField(MetaField* field);
    Q_INVOKABLE void removeField(const QString& fieldName);
    Q_INVOKABLE MetaField* getField(const QString& fieldName) const;
    Q_INVOKABLE QVariantList getFields() const;
    Q_INVOKABLE void clearFields();
    
    // 操作管理
    Q_INVOKABLE void addAction(MetaAction* action);
    Q_INVOKABLE void addItemAction(MetaAction* action);
    Q_INVOKABLE QVariantList getActions() const;
    Q_INVOKABLE QVariantList getItemActions() const;
    Q_INVOKABLE void clearActions();
    Q_INVOKABLE void clearItemActions();
    
    // 序列化
    QJsonObject toJson() const;
    static MetaView* fromJson(const QJsonObject& json, QObject* parent = nullptr);
    
signals:
    void idChanged();
    void titleChanged();
    void typeChanged();
    void modelChanged();
    void filterModelChanged();
    void layoutChanged();
    void fieldsChanged();
    void actionsChanged();
    void itemActionsChanged();
    
private:
    QString id_;
    QString title_;
    QString type_; // table/form/card/dashboard
    QString model_; // 模型名称
    QString filterModel_; // 过滤模型
    QVariantMap layout_;
    
    QVector<MetaField*> fields_;
    QVector<MetaAction*> actions_;
    QVector<MetaAction*> itemActions_;
};

// 视图加载器
class ViewLoader : public QObject {
    Q_OBJECT
public:
    explicit ViewLoader(QObject* parent = nullptr);
    
    Q_INVOKABLE MetaView* loadView(const QString& viewId);
    Q_INVOKABLE MetaView* loadViewFromFile(const QString& filePath);
    Q_INVOKABLE void saveView(const QString& filePath, MetaView* view);
    
    // 预定义视图
    Q_INVOKABLE MetaView* createFactorLibraryView();
    Q_INVOKABLE MetaView* createFactorCreateView();
    Q_INVOKABLE MetaView* createFactorDetailView();
    Q_INVOKABLE MetaView* createStrategyLibraryView();
    
private:
    QJsonObject loadJsonFile(const QString& filePath);
    void saveJsonFile(const QString& filePath, const QJsonObject& json);
    
    QString viewConfigPath_ = "config/views";
};

} // namespace AStockQuantEngine::UI