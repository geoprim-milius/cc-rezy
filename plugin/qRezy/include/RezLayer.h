#pragma once

// Vrstva popisků v okně: změřené body (značka + číslo + kód) a vložené texty.
// Kreslí se ve 2D průchodu (v popředí), poloha se promítá z 3D.

#include <ccHObject.h>

#include <QString>

#include <vector>

class RezLayer : public ccHObject
{
public:
	struct Item
	{
		CCVector3d P;        //!< poloha (lokální souřadnice mračna)
		CCVector3d up;       //!< směr „nahoru“ na obrazovce (pro výšku textu)
		QString text;
		double height = 0.0; //!< výška textu v lokálních jednotkách (0 = pevné písmo)
		bool marker = false; //!< značka bodu (křížek)
		ccColor::Rgb color = ccColor::Rgb( 255, 255, 255 );
	};

	RezLayer()
		: ccHObject( "Řezy – popisky" )
	{
		setVisible( true ); // prostý ccHObject je jinak „kontejner“ a nekreslí se
		setEnabled( true );
	}

	std::vector<Item> items;

protected:
	void drawMeOnly( CC_DRAW_CONTEXT& context ) override;
};
