from __future__ import annotations

import akshare as ak


def main() -> None:
    df = ak.stock_zh_index_daily(symbol="sh000300")
    print(df.tail(3).to_string(index=False))
    print(f"max_date={df['date'].max()}")


if __name__ == "__main__":
    main()
