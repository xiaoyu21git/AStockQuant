#!/usr/bin/env python3
"""每日自动化: 价格→库存→排名→信号"""
import sys, os, time, subprocess
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import logging
logging.basicConfig(level=logging.INFO, format="%(asctime)s [%(levelname)s] %(message)s")
logger = logging.getLogger("daily")

STEPS = [
    ("价格更新", "commodity_ranker.py --mode update_prices"),
    ("库存同步", "sync_inventory.py"),
    ("排名计算", "commodity_ranker.py --mode compute_rank --top-n 5"),
    ("事件检测", "news_event_pipeline.py"),
    ("信号监控", "inventory_monitor.py --brief"),
]

for name, cmd in STEPS:
    logger.info(f"[{name}] 开始")
    t0 = time.time()
    r = subprocess.run(f"python {cmd}", shell=True, cwd=os.path.dirname(__file__),
                       capture_output=True, text=True, timeout=600)
    dt = time.time() - t0
    if r.returncode == 0:
        logger.info(f"[{name}] OK ({dt:.0f}s)")
    else:
        logger.error(f"[{name}] FAIL ({dt:.0f}s): {r.stderr[:200]}")

logger.info("流水线完成")
