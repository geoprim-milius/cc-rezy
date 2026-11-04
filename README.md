# Řezy – plugin do CloudCompare

Řezy mračnem bodů a kreslení 2D výkresu nad nimi, výstup do MicroStation V7 (.dgn).
Postaveno na CloudCompare 2.13.2.

## Stažení
Záložka **Actions** → poslední zelený běh *Sestavení pro Windows* → dole **Artifacts** → `CloudCompare-Rezy`.
Rozbalte zip do nové složky a spusťte `CloudCompare.exe`.

## Začátek
1. Otevřete mračno (LAS), vyberte ho v seznamu a zvolte *Plugins → Řezy*.
2. Vpravo se otevře panel **Řezy**, nahoře přibude **lišta s ikonami** (popis se ukáže po najetí myší).
3. Řez se nastaví 1 m nad odhadnutou podlahu. **Posuvník** pod údajem nebo **PgUp / PgDn** posouvá řez, **Ctrl + kolečko** mění tloušťku.

## Panel Řezy
- seznam **Pohled**: půdorysy pater / svislé řezy / pohledy (koš = smazat)
- šipky a velký údaj: výška řezu nad ±0,000 (v řezu posun roviny)
- posuvník polohy řezu přes celé mračno (po 1 cm)
- **Tloušťka**: posuvník, − / +, předvolby 2 / 5 / 10 / 20 / 50 cm
- **Hloubka** (jen u pohledu): jak daleko za rovinou je mračno vidět
- ±0,000 = výška podlahy 1. patra (ikona = odhad z mračna)
- **Podlaha patra** (jen vyšší patra): výšky v patře se počítají od ní, ve 3D na ní leží kresba
- **Reference**: jiné patro šedě pod kresbou (kontrola, zda jsou zdi nad sebou), *uchopit* = uchopovat i na jeho čáry

## Lišta nástrojů
| Ikona | Nástroj | Klávesa | Postup |
|---|---|---|---|
| tužka | kreslení zap/vyp | | |
| šipka | Výběr | 1 | klik = vybrat, Ctrl+klik = přidat/ubrat, okno tažením (zleva = jen uvnitř, zprava = i křížem), Ctrl+A = vše; modré úchopy na koncích = přesun konce; s výběrem Del/8 smaže, 3→1/3→2 kopie/přesun celého výběru, F1–F4 / hladina a F5 změní hladinu / zakrytí |
| čára | Čára | Q | body, pravé kliknutí ukončí |
| kříž šipek | Přesun | 3 → 2 | čára → výchozí bod → cíl (nebo vzdálenost) |
| dvě čáry | Kopie | 3 → 1 | čára → výchozí bod → cíle |
| rovnoběžky | Rovnoběžně | 3 → 9 | čára → strana; vzdálenost z pole vedle nebo napsat + Enter |
| čára s úchopem | Upravit prvek | 7 → 1 | klik u konce čáry (chytí bližší konec), pak nové místo nebo délka; mění jen tu jednu čáru |
| přerušená čára | Rozdělit | 7 → 2 | 2 body na čáře → část mezi nimi se smaže; stejný bod 2× = jen rozdělit |
| prodloužení | Prodloužit/zkrátit | 7 → 3 | klik u konce → nový konec nebo celková délka |
| roh | Roh | 7 → 4 | dvě čáry do průsečíku (klik na části, které zůstanou) |
| hranice | Oříznout k prvku | 7 → 5 | hraniční čára, pak čáry k oříznutí / prodloužení (klik na část, která zůstane) |
| vrchol + | Vložit vrchol | 7 → 7 | klik na čáru, pak umístit nový vrchol (čára se zlomí) |
| vrchol × | Smazat vrchol | 7 → 8 | klik na spoj dvou čar → jedna rovná čára |
| křížek | Mazat | 8 / Del | klik na čáru (s výběrem smaže hned výběr) |
| metr | Měřit | D | dva body → délka, vodorovně/výškově (ΔX/ΔY), úhel |
| A | Vložit text | | klik a napsat (např. naměřenou výšku); přesun, kopie, mazání jako u čar; do DGN na hladinu z okna Uložit |
| šipka zpět | Zpět | Ctrl+Z | |
| pravý úhel | Orto | F8 | podle natočení pohledu |
| zelený kříž | Trasování | F11 | podržte kurzor na konci/středu čáry → bod se „získá“ (zelené +); pomocné čáry vodorovně/svisle od získaných bodů i od posledního bodu, uchopí se na ně, na jejich průsečíky i na průsečík s čarou; napsané číslo = vzdálenost od získaného bodu |
| čárkovaná | Zakrytá čára | F5 | |
| hladina | Zdi / Překlady / Trámy / Drobné prvky | F1–F4 | |
| otočení | Natočit pohled | | 2 body: směr 1. → 2. bod bude vodorovně doprava |
| S se šipkou | Sever nahoru | | |
| dvě patra + | Nové patro | | samostatný půdorys; podlaha se odhadne z mračna, předchozí patro se zapne jako reference |
| značka řezu | Nový svislý řez | | 2 body v půdorysu, pak klik na stranu, kam se díváte (šipky) |
| pravítko | Pomocný řez | | 2 body v půdorysu a strana → okno 3D náhledu se přepne do řezu jen pro měření (nic se neukládá) |
| křížky 1 2 | Změřené body | | načíst seznam (číslo Y X [Z] [kód], S-JTSK), zobrazit/skrýt body (i uchopování), kódy |
| dům | Nový pohled (fasáda) | | rovina před objektem: 2 body, pak strana pohledu; vidět je vše za rovinou do *Hloubky* |
| kostka | 3D náhled | | |
| složka | Uložit rozdělanou práci | | projekt CloudCompare (.bin): mračno, kresba, řezy, patra, body, nastavení panelu i pohled |
| disketa | Uložit V7 | | seed, hladiny, umístění řezů, výšky |

**Klávesy jako MicroStation:** horní řada čísel otevírá menu (3 = Manipulace, 7 = Úpravy),
v menu se volí opět číslem horní řady. Funguje podle polohy klávesy, na české klávesnici tedy
+ ě š č ř ž ý á í é. **Vzdálenost** se píše na numerické klávesnici (nebo Shift + horní řada) a Enter.

Esc = zrušit krok, bez rozdělané práce přepne na Výběr. Rychlost zoomu kolečkem: panel *Zoom kolečkem* (1,25× na zub ≈ MicroStation).

Myš: **levé** = bod, **pravé** = reset, **Shift + tažení** nebo **tažení kolečkem** = posun, **kolečko** = zoom k myši,
**prostřední kliknutí** = ukázat místo ve 3D náhledu. Esc = zrušit (2× = zpět na čáru).
Kurzor je nitkový kříž (na výběr čáry se čtverečkem).

Uchopení: konec (čtverec), průsečík (křížek), střed (trojúhelník), na čáře (kosočtverec).

## 3D náhled
Okno se zředěnou kopií mračna a nakreslenými čarami. Nahoře posuvníky **od – do**
(jako Cross Section v CloudCompare): **Šířka**, **Hloubka** (podle natočení pohledu) a **Výška**
v metrech od místa kliknutí; horní jezdec výšky doleva = uříznutá střecha.
*výchozí* = ±1,5 m / ±2 m, *vše* = celé mračno, *čáry* = zobrazení nakreslených čar,
posuvník *průhlednost* = mračno průhledně („rentgen“), čáry zůstanou plné.
**Posun kresby**: kresba patra leží ve 3D na podlaze patra – posuvníkem ji posunete nahoru/dolů
(např. na překlady). Kresba řezu je v rovině řezu, kresba pohledu na líci objektu (odhad z mračna) –
posuvník ji posouvá do hloubky. Na výkres nemá vliv. Kolečko zoomuje k místu pod myší.
Levé tažení = otáčení, kolečko = zoom.

## Změřené body
Soubor „číslo Y X [Z] [kód]“ v S-JTSK (mezery, tabulátory nebo středníky). Body se ukážou v půdorysu
se značkou, číslem a kódem a dá se na ně uchopit. Body s výškou se v řezu (±1 m od roviny)
a v pohledu (do hloubky pohledu) samy sklopí do roviny. Body se do DGN neukládají.
Jednotlivé soubory jdou vypnout zaškrtnutím v seznamu objektů (skupina *Výkres (Řezy)*).
Barvy značky, čísla a kódu a tmavý podklad pod textem: menu bodů → *Barvy bodů…*

## Pomocný řez
Natáhněte čáru v půdorysu a klikněte na stranu pohledu. Otevře se samostatné okno (3D náhled zůstane)
s pravoúhlým řezem (tloušťka = posuvník Hloubka). Levé kliknutí 2 body = vodorovně / svisle / šikmo,
u kurzoru výška nad ±0,000 a v Bpv. Pravé tažení = posun, kolečko = zoom.

## Rozdělaná práce
Ikona složky uloží projekt CloudCompare (.bin) – mračno, kresbu, řezy, patra, body i nastavení
panelu a pohled. Pokračování: *File → Open* projekt, *Plugins → Řezy*, vybrat mračno – vše se obnoví.
(Uložit lze i přes *File → Save project*, jen se nezapamatuje přiblížení pohledu.)

## Výstup V7
V okně Uložit zaškrtnete soubory: **každé patro zvlášť** (název_1NP.dgn, název_2NP.dgn …) a **řezy
s pohledy společně** (název_rezy.dgn – řezy v řadě, pohledy pod nimi, umístění jde upravit v tabulce).
Pokud je jen jeden půdorys bez řezů, uloží se jeden soubor pod zadaným názvem.
Velké souřadnice (S-JTSK) se do V7 vejdou díky automatickému posunu globálního počátku výkresu
(souřadnice v MicroStationu zůstanou skutečné).

Ikona diskety: **seed** (existující výkres – převezmou se jednotky a nastavení, nemění se),
**Hladiny a barvy**, umístění řezů/pohledů ve výkresu (X/Y = levý konec, ±0,000),
výšky relativně nebo absolutně (Bpv), výška popisů. Řezy a pohledy jsou ve výkresu 2D
s popisem „Řez A-A'“ a „±0,000 = … m n. m. Bpv“ (hladina 14). Vložené texty mají vlastní
výšku a hladinu (výchozí 15) v okně Uložit.

Nakreslené čáry jsou v seznamu objektů pod mračnem ve skupině *Výkres (Řezy)*
a uloží se s projektem CloudCompare (.bin).
