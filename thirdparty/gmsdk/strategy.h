#ifndef GMCXXSDK_STRATEGY_H
#define GMCXXSDK_STRATEGY_H

#include "gmapi.h"

#define GM_CLASS GM_CXX_API 

class GM_CLASS Strategy
{
public:
	Strategy(const char *token, const char *strategy_id, int mode);
	Strategy();
	virtual ~Strategy();

public: //基础函数

	//运行策略
	int run();

	//停止策略
	void stop();

	//设置策略ID
	void set_strategy_id(const char *strategy_id);

	//设置用户token
	void set_token(const char *token);

	//设置策略运行模式
	void set_mode(int mode);

	//定时任务
	int schedule(const char *data_rule, const char *time_rule);

	//启动定时器
	int timer(int period, int start_delay);

	//停止定时器
	void timer_stop(int timer_id);

	//当前时间
	double now();

	//设置回测参数
	int set_backtest_config(
		const char  *start_time,
		const char  *end_time,
		double initial_cash = 1000000,
		double transaction_ratio = 1,
		double commission_ratio = 0,
		double slippage_ratio = 0,
		int    adjust = 0,
		int    check_cache = 1
	);

	//设置回测参数
	int set_backtest_config(BacktestConfig &cfg);
	//获取错误信息
	const char *get_last_error_detail();
		

public: //数据函数

		// 订阅行情
	int subscribe(const char *symbols, const char * frequency, bool unsubscribe_previous = false);

	// 退订行情
	int unsubscribe(const char *symbols, const char * frequency);


public: //交易函数

	//查询交易账号
	DataArray<Account>* get_accounts();

	//查询指定交易账号状态
	int get_account_status(const char *account, AccountStatus &as);

	//查询所有交易账号状态
	DataArray<AccountStatus>* get_all_account_status();

	//按指定量委托
	Order order_volume(const char *symbol, int volume, int side, int order_type, int position_effect, double price = 0, const char *account = NULL);

	//按指定价值委托
	Order order_value(const char *symbol, double value, int side, int order_type, int position_effect, double price = 0, const char *account = NULL);

	//按总资产指定比例委托
	Order order_percent(const char *symbol, double percent, int side, int order_type, int position_effect, double price = 0, const char *account = NULL);

	//调仓到目标持仓量
	Order order_target_volume(const char *symbol, int volume, int position_side, int order_type, double price = 0, const char *account = NULL);

	//调仓到目标持仓额
	Order order_target_value(const char *symbol, double value, int position_side, int order_type, double price = 0, const char *account = NULL);

	//调仓到目标持仓比例（总资产的比例）
	Order order_target_percent(const char *symbol, double percent, int position_side, int order_type, double price = 0, const char *account = NULL);

	//平当前所有可平持仓
	DataArray<Order>* order_close_all();

	//委托撤单
	int order_cancel(const char *cl_ord_id, const char *account = NULL);

	//撤销所有委托
	int order_cancel_all();

	//委托下单
	Order place_order(const char *symbol, int volume, int side, int order_type, int position_effect, double price = 0, int order_duration = 0, int order_qualifier = 0, double stop_price = 0, int order_business = 0, const char *account = NULL);

	//委托下单
	Order place_order(OrderRequest &order_request, const char *account = NULL);

	//批量委托
	DataArray<Order>* order_batch(OrderRequest *order_request, int count, const char *account = NULL);
	
	//盘后定价交易
	Order order_after_hour(const char *symbol, int volume, int side, double price, const char *account = NULL);

	//自动追单
	int smart_reorder(SmartReorderParameter &param);

	//撤销自动追单
	int smart_reorder_cancel(int reorder_id);

	//查询委托
	DataArray<Order>* get_orders(const char *account = NULL);

	//查询未结委托
	DataArray<Order>* get_unfinished_orders(const char *account = NULL);

	//查询成交
	DataArray<ExecRpt>* get_execution_reports(const char *account = NULL);

	//查询资金
	DataArray<Cash>* get_cash(const char *accounts = NULL);

	//查询持仓
	DataArray<Position>* get_position(const char *account = NULL);

	//委托算法单
	AlgoOrder algo_order(const char *symbol, int volume, int position_effect, int side, int order_type, double price, const char *provider, const char *algo_name, const char *algo_param, const char *account = NULL);

	//委托算法单
	DataArray<AlgoOrder>* algo_order_batch(AlgoOrderRequest *order_request, int count, const char *provider, const char *algo_name, const char *algo_param, const char *account = NULL);

	//撤单算法委托
	int algo_order_cancel(const char *provider, const char *cl_ord_id, const char *account = NULL);

	//暂停/恢复算法单
	int algo_order_pause(const char *provider, const char *cl_ord_id, int status, const char *account = NULL);

	//查询算法委托
	DataArray<AlgoOrder>* get_algo_orders(const char *account = NULL);

	//查询算法子委托
	DataArray<Order>* get_algo_child_orders(const char *cl_ord_id, const char *account = NULL);

	//功能号调用
	int raw_func(const char *account, const char *func_id, const char *func_args, char*&rsp);


	/* 两融业务 */

	//融资买入
	Order credit_buying_on_margin(int position_src, const char *symbol, int volume, double price, int order_type = OrderType_Limit, int order_duration = OrderDuration_Unknown, int order_qualifier = OrderQualifier_Unknown, const char *account = NULL);
	//融券卖出
	Order credit_short_selling(int position_src, const char *symbol, int volume, double price, int order_type = OrderType_Limit, int order_duration = OrderDuration_Unknown, int order_qualifier = OrderQualifier_Unknown, const char *account = NULL);
	//买券还券
	Order credit_repay_share_by_buying_share(int position_src, const char *symbol, int volume, double price, int order_type = OrderType_Limit, int order_duration = OrderDuration_Unknown, int order_qualifier = OrderQualifier_Unknown, const char *account = NULL);
	//买券还券(指定合约)
	Order credit_repay_share_by_buying_share_on_debt(int position_src, const char *symbol, int volume, double price, const char *debtsno, int order_type = OrderType_Limit, int order_duration = OrderDuration_Unknown, int order_qualifier = OrderQualifier_Unknown, const char *account = NULL);
	//卖券还款
	Order credit_repay_cash_by_selling_share(int position_src, const char *symbol, int volume, double price, int order_type = OrderType_Limit, int order_duration = OrderDuration_Unknown, int order_qualifier = OrderQualifier_Unknown, const char *account = NULL);
	//卖券还款(指定合约与利息偿还)
	Order credit_repay_cash_by_selling_share_on_debt(int position_src, const char *symbol, int volume, double price, const char *debtsno, int bond_fee_only, int order_type = OrderType_Limit, int order_duration = OrderDuration_Unknown, int order_qualifier = OrderQualifier_Unknown, const char *account = NULL);
	//担保品买入
	Order credit_buying_on_collateral(const char *symbol, int volume, double price, int order_type = OrderType_Limit, int order_duration = OrderDuration_Unknown, int order_qualifier = OrderQualifier_Unknown, const char *account = NULL);
	//担保品卖出
	Order credit_selling_on_collateral(const char *symbol, int volume, double price, int order_type = OrderType_Limit, int order_duration = OrderDuration_Unknown, int order_qualifier = OrderQualifier_Unknown, const char *account = NULL);
	//直接还券
	Order credit_repay_share_directly(int position_src, const char *symbol, int volume, const char *account = NULL);
	//直接还券(指定合约)
	Order credit_repay_share_directly_on_debt(int position_src, const char *symbol, int volume, const char *debtsno, const char *account = NULL);
	//直接还款
	int credit_repay_cash_directly(int position_src, double amount, const char *account = NULL, double *actual_repay_amount = NULL, char *error_msg_buf = NULL, int buf_len = 0);
	//直接还款(指定合约与利息偿还)
	int credit_repay_cash_directly_ex(int position_src, double amount, const char *debtsno, int bond_fee_only, const char *account = NULL, double *actual_repay_amount = NULL, char *error_msg_buf = NULL, int buf_len = 0);
	//担保品转入
	Order credit_collateral_in(const char *symbol, int volume, const char *account = NULL);
	//担保品转出
	Order credit_collateral_out(const char *symbol, int volume, const char *account = NULL);
	//查询担保证券
	DataArray<CollateralInstrument>* credit_get_collateral_instruments(const char *account = NULL);
	//查询标的证券，可做融券标的股票列表
	DataArray<BorrowableInstrument>* credit_get_borrowable_instruments(int position_src, const char *account = NULL);
	//查询券商融券账户头寸，可用融券的数量
	DataArray<BorrowableInstrumentPosition>* credit_get_borrowable_instruments_positions(int position_src, const char *account = NULL);
	//查询融资融券合约
	DataArray<CreditContract>* credit_get_contracts(int position_src, const char *account = NULL);
	//查询融资融券资金
	int credit_get_cash(CreditCash &cash, const char *account = NULL);
	//查询融资融券合约与资金信息
	DataSet* credit_get_report(const char *func_id, const char *account = NULL);

	/* 新股/新债业务 */

	//新股/新债申购
	Order ipo_buy(const char *symbol, int volume, double price, const char *account = NULL);
	//查询客户新股/新债申购额度
	DataArray<IPOQI>* ipo_get_quota(const char *account = NULL);
	//查询当日新股/新债清单
	DataArray<IPOInstruments>* ipo_get_instruments(int security_type, const char *account = NULL);
	//配号查询
	DataArray<IPOMatchNumber>* ipo_get_match_number(const char* start_date, const char* end_date, const char *account = NULL);
	//中签查询
	DataArray<IPOLotInfo>* ipo_get_lot_info(const char* start_date, const char* end_date, const char *account = NULL);

	/* 基金业务 */

	//ETF申购
	Order fund_etf_buy(const char *symbol, int volume, double price, const char *account = NULL);
	//ETF赎回
	Order fund_etf_redemption(const char *symbol, int volume, double price, const char *account = NULL);
	//基金认购
	Order fund_subscribing(const char *symbol, int volume, const char *account = NULL);
	//基金申购
	Order fund_buy(const char *symbol, int volume, const char *account = NULL);
	//基金赎回
	Order fund_redemption(const char *symbol, int volume, const char *account = NULL);

	/* 债券业务 */

	//国债逆回购
	Order bond_reverse_repurchase_agreement(const char *symbol, int volume, double price, int order_type = OrderType_Limit, int order_duration = OrderDuration_Unknown, int order_qualifier = OrderQualifier_Unknown, const char *account = NULL);
	//可转债转股
	Order bond_convertible_call(const char *symbol, int volume, double price, const char *account = NULL);
	//可转债回售
	Order bond_convertible_put(const char *symbol, int volume, double price, const char *account = NULL);
	//可转债回售撤销
	Order bond_convertible_put_cancel(const char *symbol, int volume, const char *account = NULL);

	/* 期权业务 */

	//备兑开仓
	Order option_covered_open(const char *symbol, int volume, int order_type, double price = 0, const char *account = NULL);
	//备兑平仓
	Order option_covered_close(const char *symbol, int volume, int order_type, double price = 0, const char *account = NULL);
	//行权
	Order option_exercise(const char *symbol, int volume, const char *account = NULL);
	//获取可开数量
	int option_preorder_valid_volume(OptionPreorderValidVolumeRequest &request, int &volume);
	//计算期权卖方开仓保证金
	int option_preorder_sell_margin(OptionPreorderSellMarginRequest &request, double &margin);

public: //策略参数类函数

	//添加参数
	int add_parameters(Parameter *params, int count);

	//删除参数
	int del_parameters(const char *keys);

	//设置参数
	int set_parameters(Parameter *params, int count);

	//获取参数
	DataArray<Parameter>* get_parameters();

	//设置标的
	int set_symbols(const char *symbols);

	//获取标的
	DataArray<Symbol>* get_symbols();


public: //事件函数

		//初始化完成
		virtual void on_init();
		//收到Tick行情
		virtual void on_tick(Tick *tick);
		//收到bar行情
		virtual void on_bar(Bar *bar);
		//收到逐笔成交（L2行情时有效）
		virtual void on_l2transaction(L2Transaction *l2transaction);
		//收到逐笔委托（深交所L2行情时有效）
		virtual void on_l2order(L2Order *l2order);
		//收到委托队列（上交所L2行情时有效）
		virtual void on_l2order_queue(L2OrderQueue *l2queue);
		//委托变化
		virtual void on_order_status(Order *order);
		//执行回报
		virtual void on_execution_report(ExecRpt *rpt);
		//算法委托变化
		virtual void on_algo_order_status(AlgoOrder *order);
		//资金推送
		virtual void on_cash(Cash *cash);
		//持仓推送
		virtual void on_position(Position *position);
		//参数变化
		virtual void on_parameter(Parameter *param);
		//定时任务触发
		virtual void on_schedule(const char *data_rule, const char *time_rule);
		//定时器触发
		virtual void on_timer(int timer_id);
		//回测完成后收到绩效报告
		virtual void on_backtest_finished(Indicator *indicator);
		//实盘账号状态变化
		virtual void on_account_status(AccountStatus *account_status);
		//错误产生
		virtual void on_error(int error_code, const char *error_msg);
		//收到策略停止信号
		virtual void on_stop();
		//数据已经连接上
		virtual void on_market_data_connected(const char *connect_msg);
		//交易已经连接上
		virtual void on_trade_data_connected();
		//数据连接断开了
		virtual void on_market_data_disconnected(const char *connect_msg);
		//交易连接断开了
		virtual void on_trade_data_disconnected();

};

#endif
