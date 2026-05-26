import re

with open('receive_page_full.html', 'r', encoding='utf-8') as f:
    html = f.read()

# Find body tag
idx = html.find('<body')
if idx >= 0:
    print(f"Found <body at index {idx}")
    print(f"Context: ...{html[idx:idx+100]}...")
else:
    print("NO <body tag found!")

# Check for </body>
idx2 = html.find('</body>')
if idx2 >= 0:
    print(f"Found </body> at index {idx2}")

# Check for </script>
idx3 = html.find('</script>')
if idx3 >= 0:
    print(f"Found </script> at index {idx3}")
    # Check what comes after </script>
    after = html[idx3+len('</script>'):]
    print(f"After </script>: '{after[:50]}'")

# Check the exact onclick attributes
for m in re.finditer(r"onclick='[^']*'", html):
    print(f"onclick: {m.group()[:100]}")
