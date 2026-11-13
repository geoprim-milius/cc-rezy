# Řezy – plugin do CloudCompare

Řezy mračnem bodů a kreslení 2D výkresu nad nimi, výstup do MicroStation V7 (.dgn).
Postaveno na CloudCompare 2.13.2.

## Stažení
Záložka **Actions** → poslední zelený běh *Sestavení pro Windows* → dole **Artifacts** → `CloudCompare-Rezy`.
Rozbalte zip do nové složky a spusťte `CloudCompare.exe`.

## Začátek
1. Otevřete mračno (LAS), vyberte ho v seznamu a zvolte *Plugins → Řezy*.
2. Vpravo se otevře panel **Řezy**, nahoře přibude **pás karet** (popis nástroje se ukáže po najetí myší).
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

## Pás karet
Karty **Kreslení** (kreslení, úpravy, pomůcky vč. pomocného řezu, změřené body), **Řezy a pohledy** (řezy, pohledy, patra, zobrazení), **Výstup**; vlevo tlačítko **Kreslit**
(zapnout/vypnout kreslení – zapne se samo i výběrem nástroje). U tlačítek je drobně klávesa (Q, 3·1 = 3 a pak 1 …).
Dvojklik na kartu pás sbalí / rozbalí.

- **Řezy a pohledy → Zobrazení → Vzhled**: *Světlý (původní)*, *AutoCAD tmavý*, *Geoprim* – přebarví celý CloudCompare.
- **Řezy a pohledy → Zobrazení → Pozadí kresby**: *Tmavé / Světlé* – když mračno na tmavém pozadí není dobře vidět.
- **Řezy a pohledy → Zobrazení → Zakryté čáry**: *jemné / střední / hrubé čárky* – délka čárek zakrytých čar na obrazovce (výchozí jemné; dřív bylo hrubé). Do DGN se nepřenáší, tam platí styl čáry z hladiny.
- **Řezy a pohledy → Zobrazení → Pracovní režim**: schová lišty a panely CloudCompare (DB Tree, vlastnosti, konzole); znovu kliknout = vrátit. Pamatuje si to i po dalším spuštění.
- **Řezy a pohledy / Patra → Přejmenovat**: přejmenuje aktivní patro (i v názvu souboru) nebo řez/pohled.
- **Patra → ±0 z mračna**: odhad výšky podlahy 1. patra.
- **Výstup → Otevřít**: otevřít mračno nebo uloženou práci (.bin).

## Nástroje
| Ikona | Nástroj | Klávesa | Postup |
|---|---|---|---|
| tužka | Kreslit (zap/vyp) | | |
| šipka | Výběr | 1 | klik = vybrat, Ctrl+klik = přidat/ubrat, okno tažením (zleva = jen uvnitř, zprava = i křížem), Ctrl+A = vše; modré úchopy na koncích = přesun konce; s výběrem Del/8 smaže, 3→1/3→2 kopie/přesun celého výběru, F1–F5 / hladina a F6 změní hladinu / zakrytí |
| čára | Čára | Q | body, pravé kliknutí ukončí; **Enter bez čísla = další úsek kolmo na předchozí** (jen ten jeden úsek, pro další kolmici zase Enter) |
| oblouk | Oblouk | E | tři body podle metody v pásu: *začátek–střed–konec* (střed = bod uprostřed oblouku, oblouk jde přes všechny tři body), *začátek–konec–střed* (třetí bod uprostřed oblouku), *střed kružnice* (začátek–střed kružnice–konec, směr podle pohybu myši); do DGN jako skutečný oblouk; lze vybrat, přesunout, kopírovat, smazat, změnit hladinu |
| čára s body | Čára z mračna | V | klik k bodům řezu (na líc zdi) → čára proložená body (náhled modře); klik na ni nebo Enter = potvrdit, klik jinam = potvrdit a hned další, pravé = zahodit. Oba konce se napojí v rohu na okolní nakreslené čáry v libovolném pořadí (L roh = obě čáry do průsečíku, T = nová čára k čáře), téměř kolmá se srovná na 90°. *Mezera* = kolik bodů smí chybět (dveře/okna = konec čáry), *tol.* = rozptyl líce, *roh do* = jak daleko od konce čáry smí být průsečík, aby se konec napojil do rohu (výchozí 5 cm; dál čára končí, kde končí body) |
| čára s body | Místnost | | klik dovnitř místnosti (dál od zdi) → obrys se spočítá dvěma metodami (paprsky z kliknutí a „rozlití“ volné plochy ke zdem); hrany šikmé o víc než 5° od hlavního směru místnosti se zahodí (kromě dlouhé šikmé zdi celou délkou na bodech), ústupky a výklenky se kreslí, rohy průsečíkem, osamělé kousky v šumu pryč. Potvrzuje se ve 2 fázích: **1** hrany, na kterých se obě metody shodují, **2** hrany jen jedné metody ležící na bodech. Enter nebo klik do místnosti = potvrdit fázi, pravé = přeskočit ji, klik do jiné místnosti = další místnost |
| čára s body | Hromadně | | celý plátek → přímé úseky delší než *min.* s rohy a krátkými hranami ústupků; už nakreslené čáry se vynechají. Potvrzuje se ve 3 fázích podle jistoty: **1** jisté (uzavřené obrysy místností z čistých líců), **2** navazující čáry s volným koncem, **3** nejisté (svazky rovnoběžných čar, osamělé čáry, zašuměná místa – většinou lepší přeskočit a dokreslit ručně). Aktuální fáze je výrazná, další tlumené. Enter nebo klik = potvrdit fázi, pravé = přeskočit ji, Esc = konec |
| kříž šipek | Přesun | 3 → 2 | čára → výchozí bod → cíl (nebo vzdálenost) |
| dva obdélníky | Otočit | 3 → 4 | prvek (nebo výběr) → střed otáčení → výchozí směr → cílový směr; náhled během otáčení; čáry, oblouky i texty (text se natočí, u výškové kóty se jen posune hrot) |
| dvě čáry | Kopie | 3 → 1 | čára → výchozí bod → cíle |
| rovnoběžky | Rovnoběžně | 3 → 9 | čára → strana; vzdálenost z pole vedle nebo napsat + Enter |
| čára s úchopem | Upravit prvek | 7 → 1 | klik u konce čáry (chytí bližší konec), pak nové místo nebo délka; mění jen tu jednu čáru |
| přerušená čára | Rozdělit | 7 → 2 | 2 body na čáře → část mezi nimi se smaže; stejný bod 2× = jen rozdělit |
| prodloužení | Prodloužit/zkrátit | 7 → 3 | klik u konce → nový konec nebo celková délka |
| roh | Roh | 7 → 4 | dvě čáry do průsečíku (klik na části, které zůstanou) |
| hranice | Oříznout k prvku | 7 → 5 | hraniční čára, pak čáry k oříznutí / prodloužení (klik na část, která zůstane) |
| vrchol + | Vložit vrchol | 7 → 7 | klik na čáru, pak umístit nový vrchol (čára se zlomí) |
| vrchol × | Smazat vrchol | 7 → 8 | klik na spoj dvou čar → jedna rovná čára |
| čáry s oranžovou | Rozdělit čarou | 7 → 6 | řezná čára 2 body → všechny čáry, které protne, se v průsečíku rozdělí (nic se nemaže); řezná čára a průsečíky se ukazují oranžově, počet v nápovědě; jeden krok Zpět |
| křížek | Mazat | 8 / Del | klik na čáru (s výběrem smaže hned výběr) |
| metr | Měřit | D | dva body → délka, vodorovně/výškově (ΔX/ΔY), úhel; ve výkresu kóta s patkami a délkou (i během měření) |
| A | Vložit text | A → A | klik = levý dolní roh, napsat (např. naměřenou výšku); přesun (3·2) a kopie (3·1) – stačí kliknout na písmena, posouvaný text je vidět u kurzoru; mazání jako u čar; do DGN na hladinu z okna Uložit |
| A | Upravit text | A → B | klik na text → změnit jeho obsah |
| kačena | Kóta (výšková) | | jen v řezu a pohledu: klik na bod = hrot šipky (uchopí se na čáry a body), druhý klik = strana poličky (vlevo / vpravo). Zápis v pásu: *Bpv* (394.410), *místní* k ±0,000 z panelu (+3.315 / ±0.000 / -0.450), *oboje* (místní nad poličkou, Bpv pod ní); 3 desetinná místa. Velikost značky a textu podle měřítka výkresu (pole *1 :*, výchozí 1:70 = základní čára 0,70 m, text 0,175 m). Přesun / kopie / Upravit prvek – výška se přepočítá podle nové polohy. Do DGN čáry + text, hladina 16 a barva 135 (okno Uložit) |
| šipka zpět | Zpět | Ctrl+Z | |
| pravý úhel | Orto | F8 | podle natočení pohledu |
| zelený kříž | Trasování | F11 | podržte kurzor na konci/středu čáry → bod se „získá“ (zelené +); pomocné čáry vodorovně/svisle od získaných bodů i od posledního bodu, **v prodloužení čáry**, na jejímž konci/středu je získaný bod, a **šikmo přes poslední dva získané body**; uchopí se na ně, na jejich průsečíky i na průsečík s čarou; funguje i s Ortho a s kolmicí (Enter); napsané číslo = vzdálenost od získaného bodu |
| čárkovaná | Zakrytá čára | F5 | |
| hladina | ZDI / ZDI_SLABE / PREKLADY / OKNA / HRANA_NAD_ŘEZEM / DVERE / KOMUNIKACE (+ vlastní) | F1–F5 | výchozí podle zakládacího seedu pudorys_v7.dgn (hladiny 2 / 6 / 5 / 7 / 4 / 3 / 10); hladiny jde přidávat, mazat a řadit v *Hladiny a barvy…* (F1–F5 = prvních pět); zakrytá čára F6 |
| otočení | Natočit pohled | | 2 body: směr 1. → 2. bod bude vodorovně doprava |
| S se šipkou | Sever nahoru | | |
| dvě patra + | Nové patro | | samostatný půdorys; podlaha se odhadne z mračna, předchozí patro se zapne jako reference |
| značka řezu | Nový svislý řez | | 2 body v půdorysu, pak klik na stranu, kam se díváte (šipky) |
| pravítko | Pomocný řez | C | 2 body v půdorysu a strana → okno 3D náhledu se přepne do řezu jen pro měření (nic se neukládá); dokud je okno pomocného řezu otevřené, je jeho rovina v 3D náhledu vidět fialově (obdélník od podlahy ke stropu a stopa ve výšce řezu) |
| křížky 1 2 | Změřené body | | načíst seznam (číslo Y X [Z] [kód], S-JTSK), zobrazit/skrýt body (i uchopování), kódy |
| dům | Nový pohled (fasáda) | | rovina před objektem: 2 body, pak strana pohledu; vidět je vše za rovinou do *Hloubky* |
| kostka | 3D náhled | | výchozí šikmý pohled kolem středu okna (zároveň „reset“ 3D náhledu) |
| špendlík | Špendlík | prostřední klik | klik na místo ve výkresu → 3D náhled se tam posune a ukáže červený špendlík; **natočení, přiblížení, perspektiva a nastavení posuvníků zůstanou**. Skrýt: ve 3D náhledu zaškrtávátko *špendlík*, ve výkresu *Zobrazit špendlík* (Pomůcky). V půdorysu jde jehla přes celou výšku ořezu, v řezu/pohledu ukazuje přesně na místo |
| složka | Uložit rozdělanou práci | | projekt CloudCompare (.bin): mračno, kresba, řezy, patra, body, nastavení panelu i pohled |
| disketa | Uložit V7 | | seed, hladiny, umístění řezů, výšky |

**Klávesy jako MicroStation:** horní řada čísel otevírá menu (3 = Manipulace, 7 = Úpravy),
v menu se volí opět číslem horní řady. Funguje podle polohy klávesy, na české klávesnici tedy
+ ě š č ř ž ý á í é. **Vzdálenost** se píše na numerické klávesnici (nebo Shift + horní řada) a Enter.

Esc = zrušit krok, bez rozdělané práce přepne na Výběr. Rychlost zoomu kolečkem: panel *Zoom kolečkem* (1,25× na zub ≈ MicroStation).

Myš: **levé** = bod, **pravé** = reset, **Shift + tažení** nebo **tažení kolečkem** = posun, **kolečko** = zoom k myši,
**prostřední kliknutí** = špendlík – ukázat místo ve 3D náhledu (pohled nechá, jak je). Esc = zrušit (2× = zpět na čáru).
Kurzor je nitkový kříž (na výběr čáry se čtverečkem).

Uchopení: konec (čtverec), průsečík (křížek), střed (trojúhelník), na čáře (kosočtverec).

## 3D náhled
Okno se zředěnou kopií mračna a nakreslenými čarami. Nahoře posuvníky **od – do**
(jako Cross Section v CloudCompare): **Šířka**, **Hloubka** (podle natočení pohledu) a **Výška**
v metrech od místa kliknutí; horní jezdec výšky doleva = uříznutá střecha.
*výchozí* = ±1,5 m / ±2 m, *vše* = celé mračno, *body* = zobrazení bodů mračna (vypnout = jen čáry), *měřené body* = změřené body ze souboru (křížek a číslo; body bez výšky na podlaze patra), *špendlík*, *čáry* = zobrazení nakreslených čar,
posuvník *průhlednost* = mračno průhledně („rentgen“), čáry zůstanou plné.
**Posun kresby**: kresba patra leží ve 3D na podlaze patra – posuvníkem ji posunete nahoru/dolů
(např. na překlady). Kresba řezu je v rovině řezu, kresba pohledu na líci objektu (odhad z mračna) –
posuvník ji posouvá do hloubky. Na výkres nemá vliv. Kolečko zoomuje k místu pod myší.
Levé tažení = otáčení, kolečko = zoom.
**Prostřední klik do mračna ve 3D náhledu** = špendlík ve výkresu: okno výkresu se posune, aby místo bylo uprostřed, a ukáže červený špendlík (hledání schodů apod.). Funguje jen na viditelné body (zapnuté *body*). Špendlík zmizí při dalším prostředním kliku do výkresu.

## Změřené body
Soubor „číslo Y X [Z] [kód]“ v S-JTSK (mezery, tabulátory nebo středníky). Body se ukážou v půdorysu
se značkou, číslem a kódem a dá se na ně uchopit. Body s výškou se v řezu (±1 m od roviny)
a v pohledu (do hloubky pohledu) samy sklopí do roviny. Do DGN řezů a pohledů se ukládají jako
malý křížek s číslem na vlastní hladině (okno Uložit, výchozí hladina 16, křížek 4 cm = polovina ramene 0,02 m); do půdorysu ne.
Výšky si v řezech vynesete nástrojem **Kóta** jen tam, kde je potřebujete.
Jednotlivé soubory jdou vypnout zaškrtnutím v seznamu objektů (skupina *Výkres (Řezy)*).
**Výšky bodů** (karta Body): výška pod číslem bodu, *Bpv* nebo *místní* (k ±0,000 z panelu); jde vypnout zvlášť; jen na obrazovce, jen body s výškou.
Barvy značky, čísla, kódu a výšky a tmavý podklad pod textem: *Barvy bodů…*

## Velká mračna (rychlost)
- **Výkres (půdorys, řez, pohled):** u mračna nad 8 milionů bodů se kreslí jen kopie bodů v plátku řezu (s rezervou pár kroků PgUp/PgDn) a velké mračno se skryje – posun a zoom jsou svižné. Při posunu řezu dál, než sahá rezerva, se plátek po chvilce klidu vybere znovu (krátké čekání). V konzoli je vidět „Zobrazen plátek řezu: N bodů“. Velké mračno zůstává v paměti a v seznamu objektů; *Uložit práci* ho uloží jako viditelné.
- **3D náhled a pomocný řez:** neředí – berou všechny body jen z ukazovaného výřezu (posuvníky). Po posunu na jiné místo nebo roztažení posuvníků se výřez vybere znovu. Jen když by výřez měl přes 60 milionů bodů (např. *vše*), zředí se a náhled to napíše.

## Výměna mračna (např. za podrobnější)
Výstup → **Přenést výkres**: celý výkres (čáry, texty, kóty, patra, řezy, pohledy, body ze souboru, nastavení panelu) se přenese z aktuálního mračna na jiné načtené mračno.
1. Uložte práci. 2. Otevřete nové mračno v CloudCompare. 3. V panelu Řezy musí být vybrané staré mračno (s výkresem) → **Přenést výkres** → vyberte nové mračno.
Pokud mají mračna jiný posun souřadnic (Global shift), výkres se přepočítá, aby seděl na stejném místě. Staré mračno se skryje – když vše sedí, zavřete ho a uložte práci.

## Pomocný řez
Natáhněte čáru v půdorysu a klikněte na stranu pohledu. Otevře se samostatné okno (3D náhled zůstane)
s pravoúhlým řezem (tloušťka = posuvník Hloubka). Levé kliknutí 2 body = vodorovně / svisle / šikmo,
u kurzoru výška nad ±0,000 a v Bpv. Pravé tažení = posun, kolečko = zoom.
Okno se otevře přiblížené od podlahy ke stropu (odhad z bodů v řezu). Měření se kreslí jako výšková kóta (svislá kótovací čára s patkami a hodnotou, šikmo kliknuté body nevadí) – v pomocném řezu i v 3D náhledu.

**Zápis výšky do půdorysu**: ve sloupci *Do půdorysu* vpravo od pohledu vyberte tlačítkem druh (Překlad / Strop / Výška / Okno / Podlaha), změřte a dejte **Enter** (nebo Zapsat).
Bere se svislý rozdíl, zaokrouhlený na cm, zapsaný v mm, jako text podél čáry pomocného řezu **na místo, kde jste klikli odečtení výšky** (poslední klik):
- *Okno*: 3 kliky – podlaha, parapet, horní hrana okna (zobrazí se obě kóty) → `2300(850)` = výška okna nad parapetem (parapet nad podlahou);
- *Překlad* (2 kliky): `Vp=2060`;
- *Strop* (2 kliky): `Vs=2650` (výška stropu);
- *Podlaha* (1 klik): `+0,150 (245,320)` – výška podlahy relativně k ±0,000 a v Bpv;
- *Výška* (2 kliky): `2060`.
Tlačítko **Text 90°** zapnuté = text otočený o 90° (kolmo na čáru řezu, začíná u čáry a čte se zdola nahoru), vypnuté = text podél čáry.
Zaškrtnuté **ručně**: text se neumístí sám, ale visí na kurzoru (podél čáry řezu) a položíte ho klikem do půdorysu (Esc = zrušit) – když je na jednom místě víc údajů.

**Pravý klik** (bez tažení) zruší rozpracované měření – jen když není definovaná podlaha; při výběru podlahy zruší výběr. Pravé tažení dál posouvá pohled.

**Def. podlahy**: tlačítko *Def. podlahy* a klik na podlahu v řezu = podlaha se zapamatuje (zelená čára, na tlačítku její výška). Pak se podlaha už neklikne: Strop / Překlad / Výška = **1 klik**, Okno = 2 kliky (parapet, horní hrana). Platí i pro další pomocné řezy; znovu kliknout na tlačítko = zrušit.

**Špendlík v pomocném řezu**: prostřední klik do mračna v pomocném řezu = špendlík v půdorysu i v 3D náhledu (pohled pomocného řezu se nehýbe). A naopak: špendlík z výkresu nebo z 3D náhledu se ukáže i v pomocném řezu, pokud místo leží v řezu (z půdorysu jako jehla od podlahy ke stropu).
Každý druh má svou hladinu DGN (okno Uložit → Výšky z pomocného řezu; výchozí podle zakládacího seedu: okno a výška KOTY 16, překlad, strop a podlaha VYSKY 9; barva textu podle hladiny KOTY 135, VYSKY 162). Natočení textu se přenese i do DGN.

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
**Globální počátek (GO)** – ze seedu, nebo tlačítkem *Z výkresu…* z vašeho cílového výkresu (výstup pak sedí i při připojení jako reference „coincident“; jiný GO v seedu = posun o rozdíl počátků), **Hladiny a barvy**, umístění řezů/pohledů ve výkresu (X/Y = levý konec, ±0,000),
výšky relativně nebo absolutně (Bpv), výška popisů. Řezy a pohledy jsou ve výkresu 2D
s popisem „Řez A-A'“ a „±0,000 = … m n. m. Bpv“ (hladina 14). Vložené texty mají vlastní
výšku a hladinu (výchozí 16 = KOTY) v okně Uložit.

**Rastr mračna (TIFF + .tfw)** – zaškrtnout v okně Uložit: ke každému půdorysu patra a ke každému řezu
a pohledu se uloží barevný obrázek mračna (LZW TIFF, výchozí 1 cm/pixel, velikost tečky 2 px) a world file .tfw.
Půdorys = body v tloušťce řezu patra v S-JTSK, řez = body v tloušťce řezu, pohled = body do hloubky pohledu
(bližší přes vzdálenější), vše přesně v místě řezu/pohledu ve výkresu. Názvy: *název_1NP.tif*,
*název_rezy_Rez_A-A.tif* … V MicroStationu: Raster Manager → Attach → poloha se načte ze souboru .tfw.

**Načíst V7** (karta Výstup) – výkres rozkreslený v MicroStationu (uložený jako V7) se načte do aktivního
půdorysu (S-JTSK) nebo řezu/pohledu a dá se dál doplňovat: úsečky, lomené čáry, tvary, oblouky, kružnice
(eliptické jako lomená čára) a texty; hladiny podle čísla, pro neznámé hladiny se nabídne založit nové
(stejné číslo, barva, tloušťka). Buňky, rastry a další prvky se vynechají (vypíše se počet). Celý import = jeden
krok Zpět. Při ukládání použijte nový soubor, nebo zaškrtněte *Uložit jen doplněné prvky*.

**Hladiny a barvy** (manažer hladin): název, barva na obrazovce, hladina/barva/tloušťka/styl pro plnou
i zakrytou čáru; *Přidat*, *Smazat* (čáry ze smazané hladiny se přesunou na zvolenou), *Nahoru/Dolů*.
Oblouky se ukládají jako oblouk (typ 16).

Nakreslené čáry jsou v seznamu objektů pod mračnem ve skupině *Výkres (Řezy)*
a uloží se s projektem CloudCompare (.bin).
