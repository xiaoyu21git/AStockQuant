#include "CachedMarketDataView.h"
#include <algorithm>

namespace factor::compute {

using RowMatrixXd = Eigen::Matrix<double, Eigen::Dynamic, Eigen::Dynamic, Eigen::RowMajor>;

class CachedMarketDataView::Impl {
public:
    ColumnData m_open[1]{};
    ColumnData m_high[1]{};
    ColumnData m_low[1]{};
    ColumnData m_close[1]{};
    ColumnData m_volume[1]{};

    RowMatrixXd m_openMat;
    RowMatrixXd m_highMat;
    RowMatrixXd m_lowMat;
    RowMatrixXd m_closeMat;
    RowMatrixXd m_volumeMat;

    std::unordered_map<std::string, RowMatrixXd> m_fieldMats;

    std::vector<DateKey> m_dates;
    std::vector<InstrumentId> m_instruments;

    void buildMatrix(const ColumnData& col, RowMatrixXd& mat)
    {
        mat.resize(col.dateCount, col.instrumentCount);
        for (int d = 0; d < col.dateCount; ++d) {
            for (int i = 0; i < col.instrumentCount; ++i) {
                int idx = d * col.instrumentCount + i;
                mat(d, i) = (idx < static_cast<int>(col.values.size())) ? col.values[idx] : 0.0;
            }
        }
    }
};

CachedMarketDataView::CachedMarketDataView()
    : impl_(std::make_unique<Impl>())
{
}

CachedMarketDataView::~CachedMarketDataView() = default;

void CachedMarketDataView::loadFromColumnData(
    ColumnData open, ColumnData high, ColumnData low, ColumnData close, ColumnData volume)
{
    impl_->m_open[0]   = std::move(open);
    impl_->m_high[0]   = std::move(high);
    impl_->m_low[0]    = std::move(low);
    impl_->m_close[0]  = std::move(close);
    impl_->m_volume[0] = std::move(volume);

    impl_->buildMatrix(impl_->m_open[0], impl_->m_openMat);
    impl_->buildMatrix(impl_->m_high[0], impl_->m_highMat);
    impl_->buildMatrix(impl_->m_low[0],  impl_->m_lowMat);
    impl_->buildMatrix(impl_->m_close[0], impl_->m_closeMat);
    impl_->buildMatrix(impl_->m_volume[0], impl_->m_volumeMat);

    impl_->m_dates       = impl_->m_open[0].dates;
    impl_->m_instruments = impl_->m_open[0].instruments;
}

void CachedMarketDataView::loadAdditionalField(const std::string& fieldName, ColumnData column)
{
    if (fieldName.empty()) return;
    RowMatrixXd mat;
    impl_->buildMatrix(column, mat);
    impl_->m_fieldMats[fieldName] = std::move(mat);
}

NumericConstMatrixView CachedMarketDataView::open() const
{
    return NumericConstMatrixView{impl_->m_openMat.data(), static_cast<int>(impl_->m_openMat.rows()), static_cast<int>(impl_->m_openMat.cols()), static_cast<int>(impl_->m_openMat.cols())};
}

NumericConstMatrixView CachedMarketDataView::high() const
{
    return NumericConstMatrixView{impl_->m_highMat.data(), static_cast<int>(impl_->m_highMat.rows()), static_cast<int>(impl_->m_highMat.cols()), static_cast<int>(impl_->m_highMat.cols())};
}

NumericConstMatrixView CachedMarketDataView::low() const
{
    return NumericConstMatrixView{impl_->m_lowMat.data(), static_cast<int>(impl_->m_lowMat.rows()), static_cast<int>(impl_->m_lowMat.cols()), static_cast<int>(impl_->m_lowMat.cols())};
}

NumericConstMatrixView CachedMarketDataView::close() const
{
    return NumericConstMatrixView{impl_->m_closeMat.data(), static_cast<int>(impl_->m_closeMat.rows()), static_cast<int>(impl_->m_closeMat.cols()), static_cast<int>(impl_->m_closeMat.cols())};
}

NumericConstMatrixView CachedMarketDataView::volume() const
{
    return NumericConstMatrixView{impl_->m_volumeMat.data(), static_cast<int>(impl_->m_volumeMat.rows()), static_cast<int>(impl_->m_volumeMat.cols()), static_cast<int>(impl_->m_volumeMat.cols())};
}

std::optional<NumericConstMatrixView>
CachedMarketDataView::getField(const std::string& fieldName) const
{
    // 先查 OHLCV 基类矩阵（这些不在 m_fieldMats 中）
    if (fieldName == "open")   return open();
    if (fieldName == "high")   return high();
    if (fieldName == "low")    return low();
    if (fieldName == "close")  return close();
    if (fieldName == "volume") return volume();

    auto it = impl_->m_fieldMats.find(fieldName);
    if (it == impl_->m_fieldMats.end()) {
        return std::nullopt;
    }
    const RowMatrixXd& mat = it->second;
    return NumericConstMatrixView{
        mat.data(),
        static_cast<int>(mat.rows()),
        static_cast<int>(mat.cols()),
        static_cast<int>(mat.cols())
    };
}

bool CachedMarketDataView::hasField(const std::string& fieldName) const
{
    return impl_->m_fieldMats.find(fieldName) != impl_->m_fieldMats.end();
}

const std::vector<DateKey>& CachedMarketDataView::dates() const { return impl_->m_dates; }
const std::vector<InstrumentId>& CachedMarketDataView::instruments() const { return impl_->m_instruments; }

} // namespace factor::compute
