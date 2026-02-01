import traceback
import sys
from pathlib import Path

# 确保项目根目录在 sys.path 中，便于导入 astock_engine
PROJECT_ROOT = Path(__file__).resolve().parents[1]
if str(PROJECT_ROOT) not in sys.path:
    sys.path.insert(0, str(PROJECT_ROOT))

import astock_engine
print("pkg", astock_engine.__file__)

try:
    import astock_engine.database_native as m
    print("ok", m)
except Exception as e:
    print("ERR", type(e), e)
    traceback.print_exc()
