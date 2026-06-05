import re, pathlib
path = pathlib.Path(__file__).parent / 'test_backtest_abstractions.cpp'
c = path.read_text(encoding='utf-8')
# QStringLiteral("...") → std::string{"..."}
c = re.sub(r'QStringLiteral\("([^"]+)"\)', r'std::string{"\1"}', c)
# .append( → .push_back(
c = c.replace('.append(', '.push_back(')
path.write_text(c, encoding='utf-8')
print('done')
