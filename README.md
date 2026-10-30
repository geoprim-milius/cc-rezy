# Řezy – plugin do CloudCompare

Řezy mračnem bodů a kreslení 2D výkresu nad nimi, výstup do MicroStation V7 (.dgn).
Postaveno na CloudCompare 2.13.2.

## Stažení
Záložka **Actions** → poslední zelený běh *Sestavení pro Windows* → dole **Artifacts** → `CloudCompare-Rezy`.
Rozbalte zip do nové složky a spusťte `CloudCompare.exe`.

## Začátek
1. Otevřete mračno (LAS), vyberte ho v seznamu a zvolte *Plugins → Řezy*.
2. Vpravo se otevře panel **Řezy**, nahoře přibude **lišta s ikonami** (popis se ukáže po najetí myší).
3. Řez se nastaví 1 m nad odhadnutou podlahu. **PgUp / PgDn** posouvá řez, **Ctrl + kolečko** mění tloušťku.

## Panel Řezy
- seznam **Pohled**: Půdorys / svislé řezy / pohledy (koš = smazat)
- šipky a velký údaj: výška řezu nad ±0,000 (v řezu posun roviny)
- **Tloušťka**: posuvník, − / +, předvolby 2 / 5 / 10 / 20 / 50 cm
- **Hloubka** (jen u pohledu): jak daleko za rovinou je mračno vidět
- ±0,000 = výška podlahy (ikona = odhad z mračna)

## Lišta nástrojů
| Ikona | Nástroj | Klávesa | Postup |
|---|---|---|---|
| tužka | kreslení zap/vyp | | |
| čára | Čára | L | body, pravé kliknutí ukončí |
| kříž šipek | Přesun | M | čára → výchozí bod → cíl (nebo vzdálenost) |
| dvě čáry | Kopie | C | čára → výchozí bod → cíle |
| rovnoběžky | Rovnoběžně | O | čára → strana; vzdálenost z pole vedle nebo napsat + Enter |
| prodloužení | Prodloužit/zkrátit | E | klik u konce → nový konec nebo celková délka |
| roh | Roh | R | dvě čáry do průsečíku (klik na části, které zůstanou) |
| křížek | Mazat | Del | klik na čáru |
| metr | Měřit | D | dva body → délka, vodorovně/výškově (ΔX/ΔY), úhel |
| šipka zpět | Zpět | Ctrl+Z | |
| pravý úhel | Orto | F8 | podle natočení pohledu |
| zelený kříž | Trasování | F11 | podržte kurzor na konci/středu čáry → bod se „získá“ (zelené +), pak se ukazují pomocné čáry vodorovně/svisle a jejich průsečíky; napsané číslo = vzdálenost od získaného bodu |
| čárkovaná | Zakrytá čára | F5 | |
| hladina | Zdi / Překlady / Trámy / Drobné prvky | F1–F4 | |
| otočení | Natočit pohled | | 2 body: směr 1. → 2. bod bude vodorovně doprava |
| S se šipkou | Sever nahoru | | |
| značka řezu | Nový svislý řez | S | 2 body v půdorysu, pak klik na stranu, kam se díváte (šipky) |
| dům | Nový pohled (fasáda) | | rovina před objektem: 2 body, pak strana pohledu; vidět je vše za rovinou do *Hloubky* |
| kostka | 3D náhled | | |
| disketa | Uložit V7 | | seed, hladiny, umístění řezů, výšky |

Myš: **levé** = bod, **pravé kliknutí** = reset, **pravé tažení** = posun, **kolečko** = zoom,
**prostřední kliknutí** = ukázat místo ve 3D náhledu. Esc = zrušit (2× = zpět na čáru).

Uchopení: konec (čtverec), průsečík (křížek), střed (trojúhelník), na čáře (kosočtverec).

## 3D náhled
Okno se zředěnou kopií mračna a nakreslenými čarami. Nahoře: ořez **do stran** (poloměr),
**↑ nahoru** (menší = uříznutá střecha) a **↓ dolů** od výšky řezu, zobrazení čar.
Levé tažení = otáčení, kolečko = zoom.

## Výstup V7
Ikona diskety: **seed** (existující výkres – převezmou se jednotky a nastavení, nemění se),
**Hladiny a barvy**, umístění řezů/pohledů ve výkresu (X/Y = levý konec, ±0,000),
výšky relativně nebo absolutně (Bpv), výška popisů. Řezy a pohledy jsou ve výkresu 2D
s popisem „Řez A-A'“ a „±0,000 = … m n. m. Bpv“ (hladina 14).

Nakreslené čáry jsou v seznamu objektů pod mračnem ve skupině *Výkres (Řezy)*
a uloží se s projektem CloudCompare (.bin).
