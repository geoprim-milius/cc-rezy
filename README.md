# Řezy – plugin do CloudCompare

Řezy mračnem bodů a kreslení 2D výkresu nad nimi, výstup do MicroStation V7 (.dgn).
Postaveno na CloudCompare 2.13.2.

## Stažení
Záložka **Actions** → poslední zelený běh *Sestavení pro Windows* → dole **Artifacts** → `CloudCompare-Rezy`.
Rozbalte zip kamkoli (např. `C:\CloudCompare-Rezy`) a spusťte `CloudCompare.exe`.

## Použití
1. Otevřete mračno (LAS), vyberte ho v seznamu a zvolte *Plugins → Řezy*.
2. Vpravo se otevře panel **Řezy**. Řez se nastaví 1 m nad odhadnutou podlahu.
3. **PgUp / PgDn** posouvá řez nahoru / dolů o krok.
4. **Kreslit nad řezem** zapne kreslení:

| Akce | Ovládání |
|---|---|
| bod | levé tlačítko |
| reset (konec čáry / zpět o krok) | pravé kliknutí |
| posun pohledu | pravé tažení |
| zoom | kolečko |
| **ukázat místo ve 3D** | **prostřední kliknutí** (otevře 3D náhled okolí) |
| délka / vzdálenost z klávesnice | napsat číslo + Enter |
| zrušit | Esc (2× = zpět na čáru) |
| hladina Zdi / Překlady / Trámy / Drobné prvky | F1 / F2 / F3 / F4 |
| plná ↔ zakrytá (nad řezem) | F5 |
| ortogonálně (podle natočení pohledu) | F8 |
| zpět | Ctrl+Z |

Nástroje (tlačítka v panelu nebo klávesa, kurzor nad oknem):

| Nástroj | Klávesa | Postup |
|---|---|---|
| Čára | L | body, pravé kliknutí ukončí |
| Přesun | M | čára → výchozí bod → cílový bod (nebo vzdálenost) |
| Kopie | C | čára → výchozí bod → cílové body (nebo vzdálenost) |
| Rovnoběžně | O | čára → strana; vzdálenost z pole v panelu nebo napsat + Enter |
| Prodloužit/zkrátit | E | klik u konce čáry → nový konec, nebo napsat celkovou délku |
| Roh | R | dvě čáry do průsečíku – klikejte na části, které mají zůstat |
| Mazat | Del | klik na čáru |

**Natočit pohled podle čáry** – klik na čáru, pohled se natočí, aby byla vodorovně (souřadnice
zůstávají v S-JTSK, natočí se jen zobrazení a ortogonalita). **Sever nahoru** vrátí natočení.

Uchopení: konec (čtverec), průsečík (křížek), střed (trojúhelník), na čáře (kosočtverec).
Zakryté čáry se zobrazují čárkovaně.

### Svislé řezy
1. **Nový svislý řez (S)** – v půdorysu klikněte 2 body čáry řezu (lze šikmo k osám, funguje uchopení i orto)
   a potom stranu, **kam se díváte**. Řez se pojmenuje A-A', B-B', …
2. Pohled se přepne do řezu (seznam *Pohled* nahoře v panelu přepíná Půdorys / řezy).
   **PgUp / PgDn** posouvá rovinu řezu dál / blíž, tloušťka platí stejná jako u půdorysu.
3. Kreslí se stejně jako v půdorysu, všechny nástroje fungují. Prostřední klik ukáže místo ve 3D.
4. Ve výkresu V7 je řez 2D: vodorovně vzdálenost od levého konce řezu, svisle výška nad ±0,000
   (nebo absolutně Bpv – volba *Výšky absolutně*). Umístění ve výkresu = pole *Ve výkresu X / Y*
   (výchozí: pod mračnem, řezy pod sebou). K řezu se připíše název a „±0,000 = … m n. m. Bpv“ (hladina 14).

5. **Uložit výkres V7** – hlavička (jednotky, hladiny) se vezme ze *seed* výkresu,
   hladiny / barvy / styly čar se nastaví v **Hladiny a barvy…**.

Nakreslené čáry jsou v seznamu objektů pod mračnem ve skupině *Výkres (Řezy)*
a uloží se s projektem CloudCompare (.bin).
