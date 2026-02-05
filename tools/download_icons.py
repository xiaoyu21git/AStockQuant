#!/usr/bin/env python3
import os
import urllib.request

ICON_URLS = [
    "https://raw.githubusercontent.com/FortAwesome/Font-Awesome/6.x/svgs/solid/database.svg",
    "https://raw.githubusercontent.com/FortAwesome/Font-Awesome/6.x/svgs/solid/filter.svg",
    "https://raw.githubusercontent.com/FortAwesome/Font-Awesome/6.x/svgs/solid/sliders-h.svg",
    "https://raw.githubusercontent.com/FortAwesome/Font-Awesome/6.x/svgs/solid/eye.svg",
    "https://raw.githubusercontent.com/FortAwesome/Font-Awesome/6.x/svgs/solid/sync-alt.svg",
    "https://raw.githubusercontent.com/FortAwesome/Font-Awesome/6.x/svgs/solid/check-circle.svg",
    "https://raw.githubusercontent.com/FortAwesome/Font-Awesome/6.x/svgs/solid/table.svg",
    "https://raw.githubusercontent.com/FortAwesome/Font-Awesome/6.x/svgs/solid/columns.svg",
    "https://raw.githubusercontent.com/FortAwesome/Font-Awesome/6.x/svgs/solid/server.svg",
    # 用户追加的8个新图标
    "https://raw.githubusercontent.com/FortAwesome/Font-Awesome/6.x/svgs/solid/chart-bar.svg",
    "https://raw.githubusercontent.com/FortAwesome/Font-Awesome/6.x/svgs/solid/calculator.svg",
    "https://raw.githubusercontent.com/FortAwesome/Font-Awesome/6.x/svgs/solid/project-diagram.svg",
    "https://raw.githubusercontent.com/FortAwesome/Font-Awesome/6.x/svgs/solid/box-open.svg",
    "https://raw.githubusercontent.com/FortAwesome/Font-Awesome/6.x/svgs/solid/star.svg",
    "https://raw.githubusercontent.com/FortAwesome/Font-Awesome/6.x/svgs/solid/code-branch.svg",
    "https://raw.githubusercontent.com/FortAwesome/Font-Awesome/6.x/svgs/solid/microscope.svg",
    "https://raw.githubusercontent.com/FortAwesome/Font-Awesome/6.x/svgs/solid/search-dollar.svg",
    # 用户追加的8个新图标（第二批）
    "https://raw.githubusercontent.com/FortAwesome/Font-Awesome/6.x/svgs/solid/history.svg",
    "https://raw.githubusercontent.com/FortAwesome/Font-Awesome/6.x/svgs/solid/play-circle.svg",
    "https://raw.githubusercontent.com/FortAwesome/Font-Awesome/6.x/svgs/solid/chart-line.svg",
    "https://raw.githubusercontent.com/FortAwesome/Font-Awesome/6.x/svgs/solid/exchange-alt.svg",
    "https://raw.githubusercontent.com/FortAwesome/Font-Awesome/6.x/svgs/solid/chart-area.svg",
    "https://raw.githubusercontent.com/FortAwesome/Font-Awesome/6.x/svgs/solid/tachometer-alt.svg",
    "https://raw.githubusercontent.com/FortAwesome/Font-Awesome/6.x/svgs/solid/trophy.svg",
    "https://raw.githubusercontent.com/FortAwesome/Font-Awesome/6.x/svgs/solid/chess-knight.svg",
    # 用户追加的批量新图标
    "https://raw.githubusercontent.com/FortAwesome/Font-Awesome/6.x/svgs/solid/shield-alt.svg",
    "https://raw.githubusercontent.com/FortAwesome/Font-Awesome/6.x/svgs/solid/exclamation-triangle.svg",
    "https://raw.githubusercontent.com/FortAwesome/Font-Awesome/6.x/svgs/solid/chart-pie.svg",
    "https://raw.githubusercontent.com/FortAwesome/Font-Awesome/6.x/svgs/solid/search.svg",
    "https://raw.githubusercontent.com/FortAwesome/Font-Awesome/6.x/svgs/solid/balance-scale.svg",
    "https://raw.githubusercontent.com/FortAwesome/Font-Awesome/6.x/svgs/solid/user-shield.svg",
    "https://raw.githubusercontent.com/FortAwesome/Font-Awesome/6.x/svgs/solid/fire.svg",
    "https://raw.githubusercontent.com/FortAwesome/Font-Awesome/6.x/svgs/solid/skull-crossbones.svg",
    "https://raw.githubusercontent.com/FortAwesome/Font-Awesome/6.x/svgs/solid/home.svg",
    "https://raw.githubusercontent.com/FortAwesome/Font-Awesome/6.x/svgs/solid/cogs.svg",
    "https://raw.githubusercontent.com/FortAwesome/Font-Awesome/6.x/svgs/solid/file-alt.svg",
    "https://raw.githubusercontent.com/FortAwesome/Font-Awesome/6.x/svgs/solid/plus.svg",
    "https://raw.githubusercontent.com/FortAwesome/Font-Awesome/6.x/svgs/solid/play.svg",
    "https://raw.githubusercontent.com/FortAwesome/Font-Awesome/6.x/svgs/solid/check.svg",
    "https://raw.githubusercontent.com/FortAwesome/Font-Awesome/6.x/svgs/solid/times-circle.svg",
    "https://raw.githubusercontent.com/FortAwesome/Font-Awesome/6.x/svgs/solid/arrow-up.svg",
    "https://raw.githubusercontent.com/FortAwesome/Font-Awesome/6.x/svgs/solid/arrow-down.svg",
    "https://raw.githubusercontent.com/FortAwesome/Font-Awesome/6.x/svgs/solid/calendar-alt.svg",
    "https://raw.githubusercontent.com/FortAwesome/Font-Awesome/6.x/svgs/solid/bell.svg",
    "https://raw.githubusercontent.com/FortAwesome/Font-Awesome/6.x/svgs/solid/user.svg",
    "https://raw.githubusercontent.com/FortAwesome/Font-Awesome/6.x/svgs/solid/cog.svg",
    "https://raw.githubusercontent.com/FortAwesome/Font-Awesome/6.x/svgs/solid/menu.svg",
    "https://raw.githubusercontent.com/FortAwesome/Font-Awesome/6.x/svgs/solid/info-circle.svg",
    "https://raw.githubusercontent.com/FortAwesome/Font-Awesome/6.x/svgs/solid/question-circle.svg",
    "https://raw.githubusercontent.com/FortAwesome/Font-Awesome/6.x/svgs/solid/download.svg",
    "https://raw.githubusercontent.com/FortAwesome/Font-Awesome/6.x/svgs/solid/upload.svg",
    "https://raw.githubusercontent.com/FortAwesome/Font-Awesome/6.x/svgs/solid/tasks.svg",
    "https://raw.githubusercontent.com/FortAwesome/Font-Awesome/6.x/svgs/solid/spinner.svg",
    "https://raw.githubusercontent.com/FortAwesome/Font-Awesome/6.x/svgs/solid/hourglass-half.svg",
    "https://raw.githubusercontent.com/FortAwesome/Font-Awesome/6.x/svgs/solid/clock.svg",
    "https://raw.githubusercontent.com/FortAwesome/Font-Awesome/6.x/svgs/solid/calendar-check.svg",
    "https://raw.githubusercontent.com/FortAwesome/Font-Awesome/6.x/svgs/solid/flag.svg",
    "https://raw.githubusercontent.com/FortAwesome/Font-Awesome/6.x/svgs/solid/exclamation-circle.svg",
    "https://raw.githubusercontent.com/FortAwesome/Font-Awesome/6.x/svgs/solid/check-square.svg",
    "https://raw.githubusercontent.com/FortAwesome/Font-Awesome/6.x/svgs/solid/chart-candlestick.svg",
    "https://raw.githubusercontent.com/FortAwesome/Font-Awesome/6.x/svgs/solid/coins.svg",
    "https://raw.githubusercontent.com/FortAwesome/Font-Awesome/6.x/svgs/solid/percentage.svg",
    "https://raw.githubusercontent.com/FortAwesome/Font-Awesome/6.x/svgs/solid/money-bill-wave.svg",
    "https://raw.githubusercontent.com/FortAwesome/Font-Awesome/6.x/svgs/solid/wallet.svg",
    "https://raw.githubusercontent.com/FortAwesome/Font-Awesome/6.x/svgs/solid/trend-up.svg",
    "https://raw.githubusercontent.com/FortAwesome/Font-Awesome/6.x/svgs/solid/trend-down.svg",
    "https://raw.githubusercontent.com/FortAwesome/Font-Awesome/6.x/svgs/solid/globe.svg",
    "https://raw.githubusercontent.com/FortAwesome/Font-Awesome/6.x/svgs/solid/cloud.svg",
    "https://raw.githubusercontent.com/FortAwesome/Font-Awesome/6.x/svgs/solid/link.svg",
    "https://raw.githubusercontent.com/FortAwesome/Font-Awesome/6.x/svgs/solid/share-alt.svg",
    "https://raw.githubusercontent.com/FortAwesome/Font-Awesome/6.x/svgs/solid/copy.svg",
    "https://raw.githubusercontent.com/FortAwesome/Font-Awesome/6.x/svgs/solid/trash-alt.svg",
    "https://raw.githubusercontent.com/FortAwesome/Font-Awesome/6.x/svgs/solid/edit.svg",
    "https://raw.githubusercontent.com/FortAwesome/Font-Awesome/6.x/svgs/solid/save.svg",
]

SAVE_DIR = os.path.join(os.path.dirname(__file__), "icons")
os.makedirs(SAVE_DIR, exist_ok=True)

def download_icons():
    for url in ICON_URLS:
        filename = os.path.basename(url)
        save_path = os.path.join(SAVE_DIR, filename)
        print(f"Downloading {url} -> {save_path}")
        try:
            urllib.request.urlretrieve(url, save_path)
        except Exception as e:
            print(f"Failed to download {url}: {e}")
    print("All icons attempted.")

if __name__ == "__main__":
    download_icons()
