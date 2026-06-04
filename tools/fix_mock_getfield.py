"""Fix mock classes in test_factor_signal_engine_generate.cpp that are missing getField()."""
import re

def fix_file(path):
    with open(path, 'r', encoding='utf-8') as f:
        content = f.read()

    # Strategy: find all classes that inherit IMarketDataView, then find their volume() line,
    # and insert getField right after it if not already present.
    # We'll match class bodies using brace matching.

    class_matches = list(re.finditer(
        r'class\s+(Mock\w+MarketDataView)\s+final\s*:\s*public\s+IMarketDataView\s*\{',
        content
    ))

    offsets = []  # (position to insert, text to insert)
    for cls_match in class_matches:
        cls_name = cls_match.group(1)
        start_pos = cls_match.end()
        # Find volume() in this class body
        volume_match = re.search(
            r'volume\(\)\s+const\s+override\s+\{\s+return\s+\w+\(\);\s+\}',
            content[start_pos:start_pos + 2000]
        )
        if volume_match:
            vol_end = start_pos + volume_match.end()
            # Check if getField already exists in next few lines
            lookahead = content[vol_end:vol_end + 300]
            if 'getField' not in lookahead[:150]:
                # Insert after the volume() line
                indent = "    "
                offsets.append((vol_end, f'\n{indent}[[nodiscard]] std::optional<NumericConstMatrixView> getField(const std::string&) const override {{ return std::nullopt; }}'))

    # Apply offsets (reverse order to not mess up positions)
    for pos, text in sorted(offsets, reverse=True):
        content = content[:pos] + text + content[pos:]

    with open(path, 'w', encoding='utf-8') as f:
        f.write(content)

    print(f'Fixed {len(offsets)} missing getField methods in {path}')

if __name__ == '__main__':
    fix_file('tests/test_factor_signal_engine_generate.cpp')