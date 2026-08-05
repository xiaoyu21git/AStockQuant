    {QStringLiteral("template_eligibility_trend_participation_guard_v1"), TemplateInsight{
        QStringLiteral("用于给日线趋势策略补基础参与资格过滤，避免裸信号直接面对流动性不足、趋势失速或过度偏离均线的候选。"),
        QStringLiteral("过滤通过要点"),
        QStringLiteral("使用提醒"),
        QVariantList{QStringLiteral("最近成交活跃度不能太差，至少维持基本参与价值"), QStringLiteral("价格仍在 20/60 日线附近的可承受区间，没有明显失真"), QStringLiteral("20 日线或 60 日线斜率至少有一条没有明显转负")},
        QVariantList{QStringLiteral("它不负责给出买点，只负责先把明显不该参与的样本挡在门外"), QStringLiteral("适合作为 eligibility_core 的默认底座，和核心确认、否决规则配合使用"), QStringLiteral("绑定后会进入基础过滤阶段")}
    }},
    {QStringLiteral("template_entry_catch_up_breakout_v1"), TemplateInsight{
        QStringLiteral("用于识别龙头锁定后的补涨突破，适合后排补涨从跟随转主动的启动点。"),
        QStringLiteral("候选触发要点"),
        QStringLiteral("使用提醒"),
        QVariantList{QStringLiteral("主线龙头已经明确锁定"), QStringLiteral("后排补涨标的放量突破关键位"), QStringLiteral("题材扩散度仍在，跟风不至于塌陷")},
        QVariantList{QStringLiteral("更适合主线仍有扩散时使用"), QStringLiteral("若题材宽度收缩，应由市场风控模板提前过滤"), QStringLiteral("绑定后会进入入场/观察信号阶段")}
    }},
    {QStringLiteral("template_entry_emotion_reflow_repair_v1"), TemplateInsight{
        QStringLiteral("用于识别市场情绪从退潮切回修复、核心题材重新回流时的修复入场候选。"),
        QStringLiteral("候选触发要点"),
        QStringLiteral("使用提醒"),
        QVariantList{QStringLiteral("市场情绪先从冷却转向修复，主线重新获得承接"), QStringLiteral("核心题材回流而不是单一个股脉冲，回流强度更重要"), QStringLiteral("更适合修复初段或回流确认后的首批候选")},
        QVariantList{QStringLiteral("适合和市场风控模板联动，避免在假修复里误判开仓"), QStringLiteral("若次日回流强度不足，应由观察失效或退出模板接管保护"), QStringLiteral("绑定后会进入入场/观察信号阶段")}
    }},
    {QStringLiteral("template_entry_event_earnings_surprise_breakout_v1"), TemplateInsight{
        QStringLiteral("用于识别业绩超预期后的放量突破，适合事件驱动策略捕捉催化发酵窗口。"),
        QStringLiteral("候选触发要点"),
        QStringLiteral("使用提醒"),
        QVariantList{QStringLiteral("业绩或预告明显超预期"), QStringLiteral("事件窗口内放量突破关键位"), QStringLiteral("市场事件驱动延续率保持在可参与区间")},
        QVariantList{QStringLiteral("适合事件驱动和公告催化类策略"), QStringLiteral("事件窗口强度不足时不要强行追入"), QStringLiteral("绑定后会进入入场/观察信号阶段")}
    }},
    {QStringLiteral("template_entry_hft_orderflow_reclaim_v1"), TemplateInsight{
        QStringLiteral("用于识别盘口扫单后迅速回补的微结构机会，适合高频和超短确认。"),
        QStringLiteral("候选触发要点"),
        QStringLiteral("使用提醒"),
        QVariantList{QStringLiteral("盘口出现扫单后迅速回补"), QStringLiteral("买方承接得分明显抬升"), QStringLiteral("微结构稳定度足够，噪音不过大")},
        QVariantList{QStringLiteral("适合高频、超短和盘口驱动策略"), QStringLiteral("更强调成交微结构，不适合长线持仓"), QStringLiteral("绑定后会进入入场/观察信号阶段")}
    }},
    {QStringLiteral("template_entry_limit_up_reseal_v1"), TemplateInsight{
        QStringLiteral("用于识别炸板后的回封确认，适合做高强度回封打板或回封博弈候选。"),
        QStringLiteral("候选触发要点"),
        QStringLiteral("使用提醒"),
        QVariantList{QStringLiteral("盘中先出现炸板或开板分歧"), QStringLiteral("随后重新回封并站稳关键回封位"), QStringLiteral("承接和回封力度需要同步恢复")},
        QVariantList{QStringLiteral("更依赖市场承接环境，最好搭配市场风控模板"), QStringLiteral("若回封后再次炸板，需配合退出模板快速处理"), QStringLiteral("绑定后会进入入场/观察信号阶段")}
    }},
    {QStringLiteral("template_entry_long_term_yearline_reclaim_v1"), TemplateInsight{
        QStringLiteral("用于识别年线收复回踩确认，适合长线趋势恢复初段的候选筛选。"),
        QStringLiteral("候选触发要点"),
        QStringLiteral("使用提醒"),
        QVariantList{QStringLiteral("价格重新站回年线并完成确认"), QStringLiteral("年线方向不再向下，长期趋势斜率修复"), QStringLiteral("市场中长线趋势参与率同步改善")},
        QVariantList{QStringLiteral("适合长线趋势、配置型或波段策略"), QStringLiteral("更看重结构确认而不是短时爆发力"), QStringLiteral("绑定后会进入入场/观察信号阶段")}
    }},
    {QStringLiteral("template_entry_midterm_platform_breakout_v1"), TemplateInsight{
        QStringLiteral("用于识别中期平台放量突破，适合趋势跟随和中线突破策略。"),
        QStringLiteral("候选触发要点"),
        QStringLiteral("使用提醒"),
        QVariantList{QStringLiteral("中期平台已经形成，突破后仍站稳平台上沿"), QStringLiteral("量能相对 20 日均量明显放大"), QStringLiteral("中期趋势斜率维持向上")},
        QVariantList{QStringLiteral("适合趋势突破和中线趋势策略"), QStringLiteral("若突破后迅速跌回平台，应配合退出模板及时止错"), QStringLiteral("绑定后会进入入场/观察信号阶段")}
    }},
    {QStringLiteral("template_entry_overtake_rotation_v1"), TemplateInsight{
        QStringLiteral("用于识别题材内部的卡位上位，适合原核心转弱、新核心反超的切换行情。"),
        QStringLiteral("候选触发要点"),
        QStringLiteral("使用提醒"),
        QVariantList{QStringLiteral("原核心出现走弱或卡顿"), QStringLiteral("新标的相对强度反超并获得承接确认"), QStringLiteral("题材内部存在明显的龙头切换结构")},
        QVariantList{QStringLiteral("适合板块轮动和补位行情，不适合单边退潮段"), QStringLiteral("若卡位成功次日不加强，应配合观察失效模板"), QStringLiteral("绑定后会进入入场/观察信号阶段")}
    }},
    {QStringLiteral("template_entry_pullback_ma20_support_candidate_v1"), TemplateInsight{
        QStringLiteral("纯候选版 20 日线回踩模板，只输出正向候选，适合放入核心确认组避免被内置失效分支反向阻断。"),
        QStringLiteral("候选触发要点"),
        QStringLiteral("使用提醒"),
        QVariantList{QStringLiteral("股价回踩 20 日线但没有有效跌穿趋势支撑"), QStringLiteral("回踩后重新企稳并出现承接修复或转强确认"), QStringLiteral("更适合短中期趋势保持向上的标的")},
        QVariantList{QStringLiteral("适合作为核心确认组里的纯正向候选模板"), QStringLiteral("如果要保留失效阻断，应把旧复合模板放到单独否决路径而不是直接塞进 must_pass 组"), QStringLiteral("绑定后会进入入场/观察信号阶段")}
    }},
    {QStringLiteral("template_entry_pullback_ma20_support_v1"), TemplateInsight{
        QStringLiteral("用于识别上升趋势中的 20 日线回踩企稳，适合做趋势延续中的第一次回踩低吸。"),
        QStringLiteral("候选触发要点"),
        QStringLiteral("使用提醒"),
        QVariantList{QStringLiteral("股价回踩 20 日线但没有有效跌穿趋势支撑"), QStringLiteral("回踩后重新企稳并出现承接修复或转强确认"), QStringLiteral("更适合短中期趋势保持向上的标的")},
        QVariantList{QStringLiteral("适合和趋势突破、趋势跟随策略搭配使用"), QStringLiteral("若回踩后继续失守均线，应配合退出或观察失效模板"), QStringLiteral("绑定后会进入入场/观察信号阶段")}
    }},
    {QStringLiteral("template_entry_pullback_ma60_support_candidate_v1"), TemplateInsight{
        QStringLiteral("纯候选版 60 日线回踩模板，只输出正向候选，适合放入核心确认组避免被内置失效分支反向阻断。"),
        QStringLiteral("候选触发要点"),
        QStringLiteral("使用提醒"),
        QVariantList{QStringLiteral("股价回踩 60 日线后没有破坏中期趋势结构"), QStringLiteral("回踩后重新企稳并恢复向上斜率或支撑确认"), QStringLiteral("更适合波段趋势或年线附近的中继结构")},
        QVariantList{QStringLiteral("适合作为核心确认组里的纯正向候选模板"), QStringLiteral("如果要保留失效阻断，应把旧复合模板放到单独否决路径而不是直接塞进 must_pass 组"), QStringLiteral("绑定后会进入入场/观察信号阶段")}
    }},
    {QStringLiteral("template_entry_pullback_ma60_support_v1"), TemplateInsight{
        QStringLiteral("用于识别中期趋势中的 60 日线回踩企稳，适合做更稳健的中继趋势候选。"),
        QStringLiteral("候选触发要点"),
        QStringLiteral("使用提醒"),
        QVariantList{QStringLiteral("股价回踩 60 日线后没有破坏中期趋势结构"), QStringLiteral("回踩后重新企稳并恢复向上斜率或支撑确认"), QStringLiteral("更适合波段趋势或年线附近的中继结构")},
        QVariantList{QStringLiteral("适合偏中期的趋势跟踪或突破回踩策略"), QStringLiteral("如果 60 日线拐头走平或失守，应降低候选优先级"), QStringLiteral("绑定后会进入入场/观察信号阶段")}
    }},
    {QStringLiteral("template_entry_trend_support_near_ma_v1"), TemplateInsight{
        QStringLiteral("用于识别贴近 20/60 日线且趋势未明显走坏的趋势延续候选，比回踩确认模板更宽，适合先打破长期零命中。"),
        QStringLiteral("候选触发要点"),
        QStringLiteral("使用提醒"),
        QVariantList{QStringLiteral("收盘仍靠近 20 日线或 60 日线，没有明显远离趋势支撑"), QStringLiteral("均线斜率只要没有明显转负即可，不再强依赖回踩确认布尔值"), QStringLiteral("市场层面的趋势回踩修复率保持在较低但可接受的水平")},
        QVariantList{QStringLiteral("它比回踩确认模板更宽，适合作为先恢复交易样本的过渡模板"), QStringLiteral("如果后续成交过多或信号质量明显下降，再逐步把阈值收回去"), QStringLiteral("绑定后会进入入场/观察信号阶段")}
    }},
    {QStringLiteral("template_entry_weak_to_strong_v1"), TemplateInsight{
        QStringLiteral("用于识别先弱后强的修复入场，适合做强势回流或分歧转一致的首个候选。"),
        QStringLiteral("候选触发要点"),
        QStringLiteral("使用提醒"),
        QVariantList{QStringLiteral("前一阶段先出现明显走弱或分歧释放"), QStringLiteral("价格重新收复关键位并伴随放量确认"), QStringLiteral("更适合修复转强、分歧后再上车场景")},
        QVariantList{QStringLiteral("适合与市场修复或情绪回暖阶段搭配"), QStringLiteral("若后续承接衰减，应结合退出模板限制持仓"), QStringLiteral("绑定后会进入入场/观察信号阶段")}
    }},
    {QStringLiteral("template_exit_accelerated_catch_up_afternoon_blowup_v1"), TemplateInsight{
        QStringLiteral("用于识别加速补涨后午后炸裂的退出，适合在补涨过热后承接迅速转空时及时防守。"),
        QStringLiteral("退出触发要点"),
        QStringLiteral("执行节奏"),
        QVariantList{QStringLiteral("个股进入加速补涨阶段，前半段涨幅推进过快"), QStringLiteral("午后炸裂并失守关键支点，说明补涨合力快速瓦解"), QStringLiteral("更适合作为补涨末端高潮后的风险退出模板")},
        QVariantList{QStringLiteral("适合补涨加速、情绪高潮和午后兑现压力集中释放场景"), QStringLiteral("命中后优先退出或减仓，避免被补涨末端的崩口拖入回撤"), QStringLiteral("绑定后会进入持仓管理/退出阶段")}
    }},
    {QStringLiteral("template_exit_acceleration_volume_stall_v1"), TemplateInsight{
        QStringLiteral("用于识别加速后放量滞涨的衰减信号，适合在上涨推进停止、承接恶化时及时兑现。"),
        QStringLiteral("退出触发要点"),
        QStringLiteral("执行节奏"),
        QVariantList{QStringLiteral("个股进入加速段，但放量后涨幅推进开始停滞"), QStringLiteral("承接边际恶化，说明加速更像高潮末段而不是新一轮启动"), QStringLiteral("更适合作为加速票的高位兑现模板")},
        QVariantList{QStringLiteral("适合一致加速、补涨末端和高位情绪过热场景"), QStringLiteral("命中后应优先减仓或退出，不要继续按强趋势持有"), QStringLiteral("绑定后会进入持仓管理/退出阶段")}
    }},
    {QStringLiteral("template_exit_acceptance_breakdown_v1"), TemplateInsight{
        QStringLiteral("用于承接显著走弱后的退出，适合盘口支撑消失、回落扩大的持仓保护。"),
        QStringLiteral("退出触发要点"),
        QStringLiteral("执行节奏"),
        QVariantList{QStringLiteral("个股承接强度明显下降"), QStringLiteral("回落扩大并失守关键承接区间"), QStringLiteral("可以先减仓，再根据趋势损伤决定是否清仓")},
        QVariantList{QStringLiteral("更偏向承接崩塌后的保护性退出"), QStringLiteral("适合和高位入场或回封入场模板配套使用"), QStringLiteral("绑定后会进入持仓管理/退出阶段")}
    }},
    {QStringLiteral("template_exit_board_fade_tail_breakdown_v1"), TemplateInsight{
        QStringLiteral("用于炸板回落并在尾盘失守的退出，适合处理冲板失败后尾盘进一步恶化的持仓。"),
        QStringLiteral("退出触发要点"),
        QStringLiteral("执行节奏"),
        QVariantList{QStringLiteral("盘中冲板或炸板回落后未见修复"), QStringLiteral("尾盘继续失守关键支撑或承接位"), QStringLiteral("优先在收盘前撤退，避免隔夜再杀")},
        QVariantList{QStringLiteral("适合高位冲板失败和尾盘风险放大场景"), QStringLiteral("与回封打板类入场模板天然配对"), QStringLiteral("绑定后会进入持仓管理/退出阶段")}
    }},
    {QStringLiteral("template_exit_board_pullback_next_day_gap_down_confirmed_v1"), TemplateInsight{
        QStringLiteral("用于识别冲板回落后次日低开确认弱势的退出，适合处理冲板失败后的隔日补跌风险。"),
        QStringLiteral("退出触发要点"),
        QStringLiteral("执行节奏"),
        QVariantList{QStringLiteral("前一日冲板回落，说明强预期已经出现分歧"), QStringLiteral("次日低开并失守回落支撑，弱势得到进一步确认"), QStringLiteral("更适合作为冲板失败票的隔日防守模板")},
        QVariantList{QStringLiteral("适合冲板未封、炸板回落和次日竞价确认弱势场景"), QStringLiteral("命中后优先退出或减仓，避免把隔日补跌拖成长时间被套"), QStringLiteral("绑定后会进入持仓管理/退出阶段")}
    }},
    {QStringLiteral("template_exit_broken_board_failed_rebound_v1"), TemplateInsight{
        QStringLiteral("用于识别连板断板后的反抽修复失败，适合在断板转弱确认后快速撤退。"),
        QStringLiteral("退出触发要点"),
        QStringLiteral("执行节奏"),
        QVariantList{QStringLiteral("连板断板后尝试反抽修复，但始终没能重新站稳关键位"), QStringLiteral("再次回落说明断板后的修复只是反抽，不是重回主升"), QStringLiteral("更适合作为高位断板票的保护性退出模板")},
        QVariantList{QStringLiteral("适合高位断板、龙头首阴和断板修复失败场景"), QStringLiteral("命中后优先退出，不建议继续等二次反包确认"), QStringLiteral("绑定后会进入持仓管理/退出阶段")}
    }},
    {QStringLiteral("template_exit_cooling_mid_weak_repair_rebreak_v1"), TemplateInsight{
        QStringLiteral("用于识别退潮中继弱修复后再次破位的退出，适合处理退潮阶段假修复后的二次下杀风险。"),
        QStringLiteral("退出触发要点"),
        QStringLiteral("执行节奏"),
        QVariantList{QStringLiteral("市场处在退潮中继，个股只有弱修复而没有真正扭转结构"), QStringLiteral("再次破位并失守修复支点，说明弱修复已经彻底失效"), QStringLiteral("更适合作为退潮中继票的防守型退出模板")},
        QVariantList{QStringLiteral("适合退潮中继、弱修复、反抽后再破位场景"), QStringLiteral("命中后应优先退出或减仓，避免在退潮票上反复试错"), QStringLiteral("绑定后会进入持仓管理/退出阶段")}
    }},
    {QStringLiteral("template_exit_engulfing_board_blowup_take_profit_v1"), TemplateInsight{
        QStringLiteral("用于识别反包后冲板炸裂兑现的退出，适合在强修复没有封住、转为获利兑现时及时落袋。"),
        QStringLiteral("退出触发要点"),
        QStringLiteral("执行节奏"),
        QVariantList{QStringLiteral("个股先走出反包并尝试继续冲板"), QStringLiteral("冲板失败后炸裂兑现、失守关键支点，说明修复强度已经见顶"), QStringLiteral("更适合作为反包高潮段的落袋与防守模板")},
        QVariantList{QStringLiteral("适合反包、修复高潮和冲板未封后的兑现处理"), QStringLiteral("命中后优先退出或减仓，不再继续按强修复趋势持有"), QStringLiteral("绑定后会进入持仓管理/退出阶段")}
    }},
    {QStringLiteral("template_exit_engulfing_first_down_day_confirmed_v1"), TemplateInsight{
        QStringLiteral("用于识别反包后首个确认转弱阴线的退出时点，适合在反包延续被否定时及时防守。"),
        QStringLiteral("退出触发要点"),
        QStringLiteral("执行节奏"),
        QVariantList{QStringLiteral("个股前面完成反包，但随后出现首个明显转弱阴线"), QStringLiteral("并失守承接位，说明反包逻辑已经被破坏"), QStringLiteral("更适合作为反包票由强转弱的确认退出模板")},
        QVariantList{QStringLiteral("适合反包、弱转强后的隔日走弱确认场景"), QStringLiteral("命中后应优先退出或减仓，不再按修复成功逻辑持有"), QStringLiteral("绑定后会进入持仓管理/退出阶段")}
    }},
    {QStringLiteral("template_exit_engulfing_next_day_fade_v1"), TemplateInsight{
        QStringLiteral("用于识别反包后次日冲高回落的衰减过程，适合在反包没有形成持续性时及时兑现。"),
        QStringLiteral("退出触发要点"),
        QStringLiteral("执行节奏"),
        QVariantList{QStringLiteral("前一日完成反包，次日理应继续加强"), QStringLiteral("但盘中冲高回落并失守承接位，说明反包延续性不足"), QStringLiteral("更适合作为反包票的隔日兑现或保护性退出模板")},
        QVariantList{QStringLiteral("适合反包、弱转强后的次日延续验证"), QStringLiteral("命中后宜优先兑现，不建议再把它当成继续主升处理"), QStringLiteral("绑定后会进入持仓管理/退出阶段")}
    }},
    {QStringLiteral("template_exit_failed_rebound_engulfing_v1"), TemplateInsight{
        QStringLiteral("用于反包尝试失败后的退出，适合短线修复预期落空时快速撤退。"),
        QStringLiteral("退出触发要点"),
        QStringLiteral("执行节奏"),
        QVariantList{QStringLiteral("持仓出现反包或修复尝试"), QStringLiteral("但未能站稳关键位并重新转弱"), QStringLiteral("优先退出而不是继续赌二次修复")},
        QVariantList{QStringLiteral("适合高位修复失败和分歧转弱场景"), QStringLiteral("可和分批止盈模板搭配，形成先减后退结构"), QStringLiteral("绑定后会进入持仓管理/退出阶段")}
    }},
    {QStringLiteral("template_exit_floor_to_limit_failed_v1"), TemplateInsight{
        QStringLiteral("用于识别地天板修复失败后的撤退时点，适合处理高波动修复票失去回封确认的场景。"),
        QStringLiteral("退出触发要点"),
        QStringLiteral("执行节奏"),
        QVariantList{QStringLiteral("先有地天板式剧烈修复尝试，但未能保持关键回封位"), QStringLiteral("再次失守说明修复结构已经被破坏，抄底预期落空"), QStringLiteral("更适合作为高波动反包、地天板修复票的止损模板")},
        QVariantList{QStringLiteral("适合强波动修复票，不适合拿它去赌全天震荡后的再回封"), QStringLiteral("命中后优先退出或快速减仓，核心是防守而不是等待奇迹"), QStringLiteral("绑定后会进入持仓管理/退出阶段")}
    }},
    {QStringLiteral("template_exit_high_level_board_break_afternoon_second_kill_v1"), TemplateInsight{
        QStringLiteral("用于识别高位炸板后午后再杀的退出，适合在高位强分歧转二次崩口时快速收缩仓位。"),
        QStringLiteral("退出触发要点"),
        QStringLiteral("执行节奏"),
        QVariantList{QStringLiteral("个股高位先炸板，说明强势结构已经被打开"), QStringLiteral("午后再次转杀并跌破关键支点，说明承接彻底失效"), QStringLiteral("更适合作为高位二次崩口时的快速保护模板")},
        QVariantList{QStringLiteral("适合高位炸板、二次分歧和午后承接崩塌场景"), QStringLiteral("命中后应优先退出，不建议继续等尾盘资金回捞"), QStringLiteral("绑定后会进入持仓管理/退出阶段")}
    }},
    {QStringLiteral("template_exit_high_level_sideways_flush_v1"), TemplateInsight{
        QStringLiteral("用于识别高位横盘后突然跳水的场景，适合在高位筹码松动、整理失败时快速收缩仓位。"),
        QStringLiteral("退出触发要点"),
        QStringLiteral("执行节奏"),
        QVariantList{QStringLiteral("高位先进入横盘整理，看似稳住但抛压并未真正消化"), QStringLiteral("随后突然跳水并失守整理支点，说明筹码开始集中兑现"), QStringLiteral("更适合作为高位横盘票的防守型退出模板")},
        QVariantList{QStringLiteral("适合高位震荡、平台整理和强势股高位横盘场景"), QStringLiteral("命中后应优先退出或减仓，避免被高位平台破位拖入深回撤"), QStringLiteral("绑定后会进入持仓管理/退出阶段")}
    }},
    {QStringLiteral("template_exit_high_volume_stall_reversal_v1"), TemplateInsight{
        QStringLiteral("用于识别高位放量滞胀后转杀的场景，适合处理高位量大价不动、随后抛压爆发的持仓。"),
        QStringLiteral("退出触发要点"),
        QStringLiteral("执行节奏"),
        QVariantList{QStringLiteral("高位明显放量，但价格推进已经不再顺畅"), QStringLiteral("随后抛压加强并转为回落，说明高位兑现开始主导"), QStringLiteral("更适合作为高位滞胀票的风险退出模板")},
        QVariantList{QStringLiteral("适合高位换手、冲高放量和一致后分歧加大的持仓"), QStringLiteral("命中后优先退出或减仓，核心是防止滞胀后快速转杀"), QStringLiteral("绑定后会进入持仓管理/退出阶段")}
    }},
    {QStringLiteral("template_exit_long_term_ma120_break_v1"), TemplateInsight{
        QStringLiteral("用于识别长线趋势跌破120日线后的减仓或退出，适合长线仓位防守。"),
        QStringLiteral("退出触发要点"),
        QStringLiteral("执行节奏"),
        QVariantList{QStringLiteral("价格跌破 120 日线关键支撑"), QStringLiteral("120 日线方向转弱，说明中长期趋势走坏"), QStringLiteral("若回撤扩大，应从减仓升级为退出")},
        QVariantList{QStringLiteral("先减仓再确认是否退出，适合长线仓位管理"), QStringLiteral("不要把长线走坏误当成普通短调"), QStringLiteral("绑定后会进入持仓管理/退出阶段")}
    }},
    {QStringLiteral("template_exit_low_volume_false_repair_v1"), TemplateInsight{
        QStringLiteral("用于识别缩量假修复后的退出，适合处理价格看似企稳但并未获得真实确认的弱修复持仓。"),
        QStringLiteral("退出触发要点"),
        QStringLiteral("执行节奏"),
        QVariantList{QStringLiteral("修复过程明显缩量，缺少有效合力或主动承接"), QStringLiteral("再次跌回修复位下方，说明修复结构不成立"), QStringLiteral("更适合作为假修复、弱反抽的确认失败退出模板")},
        QVariantList{QStringLiteral("适合退潮中继、弱修复和缩量止跌失败场景"), QStringLiteral("命中后应把它视为止错，而不是等待下一次自然回抽"), QStringLiteral("绑定后会进入持仓管理/退出阶段")}
    }},
    {QStringLiteral("template_exit_rebound_over_previous_high_failed_v1"), TemplateInsight{
        QStringLiteral("用于识别反抽过前高失败后的退出，适合处理突破尝试没有站稳、转身回落的持仓。"),
        QStringLiteral("退出触发要点"),
        QStringLiteral("执行节奏"),
        QVariantList{QStringLiteral("个股反抽试图突破前高，但没有真正站稳突破位"), QStringLiteral("重新回落说明突破是假动作，兑现压力重新占优"), QStringLiteral("更适合作为前高突破失败的保护性退出模板")},
        QVariantList{QStringLiteral("适合反抽破前高、压力位突破和高位尝试再转强场景"), QStringLiteral("命中后应优先退出或减仓，不再把它按突破成功处理"), QStringLiteral("绑定后会进入持仓管理/退出阶段")}
    }},
    {QStringLiteral("template_exit_reseal_board_break_loss_v1"), TemplateInsight{
        QStringLiteral("用于识别回封成功后再次炸板并失守承接位的场景，适合在板上确认被二次打穿时快速撤退。"),
        QStringLiteral("退出触发要点"),
        QStringLiteral("执行节奏"),
        QVariantList{QStringLiteral("个股曾完成回封，给出过继续走强的确认"), QStringLiteral("但随后再次炸板并跌破关键承接位，说明板上合力瓦解"), QStringLiteral("更适合作为打板或回封持仓的快速保护模板")},
        QVariantList{QStringLiteral("适合回封票、炸板修复票和高位板上接力票"), QStringLiteral("命中后应优先退出，避免在二次炸板时继续硬扛"), QStringLiteral("绑定后会进入持仓管理/退出阶段")}
    }},
    {QStringLiteral("template_exit_scale_out_take_profit_v1"), TemplateInsight{
        QStringLiteral("用于盈利后的分批兑现，适合趋势尚未完全破坏时先落袋再等确认退出。"),
        QStringLiteral("退出触发要点"),
        QStringLiteral("执行节奏"),
        QVariantList{QStringLiteral("持仓已经达到预期盈利区间"), QStringLiteral("先执行减仓，保留一部分仓位观察趋势延续"), QStringLiteral("若后续趋势受损，再执行完全退出")},
        QVariantList{QStringLiteral("适合趋势股或补涨股的分段兑现"), QStringLiteral("能减少一次性清仓带来的踏空风险"), QStringLiteral("绑定后会进入持仓管理/退出阶段")}
    }},
    {QStringLiteral("template_exit_second_wave_repair_failed_v1"), TemplateInsight{
        QStringLiteral("用于识别二波修复失败后的退出，适合处理二次上攻未获确认、重新跌回突破位下方的持仓。"),
        QStringLiteral("退出触发要点"),
        QStringLiteral("执行节奏"),
        QVariantList{QStringLiteral("个股出现二波修复尝试，但始终没有真正站稳突破位"), QStringLiteral("重新跌回突破位下方，说明二波结构失效"), QStringLiteral("更适合作为二波修复和再启动失败的止损模板")},
        QVariantList{QStringLiteral("适合二波修复、再冲高和回流后二次强化失败场景"), QStringLiteral("命中后优先退出或减仓，不建议继续等第三次修复"), QStringLiteral("绑定后会进入持仓管理/退出阶段")}
    }},
    {QStringLiteral("template_exit_thin_volume_rebound_failed_v1"), TemplateInsight{
        QStringLiteral("用于识别地量反抽失败后的退出，适合过滤量能不足、看似修复但缺少真实合力的弱反弹。"),
        QStringLiteral("退出触发要点"),
        QStringLiteral("执行节奏"),
        QVariantList{QStringLiteral("个股尝试地量反抽，但没有出现足够量能确认"), QStringLiteral("再次失守反抽支点，说明修复更多只是弱反弹"), QStringLiteral("更适合作为缩量假修复和弱势反抽的止损模板")},
        QVariantList{QStringLiteral("适合退潮段、低量反弹和弱势票修复失败场景"), QStringLiteral("命中后优先退出或减仓，不建议继续等放量再起"), QStringLiteral("绑定后会进入持仓管理/退出阶段")}
    }},
    {QStringLiteral("template_exit_weak_repair_next_day_gap_down_kill_v1"), TemplateInsight{
        QStringLiteral("用于识别弱修复后次日低开再杀的场景，适合处理修复预期没有兑现的隔日止错。"),
        QStringLiteral("退出触发要点"),
        QStringLiteral("执行节奏"),
        QVariantList{QStringLiteral("前一日只是弱修复，没有形成真正强确认"), QStringLiteral("次日低开且继续走弱，再次跌破修复位"), QStringLiteral("说明修复预期已经落空，更适合尽快退出")},
        QVariantList{QStringLiteral("适合退潮中继、弱修复和隔日预期落空场景"), QStringLiteral("命中后优先防守，不建议继续等盘中自然翻红"), QStringLiteral("绑定后会进入持仓管理/退出阶段")}
    }},
    {QStringLiteral("template_exit_weak_to_strong_fail_consensus_take_profit_v1"), TemplateInsight{
        QStringLiteral("用于识别弱转强失败后情绪一致转兑现的退出，适合处理修复逻辑落空后的高位资金集中获利了结。"),
        QStringLiteral("退出触发要点"),
        QStringLiteral("执行节奏"),
        QVariantList{QStringLiteral("个股尝试弱转强，但并未形成持续加强"), QStringLiteral("随后情绪一致转兑现并失守修复支点，说明博弈转向获利了结"), QStringLiteral("更适合作为弱转强失败后的兑现型退出模板")},
        QVariantList{QStringLiteral("适合弱转强修复票、一致转兑现和隔日博弈失败场景"), QStringLiteral("命中后应优先减仓或退出，不再等待再次转强"), QStringLiteral("绑定后会进入持仓管理/退出阶段")}
    }},
    {QStringLiteral("template_exit_weak_to_strong_failed_gap_down_no_acceptance_v1"), TemplateInsight{
        QStringLiteral("用于弱转强失败后次日低开无承接的退出，适合修复预期落空后的隔日止损。"),
        QStringLiteral("退出触发要点"),
        QStringLiteral("执行节奏"),
        QVariantList{QStringLiteral("前一日尝试弱转强但未形成持续确认"), QStringLiteral("次日低开且承接明显不足"), QStringLiteral("更适合直接退出而不是等待盘中二次修复")},
        QVariantList{QStringLiteral("是弱转强入场模板的典型反向保护模板"), QStringLiteral("适合隔日确认失败的快速止损"), QStringLiteral("绑定后会进入持仓管理/退出阶段")}
    }},
    {QStringLiteral("template_risk_market_bear_freeze_entry_v1"), TemplateInsight{
        QStringLiteral("用于识别熊市或系统性退潮阶段，命中后冻结新增仓位。"),
        QStringLiteral("冻结条件"),
        QStringLiteral("恢复观察条件"),
        QVariantList{QStringLiteral("市场状态进入 bear 阶段"), QStringLiteral("站上 60 日线的广度显著收缩"), QStringLiteral("波动冲击和距高点回撤进入风险区间")},
        QVariantList{QStringLiteral("等待市场从 bear 重新回到 sideways 或 bull"), QStringLiteral("广度和波动冲击恢复到中性以上"), QStringLiteral("恢复后优先从精选观察开始，而不是直接全面放行")}
    }},
    {QStringLiteral("template_risk_market_bull_trend_allow_entry_v1"), TemplateInsight{
        QStringLiteral("用于识别牛市趋势阶段的总开关放行，命中后可恢复趋势类新开仓。"),
        QStringLiteral("放行条件"),
        QStringLiteral("使用提醒"),
        QVariantList{QStringLiteral("市场状态进入 bull 阶段"), QStringLiteral("中期广度明显改善，站上 60 日线标的占比抬升"), QStringLiteral("指数重新站稳 120 日线且趋势强度分维持高位")},
        QVariantList{QStringLiteral("更适合趋势突破、回踩确认和中长线趋势策略"), QStringLiteral("适合挂在市场放行组，作为总开关先于个股规则执行"), QStringLiteral("绑定后会进入市场/风控阶段")}
    }},
    {QStringLiteral("template_risk_market_emotion_cooling_freeze_v1"), TemplateInsight{
        QStringLiteral("用于识别市场从修复切入退潮后的总开关防守态，命中后优先冻结新增仓位。"),
        QStringLiteral("冻结条件"),
        QStringLiteral("恢复观察条件"),
        QVariantList{QStringLiteral("市场情绪进入 cooling 或 panic"), QStringLiteral("高位开板率升高，或回封成功率明显走弱，或题材宽度明显收缩"), QStringLiteral("适合作为整套短线策略的总风控开关")},
        QVariantList{QStringLiteral("市场情绪回到 repair"), QStringLiteral("高位开板率回落，且回封成功率与题材宽度同步修复"), QStringLiteral("恢复到谨慎观察态，只保留观察和少量候选")}
    }},
    {QStringLiteral("template_risk_market_emotion_repair_allow_entry_v1"), TemplateInsight{
        QStringLiteral("用于识别市场情绪修复后的放行窗口，命中后允许恢复新增仓位。"),
        QStringLiteral("放行条件"),
        QStringLiteral("使用提醒"),
        QVariantList{QStringLiteral("市场情绪修复确认"), QStringLiteral("高位开板率回落，回封成功率明显回升"), QStringLiteral("题材宽度恢复扩散，说明新增仓位环境改善")},
        QVariantList{QStringLiteral("适合搭配趋势延续、回踩确认等入场模板"), QStringLiteral("放行不等于满仓进攻，仍应优先精选候选"), QStringLiteral("绑定后会进入市场/风控阶段")}
    }},
    {QStringLiteral("template_risk_market_high_level_open_board_deterioration_freeze_v1"), TemplateInsight{
        QStringLiteral("用于高位接力环境恶化时的强防守冻结，重点回避高位炸板扩散阶段。"),
        QStringLiteral("冻结条件"),
        QStringLiteral("恢复观察条件"),
        QVariantList{QStringLiteral("市场情绪进入 cooling 或 panic"), QStringLiteral("高位开板率抬升到风险区间"), QStringLiteral("回封成功率同步下滑，说明高位承接塌陷")},
        QVariantList{QStringLiteral("市场情绪回到 repair"), QStringLiteral("高位开板率明显回落"), QStringLiteral("回封成功率回升后，仅恢复观察和精选候选")}
    }},
    {QStringLiteral("template_risk_market_reseal_rate_drop_freeze_v1"), TemplateInsight{
        QStringLiteral("用于识别打板确认失败环境，命中后暂停追涨打板类新开仓。"),
        QStringLiteral("冻结条件"),
        QStringLiteral("恢复观察条件"),
        QVariantList{QStringLiteral("市场情绪进入 cooling 或 panic"), QStringLiteral("回封成功率跌入低位区间"), QStringLiteral("高位开板率同步走高，说明接力确认不足")},
        QVariantList{QStringLiteral("市场情绪回到 repair"), QStringLiteral("回封成功率重新回升到健康区间"), QStringLiteral("题材宽度恢复后，只恢复观察和板块内精选候选")}
    }},
    {QStringLiteral("template_risk_market_reseal_recovery_allow_entry_v1"), TemplateInsight{
        QStringLiteral("用于识别回封率修复后的放行窗口，命中后恢复精选新增仓位。"),
        QStringLiteral("放行条件"),
        QStringLiteral("使用提醒"),
        QVariantList{QStringLiteral("市场回到 repair 或 warm 阶段"), QStringLiteral("回封成功率抬升到健康区间"), QStringLiteral("高位开板率回落且题材扩散恢复")},
        QVariantList{QStringLiteral("更适合短线修复、回封和强承接结构"), QStringLiteral("优先放行精选突破，不建议无差别追涨"), QStringLiteral("绑定后会进入市场/风控阶段")}
    }},
    {QStringLiteral("template_risk_market_sideways_selective_entry_v1"), TemplateInsight{
        QStringLiteral("用于识别震荡市环境下的精选放行，命中后只允许高确认度候选继续推进。"),
        QStringLiteral("放行条件"),
        QStringLiteral("使用提醒"),
        QVariantList{QStringLiteral("市场状态处于 sideways 阶段"), QStringLiteral("广度维持中性区间，既未全面走强也未系统性走弱"), QStringLiteral("波动冲击和距高点回撤均保持在可控范围")},
        QVariantList{QStringLiteral("适合震荡市里的低吸回补、事件催化和强确认策略"), QStringLiteral("不建议配合无差别追涨或高一致性接力模板"), QStringLiteral("绑定后会进入市场/风控阶段")}
    }},
    {QStringLiteral("template_risk_market_theme_cooling_freeze_v1"), TemplateInsight{
        QStringLiteral("用于识别主线题材退潮扩散阶段，命中后避免继续追逐已经走弱的主线。"),
        QStringLiteral("冻结条件"),
        QStringLiteral("恢复观察条件"),
        QVariantList{QStringLiteral("市场情绪进入 cooling 或 panic"), QStringLiteral("题材宽度明显收缩，主线扩散度下降"), QStringLiteral("回封成功率同步转弱，说明主线承接不足")},
        QVariantList{QStringLiteral("市场情绪回到 repair"), QStringLiteral("题材宽度重新扩散"), QStringLiteral("回封成功率和高位承接回稳后，只恢复题材观察和少量候选")}
    }},
    {QStringLiteral("template_risk_market_trend_neutral_allow_entry_v1"), TemplateInsight{
        QStringLiteral("用于识别非熊市且趋势环境尚可承受的中性市场阶段，适合给日线趋势策略提供比牛市放行更宽的市场总开关。"),
        QStringLiteral("放行触发要点"),
        QStringLiteral("使用提醒"),
        QVariantList{QStringLiteral("市场不是明显熊市，至少仍处于 bull 或 sideways 状态"), QStringLiteral("广度和趋势强度没有塌陷，阶段回撤也未显著失控"), QStringLiteral("更适合日线趋势、平台突破和中继趋势类策略继续筛股")},
        QVariantList{QStringLiteral("它比牛市趋势放行更宽，但仍保留市场总闸门，不等于无条件开仓"), QStringLiteral("如果回测仍长期零成交，应继续检查具体信号模板而不是先删掉整个市场门禁"), QStringLiteral("绑定后会进入市场环境阶段")}
    }},
    {QStringLiteral("template_watch_abandon_leader_replaced_v1"), TemplateInsight{
        QStringLiteral("用于识别旧龙头被替代后的放弃关注场景，适合在主导权切换时及时从旧核心撤离观察。"),
        QStringLiteral("失效触发要点"),
        QStringLiteral("使用提醒"),
        QVariantList{QStringLiteral("原核心失去主导地位，题材关注度转移到新龙头或新分支"), QStringLiteral("旧龙头未能维持强度，反而持续弱于新核心"), QStringLiteral("更适合作为停止跟踪旧龙头的结构化失效条件")},
        QVariantList{QStringLiteral("适合板块内部切龙、补位上位和退潮切换场景"), QStringLiteral("命中后应放弃旧核心，必要时改为观察新龙头或次核心"), QStringLiteral("绑定后会进入入场/观察信号阶段")}
    }},
    {QStringLiteral("template_watch_afternoon_chase_then_fade_v1"), TemplateInsight{
        QStringLiteral("用于识别午后抢筹后又迅速回落的失败攻击，适合过滤午后脉冲但无法站稳的追涨候选。"),
        QStringLiteral("失效触发要点"),
        QStringLiteral("使用提醒"),
        QVariantList{QStringLiteral("午后出现明显抢筹拉升，但并未形成稳定承接"), QStringLiteral("回落吞没午后攻击结构，说明追价资金没有被承接住"), QStringLiteral("更适合作为午后追涨票的停止跟踪模板")},
        QVariantList{QStringLiteral("适合辨别午后脉冲、尾盘抢筹和情绪修复中的假强化"), QStringLiteral("命中后应撤销追涨预期，避免把瞬时拉升当成趋势转强"), QStringLiteral("绑定后会进入入场/观察信号阶段")}
    }},
    {QStringLiteral("template_watch_afternoon_reflow_next_day_below_expectation_v1"), TemplateInsight{
        QStringLiteral("用于识别午后回流后次日表现低于预期的场景，适合过滤隔日没有溢价确认的回流票。"),
        QStringLiteral("失效触发要点"),
        QStringLiteral("使用提醒"),
        QVariantList{QStringLiteral("前一日午后回流成立，但隔日没能给出应有的延续确认"), QStringLiteral("弱开弱跟随并失守回流支点，说明回流质量偏弱"), QStringLiteral("更适合作为午后回流候选的隔日去伪存真模板")},
        QVariantList{QStringLiteral("适合情绪修复、题材回流和下午强化票的隔日验证"), QStringLiteral("命中后应放弃继续追踪与追涨，不再按回流主线处理"), QStringLiteral("绑定后会进入入场/观察信号阶段")}
    }},
    {QStringLiteral("template_watch_afternoon_reseal_failed_v1"), TemplateInsight{
        QStringLiteral("用于识别午后回封尝试失败的场景，适合在下午封板确认不足时及时停止追板。"),
        QStringLiteral("失效触发要点"),
        QStringLiteral("使用提醒"),
        QVariantList{QStringLiteral("午后尝试回封，但未能稳住关键回封位"), QStringLiteral("随后再次走弱，说明板上承接和回封强度都不够"), QStringLiteral("更适合过滤下午修板但确认不足的打板候选")},
        QVariantList{QStringLiteral("适合与炸板回封入场模板形成正反两套判断"), QStringLiteral("命中后应取消追板与继续观察，避免去接失败回封"), QStringLiteral("绑定后会进入入场/观察信号阶段")}
    }},
    {QStringLiteral("template_watch_consensus_acceleration_exhaustion_v1"), TemplateInsight{
        QStringLiteral("用于识别一致加速后的衰竭信号，适合在追高拥挤、首次明显分歧出现时停止继续追板。"),
        QStringLiteral("失效触发要点"),
        QStringLiteral("使用提醒"),
        QVariantList{QStringLiteral("个股或板块进入一致加速，追高筹码明显拥挤"), QStringLiteral("随后首次出现可见分歧，且承接未能接住一致预期"), QStringLiteral("更适合作为高潮段后的停止跟踪与阻断追买条件")},
        QVariantList{QStringLiteral("不适合继续用低吸思路硬接，重点是识别高潮后的衰竭"), QStringLiteral("可与分批止盈或高位退出模板形成前后衔接"), QStringLiteral("绑定后会进入入场/观察信号阶段")}
    }},
    {QStringLiteral("template_watch_consensus_reflow_tail_weakening_v1"), TemplateInsight{
        QStringLiteral("用于识别一致回流后尾盘走弱的场景，适合过滤回流票尾盘承接转差后的追涨风险。"),
        QStringLiteral("失效触发要点"),
        QStringLiteral("使用提醒"),
        QVariantList{QStringLiteral("前半段形成一致回流，但尾盘承接开始明显转弱"), QStringLiteral("失守回流支点，说明回流强度没有维持到收盘"), QStringLiteral("更适合作为回流票尾盘确认失败的过滤模板")},
        QVariantList{QStringLiteral("适合板块回流、情绪修复、一致加强后的尾盘风险识别"), QStringLiteral("命中后应取消继续追涨和跟踪，避免把尾盘走弱当作正常换手"), QStringLiteral("绑定后会进入入场/观察信号阶段")}
    }},
    {QStringLiteral("template_watch_consensus_repair_next_day_acceptance_vanish_v1"), TemplateInsight{
        QStringLiteral("用于识别一致修复后次日承接迅速消失的场景，适合过滤修复看似成立但延续失败的票。"),
        QStringLiteral("失效触发要点"),
        QStringLiteral("使用提醒"),
        QVariantList{QStringLiteral("前一日一致修复建立了强预期"), QStringLiteral("次日承接却快速消失并跌回修复支点下方"), QStringLiteral("说明修复更多是情绪反冲，而不是持续转强")},
        QVariantList{QStringLiteral("适合情绪修复、反包修复和高潮后再修复场景"), QStringLiteral("命中后应取消继续跟踪与追涨，避免继续按修复成功处理"), QStringLiteral("绑定后会进入入场/观察信号阶段")}
    }},
    {QStringLiteral("template_watch_cooling_end_counter_nuke_failed_v1"), TemplateInsight{
        QStringLiteral("用于识别退潮末端反核失败的场景，适合在退潮尾声的抄底尝试失效后及时停止跟踪。"),
        QStringLiteral("失效触发要点"),
        QStringLiteral("使用提醒"),
        QVariantList{QStringLiteral("退潮尾声出现反核尝试，但承接没有形成确认"), QStringLiteral("重新失守弱势区，说明反核只是情绪试错而非真正止跌"), QStringLiteral("更适合作为退潮尾声抄底博弈的止错模板")},
        QVariantList{QStringLiteral("适合退潮末端、反核博弈和超跌修复试错场景"), QStringLiteral("命中后应放弃继续抄底和跟踪，避免误判为退潮结束"), QStringLiteral("绑定后会进入入场/观察信号阶段")}
    }},
    {QStringLiteral("template_watch_cooling_tail_low_level_first_board_follow_insufficient_v1"), TemplateInsight{
        QStringLiteral("用于识别退潮尾声低位首板跟随不足的场景，适合过滤看似试错但未形成群体跟随的低位板。"),
        QStringLiteral("失效触发要点"),
        QStringLiteral("使用提醒"),
        QVariantList{QStringLiteral("退潮尾声出现低位首板，但并未形成有效跟随扩散"), QStringLiteral("随后失守支撑，说明试错合力不足"), QStringLiteral("更适合作为退潮尾声低位试错票的止错模板")},
        QVariantList{QStringLiteral("适合退潮尾声、低位首板、情绪试错首日场景"), QStringLiteral("命中后应停止继续接力和跟踪，不要把局部试错当成新周期开启"), QStringLiteral("绑定后会进入入场/观察信号阶段")}
    }},
    {QStringLiteral("template_watch_counter_nuke_confirmation_failed_v1"), TemplateInsight{
        QStringLiteral("用于识别反核后确认失败的场景，适合在弱势票反抽不成立时及时停止抄底跟踪。"),
        QStringLiteral("失效触发要点"),
        QStringLiteral("使用提醒"),
        QVariantList{QStringLiteral("个股先尝试反核，但未形成有效承接和结构确认"), QStringLiteral("价格重新回到弱势区间，说明反核只是情绪反抽"), QStringLiteral("更适合作为弱势票抄底失败后的撤退条件")},
        QVariantList{QStringLiteral("适合退潮段、反核博弈和高位杀跌后的弱修复场景"), QStringLiteral("命中后应放弃抄底预期，避免把反核误判为新主升启动"), QStringLiteral("绑定后会进入入场/观察信号阶段")}
    }},
    {QStringLiteral("template_watch_emotion_repair_afternoon_reversal_kill_v1"), TemplateInsight{
        QStringLiteral("用于识别情绪修复中的午后反杀，适合把高位修复失败的票从观察池里剔除。"),
        QStringLiteral("失效触发要点"),
        QStringLiteral("使用提醒"),
        QVariantList{QStringLiteral("市场看似进入修复，但午后再次转杀"), QStringLiteral("个股跌破午后修复支点或承接消失"), QStringLiteral("更适合作为观察失效和禁止追买的条件")},
        QVariantList{QStringLiteral("与入场信号模板配合使用效果更好"), QStringLiteral("命中后更偏向停止观察，而不是直接买入"), QStringLiteral("绑定后会进入入场/观察信号阶段")}
    }},
    {QStringLiteral("template_watch_emotion_repair_second_divergence_failed_v1"), TemplateInsight{
        QStringLiteral("用于识别情绪修复后二次分歧失败的场景，适合在修复尝试未能承接二次考验时停止跟踪。"),
        QStringLiteral("失效触发要点"),
        QStringLiteral("使用提醒"),
        QVariantList{QStringLiteral("市场已经走出一次修复，但二次分歧时承接再次掉线"), QStringLiteral("失守修复参考位，说明修复结构不够扎实"), QStringLiteral("更适合作为修复中段二次确认失败的过滤模板")},
        QVariantList{QStringLiteral("适合情绪修复、二次分歧、修复回流后的再确认场景"), QStringLiteral("命中后应取消继续跟踪，避免把二次失败误判为洗盘"), QStringLiteral("绑定后会进入入场/观察信号阶段")}
    }},
    {QStringLiteral("template_watch_first_board_next_day_weak_to_weaker_v1"), TemplateInsight{
        QStringLiteral("用于识别首板次日没有形成强延续、反而弱转弱的场景，适合过滤低质量接力。"),
        QStringLiteral("失效触发要点"),
        QStringLiteral("使用提醒"),
        QVariantList{QStringLiteral("首板次日没有形成应有的高开、承接或继续走强"), QStringLiteral("盘中表现由弱走向更弱，缺少接力资金承接"), QStringLiteral("更适合作为首板接力的止错型观察失效条件")},
        QVariantList{QStringLiteral("适合低位首板、补涨启动和板块扩散票的隔日筛选"), QStringLiteral("命中后优先取消接力预期，不建议继续等待弱转强反包"), QStringLiteral("绑定后会进入入场/观察信号阶段")}
    }},
    {QStringLiteral("template_watch_gap_up_fade_breakdown_v1"), TemplateInsight{
        QStringLiteral("用于识别高开后不能延续、反而快速走弱的场景，适合过滤高开低走型的假强势。"),
        QStringLiteral("失效触发要点"),
        QStringLiteral("使用提醒"),
        QVariantList{QStringLiteral("个股先高开制造强预期，但盘中没能延续攻击结构"), QStringLiteral("随后跌破开盘攻击位，说明高开资金没有持续承接"), QStringLiteral("更适合作为高开追涨候选的反向过滤模板")},
        QVariantList{QStringLiteral("适合竞价高预期、弱转强高开和板块回流高开票的止错"), QStringLiteral("命中后优先取消继续跟踪和追涨，不建议再按强势股处理"), QStringLiteral("绑定后会进入入场/观察信号阶段")}
    }},
    {QStringLiteral("template_watch_gap_up_instant_limit_acceptance_collapse_v1"), TemplateInsight{
        QStringLiteral("用于识别高开秒板后承接快速塌陷的场景，适合过滤看起来很强但承接质量极差的瞬时板。"),
        QStringLiteral("失效触发要点"),
        QStringLiteral("使用提醒"),
        QVariantList{QStringLiteral("个股高开秒板制造强烈一致预期"), QStringLiteral("随后快速开板且承接塌陷，说明板上确认并不真实"), QStringLiteral("更适合作为秒板接力和一致追板的强止错模板")},
        QVariantList{QStringLiteral("适合竞价高开、秒板、缩量一致票的风险过滤"), QStringLiteral("命中后应取消追板与继续观察，避免接在高预期兑现点上"), QStringLiteral("绑定后会进入入场/观察信号阶段")}
    }},
    {QStringLiteral("template_watch_high_divergence_weakening_v1"), TemplateInsight{
        QStringLiteral("用于识别高位强分歧走向弱分歧的衰减过程，适合在高位筹码松动时停止继续接力关注。"),
        QStringLiteral("失效触发要点"),
        QStringLiteral("使用提醒"),
        QVariantList{QStringLiteral("高位个股从可承接分歧转向承接持续衰减"), QStringLiteral("核心强度下降，回流无法重新夺回主动"), QStringLiteral("更适合用于高位接力票的观察失效过滤")},
        QVariantList{QStringLiteral("适合与高位炸板率恶化、情绪退潮类市场风控配套使用"), QStringLiteral("命中后优先停止跟踪和追涨，而不是继续等二次修复"), QStringLiteral("绑定后会进入入场/观察信号阶段")}
    }},
    {QStringLiteral("template_watch_high_level_blowup_next_day_thin_volume_drift_v1"), TemplateInsight{
        QStringLiteral("用于识别高位炸板后次日缩量阴跌，适合提前放弃高位弱化个股的继续观察。"),
        QStringLiteral("失效触发要点"),
        QStringLiteral("使用提醒"),
        QVariantList{QStringLiteral("前一日高位炸板后未能完成强修复"), QStringLiteral("次日缩量走弱且缺少承接回流"), QStringLiteral("适合在高位弱化阶段停止跟踪和接力")},
        QVariantList{QStringLiteral("更像观察过滤模板，不是主动入场模板"), QStringLiteral("可与高位炸板率恶化的市场风控搭配使用"), QStringLiteral("绑定后会进入入场/观察信号阶段")}
    }},
    {QStringLiteral("template_watch_intraday_break_ma_weakening_v1"), TemplateInsight{
        QStringLiteral("用于识别盘中跌破分时均线且回拉失败的弱化过程，适合过滤趋势维持率明显下降的候选。"),
        QStringLiteral("失效触发要点"),
        QStringLiteral("使用提醒"),
        QVariantList{QStringLiteral("盘中跌破分时均线，且回拉不能重新站回均线之上"), QStringLiteral("趋势维持率走弱，说明资金维持强势的能力下降"), QStringLiteral("更适合作为日内跟随与低吸观察票的失效判断")},
        QVariantList{QStringLiteral("适合回封票、分时承接票和日内趋势票的盘中过滤"), QStringLiteral("命中后应优先停止继续跟踪，避免把日内转弱误判为洗盘"), QStringLiteral("绑定后会进入入场/观察信号阶段")}
    }},
    {QStringLiteral("template_watch_low_level_first_board_no_premium_v1"), TemplateInsight{
        QStringLiteral("用于识别低位首板隔日无溢价的场景，适合过滤没有形成应有接力确认的低位启动票。"),
        QStringLiteral("失效触发要点"),
        QStringLiteral("使用提醒"),
        QVariantList{QStringLiteral("低位首板后次日没有给出应有溢价或继续走强确认"), QStringLiteral("失守首板支撑，说明接力资金和跟随质量不足"), QStringLiteral("更适合作为低位首板与补涨启动票的隔日止错模板")},
        QVariantList{QStringLiteral("适合低位首板、补涨首板和题材扩散初段个股的隔日验证"), QStringLiteral("命中后优先取消继续跟踪与接力，不再按启动票对待"), QStringLiteral("绑定后会进入入场/观察信号阶段")}
    }},
    {QStringLiteral("template_watch_low_volume_board_next_day_breakdown_v1"), TemplateInsight{
        QStringLiteral("用于识别缩量上板后次日失守的场景，适合过滤没有形成隔日接力质量的缩量板。"),
        QStringLiteral("失效触发要点"),
        QStringLiteral("使用提醒"),
        QVariantList{QStringLiteral("前一日缩量上板，但并未释放足够换手确认"), QStringLiteral("次日弱开并失守板上承接结构，说明接力资金不足"), QStringLiteral("更适合作为缩量板隔日接力的止错模板")},
        QVariantList{QStringLiteral("适合低换手连板、缩量首板和跟风板的隔日观察"), QStringLiteral("命中后应取消继续接力与跟踪，不要再等盘中自然转强"), QStringLiteral("绑定后会进入入场/观察信号阶段")}
    }},
    {QStringLiteral("template_watch_one_word_open_board_weakening_v1"), TemplateInsight{
        QStringLiteral("用于识别一字板开板后未形成承接、重新转弱的场景，适合过滤高一致性票的开板接力风险。"),
        QStringLiteral("失效触发要点"),
        QStringLiteral("使用提醒"),
        QVariantList{QStringLiteral("一字板开板后没有形成有效换手承接"), QStringLiteral("重新转弱并失守关键承接位，说明接力质量不足"), QStringLiteral("更适合作为高一致性开板接力的风险过滤模板")},
        QVariantList{QStringLiteral("适合一字开板、秒板开板和高预期接力票的止错"), QStringLiteral("命中后应取消继续关注与接力，避免接在高一致性崩口上"), QStringLiteral("绑定后会进入入场/观察信号阶段")}
    }},
    {QStringLiteral("template_watch_one_word_turnover_acceptance_decay_v1"), TemplateInsight{
        QStringLiteral("用于识别一字换手后承接快速衰减的场景，适合过滤高一致性票开口后的接力风险。"),
        QStringLiteral("失效触发要点"),
        QStringLiteral("使用提醒"),
        QVariantList{QStringLiteral("一字板打开后开始换手，但并未形成稳定承接结构"), QStringLiteral("承接快速衰减并跌破换手支点，说明接力质量恶化"), QStringLiteral("更适合作为高一致性开口票的强止错模板")},
        QVariantList{QStringLiteral("适合一字换手、缩量连板开口和高预期接力票的过滤"), QStringLiteral("命中后应取消继续跟踪与接力，避免把开口误判为健康换手"), QStringLiteral("绑定后会进入入场/观察信号阶段")}
    }},
    {QStringLiteral("template_watch_overtake_success_next_day_no_strengthening_v1"), TemplateInsight{
        QStringLiteral("用于识别卡位成功后次日不再加强的场景，适合过滤卡位当日成立但隔日没有持续性的票。"),
        QStringLiteral("失效触发要点"),
        QStringLiteral("使用提醒"),
        QVariantList{QStringLiteral("前一日卡位成功，但隔日没有继续增强或扩大战果"), QStringLiteral("失守卡位支点，说明卡位结果缺少持续承接"), QStringLiteral("更适合作为卡位票隔日验证失败的止错模板")},
        QVariantList{QStringLiteral("适合卡位上位、补位核心和情绪切换票的隔日验证"), QStringLiteral("命中后应放弃继续跟踪，不再把它按新核心或新龙头处理"), QStringLiteral("绑定后会进入入场/观察信号阶段")}
    }},
    {QStringLiteral("template_watch_repair_market_core_secondary_switch_v1"), TemplateInsight{
        QStringLiteral("用于识别修复行情中核心与次核心切换的场景，适合在主导权转移后停止跟踪旧核心。"),
        QStringLiteral("失效触发要点"),
        QStringLiteral("使用提醒"),
        QVariantList{QStringLiteral("修复行情中原核心失去带动能力，次核心开始承接主导"), QStringLiteral("旧核心失守参考位，说明主导权已经转移"), QStringLiteral("更适合作为修复行情内部切龙的观察失效模板")},
        QVariantList{QStringLiteral("适合板块修复、核心轮换和卡位替代场景"), QStringLiteral("命中后应停止旧核心跟踪，必要时转向观察新承接核心"), QStringLiteral("绑定后会进入入场/观察信号阶段")}
    }},
    {QStringLiteral("template_watch_sector_divergence_leader_follower_split_v1"), TemplateInsight{
        QStringLiteral("用于识别板块分歧时核心和跟风明显分化的场景，适合剔除失去联动能力的弱侧跟风票。"),
        QStringLiteral("失效触发要点"),
        QStringLiteral("使用提醒"),
        QVariantList{QStringLiteral("板块进入分歧，核心仍能维持强度，但跟风开始明显掉队"), QStringLiteral("弱侧个股失守参考位，说明联动关系已经断裂"), QStringLiteral("更适合作为板块内部强弱筛选的弱侧过滤模板")},
        QVariantList{QStringLiteral("适合主线修复、板块分歧和跟风筛选场景"), QStringLiteral("命中后应保留对核心的观察，同时停止弱侧跟风个股的继续跟踪"), QStringLiteral("绑定后会进入入场/观察信号阶段")}
    }},
    {QStringLiteral("template_watch_sector_reflow_symbol_lagging_v1"), TemplateInsight{
        QStringLiteral("用于识别板块回流但个股自身掉队的场景，适合及时剔除没有跟上板块节奏的弱侧标的。"),
        QStringLiteral("失效触发要点"),
        QStringLiteral("使用提醒"),
        QVariantList{QStringLiteral("板块整体回流，但个股没有形成同步跟随或强度明显落后"), QStringLiteral("失守参考位，说明它只是板块回流里的弱侧拖后腿品种"), QStringLiteral("更适合作为板块内选股的弱侧剔除模板")},
        QVariantList{QStringLiteral("适合板块联动、题材回流和核心带跟风的横向筛选"), QStringLiteral("命中后应保留板块视角，但放弃这个掉队个股的继续跟踪"), QStringLiteral("绑定后会进入入场/观察信号阶段")}
    }},
    {QStringLiteral("template_watch_sector_repair_leader_only_followers_stall_v1"), TemplateInsight{
        QStringLiteral("用于识别板块修复时仅核心独强、后排跟随失速的场景，适合剔除修复链条里的弱侧跟风。"),
        QStringLiteral("失效触发要点"),
        QStringLiteral("使用提醒"),
        QVariantList{QStringLiteral("板块进入修复，但真正强势只集中在核心龙头"), QStringLiteral("后排跟风失速并失守参考位，说明修复没有全面扩散"), QStringLiteral("更适合作为板块修复期的弱侧个股过滤模板")},
        QVariantList{QStringLiteral("适合板块修复、核心独强和后排补涨失败场景"), QStringLiteral("命中后应保留对核心的观察，但放弃后排失速个股"), QStringLiteral("绑定后会进入入场/观察信号阶段")}
    }},
    {QStringLiteral("template_watch_spike_fail_before_limit_v1"), TemplateInsight{
        QStringLiteral("用于识别冲高试图打板但未能封住的失败攻击，适合过滤冲板未遂后的回落票。"),
        QStringLiteral("失效触发要点"),
        QStringLiteral("使用提醒"),
        QVariantList{QStringLiteral("个股冲高接近封板，但始终没能完成有效封单确认"), QStringLiteral("快速回落并失守攻击位，说明资金只是试单而非真正回封"), QStringLiteral("更适合作为冲板失败票的停止跟踪模板")},
        QVariantList{QStringLiteral("适合炸板回封、打板试错和午后脉冲票的失败过滤"), QStringLiteral("命中后优先放弃追涨，不要把冲高未板当成蓄势待发"), QStringLiteral("绑定后会进入入场/观察信号阶段")}
    }},
    {QStringLiteral("template_watch_tail_ramp_next_day_weakening_v1"), TemplateInsight{
        QStringLiteral("用于识别尾盘偷袭后次日弱化的伪强化场景，适合过滤没有形成隔日溢价的尾盘抢筹票。"),
        QStringLiteral("失效触发要点"),
        QStringLiteral("使用提醒"),
        QVariantList{QStringLiteral("前一日靠尾盘偷袭或抢筹完成形态强化"), QStringLiteral("次日却未能延续强度，转为弱开弱跟随"), QStringLiteral("说明尾盘强化更多是抢跑式博弈，不是持续主升")},
        QVariantList{QStringLiteral("适合过滤尾盘偷板、尾盘抢修、尾盘情绪修复类候选"), QStringLiteral("命中后优先取消追踪与接力，不建议再等盘中二次拉回"), QStringLiteral("绑定后会进入入场/观察信号阶段")}
    }},
    {QStringLiteral("template_watch_tail_repair_no_follow_through_v1"), TemplateInsight{
        QStringLiteral("用于识别尾盘抢修后次日没有跟随延续的场景，适合过滤尾盘修复但确认不足的票。"),
        QStringLiteral("失效触发要点"),
        QStringLiteral("使用提醒"),
        QVariantList{QStringLiteral("尾盘抢修看似完成修复，但没有形成真实跟随合力"), QStringLiteral("次日再次失守修复支点，说明尾盘修复缺少持续性"), QStringLiteral("更适合作为尾盘修复型信号的隔日止错条件")},
        QVariantList{QStringLiteral("适合尾盘抢修、尾盘回流和情绪修复票的隔日筛选"), QStringLiteral("命中后应取消继续跟踪，避免把尾盘抢修误当作趋势反转"), QStringLiteral("绑定后会进入入场/观察信号阶段")}
    }},
    {QStringLiteral("template_watch_trend_structure_breakdown_v1"), TemplateInsight{
        QStringLiteral("用于在趋势支撑失守、均线斜率转弱或活跃度塌陷时直接阻断候选，避免放宽入场后把低质量样本一起买进来。"),
        QStringLiteral("阻断触发要点"),
        QStringLiteral("使用提醒"),
        QVariantList{QStringLiteral("价格明显跌破 20 日线或 60 日线支撑，同时斜率开始转弱"), QStringLiteral("成交活跃度塌陷，说明趋势延续所需的关注度和流动性不足"), QStringLiteral("更适合作为 signal_veto 的反例模板，而不是放进正向确认组")},
        QVariantList{QStringLiteral("它的作用是阻断反例，不是替代正向入场模板"), QStringLiteral("适合与较宽的趋势候选模板搭配，防止策略从零成交直接滑到低质量高回撤"), QStringLiteral("绑定后会进入入场/观察信号阶段")}
    }}