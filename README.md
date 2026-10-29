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
| reset (konec čáry) | pravé kliknutí |
| posun pohledu | pravé tažení |
| zoom | kolečko |
| délka z klávesnice | napsat číslo + Enter |
| zrušit rozkreslenou čáru | Esc |
| hladina Zdi / Překlady / Trámy / Drobné prvky | F1 / F2 / F3 / F4 |
| plná ↔ zakrytá (nad řezem) | F5 |
| ortogonálně | F8 (osy lze natočit tlačítkem *Osy podle čáry*) |
| mazání | Del (pak klik na čáru) |
| zpět | Ctrl+Z |

Uchopení: konec (čtverec), průsečík (křížek), střed (trojúhelník), na čáře (kosočtverec).

5. **Uložit výkres V7** – hlavička (jednotky, hladiny) se vezme ze *seed* výkresu,
   hladiny / barvy / styly čar se nastaví v **Hladiny a barvy…**.

Nakreslené čáry jsou v seznamu objektů pod mračnem ve skupině *Výkres (Řezy)*
a uloží se s projektem CloudCompare (.bin).
