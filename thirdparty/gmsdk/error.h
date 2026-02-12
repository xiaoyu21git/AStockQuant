/**
* @file    error.h
* @brief   gmsdk 错误码定义
* @version v3.0.0
*
*/

#ifndef GMSDK_ERROR_H
#define GMSDK_ERROR_H

/* 通用错误 1000～1099 */

#define SUCCESS                             0       // "成功"
#define ERR_INVALID_TOKEN                   1000    // "错误或无效的token"
#define ERR_CONNECT_TERM_SERV               1001    // "无法连接到终端服务"
#define ERR_CONNECT_HISTORY_SERV            1002    // "无法连接到历史行情服务"
#define ERR_QUERY_SERVER_ADDR_ERROR         1010    // "无法获取掘金服务器地址列表"
#define ERR_PARSE_MASSAGE                   1011    // "消息包解析错误"
#define ERR_PARSE_NETWORK_MASSAGE           1012    // "网络消息包解析错误"
#define ERR_CALL_TRADE_SERVICE              1013    // "交易服务调用错误"
#define ERR_CALL_HISTORY_SERVICE            1014    // "历史行情服务调用错误"
#define ERR_CALL_STRATEGY_SERIVCE           1015    // "策略服务调用错误"
#define ERR_CALL_RTCONFIG_SERIVCE           1016    // "动态参数调用错误"
#define ERR_CALL_FUNDMENTAL_SERVICE         1017    // "基本面数据服务调用错误"
#define ERR_CALL_BACKTEST_SERVICE           1018    // "回测服务调用错误"
#define ERR_CALL_TRADEGW_SERIVCE            1019    // "交易网关服务调用错误"
#define ERR_INVALID_ACCOUNT_ID              1020    // "无效的ACCOUNT_ID"
#define ERR_INVALID_DATE                    1021    // "非法日期格式"
#define ERR_TIMEOUT                         1022    // "执行超时"
#define ERR_TOO_MANY_REQUESTS               1023    // "执行过于频繁，服务拒绝请求"
#define ERR_GET_ORGCODE                     1024    // "获取orgcode错误"
#define ERR_CONNECT_AUTH_SERV               1025    // "无法连接到认证服务"
#define ERR_UPDATE_ENTOKEN                  1026    // "更新令牌错误"
#define ERR_INVALID_PAPAM_INPUT             1027    // "输入参数错误"
#define ERR_NOT_SUPPORT_SERVICE             1028    // "不支持的服务"
#define ERR_OVERRIDE_MAX_SETTING            1029    // "超出最大限制设置"
#define ERR_FUNC_NO_SUPPORT_MODE            1030    // "该函数不支持在当前策略运行模式"

/* 交易部分 1100～1199 */ 

#define ERR_TD_LIVE_CONNECT                 1100    // "交易消息服务连接失败"
#define ERR_TD_LIVE_CONNECT_LOST            1101    // "交易消息服务断开"

/* 数据部分 1200～1299*/

#define ERR_MD_LIVE_CONNECT                 1200    // "实时行情服务连接失败"
#define ERR_MD_LIVE_CONNECT_LOST            1201    // "实时行情服务连接断开"
#define ERR_MD_LIVE_SUBSCRIBE_FAIL          1202    // "订阅实时行情失败"
#define ERR_MD_CTP_CONNECT_TIMEOUT          1203    // "连接CTP行情服务超时"
#define ERR_MD_CTP_MODULE_LOST              1204    // "不支持CTP行情模块"

/*回测部分 1300~1399*/

#define ERR_BT_BEGIN                        1300    // "初始化回测失败，可能是终端未启动或无法连接到终端"
#define ERR_BT_INVALID_TIMESPAN             1301    // "回测时间区间错误"
#define ERR_BT_READ_CACHE_ERROR             1302    // "回测读取缓存数据错误"
#define ERR_BT_WRITE_CACHE_ERROR            1303    // "回测写入缓存数据错误"

/*新增*/

#define ERR_PERMISSION_DENIED                       2001    // "用户无此数据接口权限"
#define ERR_PAPAM_PERMISSION_DENIED                 2002    // "超出业务授权"
#define ERR_MD_LIVE_SUBSCRIBE_PERMISSION_DENIED     2003    // "订阅实时行情失败"
#define ERR_SPEED_TOO_FAST                          3001    // "流控"


/*内部*/
#define ERR_SHSE_MARKET_PRICE_PROTECT               990001 // "上交所股票市价单未指定保护价格"

#endif
