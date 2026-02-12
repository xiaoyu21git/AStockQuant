#ifndef GMCPPSDK_H
#define GMCPPSDK_H

#include <iostream>
#include "gmdef.h"

#ifdef WIN32
#ifdef GMSDKC_EXPORTS  
#define GM_CXX_API __declspec(dllexport)
#else
#define GM_CXX_API __declspec(dllimport)
#endif
#else
#define GM_CXX_API
#endif

//结果集合
class DataSet
{
public:
	//获取api调用结果, 0: 成功， 非0: 错误码
	virtual int status() = 0;

	//当status() != 0 时，返回错误描述信息, utf-8编码
	virtual const char* errer_msg() = 0;

	//判断是否已经是到达结果集末尾
	virtual bool is_end() = 0;

	//移到下一条记录
	virtual void next() = 0;

	//获取整型值
	virtual int get_integer(const char *key) = 0;

	//获取长整型值
	virtual long long get_long_integer(const char *key) = 0;

	//获取浮点型值
	virtual double get_real(const char *key) = 0;

	//获取字符串型值
	virtual const char* get_string(const char *key) = 0;

	//释放数据集合
	virtual void release() = 0;

	//打印数据
	virtual const char* debug_string() = 0;
};

//结果数组
template <typename T>
class DataArray
{
public:
	//获取api调用结果, 0: 成功， 非0: 错误码
	virtual int status() = 0;

	//当status() != 0 时，返回错误描述信息, utf-8编码
	virtual const char* errer_msg() = 0;

	//返回结构数组的指针
	virtual T* data() = 0;

	//返回数据的长度
	virtual int count() = 0;

	//返回下标为i的结构引用，从0开始
	virtual T& at(int i) = 0;

	//释放数据集合
	virtual void release() = 0;
};

// 获取sdk版本号
GM_CXX_API const char* get_version();

// 设置token
GM_CXX_API void set_token(const char *token);

// 自定义服务地址
GM_CXX_API void set_serv_addr(const char *addr);

// 自定义服务地址v5版本
GM_CXX_API void set_serv_addr_v5(const char *addr, const char *orgcode, const char *site_id);

// 设置回测线程[1-32]
GM_CXX_API void set_backtest_threadnum(int n);

// 设置ctp行情服务信息
GM_CXX_API void set_ctp_md_info(const char *front_addresses, const char *user_name = NULL, const char *password = NULL);

//第三方系统设置留痕信息
GM_CXX_API void set_mfp(const char *mfp);

GM_CXX_API void set_simtrade_only();

GM_CXX_API void set_account_id(const char *account_ids);


//工具函数

//gbk字符串转utf8字符串
GM_CXX_API char * gbk2utf(const char *gbk);

//utf8字符串转gbk字符串
GM_CXX_API char * utf2gbk(const char *utf8);

// 查询当前行情快照
GM_CXX_API DataArray<Tick>* current(const char *symbols, bool include_call_auction = false);

// 查询当前最新价
GM_CXX_API DataArray<LatestPrice>* current_price(const char *symbols);

// 查询当前行情快照，依赖订阅行情
GM_CXX_API DataArray<Tick>* last_tick(const char *symbols, bool include_call_auction = false);

// 查询历史Tick行情
GM_CXX_API DataArray<Tick>* history_ticks(const char *symbols, const char *start_time, const char *end_time, int adjust = 0, const char *adjust_end_time = NULL, bool skip_suspended = true, const char *fill_missing = NULL);

// 查询历史Bar行情
GM_CXX_API DataArray<Bar>* history_bars(const char *symbols, const char *frequency, const char *start_time, const char *end_time, int adjust = 0, const char *adjust_end_time = NULL, bool skip_suspended = true, const char *fill_missing = NULL);

// 查询最新n条Tick行情
GM_CXX_API DataArray<Tick>* history_ticks_n(const char *symbols, int n, const char *end_time = NULL, int adjust = 0, const char *adjust_end_time = NULL, bool skip_suspended = true, const char *fill_missing = NULL);

//查询最新n条Bar行情
GM_CXX_API DataArray<Bar>* history_bars_n(const char *symbols, const char *frequency, int n, const char *end_time = NULL, int adjust = 0, const char *adjust_end_time = NULL, bool skip_suspended = true, const char *fill_missing = NULL);

// 查询历史L2 Tick行情
GM_CXX_API DataArray<Tick>* history_l2ticks(const char *symbols, const char *start_time, const char *end_time, int adjust = 0, const char *adjust_end_time = NULL, bool skip_suspended = true, const char *fill_missing = NULL);

// 查询历史L2 Bar行情
GM_CXX_API DataArray<Bar>* history_l2bars(const char *symbols, const char *frequency, const char *start_time, const char *end_time, int adjust = 0, const char *adjust_end_time = NULL, bool skip_suspended = true, const char *fill_missing = NULL);

// 查询历史L2 逐笔成交
GM_CXX_API DataArray<L2Transaction>* history_l2transactions(const char *symbols, const char *start_time, const char *end_time);

// 查询历史L2 逐笔委托
GM_CXX_API DataArray<L2Order>* history_l2orders(const char *symbols, const char *start_time, const char *end_time);

// 查询历史L2 委托队列(最优价最大50笔委托量)
GM_CXX_API DataArray<L2OrderQueue>* history_l2orders_queue(const char *symbols, const char *start_time, const char *end_time);

// 查询基本面数据
GM_CXX_API DataSet* get_fundamentals(const char *table, const char *symbols, const char *start_date, const char *end_date, const char *fields, const char *filter = NULL, const char *order_by = NULL, int limit = 1000);

// 查询基本面数据最新n条
GM_CXX_API DataSet* get_fundamentals_n(const char *table, const char *symbols, const char *end_date, const char *fields, int n = 1, const char *filter = NULL, const char * order_by = NULL);

// 查询最新交易标的信息
GM_CXX_API DataSet* get_instruments(const char *exchanges = NULL, const char *sec_types = NULL, const char* fields = NULL);

// 查询交易标的历史数据
GM_CXX_API DataSet* get_history_instruments(const char *symbols, const char *start_date, const char *end_date, const char *fields = NULL);

// 查询交易标的基本信息
GM_CXX_API DataSet* get_instrumentinfos(const char *symbols = NULL, const char *exchanges = NULL, const char * sec_types = NULL, const char *names = NULL, const char *fields = NULL);

// 查询指数成份股
GM_CXX_API DataSet* get_constituents(const char *index, const char *trade_date = NULL);

// 查询行业股票列表
GM_CXX_API DataArray<Symbol>* get_industry(const char *code);

// 查询概念板块股票列表
GM_CXX_API DataArray<Symbol>* get_concept(const char *code);

// 查询交易日列表
GM_CXX_API DataArray<TradingDate>* get_trading_dates(const char *exchange, const char *start_date, const char *end_date);

// 返回指定日期的上一个交易日
GM_CXX_API int get_previous_trading_date(const char *exchange, const char *date, char *output_date);

// 返回指定日期的下一个交易日
GM_CXX_API int get_next_trading_date(const char *exchange, const char *date, char *output_date);

// 查询分红送配
GM_CXX_API DataSet* get_dividend(const char *symbol, const char *start_date, const char *end_date);

// 获取连续合约
GM_CXX_API DataSet* get_continuous_contracts(const char *csymbol, const char *start_date, const char *end_date);

// 查询查询品种信息
GM_CXX_API DataArray<VarietyInfo>* get_varietyinfos(const char *variety_names);

// 查询交易时段
GM_CXX_API DataArray<TradingTimes>* get_trading_time(const char *variety_names);

// 查询期权合约
GM_CXX_API DataArray<Symbol>* option_get_symbols_by_exchange(const char *exchange = NULL, const char *trade_date = NULL, const char *call_or_put = NULL);

// 查询实平虚值某档合约
GM_CXX_API DataArray<Symbol>* option_get_symbols_by_in_at_out(const char *underlying_symbol = NULL, const char *trade_date = NULL, int excute_month = 0, const char *call_or_put = NULL, int in_at_out = IN_AT_OUT_NO_SPECIFIED, const char *price_type = NULL, double price = -1);

// 查询期权到期日列表
GM_CXX_API DataArray<TradingDate>* option_get_delisted_dates(const char *underlying_symbol = NULL, const char *trade_date = NULL, int excute_month = 0);

// 查询期权行权价列表
GM_CXX_API DataArray<double>* option_get_exercise_prices(const char *underlying_symbol = NULL, const char *trade_date = NULL, int excute_month = 0, const char *adjust_flag = NULL);


/*************************************************************** 新增数据函数  **************************************************************************/

//通用基础数据

//查询标的基本信息
GM_CXX_API DataArray<SymbolInfo>* get_symbol_infos(long long sec_type1, long long sec_type2 = 0, const char *exchanges = NULL, const char *symbols = NULL);
//查询指定交易日多标的交易信息
GM_CXX_API DataArray<SymbolContent>* get_symbols_by_date(long long sec_type1, long long sec_type2 = 0, const char *exchanges = NULL, const char *symbols = NULL, bool skip_suspended = true, bool skip_st = true, const char *trade_date = NULL);
//查询指定标的多日交易信息
GM_CXX_API DataArray<SymbolContent>* get_history_symbol(const char *symbol, const char *start_date = NULL, const char *end_date = NULL);
//查询年度交易日历
GM_CXX_API DataArray<TradingDateContent>* get_trading_dates_by_year(const char *exchange, int start_year, int end_year);
//查询指定日期的前n个交易日
GM_CXX_API DataArray<TradingDate>* get_previous_n_trading_dates(const char *exchange, const char *date, int n);
//查询指定日期的后n个交易日
GM_CXX_API DataArray<TradingDate>* get_next_n_trading_dates(const char *exchange, const char *date, int n);

//查询交易时段
GM_CXX_API DataArray<TradingSession>* get_trading_session(const char *symbols);
// - 查询合约到期剩余天数
GM_CXX_API DataArray<ContractExpireRestDays>* get_contract_expire_rest_days(const char *symbols, const char *start_date, const char *end_date, bool trade_flag = false);
// - 查询开盘集合竞价成交
GM_CXX_API DataArray<Tick>* get_open_call_auction(const char *symbols, const char *trade_date = NULL);

//股票基础数据

//查询行业分类
GM_CXX_API DataArray<StkIndustryCategory>* stk_get_industry_category(const char *source = NULL, int level = 0);
//查询行业成分股
GM_CXX_API DataArray<StkIndustryConstituent>* stk_get_industry_constituents(const char *industry_code, const char *date = NULL);
//查询股票的所属行业
GM_CXX_API DataArray<StkSymbolIndustry>* stk_get_symbol_industry(const char *symbols, const char *source = NULL, int level = 0, const char *date = NULL);
// - 查询板块分类
GM_CXX_API DataArray<StkSectorCategory>* stk_get_sector_category(const char *sector_type);
// - 查询板块成分股
GM_CXX_API DataArray<StkSectorConstituent>* stk_get_sector_constituents(const char *sector_code);
// - 查询股票的所属板块
GM_CXX_API DataArray<StkSymbolSector>* stk_get_symbol_sector(const char *symbols, const char *sector_type);

//查询指数最新成分股 
GM_CXX_API DataArray<StkIndexConstituent>* stk_get_index_constituents(const char *symbol, const char *trade_date = NULL);
//查询股票分红送股信息
GM_CXX_API DataArray<StkDividend>* stk_get_dividend(const char *symbol, const char *start_date, const char *end_date);
// - 查询股票配股信息
GM_CXX_API DataArray<StkRation>* stk_get_ration(const char *symbol, const char *start_date = NULL, const char *end_date = NULL);

//查询股票的复权因子
GM_CXX_API DataArray<StkAdjFactor>* stk_get_adj_factor(const char *symbol, const char *start_date = NULL, const char *end_date = NULL, const char *base_date = NULL);
// - 查询股东户数
GM_CXX_API DataArray<StkShareholderNum>* stk_get_shareholder_num(const char *symbol, const char *start_date = NULL, const char *end_date = NULL);
// - 查询十大股东
GM_CXX_API DataArray<StkShareholder>* stk_get_top_shareholder(const char *symbol, const char *start_date = NULL, const char *end_date = NULL, bool tradable_holder = false);
// - 查询股本变动
GM_CXX_API DataArray<StkShareChange>* stk_get_share_change(const char *symbol, const char *start_date = NULL, const char *end_date = NULL);

// - 查询资产负债表数据
GM_CXX_API DataSet* stk_get_fundamentals_balance(const char *symbol, const char *fields, int rpt_type = 0, int data_type = 0, const char *start_date = NULL, const char *end_date = NULL);
//  - 查询现金流量表数据
GM_CXX_API DataSet* stk_get_fundamentals_cashflow(const char *symbol, const char *fields, int rpt_type = 0, int data_type = 0, const char *start_date = NULL, const char *end_date = NULL);
//  - 查询利润表数据
GM_CXX_API DataSet* stk_get_fundamentals_income(const char *symbol, const char *fields, int rpt_type = 0, int data_type = 0, const char *start_date = NULL, const char *end_date = NULL);
// - 查询资产负债表截面数据
GM_CXX_API DataSet* stk_get_fundamentals_balance_pt(const char *symbols, const char *fields, int rpt_type = 0, int data_type = 0, const char *date = NULL);
// - 查询现金流量表截面数据
GM_CXX_API DataSet* stk_get_fundamentals_cashflow_pt(const char *symbols, const char *fields, int rpt_type = 0, int data_type = 0, const char *date = NULL);
// - 查询利润表截面数据
GM_CXX_API DataSet* stk_get_fundamentals_income_pt(const char *symbols, const char *fields, int rpt_type = 0, int data_type = 0, const char *date = NULL);

// - 查询财务主要指标数据
GM_CXX_API DataSet* stk_get_finance_prime(const char *symbol, const char *fields, int rpt_type = 0, int data_type = 0, const char *start_date = NULL, const char *end_date = NULL);
// - 查询财务衍生指标数据
GM_CXX_API DataSet* stk_get_finance_deriv(const char *symbol, const char *fields, int rpt_type = 0, int data_type = 0, const char *start_date = NULL, const char *end_date = NULL);
// - 查询估值指标每日数据
GM_CXX_API DataSet* stk_get_daily_valuation(const char *symbol, const char *fields, const char *start_date = NULL, const char *end_date = NULL);
// - 查询市值指标每日数据
GM_CXX_API DataSet* stk_get_daily_mktvalue(const char *symbol, const char *fields, const char *start_date = NULL, const char *end_date = NULL);
// - 查询基础指标每日数据
GM_CXX_API DataSet* stk_get_daily_basic(const char *symbol, const char *fields, const char *start_date = NULL, const char *end_date = NULL);

// - 查询财务主要指标截面数据
GM_CXX_API DataSet* stk_get_finance_prime_pt(const char *symbols, const char *fields, int rpt_type = 0, int data_type = 0, const char *date = NULL);
// - 查询财务衍生指标截面数据
GM_CXX_API DataSet* stk_get_finance_deriv_pt(const char *symbols, const char *fields, int rpt_type = 0, int data_type = 0, const char *date = NULL);
// - 查询估值指标单日截面数据
GM_CXX_API DataSet* stk_get_daily_valuation_pt(const char *symbols, const char *fields, const char *trade_date = NULL);
// - 查询市值指标单日截面数据
GM_CXX_API DataSet* stk_get_daily_mktvalue_pt(const char *symbols, const char *fields, const char *trade_date = NULL);
// - 查询基础指标单日截面数据
GM_CXX_API DataSet* stk_get_daily_basic_pt(const char *symbols, const char *fields, const char *trade_date = NULL);

// - 查询龙虎榜股票数据
GM_CXX_API DataArray<StkAbnorChangeStocks>* stk_abnor_change_stocks(const char *symbols = NULL, const char *change_types = NULL, const char *trade_date = NULL);
// - 查询龙虎榜营业部数据
GM_CXX_API DataArray<StkAbnorChangeDetail>* stk_abnor_change_detail(const char *symbols = NULL, const char *change_types = NULL, const char *trade_date = NULL);
// - 查询沪深港通额度数据
GM_CXX_API DataArray<StkQuotaShszhkInfos>* stk_quota_shszhk_infos(const char *types = NULL, const char *start_date = NULL, const char *end_date = NULL, int count = 0);
// - 查询沪深港通标的港股机构持股明细数据
GM_CXX_API DataArray<StkHkInstHoldingDetailInfo>* stk_hk_inst_holding_detail_info(const char *symbols = NULL, const char *trade_date = NULL);
// - 查询沪深港通标的港股机构持股数据
GM_CXX_API DataArray<StkHkInstHoldingInfo>* stk_hk_inst_holding_info(const char *symbols = NULL, const char *trade_date = NULL);
// - 查询沪深港通十大活跃成交股数据
GM_CXX_API DataArray<StkActiveStockTop10ShszhkInfo>* stk_active_stock_top10_shszhk_info(const char *types = NULL, const char *trade_date = NULL);
// - 查询股票交易资金流向
GM_CXX_API DataArray<StkMoneyFlowRecord>* stk_get_money_flow(const char *symbols, const char *trade_date = NULL);
// - 查询财务审计意见
GM_CXX_API DataArray<StkFinanceAuditRecord>* stk_get_finance_audit(const char *symbols, const char *date = NULL, const char *rpt_date = NULL);
// - 查询公司业绩预告
GM_CXX_API DataArray<StkFinanceForecastRecord>* stk_get_finance_forecast(const char *symbols, const char *rpt_type = NULL, const char *date = NULL);


//期货基础数据

//查询连续合约对应的真实合约 
GM_CXX_API DataArray<FutContinuousContractsInfo>* fut_get_continuous_contracts(const char *csymbol, const char *start_date = NULL, const char *end_date = NULL);
//查询期货标准品种信息
GM_CXX_API DataArray<FutContractInfo>* fut_get_contract_info(const char *product_codes);
//查询期货每日成交持仓排名
GM_CXX_API DataArray<FutTransactionRanking>* fut_get_transaction_ranking(const char *symbol, const char *trade_date, const char *indicator = NULL);
//查询期货每日成交持仓排名(多代码，多指标)
GM_CXX_API DataArray<FutTransactionRankings>* fut_get_transaction_rankings(const char *symbols, const char *trade_date, const char *indicators = NULL);
//查询期货仓单数据
GM_CXX_API DataArray<FutWarehouseReceiptInfo>* fut_get_warehouse_receipt(const char *product_code, const char *start_date = NULL, const char *end_date = NULL);


//基金基础数据

//查询ETF最新成分股
GM_CXX_API DataArray<FndEtfConstituents>* fnd_get_etf_constituents(const char *symbol);
//查询基金资产组合(股票投资组合)
GM_CXX_API DataArray<FndPortfolioStockInfo>* fnd_get_stock_portfolio(const char *symbol, int report_type, const char *start_date = NULL, const char *end_date = NULL);
//查询基金资产组合(债券投资组合)
GM_CXX_API DataArray<FndPortfolioBondInfo>* fnd_get_bond_portfolio(const char *symbol, int report_type, const char *start_date = NULL, const char *end_date = NULL);
//查询基金资产组合(基金投资组合)
GM_CXX_API DataArray<FndPortfolioFundInfo>* fnd_get_fund_portfolio(const char *symbol, int report_type, const char *start_date = NULL, const char *end_date = NULL);
//查询基金净值数据
GM_CXX_API DataArray<FndNetValueInfo>* fnd_get_net_value(const char *symbol, const char *start_date = NULL, const char *end_date = NULL);
//查询基金复权因子
GM_CXX_API DataArray<FndAdjFactorInfo>* fnd_get_adj_factor(const char *symbol, const char *start_date = NULL, const char *end_date = NULL, const char *base_date = NULL);
//查询基金分红信息
GM_CXX_API DataArray<FndDividendInfo>* fnd_get_dividend(const char *symbol, const char *start_date, const char *end_date);
//查询基金拆分折算信息
GM_CXX_API DataArray<FndSplitInfo>* fnd_get_split(const char *symbol, const char *start_date, const char *end_date);
//查询基金规模数据
GM_CXX_API DataArray<FndShareRecord>* fnd_get_share(const char *fund, const char *start_date = NULL, const char *end_date = NULL);


//债券基础数据

//查询可转债转股价变动信息
GM_CXX_API DataArray<BndConversionPrice>* bnd_get_conversion_price(const char *symbol, const char *start_date = NULL, const char *end_date = NULL);
//查询可转债赎回信息
GM_CXX_API DataArray<BndCallInfo>* bnd_get_call_info(const char *symbol, const char *start_date = NULL, const char *end_date = NULL);
//查询可转债回售信息
GM_CXX_API DataArray<BndPutInfo>* bnd_get_put_info(const char *symbol, const char *start_date = NULL, const char *end_date = NULL);
//查询可转债剩余规模变动
GM_CXX_API DataArray<BndAmountChange>* bnd_get_amount_change(const char *symbol, const char *start_date = NULL, const char *end_date = NULL);
//查询可转债分析指标
GM_CXX_API DataArray<BndAnalysisRecord>* bnd_get_analysis(const char *symbol, const char *start_date = NULL, const char *end_date = NULL);


//标的池

//设置标的池
GM_CXX_API int universe_set(const char *universe_name, const char *universe_symbols);

//删除标的池
GM_CXX_API int universe_delete(const char *universe_name);

//获取标的池成分
GM_CXX_API DataArray<Symbol>* universe_get_symbols(const char *universe_name);

//获取全部标的池名称 
GM_CXX_API DataArray<GmName>* universe_get_names();

#endif
