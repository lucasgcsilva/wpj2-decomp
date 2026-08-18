import json
import re

json_path = r'E:\projetos\project-wonder-j2-decomp\textos\dialogos_en_extraidos_gemini.json'

with open(json_path, 'r', encoding='utf-8') as f:
    data = json.load(f)

entries = data['entries']

# CamelCase splitter: e.g. "BookOfPlanes" -> ["Book", "Of", "Planes"]
CAMEL_RE = re.compile(r'[A-Z]?[a-z]+|[A-Z]+(?=[A-Z][a-z]|\d|\W|$)|\d+')
WORD_RE = re.compile(r'[A-Za-z]+')

# Known English words or game vocabulary
KNOWN_WORDS = {
    'the', 'be', 'to', 'of', 'and', 'a', 'in', 'that', 'have', 'i', 'it', 'for', 'not', 'on', 'with',
    'he', 'as', 'you', 'do', 'at', 'this', 'but', 'his', 'by', 'from', 'they', 'we', 'say', 'her',
    'she', 'or', 'an', 'will', 'my', 'one', 'all', 'would', 'there', 'their', 'what', 'so', 'up',
    'out', 'if', 'about', 'who', 'get', 'which', 'go', 'me', 'when', 'make', 'can', 'like', 'time',
    'no', 'just', 'him', 'know', 'take', 'people', 'into', 'year', 'your', 'good', 'some', 'could',
    'them', 'see', 'other', 'than', 'then', 'now', 'look', 'only', 'come', 'its', 'over', 'think',
    'also', 'back', 'after', 'use', 'two', 'how', 'our', 'work', 'first', 'well', 'way', 'even',
    'new', 'want', 'because', 'any', 'these', 'give', 'day', 'most', 'us', 'san', 'doctor', 'josette',
    'corlo', 'gijin', 'bird', 'messala', 'proton', 'silconian', 'siliconian', 'magiteka', 'j2', 'item',
    'oil', 'heart', 'mind', 'strength', 'speed', 'jump', 'circuit', 'level', 'leveled', 'oh', 'ah',
    'health', 'scroll', 'book', 'wrench', 'dumbbell', 'hammer', 'key', 'cat', 'dog', 'fish', 'meat',
    'apple', 'bread', 'water', 'milk', 'cake', 'coin', 'gold', 'ring', 'door', 'warp', 'zone', 'floor',
    'secret', 'pickaxe', 'tray', 'encyclopedia', 'plane', 'planes', 'cook', 'cookbook', 'seasoning',
    'flower', 'flowers', 'crystal', 'cockroach', 'pudding', 'reward', 'rewarded', 'dress', 'ball',
    'toy', 'gun', 'card', 'box', 'ship', 'manual', 'medicine', 'mecha', 'fry', 'pan', 'bad', 'music',
    'map', 'legend', 'lgnd', 'nut', 'prize', 'miss', 'con', 'race', 'sea', 'god', 'festival', 'king',
    'fishermen', 'fisherman', 'ace', 'pilot', 'submarine', 'rpg', 'scenario', 'fortune', 'past', 'future',
    'happiness', 'love', 'clean', 'shower', 'arnold', 'pokko', 'human', 'morning', 'world', 'care',
    'answer', 'communicate', 'girl', 'boy', 'friend', 'father', 'mother', 'brother', 'sister', 'help'
}

JUNK_PATTERNS = re.compile(r'(rxD|rxF|hdTA|rkTA|XddD|lp!D|&rdDI|U5k0x|x0x|0x[0-9A-Fa-f]{4}|!|\^|\{|\}|\[|\]|\\)')

def is_valid_dialogue(text):
    if not text:
        return False
    s = text.strip()
    if len(s) < 2 or len(s) > 200:
        return False
    
    # Obvious garbage patterns
    if JUNK_PATTERNS.search(s):
        return False
        
    words = [w.lower() for w in WORD_RE.findall(s)]
    if not words:
        return False

    # Also check camelcase split words
    camel_words = [w.lower() for w in CAMEL_RE.findall(s) if len(w) > 1]
    
    # Check if any word is in known english vocabulary
    all_words = set(words + camel_words)
    if any(w in KNOWN_WORDS for w in all_words):
        return True
        
    # Check if text looks like readable English (has vowels, normal letter ratio)
    vowels = sum(c.lower() in 'aeiouy' for c in s)
    consonants = sum(c.lower() in 'bcdfghjklmnpqrstvwxz' for c in s)
    if len(words) >= 1 and vowels > 0 and (consonants / max(1, vowels)) < 4:
        # Check if punctuation or space exists or length >= 4
        if ' ' in s or any(c in s for c in '.!?:;\'-",') or len(s) >= 4:
            return True

    return False

valid_entries = []
invalid_entries = []

for idx, entry in enumerate(entries):
    src = entry['source_en']
    if is_valid_dialogue(src):
        valid_entries.append((idx, src))
    else:
        invalid_entries.append((idx, src))

print(f"Valid dialogue entries: {len(valid_entries)}")
print(f"Invalid / System / Noise entries: {len(invalid_entries)}")

print("\nSample Valid Entries (first 30):")
for idx, text in valid_entries[:30]:
    print(f"  [{idx:5d}] {repr(text)}")

print("\nSample Invalid Entries (first 30):")
for idx, text in invalid_entries[:30]:
    print(f"  [{idx:5d}] {repr(text)}")
