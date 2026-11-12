#pragma once

// Čáry z mračna – hromadné hledání přímých úseků v bodech plátku (2D), rohy, obrys místnosti.
// Čisté výpočty bez závislosti na CloudCompare (kromě CCVector2d).

#include <CCGeom.h>

#include <vector>

namespace CloudLinesV15
{
	struct Seg
	{
		CCVector2d a;
		CCVector2d b;
	};

	struct Params
	{
		double tol = 0.02;    //!< tolerance bodů od přímky (místní jednotky)
		double gap = 0.10;    //!< největší mezera v bodech podél čáry
		double minLen = 0.30; //!< nejkratší úsek
		double scale = 1.0;   //!< místní jednotky na metr
		bool square = true;   //!< téměř kolmé / rovnoběžné srovnat k převládajícímu směru
	};

	//! všechny přímé úseky v bodech (body se nejdřív zředí na mřížku ~tol/2)
	std::vector<Seg> extract( const std::vector<CCVector2d>& pts, const Params& p );

	//! srovnat úseky do převládajícího směru (do 3°), otočení kolem středu
	void squareUp( std::vector<Seg>& segs );

	//! rohy mezi úseky: konce, jejichž průsečík leží do „reach“ od konce (L), nebo konec k jiné čáře (T)
	void joinCorners( std::vector<Seg>& segs, double reach );

	//! obrys místnosti kolem bodu „inside“: viditelné body z místa kliknutí → úseky → uzavřený obrys s rohy.
	//! openRays = podíl paprsků, které nenarazily na zeď (místnost není uzavřená)
	std::vector<Seg> room( const std::vector<CCVector2d>& pts, const CCVector2d& inside, const Params& p, double& openRays );

	//! průsečík dvou přímek (false = rovnoběžné)
	bool intersect( const CCVector2d& a1, const CCVector2d& b1, const CCVector2d& a2, const CCVector2d& b2, CCVector2d& I );
}
