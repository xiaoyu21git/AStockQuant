import os
import requests

ICON_URLS = {
        "wallet.svg": "https://raw.githubusercontent.com/FortAwesome/Font-Awesome/6.x/svgs/solid/wallet.svg",
        "shield-alt.svg": "https://raw.githubusercontent.com/FortAwesome/Font-Awesome/6.x/svgs/solid/shield-halved.svg",
        "database.svg": "https://raw.githubusercontent.com/FortAwesome/Font-Awesome/6.x/svgs/solid/database.svg",
        "cogs.svg": "https://raw.githubusercontent.com/FortAwesome/Font-Awesome/6.x/svgs/solid/cogs.svg",
        "plus.svg": "https://raw.githubusercontent.com/FortAwesome/Font-Awesome/6.x/svgs/solid/plus.svg",
    "chart-line.svg": "https://raw.githubusercontent.com/FortAwesome/Font-Awesome/6.x/svgs/solid/chart-line.svg",
    "robot.svg": "https://raw.githubusercontent.com/FortAwesome/Font-Awesome/6.x/svgs/solid/robot.svg",
    "history.svg": "https://raw.githubusercontent.com/FortAwesome/Font-Awesome/6.x/svgs/solid/history.svg",
    "chart-bar.svg": "https://raw.githubusercontent.com/FortAwesome/Font-Awesome/6.x/svgs/solid/chart-bar.svg",
    "chat-line.svg": "https://raw.githubusercontent.com/FortAwesome/Font-Awesome/6.x/svgs/solid/comments.svg",
    "chat-bar.svg": "https://raw.githubusercontent.com/FortAwesome/Font-Awesome/6.x/svgs/solid/comment-alt.svg",
    "arrow_back.svg": "https://raw.githubusercontent.com/FortAwesome/Font-Awesome/6.x/svgs/solid/arrow-left.svg",
    "refresh.svg": "https://raw.githubusercontent.com/FortAwesome/Font-Awesome/6.x/svgs/solid/rotate-right.svg",
    "filter_list.svg": "https://raw.githubusercontent.com/FortAwesome/Font-Awesome/6.x/svgs/solid/filter.svg",
    "close.svg": "https://raw.githubusercontent.com/FortAwesome/Font-Awesome/6.x/svgs/solid/xmark.svg",
    "search.svg": "https://raw.githubusercontent.com/FortAwesome/Font-Awesome/6.x/svgs/solid/magnifying-glass.svg",
    "download.svg": "https://raw.githubusercontent.com/FortAwesome/Font-Awesome/6.x/svgs/solid/download.svg"
}

SAVE_DIR = os.path.dirname(os.path.abspath(__file__))

def download_icon(name, url):
    path = os.path.join(SAVE_DIR, name)
    try:
        resp = requests.get(url, timeout=10)
        if resp.status_code == 200:
            with open(path, 'wb') as f:
                f.write(resp.content)
            print(f"Downloaded: {name}")
        else:
            print(f"Failed: {name} (HTTP {resp.status_code})")
    except Exception as e:
        print(f"Error downloading {name}: {e}")

def main():
    for name, url in ICON_URLS.items():
        download_icon(name, url)

if __name__ == "__main__":
    main()
