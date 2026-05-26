import re, sys
sys.stdout.reconfigure(encoding='utf-8')

with open('src/network/RequestHandler.cpp', 'r', encoding='utf-8') as f:
    content = f.read()

for func_name in ['generateUploadPage', 'generateReceivePage']:
    start = content.find(f'QByteArray RequestHandler::{func_name}')
    end = content.find('.toUtf8();', start) + len('.toUtf8();')
    func = content[start:end]

    return_start = func.find('return QString(')
    return_end = func.rfind(').toUtf8()')
    raw = func[return_start + len('return QString('):return_end]

    lines = re.findall(r'"((?:[^"\\]|\\.)*)"', raw)
    html = ''.join(lines)
    html = html.replace('\\n', '\n').replace('\\t', '\t').replace('\\"', '"').replace("\\'", "'")

    script_pos = html.find('<script>')
    script_end = html.find('</script>')
    js = html[script_pos + len('<script>'):script_end]

    opens = js.count('{')
    closes = js.count('}')
    parens_o = js.count('(')
    parens_c = js.count(')')
    brackets_o = js.count('[')
    brackets_c = js.count(']')
    print(f'{func_name}:')
    print(f'  Braces: open={opens} close={closes} match={opens==closes}')
    print(f'  Parens: open={parens_o} close={parens_c} match={parens_o==parens_c}')
    print(f'  Brackets: open={brackets_o} close={brackets_c} match={brackets_o==brackets_c}')

    # Check for unescaped </ in JS
    import re as re2
    bad = re2.findall(r'</[a-zA-Z]', js)
    print(f'  Unescaped </tag in JS: {bad}')

    # Check for any remaining inline onclick in HTML (not in JS strings)
    html_before_script = html[:script_pos]
    inline_onclick = re2.findall(r"onclick\s*=\s*['\"]", html_before_script)
    print(f'  Inline onclick in HTML: {len(inline_onclick)}')
    if inline_onclick:
        for m in re2.finditer(r"onclick\s*=\s*['\"]", html_before_script):
            pos = m.start()
            print(f'    ...{html_before_script[max(0,pos-20):pos+60]}...')
