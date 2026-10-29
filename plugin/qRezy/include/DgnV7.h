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
		std::vector<Point2> pts; //!< 2 body = úsečka, více = lomená čára
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

		std::vector<uint8_t> m_header; //!< negrafické prvky ze seedu
		bool m_is3D = false;
		double m_uorPerMaster = 10000.0;
		double m_origin[3] = { 0.0, 0.0, 0.0 }; //!< globální počátek v UOR
	};
}
