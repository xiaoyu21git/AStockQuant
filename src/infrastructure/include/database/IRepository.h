#ifndef ASTOCK_INFRASTRUCTURE_DATABASE_IREPOSITORY_H
#define ASTOCK_INFRASTRUCTURE_DATABASE_IREPOSITORY_H

#include <memory>
#include <vector>
#include <optional>
#include <string>
#include <functional>

namespace astock {
namespace database {

/**
 * @brief 仓储接口模板
 * 
 * 提供统一的CRUD操作抽象
 * T: 实体类型
 * K: 主键类型
 */
template<typename T, typename K = std::string>
class IRepository {
public:
    virtual ~IRepository() = default;
    
    /**
     * @brief 根据ID查询单条记录
     */
    virtual std::optional<T> findById(const K& id) = 0;
    
    /**
     * @brief 查询所有记录
     */
    virtual std::vector<T> findAll() = 0;
    
    /**
     * @brief 条件查询
     * @param predicate 条件函数
     */
    virtual std::vector<T> findWhere(
        std::function<bool(const T&)> predicate) = 0;
    
    /**
     * @brief 保存记录
     */
    virtual bool save(const T& entity) = 0;
    
    /**
     * @brief 批量保存
     */
    virtual size_t saveBatch(const std::vector<T>& entities) = 0;
    
    /**
     * @brief 更新记录
     */
    virtual bool update(const T& entity) = 0;
    
    /**
     * @brief 删除记录
     */
    virtual bool remove(const K& id) = 0;
    
    /**
     * @brief 统计记录数
     */
    virtual size_t count() = 0;
    
    /**
     * @brief 检查是否存在
     */
    virtual bool exists(const K& id) = 0;
};

} // namespace database
} // namespace astock

#endif // ASTOCK_INFRASTRUCTURE_DATABASE_IREPOSITORY_H
