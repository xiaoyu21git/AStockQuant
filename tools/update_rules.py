import json

with open('config/rules/compiled.json', 'r', encoding='utf-8') as f:
    data = json.load(f)

for t in data['templates']:
    if t['templateId'] == 'template_exit_scale_out_take_profit_v1':
        t['summary'] = '止盈(纯趋势结构): 破5日线减30→前高90%减30→超涨MA20/110%减30'
        t['rules'] = [
            {'id':'profit-ma5-break-reduce','name':'破5日线减仓','stage':'rebalance','priority':86,
             'description':'浮盈3%以上但收盘跌破5日线, 短线动能转弱, 减仓30%观察。',
             'tags':['take_profit','ma5_break','reduce'],
             'when':{'op':'all','conditions':[
                 {'op':'ge','left':{'var':'position.pnl_percent'},'right':3},
                 {'op':'lt','left':{'var':'position.close_below_board_pivot_ratio'},'right':1.0}]},
             'then':{'result':'reduce','reason_code':'profit_ma5_break_reduce','message':'跌破5日线, 减仓30%',
                     'payload':{'reduce_ratio':0.30,'state':'ma5_reduced'}}},
            {'id':'profit-pressure-reduce','name':'前高压力减仓','stage':'rebalance','priority':83,
             'description':'浮盈3%以上且收盘接近60日前高(>=90%), 前高是重要阻力位, 先减仓30%。',
             'tags':['take_profit','pressure','reduce'],
             'when':{'op':'all','conditions':[
                 {'op':'ge','left':{'var':'position.pnl_percent'},'right':3},
                 {'op':'ge','left':{'var':'position.previous_high_breakout_ratio'},'right':0.90}]},
             'then':{'result':'reduce','reason_code':'profit_pressure_reduce','message':'接近前高压力位, 减仓30%',
                     'payload':{'reduce_ratio':0.30,'state':'pressure_reduced'}}},
            {'id':'profit-overextend-reduce','name':'超涨减仓','stage':'rebalance','priority':80,
             'description':'浮盈8%以上且收盘超MA20的110%, 短期涨幅过大, 减仓30%。',
             'tags':['take_profit','overextend','reduce'],
             'when':{'op':'all','conditions':[
                 {'op':'ge','left':{'var':'position.pnl_percent'},'right':8},
                 {'op':'ge','left':{'var':'position.close_below_rebound_pivot_ratio'},'right':1.10}]},
             'then':{'result':'reduce','reason_code':'profit_overextend_reduce','message':'超涨远离20日线, 减仓30%',
                     'payload':{'reduce_ratio':0.30,'state':'overextend_reduced'}}}
        ]
        print('Take-profit: 3 rules (MA5, pressure, overextend)')

with open('config/rules/compiled.json', 'w', encoding='utf-8') as f:
    json.dump(data, f, indent=2, ensure_ascii=False)

with open('config/rules/compiled.json', 'r', encoding='utf-8') as f:
    json.load(f)
print('JSON valid')
