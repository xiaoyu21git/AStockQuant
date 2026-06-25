#pragma once
// AStockSymbol.h — A股标的符号封装（纯 C++，零 Qt 依赖）
// 统一处理：代码/交易所/板块判断、格式转换、InstrumentId 映射

#include <cstdint>
#include <string>

namespace foundation {
namespace market {

/// @brief 交易所
enum class Exchange : uint8_t {
    Unknown = 0,
    SSE,    // 上海证券交易所
    SZSE,   // 深圳证券交易所
    BSE,    // 北京证券交易所
};

/// @brief 板块
enum class Board : uint8_t {
    Unknown = 0,
    Main,       // 主板
    ChiNext,    // 创业板 (300xxx, 301xxx)
    STAR,       // 科创板 (688xxx)
};

/// @brief A股标的符号 — 封装代码、交易所、板块判断及格式转换
class AStockSymbol final {
public:
    AStockSymbol() = default;

    /// @brief 从 "000001.SZ" / "600000.SH" 格式解析
    static AStockSymbol fromString(const std::string& symbol);

    /// @brief 从纯 6 位代码构造，根据前缀自动推断交易所
    static AStockSymbol fromCode(const std::string& code);

    [[nodiscard]] bool isValid() const noexcept { return !m_code.empty(); }

    /// @brief 6 位代码 如 "000001"
    [[nodiscard]] const std::string& code() const noexcept { return m_code; }

    /// @brief 交易所
    [[nodiscard]] Exchange exchange() const noexcept { return m_exchange; }

    /// @brief 板块
    [[nodiscard]] Board board() const noexcept { return m_board; }

    /// @brief 后缀 ".SZ" / ".SH" / ".BJ"
    [[nodiscard]] std::string suffix() const;

    /// @brief 完整符号 "000001.SZ"
    [[nodiscard]] std::string fullSymbol() const;

    /// @brief 掘金 SDK 格式 "SZSE.000001"
    [[nodiscard]] std::string gmSymbol() const;

    /// @brief 转为 InstrumentId（保留 6 位代码的完整数值，避免去前导零碰撞）
    [[nodiscard]] uint32_t instrumentId() const;

private:
    std::string m_code;        // 6 位数字字符串
    Exchange m_exchange{Exchange::Unknown};
    Board m_board{Board::Unknown};

    AStockSymbol(std::string code, Exchange exchange, Board board);
    static Exchange inferExchange(const std::string& code);
    static Board inferBoard(const std::string& code);
};

} // namespace market
} // namespace foundation
