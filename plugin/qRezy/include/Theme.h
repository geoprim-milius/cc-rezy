#pragma once

// Vzhled pluginu (a celého CloudCompare): světlý (původní), AutoCAD tmavý, Geoprim.
// Uloženo v QSettings qRezy/theme ("" = světlý, "acad", "geoprim"),
// pozadí okna s mračnem qRezy/darkBackground.

#include <QString>

class ccMainAppInterface;

namespace Theme
{
	QString current();
	//! nastaví vzhled celé aplikace (stylesheet, písmo); ikony je pak potřeba obnovit
	void apply( const QString& theme );
	//! pozadí 3D okna: tmavé (podle vzhledu) / světlé
	void applyBackground( bool dark, ccMainAppInterface* app );
	bool darkBackground();
}
