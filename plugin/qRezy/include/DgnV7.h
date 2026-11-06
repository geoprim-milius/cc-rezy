#pragma once

// Zápis 2D výkresu do formátu MicroStation V7 (.dgn).
// Hlavička výkresu (TCB, nastavení hladin, aplikační prvky) se přebírá ze seed souboru,
// grafické prvky ze seedu se vynechají a přidají se nakreslené čáry.

#include <cstdint>
#include <string>
#include <vector>

namespace DgnV7
{
	struct Point2
	{
		double x = 0.0;
		double y = 0.0;
	};

	//! Symbologie prvku
	struct Symbology
	{
		int level = 1;        //!< hladina 1–63
		int color = 0;        //!< barva 0–255
		int weight = 0;       //!< tloušťka 0–31
		int style = 0;        //!< standardní styl čáry 0–7
		bool hasLineStyle = false; //!< vlastní styl čáry (linkage 0x79F9)
		int32_t lineStyleId = 0;   //!< id vlastního stylu (např. -1, -2, -3)
	};

	struct Element
	{
		Symbology sym;
		std::vector<Point2> pts; //!< 2 body = úsečka, více = lomená čára; u textu 1 bod = počátek

		// text (typ 17)
		bool isText = false;
		std::string text;        //!< jednobajtové kódování (cp1250)
		double textHeight = 0.2; //!< výška písma v hlavních jednotkách
		int font = 0;
		int justification = 0;   //!< 0 = vlevo dole

		// oblouk (typ 16): střed, poloměr, počáteční úhel a rozsah proti směru hodinových ručiček (°);
		// pts = body po oblouku (rozsah prvku, náhrada ve 3D výkresu)
		bool isArc = false;
		Point2 center;
		double radius = 0.0;
		double startDeg = 0.0;
		double sweepDeg = 0.0;
	};

	class Writer
	{
	public:
		//! Načte seed soubor (hlavičku výkresu). Vrací false a vyplní error při chybě.
		bool loadSeed( const std::string& path, std::string& error );

		//! Načte seed z paměti
		bool loadSeedBytes( const std::vector<uint8_t>& bytes, std::string& error );

		bool is3D() const { return m_is3D; }
		double uorPerMaster() const { return m_uorPerMaster; }

		//! Zapíše výkres: hlavička ze seedu + prvky. Souřadnice v hlavních jednotkách (m).
		bool write( const std::string& path, const std::vector<Element>& elements, std::string& error ) const;

		//! Zapíše jen do paměti (pro testy)
		bool writeBytes( std::vector<uint8_t>& out, const std::vector<Element>& elements, std::string& error ) const;

	private:
		bool toUOR( double v, int axis, int32_t& out ) const;
		bool appendElement( std::vector<uint8_t>& out, const Element& e, std::string& error ) const;
		bool appendText( std::vector<uint8_t>& out, const Element& e, std::string& error ) const;
		bool appendArc( std::vector<uint8_t>& out, const Element& e, std::string& error ) const;
		void appendLineStyle( std::vector<uint8_t>& out, const Symbology& sym ) const;

		std::vector<uint8_t> m_header; //!< negrafické prvky ze seedu
		bool m_is3D = false;
		double m_uorPerMaster = 10000.0;
		mutable double m_origin[3] = { 0.0, 0.0, 0.0 }; //!< globální počátek v UOR (při přetečení se posune)
		double m_seedOrigin[3] = { 0.0, 0.0, 0.0 };
	};

	//! Prvek načtený z výkresu V7 (souřadnice v hlavních jednotkách, s globálním počátkem)
	struct ReadItem
	{
		int type = 0;              //!< 3 úsečka, 4 lomená, 6 tvar, 15 elipsa, 16 oblouk, 17 text
		int level = 0;
		int color = 0;
		int weight = 0;
		int lineStyleId = 0;       //!< vlastní styl čáry (0 = není)
		std::vector<Point2> pts;   //!< vrcholy (úsečka, lomená, tvar uzavřený)
		bool isArc = false;        //!< kruhový oblouk / kružnice (sweep 360)
		Point2 center;
		double radius = 0.0;
		double startDeg = 0.0;     //!< proti směru hod. ručiček
		double sweepDeg = 0.0;     //!< + proti směru, − po směru
		bool isText = false;
		std::string text;          //!< bajty v kódování výkresu (cp1250)
		double textHeight = 0.0;
	};

	struct ReadStats
	{
		int total = 0;
		int used = 0;
		std::vector<std::pair<int, int>> skippedTypes; //!< (typ, počet)
		bool is3D = false;
	};

	//! Načte výkres V7 (2D i 3D – Z se ignoruje). Buňky (typ 2) se rozbalí, sdílené buňky, rastry a ostatní se přeskočí.
	bool readDesign( const std::vector<uint8_t>& bytes, std::vector<ReadItem>& out, ReadStats& stats, std::string& error );
}
