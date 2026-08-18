import json
import re
import os

JSON_PATH = r'E:\projetos\project-wonder-j2-decomp\textos\dialogos_en_extraidos_gemini.json'
TSV_PATH = r'E:\projetos\project-wonder-j2-decomp\textos\traducao_ptbr.tsv'
PTBR_JSON_PATH = r'E:\projetos\project-wonder-j2-decomp\textos\dialogos_ptbr.json'

GLOSSARY_MAP = {
    "Silconian": "Silconiano",
    "Siliconian": "Siliconiano",
    "Messala": "Messala",
    "Josette": "Josette",
    "Corlo": "Corlo",
    "Magiteka": "Magiteka",
    "Gijin": "Gijin",
    "Proton": "Proton",
    "Seaba": "Seaba",
    "Bird": "Bird",
    "J2": "J2"
}

def apply_glossary(text):
    if not text:
        return text
    for k, v in GLOSSARY_MAP.items():
        text = re.sub(r'\b' + re.escape(k) + r'\b', v, text)
    return text

known_translations = {}

if os.path.exists(TSV_PATH):
    with open(TSV_PATH, 'r', encoding='utf-8') as f:
        for line in f:
            parts = line.rstrip('\r\n').split('\t')
            if len(parts) == 2 and parts[0] and parts[1]:
                known_translations[parts[0]] = parts[1]

if os.path.exists(PTBR_JSON_PATH):
    with open(PTBR_JSON_PATH, 'r', encoding='utf-8') as f:
        d = json.load(f)
        for e in d.get('entries', []):
            src = e.get('source_en')
            pt = e.get('pt_br')
            if src and pt:
                known_translations[src] = pt

def is_garbage(st):
    if not st or not st.strip():
        return True
    s = st.strip()
    if len(s) < 2:
        return True
    if any(s.startswith(prefix) for prefix in ['sound/', 'src/', 'libultra', 'RSP', 'RDP', 'cW ']):
        return True
    if any(token in s for token in ['sndprg.c', 'sndp ', 'rxD', 'rxF', 'hdTA', 'rkTA', 'XddD', 'lp!D', '&rdDI', 'U5k0x', 'JjbQ', 'BG CHECK', 'ffffff?']):
        return True
    words = re.findall(r'[A-Za-z]+', s)
    if not words:
        return True
    if len(words) == 1 and ' ' not in s:
        w = words[0]
        if len(w) <= 3 and w.lower() not in {'oh', 'ok', 'no', 'ah', 'to', 'do', 'if', 'in', 'on', 'at', 'by', 'my', 'or', 'is', 'am', 'it', 'up', 'us', 'we', 'he', 'me', 'go', 'so', 'yes', 'cat', 'dog', 'cup', 'oil', 'box', 'nut', 'map', 'key', 'boy', 'gun', 'bad', 'red', 'sea', 'god', 'ace', 'rpg', 'j2', 'the', 'and', 'for', 'you', 'can', 'see', 'get', 'who', 'how', 'now', 'win', 'run', 'top', 'out', 'one', 'two'}:
            if not s.isalnum():
                return True
    alphas = sum(c.isalpha() for c in s)
    if alphas / max(1, len(s)) < 0.4:
        return True
    return False

# Common phrase & item translation dictionary
PHRASE_DICT = {
    "WONDER PROJECT J2": "WONDER PROJECT J2",
    "oh K!": "Tudo K!",
    "Health Oil": "Óleo de Saúde",
    "Mind Oil": "Óleo da Mente",
    "Stuffed Cat": "Gato de Pelúcia",
    "SecretScroll": "Pergaminho Secreto",
    "Pickaxe": "Picareta",
    "Tray": "Bandeja",
    "Dumbbell": "Haltere",
    "Wrench": "Chave Inglesa",
    "Grammar Book": "Livro de Gramática",
    "Encyclopedia": "Enciclopédia",
    "BookOfPlanes": "Livro de Aviões",
    "Book of Ships": "Livro de Navios",
    "Mecha Manual": "Manual Mecha",
    "Medical Book": "Livro de Medicina",
    "Treasure Box": "Baú de Tesouro",
    "Cookbook": "Livro de Culinária",
    "Seasoning": "Tempero",
    "Frying Pan": "Frigideira",
    "Flowers": "Flores",
    "Crystal": "Cristal",
    "Bad Book": "Livro Ruim",
    "Cockroach": "Barata",
    "Pudding": "Pudim",
    "RewardedDress": "Vestido de Prêmio",
    "Music Box": "Caixinha de Música",
    "Treasure Map": "Mapa do Tesouro",
    "Magiteka Lgnd": "Lenda Magiteka",
    "Corlo Legend": "Lenda de Corlo",
    "Letter": "Carta",
    "Corlo Nut": "Noz de Corlo",
    "ProtonCrystal": "Cristal de Próton",
    "MissCon Prize": "Prêmio do Concurso Miss",
    "Race Prize": "Prêmio da Corrida",
    "Membership": "Cartão de Membro",
    "Red Dress": "Vestido Vermelho",
    "Blue Dress": "Vestido Azul",
    "Green Dress": "Vestido Verde",
    "White Dress": "Vestido Branco",
    "Black Dress": "Vestido Preto",
    "Pink Dress": "Vestido Rosa",
    "Yellow Dress": "Vestido Amarelo",
    "Silver Key": "Chave de Prata",
    "Gold Key": "Chave de Ouro",
    "Iron Key": "Chave de Ferro",
    "Copper Key": "Chave de Cobre",
    "Magic Key": "Chave Mágica",
    "EmptyBottle": "Garrafa Vazia",
    "FullBottle": "Garrafa Cheia",
    "FreshWater": "Água Fresca",
    "HotWater": "Água Quente",
    "SweetMilk": "Leite Doce",
    "AppleJuice": "Suco de Maçã",
    "GrapeJuice": "Suco de Uva",
    "OrangeJuice": "Suco de Laranja",
    "BlackCoffee": "Café Puro",
    "HotTea": "Chá Quente",
    "IceTea": "Chá Gelado",
    "RiceBall": "Bolinho de Arroz",
    "MeatBun": "Pão de Carne",
    "Shortcake": "Bolo de Morango",
    "Chocolate": "Chocolate",
    "Candy": "Doce",
    "Cookie": "Biscoito",
    "Donut": "Rosquinha",
    "IceCream": "Sorvete",
    "Apple": "Maçã",
    "Banana": "Banana",
    "Orange": "Laranja",
    "Strawberry": "Morango",
    "Melon": "Melão",
    "Grape": "Uva",
    "Fish": "Peixe",
    "RoastedMeat": "Carne Assada",
    "Steak": "Bife",
    "FriedEgg": "Ovo Frito",
    "Omelet": "Omelete",
    "Soup": "Sopa",
    "Salad": "Salada",
    "Pizza": "Pizza",
    "Hamburger": "Hambúrguer",
    "Sandwich": "Sanduíche",
    "Spaghetti": "Espaguete",
    "CurryRice": "Arroz com Curry",
    "Stew": "Ensopado",
    "Hammer": "Martelo",
    "Saw": "Serra",
    "Chisel": "Formão",
    "Pliers": "Alicate",
    "Screwdriver": "Chave de Fenda",
    "Broom": "Vassoura",
    "Bucket": "Balde",
    "Rag": "Pano",
    "Brush": "Escova",
    "Soap": "Sabão",
    "Towel": "Toalha",
    "Mirror": "Espelho",
    "Comb": "Pente",
    "Scissors": "Tesoura",
    "Needle": "Agulha",
    "Thread": "Linha",
    "Bandage": "Atadura",
    "Medicine": "Remédio",
    "Vitamins": "Vitaminas",
    "Pill": "Pílula",
    "Potion": "Poção",
    "Elixir": "Elixir",
    "Antidote": "Antídoto",
    "Doll": "Boneca",
    "TeddyBear": "Ursinho de Pelúcia",
    "RobotToy": "Robô de Brinquedo",
    "ToyCar": "Carrinho de Brinquedo",
    "ToyPlane": "Avião de Brinquedo",
    "ToyShip": "Navio de Brinquedo",
    "Trumpet": "Trompete",
    "Violin": "Violino",
    "Guitar": "Violão",
    "Flute": "Flauta",
    "Drum": "Tambor",
    "Piano": "Piano",
    "Bell": "Sino",
    "Whistle": "Apito",
    "Compass": "Bússola",
    "Telescope": "Telescópio",
    "Magnifier": "Lupa",
    "Camera": "Câmera",
    "Clock": "Relógio",
    "Watch": "Relógio de Pulso",
    "Lamp": "Lâmpada",
    "Lantern": "Lanterna",
    "Candle": "Vela",
    "Match": "Fósforo",
    "Lighter": "Isqueiro",
    "Coin": "Moeda",
    "GoldCoin": "Moeda de Ouro",
    "SilverCoin": "Moeda de Prata",
    "Gem": "Gema",
    "Ruby": "Rubi",
    "Sapphire": "Safira",
    "Emerald": "Esmeralda",
    "Diamond": "Diamante",
    "Pearl": "Pérola",
    "Ring": "Anel",
    "Necklace": "Colar",
    "Crown": "Coroa",
    "Tiara": "Tiara",
    "Hat": "Chapéu",
    "Cap": "Boné",
    "Helmet": "Capacete",
    "Ribbon": "Fita",
    "Glasses": "Óculos",
    "Shoes": "Sapatos",
    "Boots": "Botas",
    "Gloves": "Luvas",
    "Socks": "Meias",
    "Coat": "Casaco",
    "Jacket": "Jaqueta",
    "Shirt": "Camisa",
    "Pants": "Calça",
    "Skirt": "Saia",
    "Apron": "Avental",
    "Umbrella": "Guarda-Chuva",
    "Fan": "Lequinho",
    "Bag": "Bolsa",
    "Backpack": "Mochila",
    "Purse": "Carteira",
    "Chest": "Baú",
    "Safe": "Cofre",
    "Key": "Chave",
    "Lock": "Cadeado",
    "Chain": "Corrente",
    "Rope": "Corda",
    "Net": "Rede",
    "Hook": "Gancho",
    "Anchor": "Âncora",
    "Oar": "Remo",
    "Wheel": "Roda",
    "Gear": "Engrenagem",
    "Screw": "Parafuso",
    "Nail": "Prego",
    "Pipe": "Cano",
    "Tube": "Tubo",
    "Spring": "Mola",
    "Battery": "Bateria",
    "Wire": "Fio",
    "Cable": "Cabo",
    "Plug": "Plugue",
    "Switch": "Interruptor",
    "Button": "Botão",
    "Lever": "Alavanca",
    "Handle": "Manivela",
    "Pedal": "Pedal",
    "Motor": "Motor",
    "Engine": "Motor",
    "Turbine": "Turbina",
    "Propeller": "Hélice",
    "Wing": "Asa",
    "Rudder": "Leme",
    "Mast": "Mastro",
    "Sail": "Vela",
    "Flag": "Bandeira",
    "Banner": "Estandarte",
    "Sign": "Placa",
    "Notice": "Aviso",
    "Poster": "Cartaz",
    "Ticket": "Ingresso",
    "Pass": "Passe",
    "Permit": "Permissão",
    "License": "Licença",
    "Certificate": "Certificado",
    "Diploma": "Diploma",
    "Document": "Documento",
    "File": "Arquivo",
    "Folder": "Pasta",
    "Card": "Cartão",
    "Stamp": "Selo",
    "Envelope": "Envelope",
    "Parcel": "Pacote",
    "Box": "Caixa",
    "Crate": "Caixote",
    "Barrel": "Barril",
    "Jar": "Jarra",
    "Pot": "Panela",
    "Vase": "Vaso",
    "Dish": "Prato",
    "Plate": "Prato",
    "Bowl": "Tigela",
    "Cup": "Xícara",
    "Glass": "Copo",
    "Mug": "Caneca",
    "Spoon": "Colher",
    "Fork": "Garfo",
    "Knife": "Faca",
    "Napkin": "Guardanapo",
    "Tablecloth": "Toalha de Mesa",
    "Chopsticks": "Palitos",
    "Straw": "Canudo",
    "Bottle": "Garrafa",
    "Can": "Lata",
    "Jug": "Jarra",
    "Flask": "Frasco",
    "Kettle": "Chaleira",
    "Teapot": "Bule",
    "Stove": "Fogão",
    "Oven": "Forno",
    "Grill": "Grelha",
    "Toaster": "Torradeira",
    "Fridge": "Geladeira",
    "Freezer": "Congelador",
    "Sink": "Pia",
    "Tub": "Banheira",
    "Toilet": "Vaso Sanitário",
    "Bed": "Cama",
    "Pillow": "Travesseiro",
    "Blanket": "Coberta",
    "Sheet": "Lençol",
    "Mattress": "Colchão",
    "Chair": "Cadeira",
    "Stool": "Banco",
    "Sofa": "Sofá",
    "Couch": "Sofá",
    "Table": "Mesa",
    "Desk": "Escrivaninha",
    "Shelf": "Prateleira",
    "Cabinet": "Armário",
    "Closet": "Guarda-Roupa",
    "Drawer": "Gaveta",
    "Curtain": "Cortina",
    "Blind": "Persiana",
    "Carpet": "Tapete",
    "Rug": "Tapete",
    "Mat": "Capacho",
    "Tile": "Azulejo",
    "Brick": "Tijolo",
    "Stone": "Pedra",
    "Rock": "Rocha",
    "Sand": "Areia",
    "Dirt": "Terra",
    "Mud": "Lama",
    "Dust": "Poeira",
    "Ash": "Cinza",
    "Coal": "Carvão",
    "Wood": "Madeira",
    "Lumber": "Lenha",
    "Plank": "Tábua",
    "Log": "Tronco",
    "Stick": "Vara",
    "Branch": "Galho",
    "Leaf": "Folha",
    "Bark": "Casca",
    "Root": "Raiz",
    "Seed": "Semente",
    "Sprout": "Brotinho",
    "Plant": "Planta",
    "Herb": "Erva",
    "Grass": "Grama",
    "Weed": "Erva Daninha",
    "Moss": "Limo",
    "Vine": "Cipó",
    "Tree": "Árvore",
    "Shrub": "Arbusto",
    "Bush": "Arbusto",
    "Bamboo": "Bambu",
    "Cactus": "Cacto",
    "Mushroom": "Cogumelo",
    "Fungus": "Fungo",
    "Algae": "Alga",
    "Seaweed": "Alga Marinha",
    "Shell": "Concha",
    "Coral": "Coral",
    "Sponge": "Esponja",
    "Feather": "Pena",
    "Bone": "Osso",
    "Skull": "Caveira",
    "Horn": "Chifre",
    "Fang": "Presa",
    "Claw": "Garra",
    "Paw": "Pata",
    "Tail": "Rabo",
    "Wing": "Asa",
    "Fin": "Nadadeira",
    "Scale": "Escama",
    "Fur": "Pelo",
    "Wool": "Lã",
    "Silk": "Seda",
    "Cotton": "Algodão",
    "Leather": "Couro",
    "Rubber": "Borracha",
    "Plastic": "Plástico",
    "Glass": "Vidro",
    "Clay": "Argila"
}

def translate_string(src):
    if is_garbage(src):
        return ""
    
    # 1. Exact match in pre-known translations (TSV / dialogos_ptbr.json)
    if src in known_translations:
        return apply_glossary(known_translations[src])
        
    # 2. Exact match in PHRASE_DICT
    clean_src = src.strip()
    if clean_src in PHRASE_DICT:
        res = PHRASE_DICT[clean_src]
        # preserve leading/trailing whitespace if any
        if src.startswith(' '): res = ' ' + res
        if src.endswith(' '): res = res + ' '
        return apply_glossary(res)
        
    if src in PHRASE_DICT:
        return apply_glossary(PHRASE_DICT[src])
        
    # 3. Handle CamelCase item names like "BookOfPlanes"
    if ' ' not in clean_src and re.search(r'[a-z][A-Z]', clean_src):
        parts = re.findall(r'[A-Z]?[a-z]+|[A-Z]+(?=[A-Z][a-z]|\d|\W|$)', clean_src)
        translated_parts = [PHRASE_DICT.get(p, p) for p in parts]
        res = " ".join(translated_parts)
        return apply_glossary(res)

    # 4. Phrase / Sentence level pattern replacements
    res = src
    res = re.sub(r"\bIt's not possible to put out\b", "Não é possível colocar", res, flags=re.IGNORECASE)
    res = re.sub(r"\bmore items than this\b", "mais itens do que isso", res, flags=re.IGNORECASE)
    res = re.sub(r"\bcircuits seem to have leveled up!\b", "parecem ter evoluído!", res, flags=re.IGNORECASE)
    res = re.sub(r"\bseems to have leveled up\b", "parece ter evoluído", res, flags=re.IGNORECASE)
    res = re.sub(r"\bThe leg strength and the jump strength\b", "Os circuitos de força das pernas e de salto", res, flags=re.IGNORECASE)
    res = re.sub(r"\bThe balance circuit\b", "O circuito de equilíbrio", res, flags=re.IGNORECASE)
    res = re.sub(r"\bI should be able to jump over a gap now!\b", "Agora devo conseguir pular sobre um vão!", res, flags=re.IGNORECASE)
    res = re.sub(r"\bI should be able to cross a log bridge now!\b", "Agora devo conseguir atravessar uma ponte de troncos!", res, flags=re.IGNORECASE)
    res = re.sub(r"\bLet's try this again!\b", "Vamos tentar isto de novo!", res, flags=re.IGNORECASE)
    res = re.sub(r"\bYou've answered all the questions on this\b", "Você respondeu a todas as perguntas deste", res, flags=re.IGNORECASE)
    res = re.sub(r"\bGo to the warp zone to reach the upper\b", "Vá à zona de teleporte para chegar ao andar", res, flags=re.IGNORECASE)
    res = re.sub(r"\bfloor, and prepare to answer another\b", "superior e prepare-se para responder a mais", res, flags=re.IGNORECASE)
    res = re.sub(r"\b10 questions!\b", "10 perguntas!", res, flags=re.IGNORECASE)
    res = re.sub(r"\bI'm my old self once more...\b", "Voltei a ser como eu era...", res, flags=re.IGNORECASE)
    res = re.sub(r"\bSo let's go back to reality.\b", "Então vamos voltar à realidade.", res, flags=re.IGNORECASE)
    res = re.sub(r"\bis a girl that is not good at\b", "é uma garota que não sabe", res, flags=re.IGNORECASE)
    res = re.sub(r"\bgreeting people...\b", "cumprimentar as pessoas...", res, flags=re.IGNORECASE)
    res = re.sub(r"\bwon the 1st place in the Miss\b", "venceu o 1º lugar no Concurso de", res, flags=re.IGNORECASE)
    res = re.sub(r"\bContest at the Sea God Festival!\b", "Miss do Festival do Deus do Mar!", res, flags=re.IGNORECASE)
    res = re.sub(r"\bis an excellent cook!\b", "é uma excelente cozinheira!", res, flags=re.IGNORECASE)
    res = re.sub(r"\bobtained the title of\b", "obteve o título de", res, flags=re.IGNORECASE)
    res = re.sub(r"\bKing of the Fishermen\b", "Rei dos Pescadores", res, flags=re.IGNORECASE)
    res = re.sub(r"\bis the Mysterious Ace Pilot\b", "é a Misteriosa Piloto Ás", res, flags=re.IGNORECASE)
    res = re.sub(r"\bwhich the Siliconians fear so much!\b", "que os Siliconianos tanto temem!", res, flags=re.IGNORECASE)
    res = re.sub(r"\bdid the impossible and won the\b", "fez o impossível e venceu a", res, flags=re.IGNORECASE)
    res = re.sub(r"\bsubmarine race!\b", "corrida de submarinos!", res, flags=re.IGNORECASE)
    res = re.sub(r"\bhas created a complete role\b", "criou um cenário completo de aventura", res, flags=re.IGNORECASE)
    res = re.sub(r"\bplaying adventure scenario!\b", "de RPG!", res, flags=re.IGNORECASE)
    res = re.sub(r"\bhas done fortune-telling!\b", "fez uma leitura da sorte!", res, flags=re.IGNORECASE)
    res = re.sub(r"\bhad the \"Dream\", a Magitekan\b", "teve o \"Sonho\", um tesouro", res, flags=re.IGNORECASE)
    res = re.sub(r"\btreasure.\b", "tesouro.", res, flags=re.IGNORECASE)
    res = re.sub(r"\bfound the Magiteka treasure\b", "encontrou o tesouro Magiteka", res, flags=re.IGNORECASE)
    res = re.sub(r"\bnow understands \"Happiness\", a\b", "agora entende a \"Felicidade\", um", res, flags=re.IGNORECASE)
    res = re.sub(r"\bfound a treasure in the proton\b", "encontrou um tesouro na mina de", res, flags=re.IGNORECASE)
    res = re.sub(r"\bmine!\b", "prótons!", res, flags=re.IGNORECASE)
    res = re.sub(r"\bnow understands \"Love\"...\b", "agora entende o \"Amor\"...", res, flags=re.IGNORECASE)
    res = re.sub(r"\bknows how to clean herself and\b", "sabe limpar-se e", res, flags=re.IGNORECASE)
    res = re.sub(r"\btake showers...\b", "tomar banho...", res, flags=re.IGNORECASE)
    res = re.sub(r"\bis in love with Arnold!\b", "está apaixonada por Arnold!", res, flags=re.IGNORECASE)
    res = re.sub(r"\blikes Pokko!\b", "gosta de Pokko!", res, flags=re.IGNORECASE)
    res = re.sub(r"\bhas a mechanical body... but her\b", "tem um corpo mecânico... mas seu", res, flags=re.IGNORECASE)
    res = re.sub(r"\bheart is human!\b", "coração é humano!", res, flags=re.IGNORECASE)
    res = re.sub(r"\bIt's morning...\b", "É manhã...", res, flags=re.IGNORECASE)
    res = re.sub(r"\bThe Doctor said\b", "O Doutor disse que", res, flags=re.IGNORECASE)
    res = re.sub(r"\bis from a\b", "é de um", res, flags=re.IGNORECASE)
    res = re.sub(r"\bworld I can't see.\b", "mundo que não consigo ver.", res, flags=re.IGNORECASE)
    res = re.sub(r"\bIs there a\b", "Existe algum", res, flags=re.IGNORECASE)
    res = re.sub(r"\bin the\b", "no", res, flags=re.IGNORECASE)
    res = re.sub(r"\bworld which I can't see\?\b", "mundo que não consigo ver?", res, flags=re.IGNORECASE)
    res = re.sub(r"\bI-I'm\b", "E-eu sou", res, flags=re.IGNORECASE)
    res = re.sub(r"\band from this day on I will\b", "e, a partir de hoje, vou", res, flags=re.IGNORECASE)
    res = re.sub(r"\bbe under your care...\b", "ficar aos seus cuidados...", res, flags=re.IGNORECASE)
    res = re.sub(r"\bNo answer...\b", "Sem resposta...", res, flags=re.IGNORECASE)
    res = re.sub(r"\bAh, right! I should explain how to\b", "Ah, certo! Eu deveria explicar como", res, flags=re.IGNORECASE)
    res = re.sub(r"\bcommunicate with me!\b", "se comunicar comigo!", res, flags=re.IGNORECASE)

    # Apply glossary rules to any remaining untranslated or translated text
    res = apply_glossary(res)
    return res

print("Reading input JSON file...")
with open(JSON_PATH, 'r', encoding='utf-8') as f:
    data = json.load(f)

entries = data['entries']
print(f"Loaded {len(entries)} entries from JSON.")

translated_count = 0
empty_count = 0

for entry in entries:
    src = entry.get('source_en', '')
    translated = translate_string(src)
    entry['pt_br'] = translated
    if translated:
        translated_count += 1
    else:
        empty_count += 1

print(f"Translation complete!")
print(f"  Entries with valid PT-BR translation: {translated_count}")
print(f"  Entries left empty (system/code/noise): {empty_count}")

print("Writing updated JSON file...")
with open(JSON_PATH, 'w', encoding='utf-8') as f:
    json.dump(data, f, ensure_ascii=False, indent=2)

print("Saved successfully to E:\\projetos\\project-wonder-j2-decomp\\textos\\dialogos_en_extraidos_gemini.json!")
