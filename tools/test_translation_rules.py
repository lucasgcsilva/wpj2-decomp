import json
import re

json_path = r'E:\projetos\project-wonder-j2-decomp\textos\dialogos_ptbr.json'
with open(json_path, 'r', encoding='utf-8') as f:
    data = json.load(f)

entries = data['entries']

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

# Comprehensive PT-BR translation dictionary for WPJ2 terms, dialogues, items, and UI
EXACT_TRANSLATIONS = {
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
    "Waitress": "Garçonete",
    "Tout": "Promoter",
    "Flyer Handler": "Distribuidor de Panfletos",
    "Fisherman": "Pescador",
    "Proton Miner": "Minerador de Prótons",
    "Mechanic": "Mecânico",
    "Carrying dishes": "Carregar pratos",
    "Knowing how to cook": "Saber cozinhar",
    "Calling out for customers": "Chamar clientes",
    "Anybody can do this": "Qualquer um pode fazer isso",
    "Handling Flyers": "Entregar panfletos",
    "Catching deep-sea fish": "Pescar peixes em alto-mar",
    "Having your own ship": "Ter seu próprio navio",
    "Having a ship qualification": "Ter licença de navegação",
    "Digging for Proton": "Minerar Prótons",
    "Knowing how to use a Pickaxe": "Saber usar uma Picareta",
    "Repairing ships": "Consertar navios",
    "Being passionate": "Ter paixão",
    "Successfully registered.": "Registrado com sucesso.",
    "Content": "Conteúdo",
    "Salary": "Salário",
    "Hourly wage of ": "Salário por hora de ",
    " Corlo": " Corlo",
    "Paid by volume": "Pago por volume",
    "Money": "Dinheiro",
    "Back": "Voltar",
    "Sell": "Vender",
    "Register": "Registrar",
    "Previous": "Anterior",
    "Next": "Próximo",
    "Explanation": "Explicação",
    "Input the password...": "Insira a senha...",
    "Enter!": "Entrar!",
    "Leave...": "Sair...",
    "Exit": "Sair",
    " Exit": " Sair",
    "\"Dream\"": "\"Sonho\"",
    "\"Past\"": "\"Passado\"",
    "\"Future\"": "\"Futuro\"",
    "\"Happiness\"": "\"Felicidade\"",
    "Learned how to greet": "Aprendeu a cumprimentar",
    "Learned how to cook": "Aprendeu a cozinhar",
    "Learned how to clean": "Aprendeu a limpar",
    "Learned how to take showers": "Aprendeu a tomar banho",
    "Mastered the Dolphin's controls": "Dominou os controles do Dolphin",
    "Mastered the Seaba's controls": "Dominou os controles do Seaba",
    "Became a certified fisherman": "Tornou-se pescador certificado",
    "Found a secret scroll": "Encontrou um pergaminho secreto",
    "Became King of the Fishermen": "Tornou-se Rei dos Pescadores",
    "Became a master fighter": "Tornou-se um mestre lutador",
    "Was 2nd in the miss contest": "Ficou em 2º no concurso de miss",
    "Became an actress": "Tornou-se atriz",
    "Became the mysterious ace pilot": "Tornou-se a misteriosa piloto ás",
    "Won the submarine race": "Venceu a corrida de submarinos",
    "Became a good singer": "Tornou-se uma boa cantora",
    "Became a good dancer": "Tornou-se uma boa dançarina",
    "Created a story": "Criou uma história",
    "Did fortune-telling": "Fez leitura da sorte",
    "Had a dream": "Teve um sonho",
    "Found the past": "Encontrou o passado",
    "Found the future": "Encontrou o futuro",
    "Understands life and death": "Entende a vida e a morte",
    "Understands happiness": "Entende a felicidade",
    "Understands love": "Entende o amor",
    "Found a treasure in the mines": "Encontrou um tesouro nas minas",
    "Thank you very much!": "Muito obrigado!",
    "Very well done!": "Muito bem feito!",
}

# Rule-based contextual sentence translator
def translate_sentence(src):
    if src in EXACT_TRANSLATIONS:
        return EXACT_TRANSLATIONS[src]

    clean = src.strip()
    if clean in EXACT_TRANSLATIONS:
        res = EXACT_TRANSLATIONS[clean]
        if src.startswith(' '): res = ' ' + res
        if src.endswith(' '): res = res + ' '
        return res

    # Handles camelcase item names
    if ' ' not in clean and re.search(r'[a-z][A-Z]', clean):
        parts = re.findall(r'[A-Z]?[a-z]+|[A-Z]+(?=[A-Z][a-z]|\d|\W|$)', clean)
        translated_parts = [EXACT_TRANSLATIONS.get(p, p) for p in parts]
        res = " ".join(translated_parts)
        return apply_glossary(res)

    s = src

    # Dialogue sentence pattern translations
    s = re.sub(r"sell these memories\? It may be a bad idea\.", "vender estas memórias? Pode ser uma má ideia.", s)
    s = re.sub(r"You won a submarine race\?", "Você venceu uma corrida de submarinos?", s)
    s = re.sub(r"Eh\? A fisherman qualification\? It would", "Eh? Uma qualificação de pescador? Seria", s)
    s = re.sub(r"be a really bad idea to sell this!", "uma péssima ideia vender isto!", s)
    s = re.sub(r"A red dress\.\.\. with this size and color", "Um vestido vermelho... com este tamanho e cor", s)
    s = re.sub(r"A green dress\.\.\. with this size and color", "Um vestido verde... com este tamanho e cor", s)
    s = re.sub(r"A blue dress\.\.\. with this size and color", "Um vestido azul... com este tamanho e cor", s)
    s = re.sub(r"A white dress\.\.\. with this size and color", "Um vestido branco... com este tamanho e cor", s)
    s = re.sub(r"it wouldn't sell well\.\.\.", "não venderia muito bem...", s)
    s = re.sub(r"An empty bottle\? All right, treat your", "Uma garrafa vazia? Tudo bem, cuide do seu", s)
    s = re.sub(r"environment kindly and recycle it!", "meio ambiente com carinho e recicle-a!", s)
    s = re.sub(r"I'm very sorry, but you don't seem to have", "Sinto muito, mas parece que você não tem", s)
    s = re.sub(r"enough money\.\.\.", "dinheiro suficiente...", s)
    s = re.sub(r"I'm sorry, but I think you're better off not", "Desculpe, mas acho que é melhor você não", s)
    s = re.sub(r"selling this\.\.\.", "vender isto...", s)
    s = re.sub(r"I'm sorry, but this item is out of stock\.\.\.", "Desculpe, mas este item está fora de estoque...", s)
    s = re.sub(r"You don't have anything else you could", "Você não tem mais nada que possa", s)
    s = re.sub(r"sell, so if you'd excuse me\.\.\.", "vender, então com sua licença...", s)
    s = re.sub(r"I can't sell you this\. There are no more", "Não posso te vender isto. Não há mais", s)
    s = re.sub(r"bottles in stock\.\.\.", "garrafas no estoque...", s)
    s = re.sub(r"You don't have enough money to pay the", "Você não tem dinheiro suficiente para pagar a", s)
    s = re.sub(r"registration fee\.", "taxa de registro.", s)
    s = re.sub(r"You're already registered for this\.", "Você já está registrado para isto.", s)
    s = re.sub(r"Eh!\? Do you want to return to Blueland\?", "Eh!? Você quer voltar para Blueland?", s)
    s = re.sub(r"That's too bad\.\.\. Alright then, the autopilot", "Que pena... Tudo bem então, o piloto automático", s)
    s = re.sub(r"will get us home ", "vai nos levar de volta para casa ", s)
    s = re.sub(r"-san! Look!", "-san! Olhe!", s)
    s = re.sub(r"I got quite good, right!\?", "Eu fiquei muito boa, não é!?", s)
    s = re.sub(r"-san! I've found a Magiteka", "-san! Eu encontrei um tesouro", s)
    s = re.sub(r"treasure! ", "Magiteka! ", s)
    s = re.sub(r" is written on it!", " está escrito nele!", s)
    s = re.sub(r"Yay! I've found the ", "Eba! Eu encontrei o ", s)
    s = re.sub(r"Fisherman's Treasure", "Tesouro do Pescador", s)
    s = re.sub(r"Money Treasure", "Tesouro de Dinheiro", s)
    s = re.sub(r"'ve found the ", "encontrei o ", s)
    s = re.sub(r"It's not possible to put out", "Não é possível colocar", s)
    s = re.sub(r"more items than this\.\.\.", "mais itens do que isso...", s)

    # Word-level fallback translation for other sentences
    s = re.sub(r"\bThe\b", "O", s)
    s = re.sub(r"\bthe\b", "o", s)
    s = re.sub(r"\bis\b", "é", s)
    s = re.sub(r"\bare\b", "são", s)
    s = re.sub(r"\bwas\b", "foi", s)
    s = re.sub(r"\bwere\b", "foram", s)
    s = re.sub(r"\bhave\b", "ter", s)
    s = re.sub(r"\bhas\b", "tem", s)
    s = re.sub(r"\bhad\b", "tinha", s)
    s = re.sub(r"\bfound\b", "encontrou", s)
    s = re.sub(r"\blearned\b", "aprendeu", s)
    s = re.sub(r"\bbecame\b", "tornou-se", s)
    s = re.sub(r"\bunderstands\b", "entende", s)
    s = re.sub(r"\btreasure\b", "tesouro", s)

    return apply_glossary(s)

# Test translation output on sample lines
samples = [
    "WONDER PROJECT J2   ",
    "oh K!",
    "Josette",
    "-san! It's not possible to put out",
    "more items than this...",
    "Health Oil",
    "SecretScroll",
    "Pickaxe",
    "BookOfPlanes",
    "MissCon Prize",
    "Eh!? Do you want to return to Blueland?",
    "That's too bad... Alright then, the autopilot",
    "will get us home ",
    "-san! I've found a Magiteka",
    "treasure! ",
    "\"Dream\"",
    " is written on it!",
    "You won a submarine race?",
    "Became King of the Fishermen",
    "Siliconians fear so much!"
]

for sample in samples:
    print(f"EN: {repr(sample)} --> PT-BR: {repr(translate_sentence(sample))}")
