// yaml_facade.cpp
#include "yaml_facade.h"
#include <iostream>
#include <fstream>
#include <sstream>
#include <vector>
#include <map>
#include <yaml-cpp/yaml.h>

namespace foundation::yaml {

	class YamlFacade::Impl {
	public:
		Impl() = default;
		explicit Impl(const YAML::Node &node) : root_(node) {}

		// 加载
		bool loadFromFile(const std::string &filename) {
			try {
				root_ = YAML::LoadFile(filename);
				return true;
			} catch (const YAML::Exception &e) {
				std::cerr << "YAML加载错误: " << e.what() << std::endl;
				return false;
			}
		}

		bool loadFromString(const std::string &yaml) {
			try {
				root_ = YAML::Load(yaml);
				return true;
			} catch (const YAML::Exception &e) {
				std::cerr << "YAML解析错误: " << e.what() << std::endl;
				return false;
			}
		}

		// 保存
		bool saveToFile(const std::string &filename) const {
			try {
				std::ofstream file(filename);
				if (!file.is_open())
					return false;

				YAML::Emitter emitter;
				emitter << root_;
				file << emitter.c_str();
				return true;
			} catch (const YAML::Exception &e) {
				std::cerr << "YAML保存错误: " << e.what() << std::endl;
				return false;
			}
		}

		std::string toString() const {
			YAML::Emitter emitter;
			emitter << root_;
			return emitter.c_str();
		}

		// 获取节点
		YAML::Node getNode(const std::string &path) const {
			if (path.empty())
				return root_;

			std::istringstream iss(path);
			std::string token;
			YAML::Node current = root_;

			while (std::getline(iss, token, '.')) {
				if (!current.IsDefined())
					break;

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

		bool hasValue(const std::string &path) const { return getNode(path).IsDefined(); }

		// 值获取
		std::string getString(const std::string &path, const std::string &defaultValue) const {
			auto node = getNode(path);
			return node.IsDefined() ? node.as<std::string>() : defaultValue;
		}

		int getInt(const std::string &path, int defaultValue) const {
			auto node = getNode(path);
			return node.IsDefined() ? node.as<int>() : defaultValue;
		}

		double getDouble(const std::string &path, double defaultValue) const {
			auto node = getNode(path);
			return node.IsDefined() ? node.as<double>() : defaultValue;
		}

		bool getBool(const std::string &path, bool defaultValue) const {
			auto node = getNode(path);
			return node.IsDefined() ? node.as<bool>() : defaultValue;
		}

		// 数组获取
		std::vector<std::string> getStringArray(const std::string &path) const {
			std::vector<std::string> result;
			auto node = getNode(path);
			if (node.IsSequence()) {
				for (const auto &item : node)
					result.push_back(item.as<std::string>());
			}
			return result;
		}

		std::vector<int> getIntArray(const std::string &path) const {
			std::vector<int> result;
			auto node = getNode(path);
			if (node.IsSequence()) {
				for (const auto &item : node)
					result.push_back(item.as<int>());
			}
			return result;
		}

		std::vector<double> getDoubleArray(const std::string &path) const {
			std::vector<double> result;
			auto node = getNode(path);
			if (node.IsSequence()) {
				for (const auto &item : node)
					result.push_back(item.as<double>());
			}
			return result;
		}

		// 映射获取
		std::map<std::string, std::string> getStringMap(const std::string &path) const {
			std::map<std::string, std::string> result;
			auto node = getNode(path);
			if (node.IsMap()) {
				for (const auto &item : node)
					result[item.first.as<std::string>()] = item.second.as<std::string>();
			}
			return result;
		}

		std::map<std::string, int> getIntMap(const std::string &path) const {
			std::map<std::string, int> result;
			auto node = getNode(path);
			if (node.IsMap()) {
				for (const auto &item : node)
					result[item.first.as<std::string>()] = item.second.as<int>();
			}
			return result;
		}

		// 设置值
		void setString(const std::string &path, const std::string &value) { setNode(path) = value; }
		void setInt(const std::string &path, int value) { setNode(path) = value; }
		void setDouble(const std::string &path, double value) { setNode(path) = value; }
		void setBool(const std::string &path, bool value) { setNode(path) = value; }

		// 设置数组
		void setStringArray(const std::string &path, const std::vector<std::string> &values) {
			auto &node = setNode(path);
			node = YAML::Node(YAML::NodeType::Sequence);
			for (const auto &value : values)
				node.push_back(value);
		}

		void setIntArray(const std::string &path, const std::vector<int> &values) {
			auto &node = setNode(path);
			node = YAML::Node(YAML::NodeType::Sequence);
			for (int value : values)
				node.push_back(value);
		}

		// 设置映射
		void setStringMap(const std::string &path, const std::map<std::string, std::string> &values) {
			auto &node = setNode(path);
			node = YAML::Node(YAML::NodeType::Map);
			for (const auto &p : values)
				node[p.first] = p.second;
		}

		// 合并
		void merge(const Impl &other) { mergeNodes(root_, other.root_); }

		// 清除
		void clear() { root_ = YAML::Node(); }

	private:
		YAML::Node &setNode(const std::string &path) {
			if (path.empty())
				return root_;

			std::istringstream iss(path);
			std::string token;
			YAML::Node *current = &root_;

			while (std::getline(iss, token, '.')) {
				if (!current->IsDefined())
					*current = YAML::Node(YAML::NodeType::Map);

				// 检查是否是数组索引
				if (!token.empty() && token[0] == '[' && token.back() == ']') {
					try {
						size_t index = std::stoul(token.substr(1, token.size() - 2));
						if (!(*current)[index].IsDefined())
							(*current)[index] = YAML::Node();
						current = &(*current)[index];
					} catch (...) {
						throw std::runtime_error("无效的数组索引: " + token);
					}
				} else {
					if (!(*current)[token].IsDefined())
						(*current)[token] = YAML::Node();
					current = &(*current)[token];
				}
			}

			return *current;
		}

		void mergeNodes(YAML::Node &target, const YAML::Node &source) {
			if (!source.IsDefined())
				return;

			if (source.IsScalar()) {
				target = source;
			} else if (source.IsSequence()) {
				if (!target.IsDefined() || !target.IsSequence())
					target = YAML::Node(YAML::NodeType::Sequence);
				for (const auto &item : source)
					target.push_back(item);
			} else if (source.IsMap()) {
				if (!target.IsDefined() || !target.IsMap())
					target = YAML::Node(YAML::NodeType::Map);
				for (const auto &item : source) {
					std::string key = item.first.as<std::string>();
					if (!target[key].IsDefined())
						target[key] = item.second;
					else
						mergeNodes(target[key], item.second);
				}
			}
		}

		YAML::Node root_;
	};

	// YamlFacade 实现
	YamlFacade::YamlFacade() : impl_(std::make_unique<Impl>()) {}

	YamlFacade::YamlFacade(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}

	YamlFacade::~YamlFacade() = default;

	YamlFacade::YamlFacade(YamlFacade &&other) noexcept : impl_(std::move(other.impl_)) {}

	YamlFacade &YamlFacade::operator=(YamlFacade &&other) noexcept {
		if (this != &other)
			impl_ = std::move(other.impl_);
		return *this;
	}

	bool YamlFacade::loadFromFile(const std::string &filename) { return impl_->loadFromFile(filename); }

	bool YamlFacade::loadFromString(const std::string &yaml) { return impl_->loadFromString(yaml); }

	bool YamlFacade::saveToFile(const std::string &filename) const { return impl_->saveToFile(filename); }

	std::string YamlFacade::toString() const { return impl_->toString(); }

	bool YamlFacade::hasValue(const std::string &path) const { return impl_->hasValue(path); }

	std::string YamlFacade::getString(const std::string &path, const std::string &defaultValue) const {
		return impl_->getString(path, defaultValue);
	}

	int YamlFacade::getInt(const std::string &path, int defaultValue) const { return impl_->getInt(path, defaultValue); }

	double YamlFacade::getDouble(const std::string &path, double defaultValue) const {
		return impl_->getDouble(path, defaultValue);
	}

	bool YamlFacade::getBool(const std::string &path, bool defaultValue) const { return impl_->getBool(path, defaultValue); }

	std::vector<std::string> YamlFacade::getStringArray(const std::string &path) const { return impl_->getStringArray(path); }

	std::vector<int> YamlFacade::getIntArray(const std::string &path) const { return impl_->getIntArray(path); }

	std::vector<double> YamlFacade::getDoubleArray(const std::string &path) const { return impl_->getDoubleArray(path); }

	std::map<std::string, std::string> YamlFacade::getStringMap(const std::string &path) const { return impl_->getStringMap(path); }

	std::map<std::string, int> YamlFacade::getIntMap(const std::string &path) const { return impl_->getIntMap(path); }

	void YamlFacade::setString(const std::string &path, const std::string &value) { impl_->setString(path, value); }

	void YamlFacade::setInt(const std::string &path, int value) { impl_->setInt(path, value); }

	void YamlFacade::setDouble(const std::string &path, double value) { impl_->setDouble(path, value); }

	void YamlFacade::setBool(const std::string &path, bool value) { impl_->setBool(path, value); }

	void YamlFacade::setStringArray(const std::string &path, const std::vector<std::string> &values) {
		impl_->setStringArray(path, values);
	}

	void YamlFacade::setIntArray(const std::string &path, const std::vector<int> &values) { impl_->setIntArray(path, values); }

	void YamlFacade::setStringMap(const std::string &path, const std::map<std::string, std::string> &values) {
		impl_->setStringMap(path, values);
	}

	void YamlFacade::merge(const YamlFacade &other) { impl_->merge(*other.impl_); }

	void YamlFacade::clear() { impl_->clear(); }

	// 静态工厂方法
	YamlFacade YamlFacade::createEmpty() { return YamlFacade(std::make_unique<Impl>()); }

	YamlFacade YamlFacade::loadFrom(const std::string &filename) {
		auto facade = createEmpty();
		if (!facade.loadFromFile(filename))
			throw std::runtime_error("无法加载YAML文件: " + filename);
		return facade;
	}

	YamlFacade YamlFacade::parse(const std::string &yaml) {
		auto facade = createEmpty();
		if (!facade.loadFromString(yaml))
			throw std::runtime_error(
            "无法解析YAML字符串\n"
            "请检查YAML格式是否正确"
        );
		return facade;
	}

} // namespace foundation::yaml