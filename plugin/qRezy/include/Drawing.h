#pragma once

// Kreslicí hladiny a jejich symbologie pro výstup do MicroStation V7.
// Hladiny lze přidávat a mazat (manažer hladin); čára nese id hladiny v metadatech.
// Id 0–3 = původní pevné hladiny (Zdi, Překlady, Trámy, Drobné prvky) – staré projekty zůstávají čitelné.

#include "DgnV7.h"

#include <QColor>
#include <QString>

#include <array>
#include <vector>

namespace Drawing
{
	enum Category
	{
		Walls = 0,   //!< zdi
		Lintels = 1, //!< překlady
		Beams = 2,   //!< trámy
		Details = 3  //!< drobné prvky (okna, schody…)
	};

	struct Layer
	{
		int id = 0;
		QString name;
		QColor color;                           //!< barva na obrazovce
		std::array<DgnV7::Symbology, 2> sym;    //!< výstup V7: [0] plná, [1] zakrytá
	};

	//! aktuální seznam hladin (pořadí = F1, F2, …)
	const std::vector<Layer>& layers();
	//! uloží nový seznam hladin (QSettings qRezy/layers)
	void setLayers( const std::vector<Layer>& list );
	std::vector<Layer> defaultLayers();
	//! hladina podle id (nullptr = neexistuje)
	const Layer* layerById( int id );
	//! id hladiny na pozici (F1 = 0); -1 = není
	int layerIdAt( int index );
	int layerIndex( int id );
	int newLayerId();

	QString categoryName( int cat );

	//! barva čáry v CloudCompare (jen pro zobrazení)
	QColor displayColor( int cat, bool hidden );

	//! Symbologie pro výstup: snímek seznamu hladin
	struct SymbologyTable
	{
		std::vector<Layer> list;

		const DgnV7::Symbology& get( int cat, bool hidden ) const;

		static SymbologyTable load();
	};

	// klíče metadat u nakreslených čar
	constexpr const char* MetaCategory = "qRezy.category";
	constexpr const char* MetaHidden = "qRezy.hidden";
	constexpr const char* MetaView = "qRezy.view"; //!< id pohledu (0 = půdorys)
	constexpr const char* MetaArc = "qRezy.arc";   //!< oblouk (vrcholy po oblouku, lichý počet)
	constexpr const char* GroupName = "Výkres (Řezy)";
}
