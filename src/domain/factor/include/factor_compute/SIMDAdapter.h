#pragma once

#include "FactorSignalTypes.h"

#include <cstdint>

namespace factor::compute::simd {

// ============================================================================
/// @brief SIMD 指令集适配层
///
/// 提供统一的向量化操作接口，编译期根据平台选择最佳实现：
/// - x86_64 AVX2 + FMA (默认 Windows/Linux)
/// - ARM NEON (macOS ARM)
/// - 纯标量 fallback (无 SIMD 支持平台)
///
/// 全局假定数据类型为 signal_value_t (float)，所有操作按 float 宽度进行。
// ============================================================================

// ---------- 平台检测与宽度定义 ----------

#if defined(__AVX2__) || defined(_M_AVX2) || defined(SIGNAL_SIMD_AVX2)
    #if !defined(SIGNAL_SIMD_AVX2)
        #define SIGNAL_SIMD_AVX2
    #endif
    #if !defined(SIGNAL_SIMD_SUPPORTED)
        #define SIGNAL_SIMD_SUPPORTED
    #endif
    constexpr int kVectorWidth = 8;   // 256-bit / 32-bit float
#elif defined(__ARM_NEON) || defined(__aarch64__) || defined(_M_ARM64)
    #define SIGNAL_SIMD_NEON
    #define SIGNAL_SIMD_SUPPORTED
    constexpr int kVectorWidth = 4;   // 128-bit / 32-bit float
#else
    constexpr int kVectorWidth = 1;   // 标量 fallback
#endif

// 默认 SIMD 对齐字节数 (AVX2 需要 32 字节对齐)
static constexpr size_t kSimdAlignmentBytes = 32;

// ---------- AVX2 实现 ----------

#ifdef SIGNAL_SIMD_AVX2

#include <immintrin.h>

/// @brief 加载 8 个 float (未对齐)
inline __m256 load(const signal_value_t* ptr) noexcept
{
    return _mm256_loadu_ps(ptr);
}

/// @brief 加载 8 个 float (对齐，要求 ptr 32 字节对齐)
inline __m256 loadAligned(const signal_value_t* ptr) noexcept
{
    return _mm256_load_ps(ptr);
}

/// @brief 存储 8 个 float (未对齐)
inline void store(signal_value_t* ptr, __m256 v) noexcept
{
    _mm256_storeu_ps(ptr, v);
}

/// @brief 存储 8 个 float (对齐)
inline void storeAligned(signal_value_t* ptr, __m256 v) noexcept
{
    _mm256_store_ps(ptr, v);
}

/// @brief 逐元素加法: a + b
inline __m256 add(__m256 a, __m256 b) noexcept { return _mm256_add_ps(a, b); }

/// @brief 逐元素减法: a - b
inline __m256 sub(__m256 a, __m256 b) noexcept { return _mm256_sub_ps(a, b); }

/// @brief 逐元素乘法: a * b
inline __m256 mul(__m256 a, __m256 b) noexcept { return _mm256_mul_ps(a, b); }

/// @brief 逐元素除法: a / b
inline __m256 div(__m256 a, __m256 b) noexcept { return _mm256_div_ps(a, b); }

/// @brief FMA: a * b + c (融合乘加，更高精度)
inline __m256 fma(__m256 a, __m256 b, __m256 c) noexcept
{
    return _mm256_fmadd_ps(a, b, c);
}

/// @brief 逐元素平方根
inline __m256 sqrt(__m256 v) noexcept { return _mm256_sqrt_ps(v); }

/// @brief 广播标量到整个向量
inline __m256 broadcast(signal_value_t s) noexcept
{
    return _mm256_set1_ps(s);
}

/// @brief 逐元素最小值
inline __m256 min(__m256 a, __m256 b) noexcept { return _mm256_min_ps(a, b); }

/// @brief 逐元素最大值
inline __m256 max(__m256 a, __m256 b) noexcept { return _mm256_max_ps(a, b); }

/// @brief 条件选择: mask 为 true 时取 a，否则取 b
inline __m256 select(__m256 mask, __m256 a, __m256 b) noexcept
{
    // mask 中全 1 表示 true，全 0 表示 false
    return _mm256_blendv_ps(b, a, mask);
}

/// @brief 比较 v > threshold，返回掩码向量（全1/全0）
inline __m256 greater(__m256 v, __m256 threshold) noexcept
{
    return _mm256_cmp_ps(v, threshold, _CMP_GT_OQ);
}

// ---------- ARM NEON 实现 ----------

#elif defined(SIGNAL_SIMD_NEON)

#include <arm_neon.h>

inline float32x4_t load(const signal_value_t* ptr) noexcept
{
    return vld1q_f32(ptr);
}

inline void store(signal_value_t* ptr, float32x4_t v) noexcept
{
    vst1q_f32(ptr, v);
}

inline float32x4_t add(float32x4_t a, float32x4_t b) noexcept
{
    return vaddq_f32(a, b);
}

inline float32x4_t sub(float32x4_t a, float32x4_t b) noexcept
{
    return vsubq_f32(a, b);
}

inline float32x4_t mul(float32x4_t a, float32x4_t b) noexcept
{
    return vmulq_f32(a, b);
}

inline float32x4_t div(float32x4_t a, float32x4_t b) noexcept
{
    // NEON 无直接除法，使用近似倒数和牛顿迭代
    float32x4_t recip = vrecpeq_f32(b);
    recip = vmulq_f32(vrecpsq_f32(b, recip), recip);  // 一次牛顿迭代
    return vmulq_f32(a, recip);
}

inline float32x4_t fma(float32x4_t a, float32x4_t b, float32x4_t c) noexcept
{
    return vfmaq_f32(c, a, b);
}

inline float32x4_t sqrt(float32x4_t v) noexcept
{
    return vrsqrteq_f32(v);  // 近似 sqrt，高精度场景需牛顿迭代
}

inline float32x4_t broadcast(signal_value_t s) noexcept
{
    return vdupq_n_f32(s);
}

inline float32x4_t min(float32x4_t a, float32x4_t b) noexcept
{
    return vminq_f32(a, b);
}

inline float32x4_t max(float32x4_t a, float32x4_t b) noexcept
{
    return vmaxq_f32(a, b);
}

inline float32x4_t greater(float32x4_t v, float32x4_t threshold) noexcept
{
    return vreinterpretq_f32_u32(
        vcgtq_f32(v, threshold));
}

// ---------- 标量 Fallback 实现 ----------

#else

inline signal_value_t load(const signal_value_t* ptr) noexcept { return *ptr; }
inline void store(signal_value_t* ptr, signal_value_t v) noexcept { *ptr = v; }
inline signal_value_t add(signal_value_t a, signal_value_t b) noexcept { return a + b; }
inline signal_value_t sub(signal_value_t a, signal_value_t b) noexcept { return a - b; }
inline signal_value_t mul(signal_value_t a, signal_value_t b) noexcept { return a * b; }
inline signal_value_t div(signal_value_t a, signal_value_t b) noexcept { return a / b; }
inline signal_value_t fma(signal_value_t a, signal_value_t b, signal_value_t c) noexcept
{
    return a * b + c;
}
inline signal_value_t sqrt(signal_value_t v) noexcept
{
    return static_cast<signal_value_t>(std::sqrt(static_cast<double>(v)));
}
inline signal_value_t broadcast(signal_value_t s) noexcept { return s; }
inline signal_value_t min(signal_value_t a, signal_value_t b) noexcept
{
    return (a < b) ? a : b;
}
inline signal_value_t max(signal_value_t a, signal_value_t b) noexcept
{
    return (a > b) ? a : b;
}
inline signal_value_t greater(signal_value_t v, signal_value_t threshold) noexcept
{
    return (v > threshold) ? signal_value_t{1} : signal_value_t{0};
}

#endif

// ============================================================================
/// @brief 向量化辅助函数：对齐分配器
///
/// 用于 std::vector 的对齐分配，确保数据满足 SIMD 对齐要求。
// ============================================================================
template <typename T>
struct AlignedAllocator {
    using value_type = T;

    AlignedAllocator() noexcept = default;

    template <typename U>
    AlignedAllocator(const AlignedAllocator<U>&) noexcept {}

    T* allocate(std::size_t n)
    {
        if (n > std::size_t{0xFFFFFFFFFFFFFFFFU} / sizeof(T)) {
            throw std::bad_alloc();
        }
        void* ptr = nullptr;
        if (posix_memalign(&ptr, kSimdAlignmentBytes, n * sizeof(T)) != 0) {
            throw std::bad_alloc();
        }
        return static_cast<T*>(ptr);
    }

    void deallocate(T* ptr, std::size_t) noexcept
    {
        free(ptr);
    }
};

template <typename T, typename U>
bool operator==(const AlignedAllocator<T>&, const AlignedAllocator<U>&) { return true; }

template <typename T, typename U>
bool operator!=(const AlignedAllocator<T>&, const AlignedAllocator<U>&) { return false; }

/// @brief 使用对齐分配器的 vector
template <typename T>
using AlignedVector = std::vector<T, AlignedAllocator<T>>;

} // namespace factor::compute::simd