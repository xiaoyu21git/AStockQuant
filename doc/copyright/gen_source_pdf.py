# -*- coding: utf-8 -*-
"""生成软著源代码鉴别材料 PDF:
前 30 页 + 后 30 页, 每页 50 行, A4, 页眉: 软件名称/版本/页码/著作权人
取材范围: src/ 下全部 .cpp/.h/.qml(路径排序 = 程序源码整体顺序), 一般交存(全可见)
"""
import math
import os

from reportlab.lib.pagesizes import A4
from reportlab.pdfbase import pdfmetrics
from reportlab.pdfbase.cidfonts import UnicodeCIDFont
from reportlab.pdfgen import canvas

ROOT = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", ".."))
SRC_DIR = os.path.join(ROOT, "src")
OUT_DIR = os.path.dirname(os.path.abspath(__file__))
OUT_PDF = os.path.join(OUT_DIR, "衡策量化交易系统软件V1.0_源代码鉴别材料.pdf")

SOFT_NAME = "衡策量化交易系统软件 V1.0"
HOLDER = "著作权人:王春发"
LINES_PER_PAGE = 50
TOTAL_PAGES = 60

FONT_NAME = "STSong-Light"
FONT_SIZE = 8.0
LINE_LEADING = 9.6

# A4 可用宽度预算(磅): 页宽 595pt - 左右边距各 20mm(≈56.7pt)
TEXT_BUDGET = 595.0 - 56.7 * 2
MARGIN_LEFT = 56.7
PAGE_H = A4[1]
TOP_Y = PAGE_H - 56.7  # 上边距 20mm


def char_w(ch):
    return 8.0 if ord(ch) > 0x2E80 else 4.0  # CJK 约 2 倍 ASCII 宽(8pt 字号下)


def wrap_line(line, budget=TEXT_BUDGET):
    """按字符宽度贪心折行, 返回物理行列表; 空行保留 1 行"""
    if line == "":
        return [""]
    out, cur, w = [], [], 0.0
    for ch in line:
        cw = char_w(ch)
        if cur and w + cw > budget:
            out.append("".join(cur))
            cur, w = [], 0.0
        cur.append(ch)
        w += cw
    if cur:
        out.append("".join(cur))
    return out


def collect_files():
    files = []
    for dirpath, dirnames, filenames in os.walk(SRC_DIR):
        dirnames[:] = [d for d in dirnames if d not in ("build", "thirdparty", ".git")]
        for fn in filenames:
            if fn.endswith((".cpp", ".h", ".qml")):
                files.append(os.path.join(dirpath, fn))
    return sorted(files, key=lambda p: os.path.relpath(p, ROOT).replace("\\", "/"))


def load_wrapped(files):
    """按顺序读取全部文件, 返回 [(relpath, [物理行...]), ...]"""
    data = []
    for fp in files:
        with open(fp, "r", encoding="utf-8", errors="replace") as f:
            raw = f.read().replace("\r\n", "\n").replace("\r", "\n")
        phys = []
        for ln in raw.split("\n"):
            ln = ln.expandtabs(4)
            if ln.strip() == "":
                continue  # 空行不占版位, 保证每页 50 个可见代码行
            phys.extend(wrap_line(ln))
        data.append((os.path.relpath(fp, ROOT).replace("\\", "/"), phys))
    return data


def pick_head_tail(data, need=TOTAL_PAGES * LINES_PER_PAGE):
    """取前 need/2 物理行与后 need/2 物理行"""
    half = need // 2
    head, tail, hcnt, tcnt = [], [], 0, 0
    for rel, phys in data:
        if hcnt < half:
            take = phys[: half - hcnt]
            if take:
                head.append((rel, take))
            hcnt += len(take)
    for rel, phys in reversed(data):
        if tcnt < half:
            take = phys[-(half - tcnt):]
            if take:
                tail.append((rel, take))
            tcnt += len(take)
    return head, tail


def build_pages(head, tail):
    """60 页: 前30页=head, 后30页=tail, 每页 50 物理行"""
    pages = []
    for chunk in (head, tail):
        flat = [(rel, ln) for rel, phys in chunk for ln in phys]
        for i in range(0, len(flat), LINES_PER_PAGE):
            pages.append(flat[i:i + LINES_PER_PAGE])
    return pages


def main():
    pdfmetrics.registerFont(UnicodeCIDFont(FONT_NAME))
    files = collect_files()
    data = load_wrapped(files)
    total_phys = sum(len(p) for _, p in data)
    head, tail = pick_head_tail(data)
    pages = build_pages(head, tail)

    print(f"文件总数: {len(files)}  物理行总数: {total_phys}")
    print(f"头部取材: {head[0][0]} .. {head[-1][0]}")
    print(f"尾部取材: {tail[0][0]} .. {tail[-1][0]}")
    print(f"页数: {len(pages)}")

    assert len(pages) == TOTAL_PAGES, "页数必须为 60"
    for p in pages:
        assert len(p) == LINES_PER_PAGE, "每页必须 50 行"

    c = canvas.Canvas(OUT_PDF, pagesize=A4)
    for pi, page in enumerate(pages, 1):
        y = TOP_Y
        for _rel, ln in page:
            c.setFont(FONT_NAME, FONT_SIZE)
            c.drawString(MARGIN_LEFT, y, ln)
            y -= LINE_LEADING
        # 页眉
        c.setFont(FONT_NAME, 8.5)
        c.drawString(MARGIN_LEFT, PAGE_H - 36, SOFT_NAME)
        c.drawString(MARGIN_LEFT + 235, PAGE_H - 36, f"第 {pi} 页 共 {TOTAL_PAGES} 页")
        c.drawString(595 - 56.7 - 90, PAGE_H - 36, HOLDER)
        c.showPage()
    c.save()
    print(f"已生成: {OUT_PDF}")


if __name__ == "__main__":
    main()
