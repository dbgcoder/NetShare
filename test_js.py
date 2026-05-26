import re

with open(r'src\network\RequestHandler.cpp', 'r', encoding='utf-8') as f:
    content = f.read()

start = content.find('QByteArray RequestHandler::generateReceivePage')
end = content.find('\n}', content.find('</script>', start))
func = content[start:end]

matches = re.findall(r'"((?:[^"\\]|\\.)*)"', func)
html = ''.join(matches)

js_start = html.find('<script>') + 8
js_end = html.find('</script>')
js = html[js_start:js_end]

# Find uploadChunked and track depth step by step
chunked_start = js.find('function uploadChunked')
chunked_end = js.find('function uploadSmall')
chunked_js = js[chunked_start:chunked_end]

# Track depth and find where it never returns to expected level
depth = 0
positions = []
for i, ch in enumerate(chunked_js):
    if ch == '{':
        depth += 1
    elif ch == '}':
        depth -= 1
    positions.append(depth)

# Find the minimum depth reached after the function opens
# The function opens at depth 1, should close back to 0
print(f'Depth at start: {positions[0]}')
print(f'Depth at end: {positions[-1]}')

# Find where depth reaches 0 for the last time within uploadChunked
last_zero = -1
for i, d in enumerate(positions):
    if d == 0:
        last_zero = i

print(f'Last time depth=0 at position {last_zero}')
print(f'Context around last depth=0: ...{chunked_js[max(0,last_zero-20):last_zero+80]}...')

# Show depth transitions
print('\nDepth transitions (where depth changes):')
prev_depth = 0
for i, d in enumerate(positions):
    if d != prev_depth:
        ctx_start = max(0, i-5)
        ctx_end = min(len(chunked_js), i+30)
        print(f'  pos {i}: depth {prev_depth}->{d} | ...{chunked_js[ctx_start:ctx_end]}...')
        prev_depth = d
