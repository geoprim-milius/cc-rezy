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
- seznam **Pohled**: Půdorys / svislé řezy / pohledy (koš = smazat)
- šipky a velký údaj: výška řezu nad ±0,000 (v řezu posun roviny)
- posuvník polohy řezu přes celé mračno (po 1 cm)
- **Tloušťka**: posuvník, − / +, předvolby 2 / 5 / 10 / 20 / 50 cm
- **Hloubka** (jen u pohledu): jak daleko za rovinou je mračno vidět
- ±0,000 = výška podlahy (ikona = odhad z mračna)

## Lišta nástrojů
| Ikona | Nástroj | Klávesa | Postup |
|---|---|---|---|
| tužka | kreslení zap/vyp | | |
| čára | Čára | Q | body, pravé kliknutí ukončí |
| kříž šipek | Přesun | 3 → 2 | čára → výchozí bod → cíl (nebo vzdálenost) |
| dvě čáry | Kopie | 3 → 1 | čára → výchozí bod → cíle |
| rovnoběžky | Rovnoběžně | 3 → 9 | čára → strana; vzdálenost z pole vedle nebo napsat + Enter |
| přerušená čára | Rozdělit | 7 → 2 | 2 body na čáře → část mezi nimi se smaže; stejný bod 2× = jen rozdělit |
| prodloužení | Prodloužit/zkrátit | 7 → 3 | klik u konce → nový konec nebo celková délka |
| roh | Roh | 7 → 4 | dvě čáry do průsečíku (klik na části, které zůstanou) |
| hranice | Oříznout k prvku | 7 → 5 | hraniční čára, pak čáry k oříznutí / prodloužení (klik na část, která zůstane) |
| křížek | Mazat | 8 / Del | klik na čáru |
| metr | Měřit | D | dva body → délka, vodorovně/výškově (ΔX/ΔY), úhel |
| šipka zpět | Zpět | Ctrl+Z | |
| pravý úhel | Orto | F8 | podle natočení pohledu |
| zelený kříž | Trasování | F11 | podržte kurzor na konci/středu čáry → bod se „získá“ (zelené +); pomocné čáry vodorovně/svisle od získaných bodů i od posledního bodu, uchopí se na ně, na jejich průsečíky i na průsečík s čarou; napsané číslo = vzdálenost od získaného bodu |
| čárkovaná | Zakrytá čára | F5 | |
| hladina | Zdi / Překlady / Trámy / Drobné prvky | F1–F4 | |
| otočení | Natočit pohled | | 2 body: směr 1. → 2. bod bude vodorovně doprava |
| S se šipkou | Sever nahoru | | |
| značka řezu | Nový svislý řez | | 2 body v půdorysu, pak klik na stranu, kam se díváte (šipky) |
| dům | Nový pohled (fasáda) | | rovina před objektem: 2 body, pak strana pohledu; vidět je vše za rovinou do *Hloubky* |
| kostka | 3D náhled | | |
| disketa | Uložit V7 | | seed, hladiny, umístění řezů, výšky |

**Klávesy jako MicroStation:** horní řada čísel otevírá menu (3 = Manipulace, 7 = Úpravy),
v menu se volí opět číslem horní řady. Funguje podle polohy klávesy, na české klávesnici tedy
+ ě š č ř ž ý á í é. **Vzdálenost** se píše na numerické klávesnici (nebo Shift + horní řada) a Enter.

Myš: **levé** = bod, **pravé** = reset, **Shift + tažení** nebo **tažení kolečkem** = posun, **kolečko** = zoom,
**prostřední kliknutí** = ukázat místo ve 3D náhledu. Esc = zrušit (2× = zpět na čáru).
Kurzor je nitkový kříž (na výběr čáry se čtverečkem).

Uchopení: konec (čtverec), průsečík (křížek), střed (trojúhelník), na čáře (kosočtverec).

## 3D náhled
Okno se zředěnou kopií mračna a nakreslenými čarami. Nahoře posuvníky **od – do**
(jako Cross Section v CloudCompare): **Šířka**, **Hloubka** (podle natočení pohledu) a **Výška**
v metrech od místa kliknutí; horní jezdec výšky doleva = uříznutá střecha.
*výchozí* = ±1,5 m / ±2 m, *vše* = celé mračno, *čáry* = zobrazení nakreslených čar.
Levé tažení = otáčení, kolečko = zoom.

## Výstup V7
Ikona diskety: **seed** (existující výkres – převezmou se jednotky a nastavení, nemění se),
**Hladiny a barvy**, umístění řezů/pohledů ve výkresu (X/Y = levý konec, ±0,000),
výšky relativně nebo absolutně (Bpv), výška popisů. Řezy a pohledy jsou ve výkresu 2D
s popisem „Řez A-A'“ a „±0,000 = … m n. m. Bpv“ (hladina 14).

Nakreslené čáry jsou v seznamu objektů pod mračnem ve skupině *Výkres (Řezy)*
a uloží se s projektem CloudCompare (.bin).
