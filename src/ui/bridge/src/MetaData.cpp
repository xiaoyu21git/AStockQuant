#include "MetaData.h"
#include <QJsonArray>
#include <QJsonValue>
#include <QJsonDocument>
#include <QFile>
#include <QIODevice>
#include "foundation/log/logging.hpp"
#include <QHash>

namespace AStockQuantEngine::UI {

// FieldType 工具函数
QString fieldTypeToString(FieldType type) {
    switch(type) {
        case FieldType::Text: return "text";
        case FieldType::Number: return "number";
        case FieldType::Boolean: return "boolean";
        case FieldType::Date: return "date";
        case FieldType::DateTime: return "datetime";
        case FieldType::Select: return "select";
        case FieldType::Tags: return "tags";
        case FieldType::Progress: return "progress";
        case FieldType::Badge: return "badge";
        case FieldType::Link: return "link";
        case FieldType::Action: return "action";
        default: return "text";
    }
}

FieldType stringToFieldType(const QString& typeStr) {
    if (typeStr == "text") return FieldType::Text;
    if (typeStr == "number") return FieldType::Number;
    if (typeStr == "boolean") return FieldType::Boolean;
    if (typeStr == "date") return FieldType::Date;
    if (typeStr == "datetime") return FieldType::DateTime;
    if (typeStr == "select") return FieldType::Select;
    if (typeStr == "tags") return FieldType::Tags;
    if (typeStr == "progress") return FieldType::Progress;
    if (typeStr == "badge") return FieldType::Badge;
    if (typeStr == "link") return FieldType::Link;
    if (typeStr == "action") return FieldType::Action;
    return FieldType::Text;
}

// MetaField 实现
MetaField::MetaField(QObject* parent) : QObject(parent) {}

MetaField::MetaField(const MetaField& other) : QObject(other.parent()) {
    name_ = other.name_;
    label_ = other.label_;
    type_ = other.type_;
    visible_ = other.visible_;
    sortable_ = other.sortable_;
    filterable_ = other.filterable_;
    editable_ = other.editable_;
    required_ = other.required_;
    format_ = other.format_;
    unit_ = other.unit_;
    width_ = other.width_;
    precision_ = other.precision_;
    placeholder_ = other.placeholder_;
    description_ = other.description_;
    defaultValue_ = other.defaultValue_;
    options_ = other.options_;
    valueMappings_ = other.valueMappings_;
}

MetaField& MetaField::operator=(const MetaField& other) {
    if (this != &other) {
        name_ = other.name_;
        label_ = other.label_;
        type_ = other.type_;
        visible_ = other.visible_;
        sortable_ = other.sortable_;
        filterable_ = other.filterable_;
        editable_ = other.editable_;
        required_ = other.required_;
        format_ = other.format_;
        unit_ = other.unit_;
        width_ = other.width_;
        precision_ = other.precision_;
        placeholder_ = other.placeholder_;
        description_ = other.description_;
        defaultValue_ = other.defaultValue_;
        options_ = other.options_;
        valueMappings_ = other.valueMappings_;
    }
    return *this;
}

void MetaField::setName(const QString& name) {
    if (name_ != name) {
        name_ = name;
        emit nameChanged();
    }
}

void MetaField::setLabel(const QString& label) {
    if (label_ != label) {
        label_ = label;
        emit labelChanged();
    }
}

void MetaField::setType(FieldType type) {
    if (type_ != type) {
        type_ = type;
        emit typeChanged();
    }
}

void MetaField::setVisible(bool visible) {
    if (visible_ != visible) {
        visible_ = visible;
        emit visibleChanged();
    }
}

void MetaField::setSortable(bool sortable) {
    if (sortable_ != sortable) {
        sortable_ = sortable;
        emit sortableChanged();
    }
}

void MetaField::setFilterable(bool filterable) {
    if (filterable_ != filterable) {
        filterable_ = filterable;
        emit filterableChanged();
    }
}

void MetaField::setEditable(bool editable) {
    if (editable_ != editable) {
        editable_ = editable;
        emit editableChanged();
    }
}

void MetaField::setRequired(bool required) {
    if (required_ != required) {
        required_ = required;
        emit requiredChanged();
    }
}

void MetaField::setFormat(const QString& format) {
    if (format_ != format) {
        format_ = format;
        emit formatChanged();
    }
}

void MetaField::setUnit(const QString& unit) {
    if (unit_ != unit) {
        unit_ = unit;
        emit unitChanged();
    }
}

void MetaField::setWidth(int width) {
    if (width_ != width) {
        width_ = width;
        emit widthChanged();
    }
}

void MetaField::setPrecision(int precision) {
    if (precision_ != precision) {
        precision_ = precision;
        emit precisionChanged();
    }
}

void MetaField::setPlaceholder(const QString& placeholder) {
    if (placeholder_ != placeholder) {
        placeholder_ = placeholder;
        emit placeholderChanged();
    }
}

void MetaField::setDescription(const QString& description) {
    if (description_ != description) {
        description_ = description;
        emit descriptionChanged();
    }
}

void MetaField::setDefaultValue(const QVariant& value) {
    if (defaultValue_ != value) {
        defaultValue_ = value;
        emit defaultValueChanged();
    }
}

void MetaField::addOption(const QString& label, const QVariant& value) {
    options_.append({label, value});
}

QVariantList MetaField::getOptions() const {
    QVariantList result;
    for (const auto& option : options_) {
        QVariantMap optionMap;
        optionMap["label"] = option.label;
        optionMap["value"] = option.value;
        result.append(optionMap);
    }
    return result;
}

void MetaField::clearOptions() {
    options_.clear();
}

void MetaField::addValueMapping(const QVariant& value, const QString& label, const QString& color) {
    // 将QVariant转换为QString作为键，因为QVariant不能直接用作QHash键
    valueMappings_[value.toString()] = {label, QColor(color)};
}

QVariantMap MetaField::getValueMappings() const {
    QVariantMap result;
    for (auto it = valueMappings_.constBegin(); it != valueMappings_.constEnd(); ++it) {
        QVariantMap mapping;
        mapping["label"] = it.value().label;
        mapping["color"] = it.value().color.name();
        result[it.key()] = mapping;  // it.key() is already QString
    }
    return result;
}

void MetaField::clearValueMappings() {
    valueMappings_.clear();
}

QJsonObject MetaField::toJson() const {
    QJsonObject json;
    json["name"] = name_;
    json["label"] = label_;
    json["type"] = fieldTypeToString(type_);
    json["visible"] = visible_;
    json["sortable"] = sortable_;
    json["filterable"] = filterable_;
    json["editable"] = editable_;
    json["required"] = required_;
    
    if (!format_.isEmpty()) json["format"] = format_;
    if (!unit_.isEmpty()) json["unit"] = unit_;
    if (width_ != -1) json["width"] = width_;
    if (precision_ != 2) json["precision"] = precision_;
    if (!placeholder_.isEmpty()) json["placeholder"] = placeholder_;
    if (!description_.isEmpty()) json["description"] = description_;
    if (!defaultValue_.isNull()) {
        json["defaultValue"] = QJsonValue::fromVariant(defaultValue_);
    }
    
    // 选项
    if (!options_.isEmpty()) {
        QJsonArray optionsArray;
        for (const auto& option : options_) {
            QJsonObject optionObj;
            optionObj["label"] = option.label;
            optionObj["value"] = QJsonValue::fromVariant(option.value);
            optionsArray.append(optionObj);
        }
        json["options"] = optionsArray;
    }
    
    // 值映射
    if (!valueMappings_.isEmpty()) {
        QJsonObject mappingsObj;
        for (auto it = valueMappings_.constBegin(); it != valueMappings_.constEnd(); ++it) {
            QJsonObject mappingObj;
            mappingObj["label"] = it.value().label;
            mappingObj["color"] = it.value().color.name();
            mappingsObj[it.key()] = mappingObj;  // it.key() is already QString
        }
        json["valueMap"] = mappingsObj;
    }
    
    return json;
}

MetaField* MetaField::fromJson(const QJsonObject& json, QObject* parent) {
    MetaField* field = new MetaField(parent);
    
    field->setName(json.value("name").toString());
    field->setLabel(json.value("label").toString());
    field->setType(stringToFieldType(json.value("type").toString("text")));
    field->setVisible(json.value("visible").toBool(true));
    field->setSortable(json.value("sortable").toBool(false));
    field->setFilterable(json.value("filterable").toBool(false));
    field->setEditable(json.value("editable").toBool(false));
    field->setRequired(json.value("required").toBool(false));
    
    if (json.contains("format")) field->setFormat(json.value("format").toString());
    if (json.contains("unit")) field->setUnit(json.value("unit").toString());
    if (json.contains("width")) field->setWidth(json.value("width").toInt(-1));
    if (json.contains("precision")) field->setPrecision(json.value("precision").toInt(2));
    if (json.contains("placeholder")) field->setPlaceholder(json.value("placeholder").toString());
    if (json.contains("description")) field->setDescription(json.value("description").toString());
    if (json.contains("defaultValue")) {
        field->setDefaultValue(json.value("defaultValue").toVariant());
    }
    
    // 解析选项
    if (json.contains("options") && json.value("options").isArray()) {
        QJsonArray optionsArray = json.value("options").toArray();
        for (const QJsonValue& optionValue : optionsArray) {
            if (optionValue.isObject()) {
                QJsonObject optionObj = optionValue.toObject();
                field->addOption(
                    optionObj.value("label").toString(),
                    optionObj.value("value").toVariant()
                );
            }
        }
    }
    
    // 解析值映射
    if (json.contains("valueMap") && json.value("valueMap").isObject()) {
        QJsonObject mappingsObj = json.value("valueMap").toObject();
        for (const QString& key : mappingsObj.keys()) {
            QJsonObject mappingObj = mappingsObj.value(key).toObject();
            field->addValueMapping(
                key,
                mappingObj.value("label").toString(),
                mappingObj.value("color").toString()
            );
        }
    }
    
    return field;
}

// MetaAction 实现
MetaAction::MetaAction(QObject* parent) : QObject(parent) {}

void MetaAction::setName(const QString& name) {
    if (name_ != name) {
        name_ = name;
        emit nameChanged();
    }
}

void MetaAction::setLabel(const QString& label) {
    if (label_ != label) {
        label_ = label;
        emit labelChanged();
    }
}

void MetaAction::setIcon(const QString& icon) {
    if (icon_ != icon) {
        icon_ = icon;
        emit iconChanged();
    }
}

void MetaAction::setType(const QString& type) {
    if (type_ != type) {
        type_ = type;
        emit typeChanged();
    }
}

void MetaAction::setConfirm(bool confirm) {
    if (confirm_ != confirm) {
        confirm_ = confirm;
        emit confirmChanged();
    }
}

void MetaAction::setConfirmText(const QString& text) {
    if (confirmText_ != text) {
        confirmText_ = text;
        emit confirmTextChanged();
    }
}

QJsonObject MetaAction::toJson() const {
    QJsonObject json;
    json["name"] = name_;
    json["label"] = label_;
    if (!icon_.isEmpty()) json["icon"] = icon_;
    json["type"] = type_;
    json["confirm"] = confirm_;
    if (confirm_) {
        json["confirmText"] = confirmText_;
    }
    return json;
}

MetaAction* MetaAction::fromJson(const QJsonObject& json, QObject* parent) {
    MetaAction* action = new MetaAction(parent);
    
    action->setName(json.value("name").toString());
    action->setLabel(json.value("label").toString());
    if (json.contains("icon")) action->setIcon(json.value("icon").toString());
    action->setType(json.value("type").toString("default"));
    action->setConfirm(json.value("confirm").toBool(false));
    if (json.contains("confirmText")) {
        action->setConfirmText(json.value("confirmText").toString());
    }
    
    return action;
}

// MetaView 实现
MetaView::MetaView(QObject* parent) : QObject(parent) {}

void MetaView::setId(const QString& id) {
    if (id_ != id) {
        id_ = id;
        emit idChanged();
    }
}

void MetaView::setTitle(const QString& title) {
    if (title_ != title) {
        title_ = title;
        emit titleChanged();
    }
}

void MetaView::setType(const QString& type) {
    if (type_ != type) {
        type_ = type;
        emit typeChanged();
    }
}

void MetaView::setModel(const QString& model) {
    if (model_ != model) {
        model_ = model;
        emit modelChanged();
    }
}

void MetaView::setFilterModel(const QString& filterModel) {
    if (filterModel_ != filterModel) {
        filterModel_ = filterModel;
        emit filterModelChanged();
    }
}

void MetaView::setLayout(const QVariantMap& layout) {
    if (layout_ != layout) {
        layout_ = layout;
        emit layoutChanged();
    }
}

void MetaView::addField(MetaField* field) {
    if (field && !fields_.contains(field)) {
        fields_.append(field);
        emit fieldsChanged();
    }
}

void MetaView::removeField(const QString& fieldName) {
    for (int i = 0; i < fields_.size(); ++i) {
        if (fields_[i]->name() == fieldName) {
            fields_.remove(i);
            emit fieldsChanged();
            break;
        }
    }
}

MetaField* MetaView::getField(const QString& fieldName) const {
    for (MetaField* field : fields_) {
        if (field->name() == fieldName) {
            return field;
        }
    }
    return nullptr;
}

QVariantList MetaView::getFields() const {
    QVariantList result;
    for (MetaField* field : fields_) {
        result.append(QVariant::fromValue(field));
    }
    return result;
}

void MetaView::clearFields() {
    if (!fields_.isEmpty()) {
        fields_.clear();
        emit fieldsChanged();
    }
}

void MetaView::addAction(MetaAction* action) {
    if (action && !actions_.contains(action)) {
        actions_.append(action);
        emit actionsChanged();
    }
}

void MetaView::addItemAction(MetaAction* action) {
    if (action && !itemActions_.contains(action)) {
        itemActions_.append(action);
        emit itemActionsChanged();
    }
}

QVariantList MetaView::getActions() const {
    QVariantList result;
    for (MetaAction* action : actions_) {
        result.append(QVariant::fromValue(action));
    }
    return result;
}

QVariantList MetaView::getItemActions() const {
    QVariantList result;
    for (MetaAction* action : itemActions_) {
        result.append(QVariant::fromValue(action));
    }
    return result;
}

void MetaView::clearActions() {
    if (!actions_.isEmpty()) {
        actions_.clear();
        emit actionsChanged();
    }
}

void MetaView::clearItemActions() {
    if (!itemActions_.isEmpty()) {
        itemActions_.clear();
        emit itemActionsChanged();
    }
}

QJsonObject MetaView::toJson() const {
    QJsonObject json;
    json["id"] = id_;
    json["title"] = title_;
    json["type"] = type_;
    json["model"] = model_;
    if (!filterModel_.isEmpty()) json["filterModel"] = filterModel_;
    
    // 字段
    QJsonArray fieldsArray;
    for (MetaField* field : fields_) {
        fieldsArray.append(field->toJson());
    }
    json["fields"] = fieldsArray;
    
    // 操作
    if (!actions_.isEmpty()) {
        QJsonArray actionsArray;
        for (MetaAction* action : actions_) {
            actionsArray.append(action->toJson());
        }
        json["actions"] = actionsArray;
    }
    
    // 行内操作
    if (!itemActions_.isEmpty()) {
        QJsonArray itemActionsArray;
        for (MetaAction* action : itemActions_) {
            itemActionsArray.append(action->toJson());
        }
        json["itemActions"] = itemActionsArray;
    }
    
    // 布局
    if (!layout_.isEmpty()) {
        QJsonObject layoutObj;
        for (auto it = layout_.constBegin(); it != layout_.constEnd(); ++it) {
            layoutObj[it.key()] = QJsonValue::fromVariant(it.value());
        }
        json["layout"] = layoutObj;
    }
    
    return json;
}

MetaView* MetaView::fromJson(const QJsonObject& json, QObject* parent) {
    MetaView* view = new MetaView(parent);
    
    view->setId(json.value("id").toString());
    view->setTitle(json.value("title").toString());
    view->setType(json.value("type").toString("table"));
    view->setModel(json.value("model").toString());
    if (json.contains("filterModel")) {
        view->setFilterModel(json.value("filterModel").toString());
    }
    
    // 解析字段
    if (json.contains("fields") && json.value("fields").isArray()) {
        QJsonArray fieldsArray = json.value("fields").toArray();
        for (const QJsonValue& fieldValue : fieldsArray) {
            if (fieldValue.isObject()) {
                MetaField* field = MetaField::fromJson(fieldValue.toObject(), view);
                view->addField(field);
            }
        }
    }
    
    // 解析操作
    if (json.contains("actions") && json.value("actions").isArray()) {
        QJsonArray actionsArray = json.value("actions").toArray();
        for (const QJsonValue& actionValue : actionsArray) {
            if (actionValue.isObject()) {
                MetaAction* action = MetaAction::fromJson(actionValue.toObject(), view);
                view->addAction(action);
            }
        }
    }
    
    // 解析行内操作
    if (json.contains("itemActions") && json.value("itemActions").isArray()) {
        QJsonArray itemActionsArray = json.value("itemActions").toArray();
        for (const QJsonValue& actionValue : itemActionsArray) {
            if (actionValue.isObject()) {
                MetaAction* action = MetaAction::fromJson(actionValue.toObject(), view);
                view->addItemAction(action);
            }
        }
    }
    
    // 解析布局
    if (json.contains("layout") && json.value("layout").isObject()) {
        QJsonObject layoutObj = json.value("layout").toObject();
        QVariantMap layoutMap;
        for (const QString& key : layoutObj.keys()) {
            layoutMap[key] = layoutObj.value(key).toVariant();
        }
        view->setLayout(layoutMap);
    }
    
    return view;
}

// ViewLoader 实现
ViewLoader::ViewLoader(QObject* parent) : QObject(parent) {}

MetaView* ViewLoader::loadView(const QString& viewId) {
    QString filePath = viewConfigPath_ + "/" + viewId + ".json";
    return loadViewFromFile(filePath);
}

MetaView* ViewLoader::loadViewFromFile(const QString& filePath) {
    QJsonObject json = loadJsonFile(filePath);
    if (!json.isEmpty()) {
        return MetaView::fromJson(json, this);
    }
    return nullptr;
}

void ViewLoader::saveView(const QString& filePath, MetaView* view) {
    if (view) {
        QJsonObject json = view->toJson();
        saveJsonFile(filePath, json);
    }
}

MetaView* ViewLoader::createFactorLibraryView() {
    MetaView* view = new MetaView(this);
    view->setId("factorLibrary");
    view->setTitle("因子库");
    view->setType("table");
    view->setModel("factorModel");
    view->setFilterModel("filteredModel");
    
    // 字段定义
    MetaField* nameField = new MetaField(view);
    nameField->setName("displayName");
    nameField->setLabel("因子名称");
    nameField->setType(FieldType::Text);
    nameField->setWidth(180);
    nameField->setSortable(true);
    nameField->setFilterable(true);
    view->addField(nameField);
    
    MetaField* categoryField = new MetaField(view);
    categoryField->setName("majorCategory");
    categoryField->setLabel("类别");
    categoryField->setType(FieldType::Badge);
    categoryField->setWidth(100);
    categoryField->addValueMapping("动量类", "动量", "#3B82F6");
    categoryField->addValueMapping("价值类", "价值", "#F59E0B");
    categoryField->addValueMapping("质量类", "质量", "#10B981");
    categoryField->addValueMapping("成长类", "成长", "#8B5CF6");
    categoryField->addValueMapping("情绪类", "情绪", "#EC4899");
    view->addField(categoryField);
    
    MetaField* icField = new MetaField(view);
    icField->setName("icValue");
    icField->setLabel("IC");
    icField->setType(FieldType::Number);
    icField->setFormat("%.3f");
    icField->setWidth(80);
    icField->setSortable(true);
    icField->addValueMapping(0.04, "良好", "#10B981");
    icField->addValueMapping(0.03, "中等", "#F59E0B");
    view->addField(icField);
    
    MetaField* statusField = new MetaField(view);
    statusField->setName("status");
    statusField->setLabel("状态");
    statusField->setType(FieldType::Badge);
    statusField->setWidth(80);
    statusField->addValueMapping("ACTIVE", "活跃", "#10B981");
    statusField->addValueMapping("DEPRECATED", "废弃", "#EF4444");
    view->addField(statusField);
    
    // 工具栏操作
    MetaAction* createAction = new MetaAction(view);
    createAction->setName("create");
    createAction->setLabel("新建因子");
    createAction->setType("primary");
    view->addAction(createAction);
    
    // 行内操作
    MetaAction* detailAction = new MetaAction(view);
    detailAction->setName("detail");
    detailAction->setLabel("详情");
    view->addItemAction(detailAction);
    
    MetaAction* deleteAction = new MetaAction(view);
    deleteAction->setName("delete");
    deleteAction->setLabel("删除");
    deleteAction->setType("danger");
    deleteAction->setConfirm(true);
    deleteAction->setConfirmText("确定删除该因子吗？");
    view->addItemAction(deleteAction);
    
    return view;
}

MetaView* ViewLoader::createFactorCreateView() {
    MetaView* view = new MetaView(this);
    view->setId("factorCreate");
    view->setTitle("新建因子");
    view->setType("form");
    
    // 字段定义
    MetaField* nameField = new MetaField(view);
    nameField->setName("name");
    nameField->setLabel("因子名称");
    nameField->setType(FieldType::Text);
    nameField->setRequired(true);
    nameField->setPlaceholder("请输入因子名称");
    view->addField(nameField);
    
    MetaField* categoryField = new MetaField(view);
    categoryField->setName("category");
    categoryField->setLabel("因子类别");
    categoryField->setType(FieldType::Select);
    categoryField->setRequired(true);
    categoryField->addOption("动量类", "momentum");
    categoryField->addOption("价值类", "value");
    categoryField->addOption("质量类", "quality");
    categoryField->addOption("成长类", "growth");
    categoryField->addOption("情绪类", "sentiment");
    view->addField(categoryField);
    
    MetaField* tagsField = new MetaField(view);
    tagsField->setName("tags");
    tagsField->setLabel("标签");
    tagsField->setType(FieldType::Tags);
    tagsField->setPlaceholder("输入标签后按回车");
    view->addField(tagsField);
    
    // 表单操作
    MetaAction* cancelAction = new MetaAction(view);
    cancelAction->setName("cancel");
    cancelAction->setLabel("取消");
    cancelAction->setType("default");
    view->addAction(cancelAction);
    
    MetaAction* saveAction = new MetaAction(view);
    saveAction->setName("save");
    saveAction->setLabel("保存");
    saveAction->setType("primary");
    view->addAction(saveAction);
    
    return view;
}

MetaView* ViewLoader::createFactorDetailView() {
    MetaView* view = new MetaView(this);
    view->setId("factorDetail");
    view->setTitle("因子详情");
    view->setType("card");
    
    // 字段定义
    MetaField* nameField = new MetaField(view);
    nameField->setName("displayName");
    nameField->setLabel("因子名称");
    nameField->setType(FieldType::Text);
    view->addField(nameField);
    
    MetaField* descriptionField = new MetaField(view);
    descriptionField->setName("description");
    descriptionField->setLabel("描述");
    descriptionField->setType(FieldType::Text);
    view->addField(descriptionField);
    
    MetaField* icField = new MetaField(view);
    icField->setName("icValue");
    icField->setLabel("信息系数(IC)");
    icField->setType(FieldType::Number);
    icField->setFormat("%.3f");
    view->addField(icField);
    
    // 操作
    MetaAction* closeAction = new MetaAction(view);
    closeAction->setName("close");
    closeAction->setLabel("关闭");
    view->addAction(closeAction);
    
    return view;
}

MetaView* ViewLoader::createStrategyLibraryView() {
    MetaView* view = new MetaView(this);
    view->setId("strategyLibrary");
    view->setTitle("策略库");
    view->setType("table");
    view->setModel("strategyModel");
    
    // 字段定义
    MetaField* nameField = new MetaField(view);
    nameField->setName("name");
    nameField->setLabel("策略名称");
    nameField->setType(FieldType::Text);
    nameField->setWidth(200);
    nameField->setSortable(true);
    view->addField(nameField);
    
    MetaField* returnsField = new MetaField(view);
    returnsField->setName("returns");
    returnsField->setLabel("收益率");
    returnsField->setType(FieldType::Number);
    returnsField->setFormat("%.1f%%");
    returnsField->setWidth(100);
    returnsField->setSortable(true);
    view->addField(returnsField);
    
    MetaField* statusField = new MetaField(view);
    statusField->setName("status");
    statusField->setLabel("状态");
    statusField->setType(FieldType::Badge);
    statusField->setWidth(80);
    statusField->addValueMapping("running", "运行中", "#10B981");
    statusField->addValueMapping("paused", "已暂停", "#F59E0B");
    statusField->addValueMapping("stopped", "已停止", "#EF4444");
    view->addField(statusField);
    
    // 操作
    MetaAction* createAction = new MetaAction(view);
    createAction->setName("create");
    createAction->setLabel("新建策略");
    createAction->setType("primary");
    view->addAction(createAction);
    
    return view;
}

QJsonObject ViewLoader::loadJsonFile(const QString& filePath) {
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        INTERNAL_WARN_STREAM << "Failed to open JSON file:" << filePath.toStdString();
        return QJsonObject();
    }
    
    QByteArray data = file.readAll();
    file.close();
    
    QJsonDocument doc = QJsonDocument::fromJson(data);
    if (doc.isNull()) {
        INTERNAL_WARN_STREAM << "Failed to parse JSON from file:" << filePath.toStdString();
        return QJsonObject();
    }
    
    return doc.object();
}

void ViewLoader::saveJsonFile(const QString& filePath, const QJsonObject& json) {
    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly)) {
        INTERNAL_WARN_STREAM << "Failed to open JSON file for writing:" << filePath.toStdString();
        return;
    }
    
    QJsonDocument doc(json);
    file.write(doc.toJson(QJsonDocument::Indented));
    file.close();
}

} // namespace AStockQuantEngine::UI