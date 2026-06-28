#include "DataTableAssembler.h"

#include <algorithm>
#include <cstdlib>

namespace domain::data {

std::shared_ptr<arrow::Table> DataTableAssembler::buildFromSqlRows(
    const std::vector<astock::database::SqlQueryResultRow>& rows,
    const std::vector<std::string>& allFields,
    const std::unordered_set<std::string>& numericFields,
    const FinancialCache& finCache,
    const IndexCodeMap& indexMap)
{
    const int64_t nRows = static_cast<int64_t>(rows.size());
    if (nRows == 0 || allFields.empty()) return nullptr;

    std::vector<std::shared_ptr<arrow::ArrayBuilder>> builders;
    std::vector<std::shared_ptr<arrow::Field>> schemaFields;
    builders.reserve(allFields.size());
    schemaFields.reserve(allFields.size());
    for (const auto& fname : allFields) {
        if (numericFields.count(fname)) {
            builders.push_back(std::make_shared<arrow::DoubleBuilder>());
            schemaFields.push_back(arrow::field(fname, arrow::float64()));
        } else {
            builders.push_back(std::make_shared<arrow::StringBuilder>());
            schemaFields.push_back(arrow::field(fname, arrow::utf8()));
        }
    }

    for (int64_t ri = 0; ri < nRows; ++ri) {
        const auto& vals = rows[static_cast<size_t>(ri)].getValues();
        std::string sym, td;
        auto si = vals.find("symbol");
        auto ti = vals.find("trade_date");
        if (si != vals.end()) sym = si->second;
        if (ti != vals.end()) td = ti->second;

        const std::unordered_map<std::string, std::string>* fv = nullptr;
        if (!sym.empty() && !td.empty()) {
            auto fit = finCache.find(sym);
            if (fit != finCache.end() && !fit->second.empty()) {
                auto it = std::upper_bound(fit->second.begin(), fit->second.end(), td,
                    [](const std::string& d, const auto& rp) { return d < rp.first; });
                if (it != fit->second.begin()) fv = &(it - 1)->second;
            }
        }

        for (size_t ci = 0; ci < allFields.size(); ++ci) {
            const auto& cn = allFields[ci];
            bool isNum = numericFields.count(cn) > 0;

            auto vit = vals.find(cn);
            if (vit != vals.end() && !vit->second.empty()) {
                if (isNum) {
                    char* e = nullptr;
                    double d = strtod(vit->second.c_str(), &e);
                    if (e && static_cast<size_t>(e - vit->second.c_str()) == vit->second.size())
                        static_cast<arrow::DoubleBuilder*>(builders[ci].get())->Append(d);
                    else
                        static_cast<arrow::StringBuilder*>(builders[ci].get())->Append(vit->second);
                } else {
                    static_cast<arrow::StringBuilder*>(builders[ci].get())->Append(vit->second);
                }
            } else if (fv) {
                auto fit = fv->find(cn);
                if (fit != fv->end() && !fit->second.empty()) {
                    if (isNum) {
                        char* e = nullptr;
                        double d = strtod(fit->second.c_str(), &e);
                        if (e && static_cast<size_t>(e - fit->second.c_str()) == fit->second.size())
                            static_cast<arrow::DoubleBuilder*>(builders[ci].get())->Append(d);
                        else
                            static_cast<arrow::StringBuilder*>(builders[ci].get())->Append(fit->second);
                    } else {
                        static_cast<arrow::StringBuilder*>(builders[ci].get())->Append(fit->second);
                    }
                } else {
                    if (isNum) static_cast<arrow::DoubleBuilder*>(builders[ci].get())->AppendNull();
                    else       static_cast<arrow::StringBuilder*>(builders[ci].get())->AppendNull();
                }
            } else if (cn == "index_code" && !sym.empty()) {
                auto im = indexMap.find(sym);
                if (im != indexMap.end())
                    static_cast<arrow::StringBuilder*>(builders[ci].get())->Append(im->second);
                else
                    static_cast<arrow::StringBuilder*>(builders[ci].get())->AppendNull();
            } else {
                if (isNum) static_cast<arrow::DoubleBuilder*>(builders[ci].get())->AppendNull();
                else       static_cast<arrow::StringBuilder*>(builders[ci].get())->AppendNull();
            }
        }
    }

    std::vector<std::shared_ptr<arrow::ChunkedArray>> columns;
    columns.reserve(builders.size());
    for (auto& b : builders) {
        std::shared_ptr<arrow::Array> arr;
        b->Finish(&arr);
        columns.push_back(std::make_shared<arrow::ChunkedArray>(arr));
    }
    return arrow::Table::Make(arrow::schema(schemaFields), columns, nRows);
}

} // namespace domain::data
