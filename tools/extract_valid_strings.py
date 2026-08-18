import json
import re

json_path = r'E:\projetos\project-wonder-j2-decomp\textos\dialogos_en_extraidos_gemini.json'

with open(json_path, 'r', encoding='utf-8') as f:
    data = json.load(f)

entries = data['entries']

WORD_RE = re.compile(r'[A-Za-z]+')

def is_valid_text(s):
    if not s:
        return False
    s = s.strip()
    if len(s) < 2 or len(s) > 250:
        return False
        
    # Exclude system source files / compiler logs
    if s.startswith('sound/') or s.startswith('src/') or 'sndprg.c' in s or 'sndp ' in s:
        return False
    if s in ('BG CHECK', 'ffffff?', 'cW )z', 'U5k0x', 'JjbQ'):
        return False
    if re.search(r'(rxD|rxF|hdTA|rkTA|XddD|lp!D|&rdDI|0x[0-9A-Fa-f]{4})', s):
        return False
        
    words = WORD_RE.findall(s)
    if not words:
        return False
        
    letters = sum(len(w) for w in words)
    if letters < 2:
        return False
        
    return True

valid_map = {}
invalid_count = 0

for entry in entries:
    src = entry['source_en']
    if is_valid_text(src):
        valid_map[src] = valid_map.get(src, 0) + 1
    else:
        invalid_count += 1

print(f"Total entries: {len(entries)}")
print(f"Unique valid text strings: {len(valid_map)}")
print(f"Invalid / Code / Junk entries count: {invalid_count}")

# Print sample valid strings
print("\nSample unique valid strings (top 40):")
for src in list(valid_map.keys())[:40]:
    print(" ", repr(src))
