import mysql.connector
import json
import traceback

def solve():
    config = {
        "host": "127.0.0.1",
        "port": 3306,
        "user": "root",
        "password": "123456a",
        "database": "astock_quant"
    }

    # Internal mappings
    factorType_map = {"value": 0, "momentum": 1, "size": 2, "quality": 3, "growth": 4, "dividend": 5, "technical": 6, "liquidity": 7, "macro": 8, "industry": 9, "sentiment": 10, "custom": 11, "low_volatility": 12, "价值因子": 0, "动量因子": 1, "规模因子": 2, "质量因子": 3, "成长因子": 4}
    freq_map = {"daily": 0, "日频": 0, "日": 0, "weekly": 1, "周频": 1, "周": 1, "monthly": 2, "月频": 2, "月": 2, "quarterly": 3, "季频": 3, "季": 3, "annual": 4, "yearly": 4, "年频": 4, "年": 4}
    common_std_map = {"none": 0, "不处理": 0, "zscore": 1, "z_score": 1, "z-score": 1, "z score": 1, "minmax": 2, "min_max": 2, "min-max": 2, "percentile": 3, "rank": 3, "分位数": 3}
    conf_std_map = {"none": 0, "不处理": 0, "zscore": 1, "z_score": 1, "z-score": 1, "z score": 1, "minmax": 2, "min_max": 2, "min-max": 2, "rank": 3, "percentile": 4, "分位数": 4}
    mom_type_map = {"simple": 0, "rank": 1, "normalized": 2, "exponential": 3}
    qual_metric_map = {"roe": 0, "roa": 1, "gross_margin": 2, "profit_margin": 2, "operating_margin": 3, "earnings_quality": 4}
    val_metric_map = {"bp": 0, "ep": 1, "dividend_yield": 2, "cf_p": 3}
    size_metric_map = {"market_cap": 0, "circulating_market_cap": 1, "total_assets": 2}
    growth_metric_map = {"revenue_growth": 0, "net_profit_growth": 1, "delta_roe": 2, "sue": 3}
    div_metric_map = {"dividend_yield": 0, "payout_ratio": 1, "dividend_stability": 2}
    liq_metric_map = {"turnover_rate": 0, "volume": 1, "amihud_illiquidity": 2, "amplitude": 3}
    ind_metric_map = {"industry_prosperity": 0, "industry_momentum": 1, "industry_concentration": 2}
    sent_metric_map = {"sentiment_score": 0, "social_sentiment": 1, "investor_sentiment": 2, "market_sentiment": 3}
    sent_src_map = {"news": 0, "news_sentiment": 0, "social_media": 1, "analyst_rating": 2, "market": 3, "market_sentiment": 3, "policy": 4, "alternative": 5, "derivatives": 6}
    price_type_map = {"close": 0, "open": 1, "high": 2, "low": 3}
    sector_type_map = {"sw_l1": 0, "申万一级": 0, "sw_l2": 1, "申万二级": 1, "citic_l1": 2, "中信一级": 2, "citic_l2": 3, "中信二级": 3}
    low_vol_map = {"volatility": 0, "drawdown": 1, "beta": 2}
    tech_ind_map = {"rsi": 0, "macd": 1, "ma": 2, "ema": 3, "boll": 4, "kdj": 5, "atr": 6, "obv": 7, "vwap": 8, "volume_ratio": 9, "turnover_stability": 10}
    tech_comb_map = {"equalweight": 0, "equal_weight": 0, "normalizedaverage": 1, "normalized_average": 1}
    macro_dim_map = {"growth": 0, "inflation": 1, "credit": 2, "rates": 3, "policy": 4, "risk_appetite": 5}
    macro_ind_map = {"industrial_added_value_yoy": 0, "manufacturing_pmi": 1, "gdp_yoy": 2, "cpi_yoy": 3, "ppi_yoy": 4, "m2_yoy": 5, "social_financing_stock_yoy": 6, "m1_m2_spread": 7, "ten_year_bond_yield": 8, "shibor_3m": 9, "lpr_1y": 10, "reserve_requirement_ratio": 11, "aa_credit_spread": 12, "vix_proxy": 13}

    map_registry = {
        "factorType": factorType_map,
        "frequency": freq_map,
        "macroFrequency": freq_map,
        "standardization": None, 
        "type": mom_type_map,
        "metric": None, 
        "valuationMetrics": val_metric_map,
        "growthMetrics": growth_metric_map,
        "dividendMetrics": div_metric_map,
        "sizeMetric": size_metric_map,
        "industryMetric": ind_metric_map,
        "sentimentSource": sent_src_map,
        "sectorType": sector_type_map,
        "technicalPriceType": price_type_map,
        "priceType": price_type_map,
        "components": low_vol_map,
        "technicalIndicators": tech_ind_map,
        "combinationMode": tech_comb_map,
        "macroDimensions": macro_dim_map,
        "macroIndicators": macro_ind_map
    }

    try:
        conn = mysql.connector.connect(**config)
        cursor = conn.cursor(dictionary=True)
        conn.start_transaction()

        # No factor table, just infer from category in full_config if metadata contains it or rely on existing fields
        cursor.execute("SELECT instance_id, full_config FROM factor_instance")
        rows = cursor.fetchall()

        updates = []
        errors = []
        
        for row in rows:
            fid = row["instance_id"]
            if not row["full_config"]: continue
            cfg = json.loads(row["full_config"]) or {}
            calc = cfg.get("calculation", {})
            meta = cfg.get("metadata", {})
            if not isinstance(calc, dict): continue
            
            changed = False

            legacy_aliases = {
                "lookback_period": "lookbackWindow",
                "lookbackPeriod": "lookbackWindow",
                "quality_threshold": "qualityThreshold",
                "neutralization_enabled": "neutralizationEnabled",
                "use_volume": "useVolume",
                "skip_recent": "skipRecent",
                "lagEnabled": "laggedEnabled"
            }
            legacy_hits = False
            for legacy_key, canonical_key in legacy_aliases.items():
                if legacy_key in calc:
                    errors.append({
                        "fid": fid,
                        "field": legacy_key,
                        "issue": f"legacy-key-use-canonical:{canonical_key}"
                    })
                    legacy_hits = True
            if legacy_hits:
                continue
            
            for d_field in ["timeframe", "method", "qualityMetrics", "price_type"]:
                if d_field in calc and isinstance(calc[d_field], str):
                    del calc[d_field]
                    changed = True

            if "adjustPriceType" not in calc:
                if "price_type" in calc or "priceType" in calc:
                    calc["adjustPriceType"] = 1
                    changed = True

            for key, val in list(calc.items()):
                mapping = None
                if key in map_registry:
                    mapping = map_registry[key]
                
                # Special cases for composite fields
                m_cat = meta.get("category", "") or cfg.get("category", "")
                f_type_raw = calc.get("factorType")
                
                if key == "standardization":
                    if m_cat == "自定义因子" or f_type_raw == 11 or f_type_raw == "custom":
                        mapping = conf_std_map
                    else:
                        mapping = common_std_map
                elif key == "metric":
                    if m_cat == "质量因子" or f_type_raw in [3, "quality"]: mapping = qual_metric_map
                    elif m_cat in ["价值因子", "估值因子"] or f_type_raw in [0, "value"]: mapping = val_metric_map
                    elif m_cat == "动量因子" or f_type_raw in [1, "momentum"]: mapping = mom_type_map
                    elif m_cat == "流动性因子" or f_type_raw in [7, "liquidity"]: mapping = liq_metric_map
                    elif m_cat == "情绪因子" or f_type_raw in [10, "sentiment"]: mapping = sent_metric_map
                
                if mapping:
                    def convert(v):
                        if not isinstance(v, str): return v
                        v_low = v.lower()
                        if v_low in mapping:
                            return mapping[v_low]
                        return v

                    if isinstance(val, list):
                        new_list = []
                        for item in val:
                            res = convert(item)
                            if isinstance(res, str):
                                errors.append({"fid": fid, "field": key, "value": res})
                            new_list.append(res)
                        if new_list != val:
                            calc[key] = new_list
                            changed = True
                    else:
                        res = convert(val)
                        if isinstance(res, str):
                            errors.append({"fid": fid, "field": key, "value": res})
                        elif res != val:
                            calc[key] = res
                            changed = True

            if changed:
                cfg["calculation"] = calc
                updates.append((json.dumps(cfg, ensure_ascii=False), fid))

        if errors:
            conn.rollback()
            print(json.dumps({"success": False, "errors": errors[:5], "total_errors": len(errors)}))
        else:
            count = 0
            for u in updates:
                cursor.execute("UPDATE factor_instance SET full_config = %s WHERE instance_id = %s", u)
                count += 1
            conn.commit()
            
            check_fields = ["frequency", "standardization", "metric", "growthMetrics", "valuationMetrics", "dividendMetrics", "sizeMetric", "sentimentSource", "industryMetric", "sectorType", "technicalPriceType", "technicalIndicators", "components", "macroDimensions", "macroIndicators", "macroFrequency", "priceType", "type", "adjustPriceType"]
            stats = {f: 0 for f in check_fields}
            cursor.execute("SELECT full_config FROM factor_instance")
            for row in cursor.fetchall():
                c = json.loads(row["full_config"] or "{}").get("calculation", {})
                if not isinstance(c, dict): continue
                for f in check_fields:
                    v = c.get(f)
                    if isinstance(v, str):
                        stats[f] += 1
                    elif isinstance(v, list) and any(isinstance(i, str) for i in v):
                        stats[f] += 1
            
            print(json.dumps({"success": True, "updated": count, "remaining_str_counts": stats}))

    except Exception as e:
        if "conn" in locals() and conn: conn.rollback()
        print(json.dumps({"success": False, "error": str(e)}))
    finally:
        if "conn" in locals() and conn: conn.close()

solve()
