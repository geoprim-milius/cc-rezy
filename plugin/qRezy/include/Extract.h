#pragma once
// výběr bodů mračna uvnitř poloprostorů (plátek řezu, výřez 3D náhledu) – rychlá kopie pro zobrazení

#include <ccDrawableObject.h>

#include <vector>

class ccPointCloud;

namespace Extract
{
	//! poloprostor a·x + b·y + c·z + d ≥ 0 (stejně jako ccClipPlane)
	using Plane = Tuple4Tpl<double>;

	//! kopie bodů uvnitř všech rovin (barvy a skalární pole se přenesou);
	//! cap > 0: nad tento počet se kopie rovnoměrně zředí (jen pojistka proti nedostatku paměti)
	//! total = počet bodů uvnitř (před případným zředěním); vrací nullptr při chybě / nedostatku paměti
	ccPointCloud* byPlanes( ccPointCloud* src, const std::vector<Plane>& planes, unsigned cap, unsigned& total );

	//! rovina posunutá ven o m (lokální jednotky) – výběr s rezervou
	inline Plane widened( const Plane& p, double m ) { return Plane( p.x, p.y, p.z, p.w + m ); }

	//! leží poloprostor q celý uvnitř p? (stejná normála, q užší)
	bool inside( const Plane& q, const Plane& p );
}
