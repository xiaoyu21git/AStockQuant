// yaml_facade.cpp
#include "foundation/yaml/yaml_facade.h"
#include <iostream>
#include <fstream>
#include <sstream>
#include <vector>
#include <map>
#include <yaml-cpp/yaml.h>
#include "foundation/Utils/string.hpp"

namespace foundation::yaml {

// ============ Impl 类实现 ============

class YamlFacade::Impl {
public:
    // 文档结构
    struct Document {
        YAML::Node node;
        DocumentSelector selector;
        
        Document() : selector() {}
    };
    
    std::vector<Document> documents_;
    size_t currentDocumentIndex_ = 0;
    bool isMultiDocument_ = false;

    // ============ 单文档操作（原有功能） ============
    
    // 加载文件（单文档）
    bool loadFromFileImpl(const std::string &filename) {
        try {
            clearImpl();
            
            Document doc;
            doc.node = YAML::LoadFile(filename);
            extractDocumentSelector(doc);
            
            documents_.push_back(doc);
            currentDocumentIndex_ = 0;
            isMultiDocument_ = false;
            
            return true;
        } catch (const YAML::Exception &e) {
            std::cerr << "YAML加载错误: " << e.what() << std::endl;
            return false;
        } catch (const std::exception &e) {
            std::cerr << "文件错误: " << e.what() << std::endl;
            return false;
        }
    }
    
    // 加载字符串（单文档）
    bool loadFromStringImpl(const std::string &yaml) {
        try {
            clearImpl();
            
            Document doc;
            doc.node = YAML::Load(yaml);
            extractDocumentSelector(doc);
            
            documents_.push_back(doc);
            currentDocumentIndex_ = 0;
            isMultiDocument_ = false;
            
            return true;
        } catch (const YAML::Exception &e) {
            std::cerr << "YAML解析错误: " << e.what() << std::endl;
            return false;
        }
    }
    
    // 保存到文件（当前文档）
    bool saveToFileImpl(const std::string &filename) const {
        try {
            std::ofstream file(filename);
            if (!file.is_open()) {
                std::cerr << "无法打开文件: " << filename << std::endl;
                return false;
            }
            
            if (documents_.empty()) {
                return true;  // 空文档
            }
            
            const Document& currentDoc = documents_[currentDocumentIndex_];
            YAML::Emitter emitter;
            emitter << currentDoc.node;
            file << emitter.c_str();
            
            return true;
        } catch (const YAML::Exception &e) {
            std::cerr << "YAML保存错误: " << e.what() << std::endl;
            return false;
        }
    }
    
    // 转换为字符串（当前文档）
    std::string toStringImpl() const {
        if (documents_.empty()) {
            return "";
        }
        
        const Document& currentDoc = documents_[currentDocumentIndex_];
        YAML::Emitter emitter;
        emitter << currentDoc.node;
        return emitter.c_str();
    }
    
    // 获取节点（当前文档）
    YAML::Node getNodeImpl(const std::string &path) const {
        if (documents_.empty()) {
            return YAML::Node();
        }
        
        const Document& currentDoc = documents_[currentDocumentIndex_];
        if (path.empty()) {
            return currentDoc.node;
        }
        
        std::istringstream iss(path);
        std::string token;
        YAML::Node current = currentDoc.node;
        
        while (std::getline(iss, token, '.')) {
            if (!current.IsDefined()) {
                break;
            }
            
            // 检查是否是数组索引
            if (!token.empty() && token[0] == '[' && token.back() == ']') {
                try {
                    size_t index = std::stoul(token.substr(1, token.size() - 2));
                    current = current[index];
                } catch (...) {
                    return YAML::Node();
                }
            } else {
                current = current[token];
            }
        }
        
        return current;
    }
    
    bool hasValueImpl(const std::string &path) const { 
        return getNodeImpl(path).IsDefined(); 
    }
    
    std::string getStringImpl(const std::string &path, const std::string &defaultValue) const {
        auto node = getNodeImpl(path);
        return node.IsDefined() ? node.as<std::string>() : defaultValue;
    }
    
    int getIntImpl(const std::string &path, int defaultValue) const {
        auto node = getNodeImpl(path);
        return node.IsDefined() ? node.as<int>() : defaultValue;
    }
    
    double getDoubleImpl(const std::string &path, double defaultValue) const {
        auto node = getNodeImpl(path);
        return node.IsDefined() ? node.as<double>() : defaultValue;
    }
    
    bool getBoolImpl(const std::string &path, bool defaultValue) const {
        auto node = getNodeImpl(path);
        return node.IsDefined() ? node.as<bool>() : defaultValue;
    }
    
    // 数组获取
    std::vector<std::string> getStringArrayImpl(const std::string &path) const {
        std::vector<std::string> result;
        auto node = getNodeImpl(path);
        if (node.IsSequence()) {
            for (const auto &item : node) {
                result.push_back(item.as<std::string>());
            }
        }
        return result;
    }
    
    std::vector<int> getIntArrayImpl(const std::string &path) const {
        std::vector<int> result;
        auto node = getNodeImpl(path);
        if (node.IsSequence()) {
            for (const auto &item : node) {
                result.push_back(item.as<int>());
            }
        }
        return result;
    }
    
    std::vector<double> getDoubleArrayImpl(const std::string &path) const {
        std::vector<double> result;
        auto node = getNodeImpl(path);
        if (node.IsSequence()) {
            for (const auto &item : node) {
                result.push_back(item.as<double>());
            }
        }
        return result;
    }
    void setDoubleArrayImpl(const std::string &path, const std::vector<double> &values) {
        auto &node = setNodeImpl(path);
        node = YAML::Node(YAML::NodeType::Sequence);
        for (double value : values) {
            node.push_back(value);
        }
    }
    // 映射获取
    std::map<std::string, std::string> getStringMapImpl(const std::string &path) const {
        std::map<std::string, std::string> result;
        auto node = getNodeImpl(path);
        if (node.IsMap()) {
            for (const auto &item : node) {
                result[item.first.as<std::string>()] = item.second.as<std::string>();
            }
        }
        return result;
    }
    
    std::map<std::string, int> getIntMapImpl(const std::string &path) const {
        std::map<std::string, int> result;
        auto node = getNodeImpl(path);
        if (node.IsMap()) {
            for (const auto &item : node) {
                result[item.first.as<std::string>()] = item.second.as<int>();
            }
        }
        return result;
    }
    
    // 设置值
    YAML::Node &setNodeImpl(const std::string &path) {
        if (documents_.empty()) {
            // 创建第一个文档
            Document doc;
            documents_.push_back(doc);
            currentDocumentIndex_ = 0;
        }
        
        Document& currentDoc = documents_[currentDocumentIndex_];
        if (path.empty()) {
            return currentDoc.node;
        }
        
        std::istringstream iss(path);
        std::string token;
        YAML::Node *current = &currentDoc.node;
        
        while (std::getline(iss, token, '.')) {
            if (!current->IsDefined()) {
                *current = YAML::Node(YAML::NodeType::Map);
            }
            
            if (!token.empty() && token[0] == '[' && token.back() == ']') {
                try {
                    size_t index = std::stoul(token.substr(1, token.size() - 2));
                    if (!(*current)[index].IsDefined()) {
                        (*current)[index] = YAML::Node();
                    }
                    current = &(*current)[index];
                } catch (...) {
                    throw std::runtime_error("无效的数组索引: " + token);
                }
            } else {
                if (!(*current)[token].IsDefined()) {
                    (*current)[token] = YAML::Node();
                }
                current = &(*current)[token];
            }
        }
        
        return *current;
    }
    
    void setStringImpl(const std::string &path, const std::string &value) { 
        setNodeImpl(path) = value; 
    }
    
    void setIntImpl(const std::string &path, int value) { 
        setNodeImpl(path) = value; 
    }
    
    void setDoubleImpl(const std::string &path, double value) { 
        setNodeImpl(path) = value; 
    }
    
    void setBoolImpl(const std::string &path, bool value) { 
        setNodeImpl(path) = value; 
    }
    
    void setStringArrayImpl(const std::string &path, const std::vector<std::string> &values) {
        auto &node = setNodeImpl(path);
        node = YAML::Node(YAML::NodeType::Sequence);
        for (const auto &value : values) {
            node.push_back(value);
        }
    }
    
    void setIntArrayImpl(const std::string &path, const std::vector<int> &values) {
        auto &node = setNodeImpl(path);
        node = YAML::Node(YAML::NodeType::Sequence);
        for (int value : values) {
            node.push_back(value);
        }
    }
    
    void setStringMapImpl(const std::string &path, const std::map<std::string, std::string> &values) {
        auto &node = setNodeImpl(path);
        node = YAML::Node(YAML::NodeType::Map);
        for (const auto &p : values) {
            node[p.first] = p.second;
        }
    }
    
    // 合并
    void mergeImpl(const Impl &other) {
        if (documents_.empty() || other.documents_.empty()) {
            return;
        }
        
        Document& currentDoc = documents_[currentDocumentIndex_];
        const Document& otherDoc = other.documents_[other.currentDocumentIndex_];
        
        mergeNodes(currentDoc.node, otherDoc.node);
    }
    
    void mergeNodes(YAML::Node &target, const YAML::Node &source) {
        if (!source.IsDefined()) {
            return;
        }
        
        if (source.IsScalar()) {
            target = source;
        } else if (source.IsSequence()) {
            if (!target.IsDefined() || !target.IsSequence()) {
                target = YAML::Node(YAML::NodeType::Sequence);
            }
            for (const auto &item : source) {
                target.push_back(item);
            }
        } else if (source.IsMap()) {
            if (!target.IsDefined() || !target.IsMap()) {
                target = YAML::Node(YAML::NodeType::Map);
            }
            for (const auto &item : source) {
                std::string key = item.first.as<std::string>();
                if (!target[key].IsDefined()) {
                    target[key] = item.second;
                } else {
                    mergeNodes(target[key], item.second);
                }
            }
        }
    }
    
    void clearImpl() {
        documents_.clear();
        currentDocumentIndex_ = 0;
        isMultiDocument_ = false;
    }
    
    // ============ 多文档操作（新增功能） ============
    
    bool loadMultiDocumentImpl(const std::string &filename) {
        try {
            clearImpl();
            
            std::ifstream file(filename);
            if (!file.is_open()) {
                std::cerr << "无法打开文件: " << filename << std::endl;
                return false;
            }
            
            std::stringstream buffer;
            buffer << file.rdbuf();
            std::string content = buffer.str();
            
            return loadMultiDocumentFromStringImpl(content);
            
        } catch (const std::exception &e) {
            std::cerr << "多文档加载错误: " << e.what() << std::endl;
            return false;
        }
    }
    
    bool loadMultiDocumentFromStringImpl(const std::string &yaml) {
        try {
            clearImpl();
            
            std::istringstream stream(yaml);
            std::string line;
            std::stringstream docBuffer;
            bool firstDocument = true;
            bool inDocument = false;
            
            while (std::getline(stream, line)) {
                // 检查文档分隔符
                if (line.rfind("---", 0) == 0) {
                    if (inDocument || firstDocument) {
                        // 完成一个文档
                        if (!docBuffer.str().empty()) {
                            processDocumentBuffer(docBuffer.str());
                            docBuffer.str("");
                            docBuffer.clear();
                        }
                    }
                    inDocument = true;
                    firstDocument = false;
                } else {
                    docBuffer << line << "\n";
                    if (!inDocument) {
                        inDocument = true;  // 第一个文档没有分隔符
                    }
                }
            }
            
            // 处理最后一个文档
            if (!docBuffer.str().empty()) {
                processDocumentBuffer(docBuffer.str());
            }
            
            // 如果没有文档，创建一个空的
            if (documents_.empty()) {
                Document doc;
                documents_.push_back(doc);
            }
            
            isMultiDocument_ = (documents_.size() > 1);
            return true;
            
        } catch (const YAML::Exception &e) {
            std::cerr << "多文档解析错误: " << e.what() << std::endl;
            return false;
        }
    }
    
    bool saveAsMultiDocumentImpl(const std::string &filename) const {
        try {
            std::ofstream file(filename);
            if (!file.is_open()) {
                std::cerr << "无法打开文件: " << filename << std::endl;
                return false;
            }
            
            for (size_t i = 0; i < documents_.size(); ++i) {
                if (i > 0) {
                    file << "---\n";
                }
                
                YAML::Emitter emitter;
                emitter << documents_[i].node;
                file << emitter.c_str();
                
                // 如果不是最后一个文档，添加换行
                if (i < documents_.size() - 1) {
                    file << "\n";
                }
            }
            
            return true;
        } catch (const YAML::Exception &e) {
            std::cerr << "多文档保存错误: " << e.what() << std::endl;
            return false;
        }
    }
    
    size_t getDocumentCountImpl() const {
        return documents_.size();
    }
    
    size_t getCurrentDocumentIndexImpl() const {
        return currentDocumentIndex_;
    }
    
    bool setCurrentDocumentImpl(size_t index) {
        if (index < documents_.size()) {
            currentDocumentIndex_ = index;
            return true;
        }
        return false;
    }
    
    bool findDocumentImpl(const DocumentSelector& selector) {
        for (size_t i = 0; i < documents_.size(); ++i) {
            const auto& docSelector = documents_[i].selector;
            
            bool match = true;
            
            // 匹配 profile
            if (!selector.profile.empty() && 
                docSelector.profile != selector.profile) {
                match = false;
            }
            
            // 匹配 tag
            if (!selector.tag.empty() && 
                docSelector.tag != selector.tag) {
                match = false;
            }
            
            // 匹配 default
            if (selector.isDefault && 
                !docSelector.isDefault) {
                match = false;
            }
            
            if (match) {
                currentDocumentIndex_ = i;
                return true;
            }
        }
        
        return false;
    }
    
    DocumentSelector getCurrentDocumentMetaImpl() const {
        if (currentDocumentIndex_ < documents_.size()) {
            return documents_[currentDocumentIndex_].selector;
        }
        return DocumentSelector();
    }
    
    void addDocumentImpl(const YamlFacade& document, const DocumentSelector& selector) {
        Document newDoc;
        
        // 尝试获取文档内容
        try {
            // 假设document是单文档，获取其内容
            auto otherImpl = document.impl_.get();
            if (otherImpl && !otherImpl->documents_.empty()) {
                newDoc.node = otherImpl->documents_[otherImpl->currentDocumentIndex_].node;
            } else {
                // 如果无法获取，创建一个空文档
                newDoc.node = YAML::Node();
            }
        } catch (...) {
            newDoc.node = YAML::Node();
        }
        
        newDoc.selector = selector;
        documents_.push_back(newDoc);
        
        // 更新多文档标志
        if (documents_.size() > 1) {
            isMultiDocument_ = true;
        }
    }
    
    bool removeDocumentImpl(size_t index) {
        if (index < documents_.size()) {
            documents_.erase(documents_.begin() + index);
            
            // 调整当前索引
            if (currentDocumentIndex_ >= documents_.size()) {
                currentDocumentIndex_ = documents_.empty() ? 0 : documents_.size() - 1;
            }
            
            // 更新多文档标志
            isMultiDocument_ = (documents_.size() > 1);
            return true;
        }
        return false;
    }
    
    bool isMultiDocumentImpl() const {
        return isMultiDocument_;
    }
    
    bool selectDocumentByProfileImpl(const std::string& profile) {
        return findDocumentImpl(DocumentSelector(profile, "", false));
    }
    
private:
    void processDocumentBuffer(const std::string &buffer) {
        if (buffer.empty()) {
            return;
        }
        
        try {
            Document doc;
            doc.node = YAML::Load(buffer);
            extractDocumentSelector(doc);
            documents_.push_back(doc);
        } catch (const YAML::Exception &e) {
            std::cerr << "文档解析错误: " << e.what() << std::endl;
            // 创建空文档
            Document doc;
            documents_.push_back(doc);
        }
    }
    
    void extractDocumentSelector(Document &doc) {
        if (doc.node.IsMap()) {
            // 检查 profile 字段
            if (doc.node["profile"] && doc.node["profile"].IsScalar()) {
                doc.selector.profile = doc.node["profile"].as<std::string>();
            }
            
            // 检查 tag 字段
            if (doc.node["tag"] && doc.node["tag"].IsScalar()) {
                doc.selector.tag = doc.node["tag"].as<std::string>();
            }
            
            // 检查 default 字段
            if (doc.node["default"] && doc.node["default"].IsScalar()) {
                doc.selector.isDefault = doc.node["default"].as<bool>();
            }
        }
    }
};

// ============ YamlFacade 类实现 ============

// 构造函数和析构函数
YamlFacade::YamlFacade() : impl_(std::make_unique<Impl>()) {}

YamlFacade::YamlFacade(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}

YamlFacade::~YamlFacade() = default;

YamlFacade::YamlFacade(YamlFacade &&other) noexcept : impl_(std::move(other.impl_)) {}

YamlFacade &YamlFacade::operator=(YamlFacade &&other) noexcept {
    if (this != &other) {
        impl_ = std::move(other.impl_);
    }
    return *this;
}
void YamlFacade::setDoubleArray(const std::string &path, const std::vector<double> &values) { 
    impl_->setDoubleArrayImpl(path, values); 
}
// ============ 单文档接口实现 ============

bool YamlFacade::loadFromFile(const std::string &filename) { 
    return impl_->loadFromFileImpl(filename); 
}

bool YamlFacade::loadFromString(const std::string &yaml) { 
    return impl_->loadFromStringImpl(yaml); 
}

bool YamlFacade::saveToFile(const std::string &filename) const { 
    return impl_->saveToFileImpl(filename); 
}

std::string YamlFacade::toString() const { 
    return impl_->toStringImpl(); 
}

bool YamlFacade::hasValue(const std::string &path) const { 
    return impl_->hasValueImpl(path); 
}

std::string YamlFacade::getString(const std::string &path, const std::string &defaultValue) const {
    return impl_->getStringImpl(path, defaultValue);
}

int YamlFacade::getInt(const std::string &path, int defaultValue) const { 
    return impl_->getIntImpl(path, defaultValue); 
}

double YamlFacade::getDouble(const std::string &path, double defaultValue) const {
    return impl_->getDoubleImpl(path, defaultValue);
}

bool YamlFacade::getBool(const std::string &path, bool defaultValue) const { 
    return impl_->getBoolImpl(path, defaultValue); 
}

std::vector<std::string> YamlFacade::getStringArray(const std::string &path) const { 
    return impl_->getStringArrayImpl(path); 
}

std::vector<int> YamlFacade::getIntArray(const std::string &path) const { 
    return impl_->getIntArrayImpl(path); 
}

std::vector<double> YamlFacade::getDoubleArray(const std::string &path) const { 
    return impl_->getDoubleArrayImpl(path); 
}

std::map<std::string, std::string> YamlFacade::getStringMap(const std::string &path) const { 
    return impl_->getStringMapImpl(path); 
}

std::map<std::string, int> YamlFacade::getIntMap(const std::string &path) const { 
    return impl_->getIntMapImpl(path); 
}

void YamlFacade::setString(const std::string &path, const std::string &value) { 
    impl_->setStringImpl(path, value); 
}

void YamlFacade::setInt(const std::string &path, int value) { 
    impl_->setIntImpl(path, value); 
}

void YamlFacade::setDouble(const std::string &path, double value) { 
    impl_->setDoubleImpl(path, value); 
}

void YamlFacade::setBool(const std::string &path, bool value) { 
    impl_->setBoolImpl(path, value); 
}

void YamlFacade::setStringArray(const std::string &path, const std::vector<std::string> &values) {
    impl_->setStringArrayImpl(path, values);
}

void YamlFacade::setIntArray(const std::string &path, const std::vector<int> &values) { 
    impl_->setIntArrayImpl(path, values); 
}

void YamlFacade::setStringMap(const std::string &path, const std::map<std::string, std::string> &values) {
    impl_->setStringMapImpl(path, values);
}

void YamlFacade::merge(const YamlFacade &other) { 
    impl_->mergeImpl(*other.impl_); 
}

void YamlFacade::clear() { 
    impl_->clearImpl(); 
}

// ============ 多文档接口实现 ============

bool YamlFacade::loadMultiDocument(const std::string &filename) { 
    return impl_->loadMultiDocumentImpl(filename); 
}

bool YamlFacade::loadMultiDocumentFromString(const std::string &yaml) { 
    return impl_->loadMultiDocumentFromStringImpl(yaml); 
}

bool YamlFacade::saveAsMultiDocument(const std::string &filename) const { 
    return impl_->saveAsMultiDocumentImpl(filename); 
}

size_t YamlFacade::getDocumentCount() const { 
    return impl_->getDocumentCountImpl(); 
}

size_t YamlFacade::getCurrentDocumentIndex() const { 
    return impl_->getCurrentDocumentIndexImpl(); 
}

bool YamlFacade::setCurrentDocument(size_t index) { 
    return impl_->setCurrentDocumentImpl(index); 
}

bool YamlFacade::findDocument(const DocumentSelector &selector) { 
    return impl_->findDocumentImpl(selector); 
}

YamlFacade::DocumentSelector YamlFacade::getCurrentDocumentMeta() const { 
    return impl_->getCurrentDocumentMetaImpl(); 
}

void YamlFacade::addDocument(const YamlFacade &document, const DocumentSelector &selector) { 
    impl_->addDocumentImpl(document, selector); 
}

bool YamlFacade::removeDocument(size_t index) { 
    return impl_->removeDocumentImpl(index); 
}

bool YamlFacade::isMultiDocument() const { 
    return impl_->isMultiDocumentImpl(); 
}

bool YamlFacade::selectDocumentByProfile(const std::string &profile) { 
    return impl_->selectDocumentByProfileImpl(profile); 
}

// ============ 静态工厂方法 ============

YamlFacade YamlFacade::createEmpty() { 
    return YamlFacade(std::make_unique<Impl>()); 
}

YamlFacade YamlFacade::createFrom(const std::string &filename) {
    auto facade = createEmpty();
    if (!facade.loadFromFile(filename)) {
        throw std::runtime_error("无法加载YAML文件: " + filename);
    }
    return facade;
}

YamlFacade YamlFacade::parse(const std::string &yaml) {
    auto facade = createEmpty();
    if (!facade.loadFromString(yaml)) {
        throw std::runtime_error("Cannot parse YAML string");//会报常量中有换行符所有改成英文
    }
    return facade;
}

} // namespace foundation::yaml