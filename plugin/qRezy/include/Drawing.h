#pragma once

// Kreslicí hladiny (kategorie) a jejich symbologie pro výstup do MicroStation V7

#include "DgnV7.h"

#include <QColor>
#include <QString>

#include <array>

namespace Drawing
{
	constexpr int CategoryCount = 4;

	enum Category
	{
		Walls = 0,   //!< zdi
		Lintels = 1, //!< překlady
		Beams = 2,   //!< trámy
		Details = 3  //!< drobné prvky (okna, schody…)
	};

	QString categoryName( int cat );

	//! barva čáry v CloudCompare (jen pro zobrazení)
	QColor displayColor( int cat, bool hidden );

	//! Symbologie pro všechny kategorie × (plná, zakrytá)
	struct SymbologyTable
	{
		std::array<std::array<DgnV7::Symbology, 2>, CategoryCount> sym;

		const DgnV7::Symbology& get( int cat, bool hidden ) const { return sym[cat][hidden ? 1 : 0]; }
		DgnV7::Symbology& get( int cat, bool hidden ) { return sym[cat][hidden ? 1 : 0]; }

		static SymbologyTable defaults();
		static SymbologyTable load();   //!< z nastavení (QSettings), jinak výchozí
		void save() const;
	};

	// klíče metadat u nakreslených čar
	constexpr const char* MetaCategory = "qRezy.category";
	constexpr const char* MetaHidden = "qRezy.hidden";
	constexpr const char* MetaView = "qRezy.view"; //!< id pohledu (0 = půdorys)
	constexpr const char* GroupName = "Výkres (Řezy)";
}
