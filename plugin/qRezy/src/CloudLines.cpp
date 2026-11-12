#include "CloudLines.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <unordered_map>

namespace CloudLines
{
	namespace
	{
		constexpr double Pi = 3.14159265358979323846;
		constexpr double Deg = Pi / 180.0;

		inline double dist( const CCVector2d& a, const CCVector2d& b )
		{
			return ( a - b ).norm();
		}

		inline int64_t key( int64_t ix, int64_t iy )
		{
			return ( ix << 32 ) ^ ( iy & 0xffffffffLL );
		}

		double wrapPi( double a )
		{
			while ( a > Pi )
			{
				a -= 2.0 * Pi;
			}
			while ( a <= -Pi )
			{
				a += 2.0 * Pi;
			}
			return a;
		}

		//! PCA přímky přes body: střed, směr, rozptyl kolmo (λmin / n)
		bool fit( const std::vector<CCVector2d>& q, const std::vector<int>& idx, CCVector2d& m, CCVector2d& D, double& perpVar )
		{
			if ( idx.size() < 2 )
			{
				return false;
			}
			m = CCVector2d( 0, 0 );
			for ( int i : idx )
			{
				m += q[i];
			}
			m /= static_cast<double>( idx.size() );
			double sxx = 0.0;
			double sxy = 0.0;
			double syy = 0.0;
			for ( int i : idx )
			{
				const CCVector2d d = q[i] - m;
				sxx += d.x * d.x;
				sxy += d.x * d.y;
				syy += d.y * d.y;
			}
			const double ang = 0.5 * std::atan2( 2.0 * sxy, sxx - syy );
			D = CCVector2d( std::cos( ang ), std::sin( ang ) );
			const double tr = 0.5 * ( sxx + syy );
			const double rt = std::sqrt( 0.25 * ( sxx - syy ) * ( sxx - syy ) + sxy * sxy );
			perpVar = std::max( 0.0, tr - rt ) / static_cast<double>( idx.size() );
			return true;
		}

		//! prostorová mřížka indexů bodů
		struct Hash
		{
			double h = 1.0;
			std::unordered_map<int64_t, std::vector<int>> cells;

			void build( const std::vector<CCVector2d>& q, double cell )
			{
				h = cell;
				cells.clear();
				cells.reserve( q.size() / 4 + 16 );
				for ( int i = 0; i < static_cast<int>( q.size() ); ++i )
				{
					cells[key( static_cast<int64_t>( std::floor( q[i].x / h ) ), static_cast<int64_t>( std::floor( q[i].y / h ) ) )].push_back( i );
				}
			}

			template <class F>
			void around( const CCVector2d& P, int r, F&& f ) const
			{
				const int64_t cx = static_cast<int64_t>( std::floor( P.x / h ) );
				const int64_t cy = static_cast<int64_t>( std::floor( P.y / h ) );
				for ( int64_t dx = -r; dx <= r; ++dx )
				{
					for ( int64_t dy = -r; dy <= r; ++dy )
					{
						auto it = cells.find( key( cx + dx, cy + dy ) );
						if ( it != cells.end() )
						{
							for ( int i : it->second )
							{
								f( i );
							}
						}
					}
				}
			}
		};

		//! body zředěné na mřížku (těžiště buněk)
		std::vector<CCVector2d> thin( const std::vector<CCVector2d>& pts, double cell )
		{
			std::unordered_map<int64_t, std::pair<CCVector2d, int>> m;
			m.reserve( pts.size() / 2 + 16 );
			for ( const CCVector2d& P : pts )
			{
				auto& c = m[key( static_cast<int64_t>( std::floor( P.x / cell ) ), static_cast<int64_t>( std::floor( P.y / cell ) ) )];
				c.first += P;
				++c.second;
			}
			std::vector<CCVector2d> q;
			q.reserve( m.size() );
			for ( const auto& kv : m )
			{
				q.push_back( kv.second.first / static_cast<double>( kv.second.second ) );
			}
			// pevné pořadí (opakovatelný výsledek)
			std::sort( q.begin(), q.end(), []( const CCVector2d& a, const CCVector2d& b ) { return a.x < b.x || ( a.x == b.x && a.y < b.y ); } );
			return q;
		}
	}

	bool intersect( const CCVector2d& a1, const CCVector2d& b1, const CCVector2d& a2, const CCVector2d& b2, CCVector2d& I )
	{
		const CCVector2d d1 = b1 - a1;
		const CCVector2d d2 = b2 - a2;
		const double den = d1.x * d2.y - d1.y * d2.x;
		if ( std::abs( den ) < 1e-12 * std::max( 1e-12, d1.norm() * d2.norm() ) )
		{
			return false;
		}
		const CCVector2d w = a2 - a1;
		const double s = ( w.x * d2.y - w.y * d2.x ) / den;
		I = a1 + d1 * s;
		return true;
	}


	std::vector<Seg> extract( const std::vector<CCVector2d>& pts, const Params& p )
	{
		std::vector<Seg> out;
		const double sc = p.scale;
		const double tol = std::max( 0.002 * sc, p.tol );
		const double gap = std::max( 0.01 * sc, p.gap );
		const double cell = std::max( 0.005 * sc, 0.5 * tol );
		const std::vector<CCVector2d> q = thin( pts, cell );
		if ( q.size() < 10 )
		{
			return out;
		}
		Hash H;
		H.build( q, std::max( 0.10 * sc, 4.0 * tol ) );
		const int n = static_cast<int>( q.size() );
		std::vector<char> used( n, 0 );
		std::vector<int> stamp( n, 0 );
		int cur = 0;

		// body v pásu kolem přímky (P0, D) mezi t0 a t1 (all = i už použité)
		auto band = [&]( const CCVector2d& P0, const CCVector2d& D, double t0, double t1, double tolB, std::vector<int>& res, bool all )
		{
			res.clear();
			++cur;
			const CCVector2d N( -D.y, D.x );
			const double step = 0.5 * H.h;
			for ( double t = t0;; t += step )
			{
				const double tt = std::min( t, t1 );
				H.around( P0 + D * tt, 1, [&]( int i )
				{
					if ( ( used[i] && !all ) || stamp[i] == cur )
					{
						return;
					}
					stamp[i] = cur;
					const CCVector2d d = q[i] - P0;
					const double ti = d.dot( D );
					if ( ti >= t0 && ti <= t1 && std::abs( d.dot( N ) ) <= tolB )
					{
						res.push_back( i );
					}
				} );
				if ( tt >= t1 )
				{
					break;
				}
			}
		};

		std::vector<int> nb;
		std::vector<int> win;
		std::vector<int> mem;
		// nejdřív dlouhé líce (okolí 15 cm), potom krátké hrany ústupků (6 a 3,5 cm) – jen když je povolená krátká délka
		const double radii[3] = { std::max( 0.15 * sc, 5.0 * cell ), 0.06 * sc, 0.035 * sc };
		const int phases = p.minLen < 0.25 * sc ? 3 : 1;
		for ( int phase = 0; phase < phases; ++phase )
		{
			const double r = radii[phase];
			const double rmsMax = std::min( tol, r / 6.0 );
			const size_t nbMin = phase == 0 ? 6 : 5;
			for ( int s = 0; s < n; ++s )
			{
				if ( used[s] )
				{
					continue;
				}
				nb.clear();
				H.around( q[s], 2, [&]( int i )
				{
					if ( !used[i] && dist( q[i], q[s] ) <= r )
					{
						nb.push_back( i );
					}
				} );
				if ( nb.size() < nbMin )
				{
					continue;
				}
				CCVector2d P0;
				CCVector2d D;
				double pv = 0.0;
				if ( !fit( q, nb, P0, D, pv ) || std::sqrt( pv ) > rmsMax )
				{
					continue; // okolí není přímka (roh, nábytek, šum)
				}
				double tMin = -r;
				double tMax = r;
				bool ok = true;
				for ( int pass = 0; pass < 20 && ok; ++pass )
				{
					const double prevMin = tMin;
					const double prevMax = tMax;
					const CCVector2d prevP = P0;
					// růst podél přímky, dokud mezera nepřesáhne „gap“
					for ( int dir = 0; dir < 2; ++dir )
					{
						for ( int guard = 0; guard < 100000; ++guard )
						{
							if ( dir == 0 )
								band( P0, D, tMax, tMax + gap, tol, win, false );
							else
								band( P0, D, tMin - gap, tMin, tol, win, false );
							double best = dir == 0 ? tMax : tMin;
							for ( int i : win )
							{
								const double t = ( q[i] - P0 ).dot( D );
								best = dir == 0 ? std::max( best, t ) : std::min( best, t );
							}
							if ( dir == 0 ? best <= tMax + 1e-9 : best >= tMin - 1e-9 )
							{
								break;
							}
							( dir == 0 ? tMax : tMin ) = best;
						}
					}
					band( P0, D, tMin, tMax, tol, mem, false );
					if ( mem.size() < nbMin )
					{
						ok = false;
						break;
					}
					CCVector2d m;
					CCVector2d Dn;
					if ( !fit( q, mem, m, Dn, pv ) )
					{
						ok = false;
						break;
					}
					if ( Dn.dot( D ) < 0 )
					{
						Dn = Dn * -1.0;
					}
					P0 = m;
					D = Dn;
					tMin = 1e300;
					tMax = -1e300;
					for ( int i : mem )
					{
						const double t = ( q[i] - P0 ).dot( D );
						tMin = std::min( tMin, t );
						tMax = std::max( tMax, t );
					}
					// dokud se úsek prodlužuje (zpřesněný směr dosáhne dál), opakovat
					const double shift = ( P0 - prevP ).dot( D );
					if ( std::abs( ( tMin + shift ) - prevMin ) < 0.2 * cell && std::abs( ( tMax + shift ) - prevMax ) < 0.2 * cell )
					{
						break;
					}
				}
				if ( !ok )
				{
					continue;
				}
				const double L = tMax - tMin;
				// hustota: aspoň 30 % délky pokryto body (zředěné po „cell“)
				if ( L < p.minLen || static_cast<double>( mem.size() ) < std::max( static_cast<double>( nbMin ), 0.3 * L / cell ) )
				{
					continue;
				}
				// souvislost: body musí pokrývat aspoň 60 % délky (po 2 buňkách) – řídký rozptyl není zeď
				{
					const double bin = 2.0 * cell;
					const int nb2 = std::max( 1, static_cast<int>( L / bin ) + 1 );
					std::vector<char> hit( nb2, 0 );
					for ( int i : mem )
					{
						const int b = static_cast<int>( ( ( q[i] - P0 ).dot( D ) - tMin ) / bin );
						hit[std::max( 0, std::min( nb2 - 1, b ) )] = 1;
					}
					int cov = 0;
					for ( char h : hit )
					{
						cov += h;
					}
					if ( cov < 0.6 * nb2 )
					{
						continue;
					}
				}
				// líc zdi je tenká čára bodů – shluk (nábytek, zeleň) má body i vedle: v pásu 2,5·tol musí být většina u přímky
				const size_t nIn = mem.size();
				band( P0, D, tMin, tMax, 2.5 * tol, win, false );
				if ( static_cast<double>( nIn ) < 0.55 * static_cast<double>( win.size() ) )
				{
					continue;
				}
				bool clean = true;
				if ( p.strict )
				{
					// čistý líc: v pásu ±6 cm smí být mimo čáru nejvýš ~40 % bodů (jinak fasáda se šumem, rámy oken, nábytek)
					band( P0, D, tMin, tMax, std::max( 3.0 * tol, 0.06 * sc ), win, true );
					clean = static_cast<double>( nIn ) >= 0.62 * static_cast<double>( win.size() );
				}
				out.push_back( { P0 + D * tMin, P0 + D * tMax, clean, 0 } );
				// spotřebovat body líce (i trochu širší pás – ať nevznikne zdvojená čára)
				band( P0, D, tMin - tol, tMax + tol, 2.0 * tol, mem, false );
				for ( int i : mem )
				{
					used[i] = 1;
				}
			}
		}
		std::sort( out.begin(), out.end(), []( const Seg& x, const Seg& y ) { return dist( x.a, x.b ) > dist( y.a, y.b ); } );
		return out;
	}

	void squareUp( std::vector<Seg>& segs, double maxShift )
	{
		if ( segs.empty() )
		{
			return;
		}
		// převládající směr (mod 90°) vážený délkou
		double hist[90] = {};
		for ( const Seg& s : segs )
		{
			double a = std::atan2( s.b.y - s.a.y, s.b.x - s.a.x ) / Deg;
			a = std::fmod( a, 90.0 );
			if ( a < 0 )
			{
				a += 90.0;
			}
			hist[static_cast<int>( a ) % 90] += dist( s.a, s.b );
		}
		int best = 0;
		double bestW = -1.0;
		for ( int b = 0; b < 90; ++b )
		{
			const double w = hist[b] + 0.5 * ( hist[( b + 89 ) % 90] + hist[( b + 1 ) % 90] );
			if ( w > bestW )
			{
				bestW = w;
				best = b;
			}
		}
		// zpřesnit: vážený průměr úhlů do ±1,5° od vrcholu
		double sw = 0.0;
		double sa = 0.0;
		for ( const Seg& s : segs )
		{
			double a = std::atan2( s.b.y - s.a.y, s.b.x - s.a.x ) / Deg;
			double d = std::fmod( a - ( best + 0.5 ), 90.0 );
			if ( d > 45.0 )
				d -= 90.0;
			if ( d < -45.0 )
				d += 90.0;
			if ( std::abs( d ) <= 1.5 )
			{
				const double w = dist( s.a, s.b );
				sw += w;
				sa += w * d;
			}
		}
		const double theta0 = ( best + 0.5 + ( sw > 0 ? sa / sw : 0.0 ) ) * Deg;
		for ( Seg& s : segs )
		{
			const double a = std::atan2( s.b.y - s.a.y, s.b.x - s.a.x );
			double d = std::fmod( a - theta0, Pi / 2 );
			if ( d > Pi / 4 )
				d -= Pi / 2;
			if ( d < -Pi / 4 )
				d += Pi / 2;
			const double half = 0.5 * dist( s.a, s.b );
			// jen když se tím konce neodsunou od bodů (dlouhá zeď v jiném úhlu zůstane, jak je)
			if ( std::abs( d ) < 3.0 * Deg && std::abs( d ) > 1e-12 && half * std::abs( std::sin( d ) ) <= maxShift )
			{
				const CCVector2d m = ( s.a + s.b ) * 0.5;
				const double na = a - d;
				const CCVector2d u( std::cos( na ), std::sin( na ) );
				s.a = m - u * half;
				s.b = m + u * half;
			}
		}
	}

	std::vector<char> joinCorners( std::vector<Seg>& segs, double reach, double scale )
	{
		struct Cand
		{
			double score;
			int i;
			int e;
			int j;
			int je; //!< -1 = T (čára j se nemění)
			CCVector2d I;
		};
		std::vector<Cand> cands;
		const int n = static_cast<int>( segs.size() );
		const double cos20 = std::cos( 20.0 * Deg );
		// krátká hrana nesmí „skákat“ daleko: dosah konce nejvýš délka úseku
		auto reachOf = [&]( double L ) { return std::min( reach, std::max( 0.08 * scale, L ) ); };
		for ( int i = 0; i < n; ++i )
		{
			const CCVector2d ai = segs[i].a;
			const CCVector2d bi = segs[i].b;
			const double Li = dist( ai, bi );
			if ( Li < 1e-9 )
			{
				continue;
			}
			const CCVector2d Di = ( bi - ai ) / Li;
			const double ri = reachOf( Li );
			for ( int j = 0; j < n; ++j )
			{
				if ( j == i )
				{
					continue;
				}
				const CCVector2d aj = segs[j].a;
				const CCVector2d bj = segs[j].b;
				const double Lj = dist( aj, bj );
				if ( Lj < 1e-9 )
				{
					continue;
				}
				const CCVector2d Dj = ( bj - aj ) / Lj;
				if ( std::abs( Di.dot( Dj ) ) > cos20 )
				{
					continue;
				}
				CCVector2d I;
				if ( !intersect( ai, bi, aj, bj, I ) )
				{
					continue;
				}
				const double si = ( I - ai ).dot( Di ) / Li;
				const double tj = ( I - aj ).dot( Dj ) / Lj;
				const double rj = reachOf( Lj );
				for ( int e = 0; e < 2; ++e )
				{
					const CCVector2d E = e == 0 ? ai : bi;
					const double dE = dist( I, E );
					if ( dE > ri || ( e == 0 ? si > 0.5 : si < 0.5 ) )
					{
						continue;
					}
					const double dA = dist( I, aj );
					const double dB = dist( I, bj );
					const int je = dA <= dB ? 0 : 1;
					if ( std::min( dA, dB ) <= rj && ( je == 0 ? tj < 0.5 : tj > 0.5 ) )
					{
						cands.push_back( { dE + std::min( dA, dB ), i, e, j, je, I } );
					}
					else if ( tj > 0.0 && tj < 1.0 )
					{
						cands.push_back( { dE + reach, i, e, j, -1, I } ); // T: konec k čáře
					}
				}
			}
		}
		std::sort( cands.begin(), cands.end(), []( const Cand& x, const Cand& y ) { return x.score < y.score; } );
		std::vector<char> done( 2 * segs.size(), 0 );
		std::vector<CCVector2d> at( 2 * segs.size() );
		for ( const Cand& c : cands )
		{
			if ( done[2 * c.i + c.e] )
			{
				continue;
			}
			if ( c.je >= 0 && done[2 * c.j + c.je] && dist( at[2 * c.j + c.je], c.I ) > 1e-6 )
			{
				continue; // druhý konec už je v jiném rohu
			}
			( c.e == 0 ? segs[c.i].a : segs[c.i].b ) = c.I;
			done[2 * c.i + c.e] = 1;
			at[2 * c.i + c.e] = c.I;
			if ( c.je >= 0 )
			{
				( c.je == 0 ? segs[c.j].a : segs[c.j].b ) = c.I;
				done[2 * c.j + c.je] = 1;
				at[2 * c.j + c.je] = c.I;
			}
		}
		return done;
	}

	std::vector<Seg> batch( const std::vector<CCVector2d>& pts, const Params& p )
	{
		// Výsledek má tři stupně jistoty (Seg::cls) a potvrzuje se po fázích:
		//  0 = čisté líce, které tvoří uzavřené obrysy (místnosti) – jisté
		//  1 = čisté líce navazující na ostatní, ale s volným koncem
		//  2 = nejisté: osamělé čáry, svazky čar těsně u sebe, zašuměné líce, pahýly
		const double sc = p.scale;
		Params pe = p;
		pe.strict = true;
		pe.minLen = std::min( p.minLen, 0.06 * sc ); // i krátké hrany ústupků – zůstanou jen ty, které spojují dvě zdi
		std::vector<Seg> all = extract( pts, pe );
		if ( p.square )
		{
			squareUp( all, std::max( p.tol, 0.015 * sc ) );
		}
		auto len = []( const Seg& g ) { return dist( g.a, g.b ); };
		const int nAll = static_cast<int>( all.size() );
		// svazky: dvě skoro rovnoběžné čáry těsně vedle sebe (do 9 cm), které se podélně překrývají → nejisté obě
		std::vector<char> good( nAll, 1 );
		for ( int i = 0; i < nAll; ++i )
		{
			const double Li = len( all[i] );
			if ( Li < 1e-9 || !all[i].clean )
			{
				good[i] = 0;
			}
			if ( Li < 1e-9 )
			{
				continue;
			}
			const CCVector2d Di = ( all[i].b - all[i].a ) / Li;
			const CCVector2d Ni( -Di.y, Di.x );
			for ( int j = i + 1; j < nAll; ++j )
			{
				const double Lj = len( all[j] );
				if ( Lj < 1e-9 || std::abs( Di.dot( all[j].b - all[j].a ) ) / Lj < std::cos( 6.0 * Deg ) )
				{
					continue;
				}
				const double ta = ( all[j].a - all[i].a ).dot( Di );
				const double tb = ( all[j].b - all[i].a ).dot( Di );
				const double ov = std::min( Li, std::max( ta, tb ) ) - std::max( 0.0, std::min( ta, tb ) );
				if ( ov < 0.3 * std::min( Li, Lj ) || ov < 0.05 * sc )
				{
					continue;
				}
				if ( std::abs( ( ( all[j].a + all[j].b ) * 0.5 - all[i].a ).dot( Ni ) ) < 0.09 * sc )
				{
					// kratší z dvojice je vždy nejistá; delší jen když jsou srovnatelné (překryv přes půlku delší)
					( Li < Lj ? good[i] : good[j] ) = 0;
					if ( ov > 0.5 * std::max( Li, Lj ) )
					{
						good[i] = 0;
						good[j] = 0;
					}
				}
			}
		}
		auto onSeg = [&]( const CCVector2d& P, const Seg& g )
		{
			const CCVector2d d = g.b - g.a;
			const double L2 = d.dot( d );
			if ( L2 < 1e-18 )
			{
				return false;
			}
			const double t = std::max( 0.0, std::min( 1.0, ( P - g.a ).dot( d ) / L2 ) );
			return dist( g.a + d * t, P ) < 1e-6 * sc + 1e-9;
		};
		// konec čáry i navazuje na jinou čáru z množiny (roh nebo T)
		auto touches = [&]( const std::vector<Seg>& v, const std::vector<char>& in, int i, int e )
		{
			const CCVector2d E = e == 0 ? v[i].a : v[i].b;
			for ( int k = 0; k < static_cast<int>( v.size() ); ++k )
			{
				if ( k != i && in[k] && onSeg( E, v[k] ) )
				{
					return true;
				}
			}
			return false;
		};
		// rohy + kolmé spojky odskoků na dané množině čar; vrací geometrii (původní čáry na stejných indexech,
		// spojky za nimi)
		auto build = [&]( const std::vector<Seg>& src )
		{
			std::vector<Seg> g = src;
			const size_t n = g.size();
			std::vector<char> joined = joinCorners( g, 0.6 * sc, sc );
			for ( size_t i = 0; i < n; ++i )
			{
				if ( len( src[i] ) < p.minLen )
				{
					continue;
				}
				for ( int e = 0; e < 2; ++e )
				{
					if ( joined[2 * i + e] )
					{
						continue;
					}
					const CCVector2d E = e == 0 ? g[i].a : g[i].b;
					const CCVector2d Di = ( g[i].b - g[i].a ) / len( g[i] );
					const CCVector2d Ni( -Di.y, Di.x );
					int bj = -1;
					int bf = 0;
					double bd = 0.35 * sc;
					for ( size_t j = i + 1; j < n; ++j )
					{
						if ( len( src[j] ) < p.minLen )
						{
							continue;
						}
						const CCVector2d Dj = ( g[j].b - g[j].a ) / len( g[j] );
						if ( std::abs( Di.dot( Dj ) ) < std::cos( 3.0 * Deg ) )
						{
							continue;
						}
						for ( int f = 0; f < 2; ++f )
						{
							if ( joined[2 * j + f] )
							{
								continue;
							}
							const CCVector2d F = f == 0 ? g[j].a : g[j].b;
							const double off = std::abs( ( F - E ).dot( Ni ) );
							const double along = ( F - E ).dot( Di ) * ( e == 0 ? -1.0 : 1.0 ); // kladné = za koncem
							if ( off > 2.0 * p.tol && off < 0.3 * sc && along > -0.05 * sc && dist( E, F ) < bd )
							{
								bd = dist( E, F );
								bj = static_cast<int>( j );
								bf = f;
							}
						}
					}
					if ( bj < 0 )
					{
						continue;
					}
					CCVector2d& F = bf == 0 ? g[bj].a : g[bj].b;
					CCVector2d& E2 = e == 0 ? g[i].a : g[i].b;
					const double tm = 0.5 * ( F - E2 ).dot( Di );
					const CCVector2d P = E2 + Di * tm;                      // nový konec čáry i
					const CCVector2d Q = P + Ni * ( ( F - E2 ).dot( Ni ) ); // nový konec čáry j
					E2 = P;
					F = Q;
					joined[2 * i + e] = 1;
					joined[2 * bj + bf] = 1;
					g.push_back( { P, Q, true, 0 } ); // kolmá spojka odskoku
				}
			}
			return g;
		};
		// navazující skupiny: nechat jen čáry ve skupině (aspoň 2 čáry a 2,5 m), ne osamělé a ne pahýly;
		// krátké hrany jen když spojují dvě čáry. Opakovat, dokud se něco vyhazuje.
		std::vector<Seg> segs;
		std::vector<Seg> rest;
		for ( int i = 0; i < nAll; ++i )
		{
			( good[i] ? segs : rest ).push_back( all[i] );
		}
		std::vector<Seg> geo;
		for ( int round = 0; round < 8; ++round )
		{
			geo = build( segs );
			const int n0 = static_cast<int>( segs.size() );
			const int ng = static_cast<int>( geo.size() );
			std::vector<int> comp( ng );
			for ( int i = 0; i < ng; ++i )
			{
				comp[i] = i;
			}
			auto find = [&]( int x )
			{
				while ( comp[x] != x )
				{
					x = comp[x] = comp[comp[x]];
				}
				return x;
			};
			std::vector<char> je( 2 * ng, 0 );
			for ( int i = 0; i < ng; ++i )
			{
				for ( int e = 0; e < 2; ++e )
				{
					const CCVector2d E = e == 0 ? geo[i].a : geo[i].b;
					for ( int k = 0; k < ng; ++k )
					{
						if ( k != i && onSeg( E, geo[k] ) )
						{
							je[2 * i + e] = 1;
							comp[find( i )] = find( k );
						}
					}
				}
			}
			std::vector<double> total( ng, 0.0 );
			std::vector<int> count( ng, 0 );
			for ( int i = 0; i < ng; ++i )
			{
				total[find( i )] += len( geo[i] );
				++count[find( i )];
			}
			std::vector<Seg> keep;
			for ( int i = 0; i < n0; ++i )
			{
				const int c = find( i );
				const double L = len( segs[i] );
				const bool both = je[2 * i] && je[2 * i + 1];
				const bool lonely = !je[2 * i] && !je[2 * i + 1];
				const bool stub = !both && L < std::max( p.minLen, 0.6 * sc ); // výběžek, kousek shluku, volná krátká hrana
				if ( count[c] >= 2 && total[c] >= 2.5 * sc && !lonely && !stub )
				{
					keep.push_back( segs[i] );
				}
				else
				{
					rest.push_back( segs[i] );
				}
			}
			const bool same = keep.size() == segs.size();
			segs.swap( keep );
			if ( same )
			{
				break;
			}
		}
		segs = geo; // konečná geometrie (rohy, spojky)
		// jisté = uzavřené obrysy: postupně odebírat čáry s volným koncem, co zbude, je uzavřené
		const int nk = static_cast<int>( segs.size() );
		std::vector<char> core( nk, 1 );
		for ( bool changed = true; changed; )
		{
			changed = false;
			for ( int i = 0; i < nk; ++i )
			{
				if ( core[i] && ( !touches( segs, core, i, 0 ) || !touches( segs, core, i, 1 ) ) )
				{
					core[i] = 0;
					changed = true;
				}
			}
		}
		std::vector<Seg> out;
		for ( int i = 0; i < nk; ++i )
		{
			segs[i].cls = core[i] ? 0 : 1;
			out.push_back( segs[i] );
		}
		for ( Seg& g : rest )
		{
			if ( len( g ) >= p.minLen )
			{
				g.cls = 2;
				out.push_back( g );
			}
		}
		return out;
	}

	std::vector<Seg> room( const std::vector<CCVector2d>& pts, const CCVector2d& inside, const Params& p, double& openRays )
	{
		openRays = 1.0;
		std::vector<Seg> none;
		const double sc = p.scale;
		const double g = 0.02 * sc;
		// mřížka přes body v dosahu 25 m
		const double R = 25.0 * sc;
		double x0 = inside.x;
		double x1 = inside.x;
		double y0 = inside.y;
		double y1 = inside.y;
		for ( const CCVector2d& P : pts )
		{
			if ( std::abs( P.x - inside.x ) <= R && std::abs( P.y - inside.y ) <= R )
			{
				x0 = std::min( x0, P.x );
				x1 = std::max( x1, P.x );
				y0 = std::min( y0, P.y );
				y1 = std::max( y1, P.y );
			}
		}
		const double margin = 2.0 * sc;
		const CCVector2d O( x0 - margin, y0 - margin );
		const int nx = static_cast<int>( ( x1 - x0 + 2.0 * margin ) / g ) + 1;
		const int ny = static_cast<int>( ( y1 - y0 + 2.0 * margin ) / g ) + 1;
		if ( nx < 8 || ny < 8 || static_cast<double>( nx ) * ny > 4.0e7 )
		{
			return none;
		}
		const size_t N = static_cast<size_t>( nx ) * ny;
		auto at = [nx]( int x, int y ) { return static_cast<size_t>( y ) * nx + x; };
		auto cellOf = [&]( const CCVector2d& P, int& ix, int& iy ) -> bool
		{
			ix = static_cast<int>( std::floor( ( P.x - O.x ) / g ) );
			iy = static_cast<int>( std::floor( ( P.y - O.y ) / g ) );
			return ix >= 0 && iy >= 0 && ix < nx && iy < ny;
		};
		// obsazenost (rozšířená o buňku – líc bez děr)
		std::vector<uint8_t> occ( N, 0 );
		{
			std::vector<uint8_t> raw( N, 0 );
			for ( const CCVector2d& P : pts )
			{
				int ix;
				int iy;
				if ( cellOf( P, ix, iy ) )
				{
					raw[at( ix, iy )] = 1;
				}
			}
			for ( int y = 0; y < ny; ++y )
			{
				for ( int x = 0; x < nx; ++x )
				{
					if ( !raw[at( x, y )] )
					{
						continue;
					}
					for ( int dy = -1; dy <= 1; ++dy )
					{
						for ( int dx = -1; dx <= 1; ++dx )
						{
							const int xx = x + dx;
							const int yy = y + dy;
							if ( xx >= 0 && yy >= 0 && xx < nx && yy < ny )
							{
								occ[at( xx, yy )] = 1;
							}
						}
					}
				}
			}
		}
		// vzdálenost k nejbližší zdi (chamfer 3–4, v třetinách buňky)
		std::vector<uint16_t> dt( N, 60000 );
		for ( size_t i = 0; i < N; ++i )
		{
			if ( occ[i] )
			{
				dt[i] = 0;
			}
		}
		auto relax = [&]( int x, int y, int dx, int dy, int w )
		{
			const int xx = x + dx;
			const int yy = y + dy;
			if ( xx >= 0 && yy >= 0 && xx < nx && yy < ny )
			{
				const int v = dt[at( xx, yy )] + w;
				if ( v < dt[at( x, y )] )
				{
					dt[at( x, y )] = static_cast<uint16_t>( v );
				}
			}
		};
		for ( int y = 0; y < ny; ++y )
		{
			for ( int x = 0; x < nx; ++x )
			{
				relax( x, y, -1, 0, 3 );
				relax( x, y, 0, -1, 3 );
				relax( x, y, -1, -1, 4 );
				relax( x, y, 1, -1, 4 );
			}
		}
		for ( int y = ny - 1; y >= 0; --y )
		{
			for ( int x = nx - 1; x >= 0; --x )
			{
				relax( x, y, 1, 0, 3 );
				relax( x, y, 0, 1, 3 );
				relax( x, y, 1, 1, 4 );
				relax( x, y, -1, 1, 4 );
			}
		}
		int cx;
		int cy;
		if ( !cellOf( inside, cx, cy ) )
		{
			return none;
		}
		// „rozlití“ volné plochy od kliknutí kruhem o poloměru D: otvory užší než 2·D (dveře, okna) neprojde
		std::vector<uint8_t> core;
		std::vector<int> queue;
		auto flood = [&]( double D, bool& leak ) -> bool
		{
			const int need = static_cast<int>( 3.0 * D / g );
			if ( dt[at( cx, cy )] < need )
			{
				return false; // kliknutí je blíž ke zdi než D
			}
			core.assign( N, 0 );
			queue.clear();
			queue.push_back( static_cast<int>( at( cx, cy ) ) );
			core[at( cx, cy )] = 1;
			leak = false;
			for ( size_t h = 0; h < queue.size(); ++h )
			{
				const int x = queue[h] % nx;
				const int y = queue[h] / nx;
				if ( x == 0 || y == 0 || x == nx - 1 || y == ny - 1 )
				{
					leak = true; // plocha utekla ven z budovy
					continue;
				}
				const int nbx[4] = { x - 1, x + 1, x, x };
				const int nby[4] = { y, y, y - 1, y + 1 };
				for ( int k = 0; k < 4; ++k )
				{
					const size_t i = at( nbx[k], nby[k] );
					if ( !core[i] && dt[i] >= need )
					{
						core[i] = 1;
						queue.push_back( static_cast<int>( i ) );
					}
				}
			}
			return true;
		};
		double D = 0.0;
		bool leak = true;
		{
			const double tryD[5] = { 0.5 * sc, 0.35 * sc, 0.25 * sc, 0.15 * sc, 0.08 * sc };
			bool found = false;
			for ( double d : tryD )
			{
				if ( flood( d, leak ) )
				{
					D = d;
					found = true;
					break;
				}
			}
			if ( !found )
			{
				return none;
			}
			if ( leak )
			{
				// uteklo otvorem ven – zkusit větší kruh (zavře i širší otvory)
				const double bigger[3] = { 0.7 * sc, 1.0 * sc, 1.5 * sc };
				for ( double d : bigger )
				{
					bool l2 = true;
					if ( d > D && flood( d, l2 ) && !l2 )
					{
						D = d;
						leak = false;
						break;
					}
				}
				if ( leak )
				{
					flood( D, leak ); // zůstat u původního (hlášení: místnost není uzavřená)
				}
			}
		}
		// dorůst zpět ke zdem (do rohů a výklenků) – nejvýš o D, jen volnými buňkami
		std::vector<uint8_t> reg = core;
		{
			std::vector<int> frontier = queue;
			std::vector<int> next;
			const int steps = static_cast<int>( D / g ) + 2;
			for ( int s = 0; s < steps && !frontier.empty(); ++s )
			{
				next.clear();
				for ( int c : frontier )
				{
					const int x = c % nx;
					const int y = c / nx;
					for ( int dy = -1; dy <= 1; ++dy )
					{
						for ( int dx = -1; dx <= 1; ++dx )
						{
							const int xx = x + dx;
							const int yy = y + dy;
							if ( xx < 0 || yy < 0 || xx >= nx || yy >= ny )
							{
								continue;
							}
							const size_t i = at( xx, yy );
							if ( !reg[i] && !occ[i] )
							{
								reg[i] = 1;
								next.push_back( static_cast<int>( i ) );
							}
						}
					}
				}
				frontier.swap( next );
			}
		}
		// vnější obrys plochy (Moore) – pořadí buněk podél zdí
		std::vector<int> contour;
		{
			int sx = -1;
			int sy = -1;
			for ( int y = 0; y < ny && sx < 0; ++y )
			{
				for ( int x = 0; x < nx; ++x )
				{
					if ( reg[at( x, y )] )
					{
						sx = x;
						sy = y;
						break;
					}
				}
			}
			if ( sx < 0 )
			{
				return none;
			}
			// 8 směrů po směru hodinek (osa y nahoru): Z, SZ, S, SV, V, JV, J, JZ
			const int ddx[8] = { -1, -1, 0, 1, 1, 1, 0, -1 };
			const int ddy[8] = { 0, 1, 1, 1, 0, -1, -1, -1 };
			auto in = [&]( int x, int y ) { return x >= 0 && y >= 0 && x < nx && y < ny && reg[at( x, y )]; };
			int x = sx;
			int y = sy;
			int b = 6; // přišli jsme zdola (pod startem je prázdno)
			int fx = -1;
			int fy = -1;
			const size_t limit = 8 * ( static_cast<size_t>( nx ) + ny ) * 20;
			contour.push_back( static_cast<int>( at( x, y ) ) );
			while ( contour.size() < limit )
			{
				int found = -1;
				for ( int i = 1; i <= 8; ++i )
				{
					const int d = ( b + i ) % 8;
					if ( in( x + ddx[d], y + ddy[d] ) )
					{
						found = d;
						break;
					}
				}
				if ( found < 0 )
				{
					break; // osamělá buňka
				}
				const int px = x + ddx[( found + 7 ) % 8];
				const int py = y + ddy[( found + 7 ) % 8];
				const int nxt = x + ddx[found];
				const int nyt = y + ddy[found];
				if ( x == sx && y == sy )
				{
					if ( fx < 0 )
					{
						fx = nxt;
						fy = nyt;
					}
					else if ( nxt == fx && nyt == fy )
					{
						break; // kolem dokola
					}
				}
				// nový směr „odkud“: od nové buňky k poslední prázdné
				int nb2 = 0;
				for ( int d = 0; d < 8; ++d )
				{
					if ( nxt + ddx[d] == px && nyt + ddy[d] == py )
					{
						nb2 = d;
						break;
					}
				}
				x = nxt;
				y = nyt;
				b = nb2;
				contour.push_back( static_cast<int>( at( x, y ) ) );
			}
			if ( contour.size() > 1 && contour.back() == contour.front() )
			{
				contour.pop_back();
			}
		}
		const int nc = static_cast<int>( contour.size() );
		if ( nc < 8 )
		{
			return none;
		}
		// buňky obrysu: index podél obrysu, volný okraj (otvor) a maska bodů u obrysu
		std::unordered_map<int, int> cidx;
		cidx.reserve( contour.size() * 2 );
		std::vector<uint8_t> nearC( N, 0 );
		std::vector<int> freeSum( nc + 1, 0 );
		for ( int k = 0; k < nc; ++k )
		{
			const int c = contour[k];
			cidx.emplace( c, k );
			const int x = c % nx;
			const int y = c / nx;
			bool wall = false;
			for ( int dy = -3; dy <= 3; ++dy )
			{
				for ( int dx = -3; dx <= 3; ++dx )
				{
					const int xx = x + dx;
					const int yy = y + dy;
					if ( xx < 0 || yy < 0 || xx >= nx || yy >= ny )
					{
						continue;
					}
					nearC[at( xx, yy )] = 1;
					if ( std::abs( dx ) <= 1 && std::abs( dy ) <= 1 && occ[at( xx, yy )] )
					{
						wall = true;
					}
				}
			}
			freeSum[k + 1] = freeSum[k] + ( wall ? 0 : 1 );
		}
		openRays = leak ? 1.0 : static_cast<double>( freeSum[nc] ) / nc;
		auto freeBetween = [&]( int i0, int i1 ) // volné buňky obrysu od i0 do i1 (po směru)
		{
			if ( i0 <= i1 )
			{
				return freeSum[i1 + 1] - freeSum[i0];
			}
			return freeSum[nc] - freeSum[i0] + freeSum[i1 + 1];
		};
		auto idxOf = [&]( const CCVector2d& P ) -> int
		{
			int ix;
			int iy;
			if ( !cellOf( P, ix, iy ) )
			{
				return -1;
			}
			int best = -1;
			int bestD = 1 << 30;
			for ( int dy = -7; dy <= 7; ++dy )
			{
				for ( int dx = -7; dx <= 7; ++dx )
				{
					const int xx = ix + dx;
					const int yy = iy + dy;
					if ( xx < 0 || yy < 0 || xx >= nx || yy >= ny )
					{
						continue;
					}
					auto it = cidx.find( static_cast<int>( at( xx, yy ) ) );
					if ( it != cidx.end() && dx * dx + dy * dy < bestD )
					{
						bestD = dx * dx + dy * dy;
						best = it->second;
					}
				}
			}
			return best;
		};
		// body u obrysu → úseky (i krátké hrany ústupků)
		std::vector<CCVector2d> seen;
		for ( const CCVector2d& P : pts )
		{
			int ix;
			int iy;
			if ( cellOf( P, ix, iy ) && nearC[at( ix, iy )] )
			{
				seen.push_back( P );
			}
		}
		Params pr = p;
		pr.minLen = std::min( p.minLen, 0.06 * sc );
		std::vector<Seg> segs = extract( seen, pr );
		if ( p.square )
		{
			squareUp( segs, std::max( p.tol, 0.015 * sc ) );
		}
		// seřadit podél obrysu
		struct Ord
		{
			Seg s;
			int ia;
			int ib;
			int im;
		};
		std::vector<Ord> ord;
		for ( const Seg& s : segs )
		{
			Ord o{ s, idxOf( s.a ), idxOf( s.b ), idxOf( ( s.a + s.b ) * 0.5 ) };
			if ( o.ia < 0 && o.ib < 0 && o.im < 0 )
			{
				continue;
			}
			if ( o.ia < 0 )
				o.ia = o.im >= 0 ? o.im : o.ib;
			if ( o.ib < 0 )
				o.ib = o.im >= 0 ? o.im : o.ia;
			if ( o.im < 0 )
				o.im = o.ia;
			if ( ( o.ib - o.ia + nc ) % nc > nc / 2 )
			{
				std::swap( o.s.a, o.s.b );
				std::swap( o.ia, o.ib );
			}
			ord.push_back( o );
		}
		std::sort( ord.begin(), ord.end(), []( const Ord& x, const Ord& y ) { return x.im < y.im; } );
		if ( ord.size() < 2 )
		{
			std::vector<Seg> r;
			for ( const Ord& o : ord )
			{
				r.push_back( o.s );
			}
			return r;
		}
		// otvor (okno, dveře): mezi dvěma částmi téže zdi je volný okraj → zeď vést rovně přes (ostění pryč)
		{
			const double lineTol = 3.0 * p.tol + 0.01 * sc;
			bool changed = true;
			while ( changed && ord.size() >= 3 )
			{
				changed = false;
				const int m0 = static_cast<int>( ord.size() );
				for ( int i = 0; i < m0 && !changed; ++i )
				{
					const Seg& A = ord[i].s;
					const double La = dist( A.a, A.b );
					if ( La < 1e-9 )
					{
						continue;
					}
					const CCVector2d Da = ( A.b - A.a ) / La;
					const CCVector2d Na( -Da.y, Da.x );
					for ( int step = 1; step <= 5 && step < m0; ++step )
					{
						const int j = ( i + step ) % m0;
						const Seg& B = ord[j].s;
						const double Lb = dist( B.a, B.b );
						if ( Lb < 1e-9 || ( B.b - B.a ).dot( Da ) / Lb < std::cos( 3.0 * Deg ) )
						{
							continue;
						}
						if ( std::abs( ( B.a - A.a ).dot( Na ) ) > lineTol || std::abs( ( B.b - A.a ).dot( Na ) ) > lineTol )
						{
							continue;
						}
						const double span = ( B.a - A.b ).dot( Da );
						if ( span < -lineTol || span > 3.0 * sc )
						{
							continue;
						}
						// výklenek (zeď všude) nechat; přes jen když je mezi nimi otvor aspoň 30 cm, nebo nic (step 1)
						if ( step > 1 && freeBetween( ord[i].ib, ord[j].ia ) * g < 0.3 * sc )
						{
							continue;
						}
						Ord merged = ord[i];
						merged.s.b = A.a + Da * ( ( B.b - A.a ).dot( Da ) );
						merged.ib = ord[j].ib;
						std::vector<Ord> ns;
						for ( int k = 0; k < m0; ++k )
						{
							bool between = false;
							for ( int t = 1; t < step; ++t )
							{
								between = between || k == ( i + t ) % m0;
							}
							if ( k == i )
								ns.push_back( merged );
							else if ( k != j && !between )
								ns.push_back( ord[k] );
						}
						ord.swap( ns );
						changed = true;
						break;
					}
				}
			}
		}
		// krátká hrana ústupku mezi dvěma rovnoběžnými zdmi: body v rohu bývají „kulaté“ → hranu postavit kolmo na zdi
		if ( p.square && ord.size() >= 3 )
		{
			for ( int k = 0; k < static_cast<int>( ord.size() ) && ord.size() >= 3; ++k )
			{
				const int m0 = static_cast<int>( ord.size() );
				Seg& S = ord[k].s;
				const Seg& A = ord[( k + m0 - 1 ) % m0].s;
				const Seg& B = ord[( k + 1 ) % m0].s;
				const double Ls = dist( S.a, S.b );
				const double La = dist( A.a, A.b );
				const double Lb = dist( B.a, B.b );
				if ( Ls < 1e-9 || Ls > 0.35 * sc || La < 1e-9 || Lb < 1e-9 || La < Ls || Lb < Ls )
				{
					continue;
				}
				const CCVector2d Da = ( A.b - A.a ) / La;
				const CCVector2d Db = ( B.b - B.a ) / Lb;
				if ( std::abs( Da.dot( Db ) ) < std::cos( 3.0 * Deg ) )
				{
					continue;
				}
				CCVector2d Nn( -Da.y, Da.x );
				const CCVector2d Ds = ( S.b - S.a ) / Ls;
				const double c = Nn.dot( Ds );
				if ( std::abs( c ) < std::cos( 35.0 * Deg ) )
				{
					// šikmá „hrana“ z kulatého rohu – pryč, odskok se dopočítá z obrysu plochy
					ord.erase( ord.begin() + k );
					--k;
					continue;
				}
				if ( c < 0 )
				{
					Nn = Nn * -1.0;
				}
				const CCVector2d mid = ( S.a + S.b ) * 0.5;
				S.a = mid - Nn * ( 0.5 * Ls );
				S.b = mid + Nn * ( 0.5 * Ls );
			}
		}
		// rohy mezi sousedy podél obrysu; kde průsečík nevyjde, spojí se konce rovnou čarou
		const int m = static_cast<int>( ord.size() );
		const double cos20 = std::cos( 20.0 * Deg );
		for ( int k = 0; k < m && m >= 2; ++k )
		{
			Seg& s = ord[k].s;
			Seg& t = ord[( k + 1 ) % m].s;
			const CCVector2d ds = s.b - s.a;
			const CCVector2d dt2 = t.b - t.a;
			const double ls = ds.norm();
			const double lt = dt2.norm();
			CCVector2d I;
			if ( ls > 1e-9 && lt > 1e-9 && std::abs( ds.dot( dt2 ) / ( ls * lt ) ) < cos20 && intersect( s.a, s.b, t.a, t.b, I ) )
			{
				const double ps = ( I - s.a ).dot( ds ) / ( ls * ls );
				const double pt = ( I - t.a ).dot( dt2 ) / ( lt * lt );
				const double reach = 0.5 * sc;
				if ( dist( I, s.b ) < reach && dist( I, t.a ) < reach && ps > 0.5 && pt < 0.5 )
				{
					s.b = I;
					t.a = I;
				}
			}
			else if ( ls > 1e-9 && lt > 1e-9 && ds.dot( dt2 ) / ( ls * lt ) > std::cos( 3.0 * Deg ) )
			{
				// dvě rovnoběžné zdi s odskokem a hrana mezi nimi se nenašla (kulatý roh, málo bodů):
				// místo odskoku podle obrysu plochy, hrana kolmo
				const CCVector2d Ds = ds / ls;
				const CCVector2d Ns( -Ds.y, Ds.x );
				const double off = ( ( t.a + t.b ) * 0.5 - s.a ).dot( Ns );
				const int i0 = ord[k].ib;
				const int i1 = ord[( k + 1 ) % m].ia;
				const int len = ( i1 - i0 + nc ) % nc;
				if ( std::abs( off ) > 2.0 * p.tol && std::abs( off ) < 1.0 * sc && len > 0 && len * g < 3.0 * sc
				     && freeBetween( i0, i1 ) * g < 0.1 * sc )
				{
					// odskok doprostřed mezi konce obou zdí (tam, kde přestávají jejich body), hrana kolmo
					const double tA = ( s.b - s.a ).dot( Ds );
					const double tB = ( t.a - s.a ).dot( Ds );
					double ts = 0.5 * ( tA + tB );
					// přesněji: kde leží body mezi oběma líci (čelo ústupku)
					double sum = 0.0;
					int cnt = 0;
					for ( const CCVector2d& P : seen )
					{
						const CCVector2d d = P - s.a;
						const double tp = d.dot( Ds );
						const double np = d.dot( Ns ) / off;
						if ( np > 0.25 && np < 0.75 && tp > std::min( tA, tB ) - 0.1 * sc && tp < std::max( tA, tB ) + 0.1 * sc )
						{
							sum += tp;
							++cnt;
						}
					}
					if ( cnt >= 3 )
					{
						ts = sum / cnt;
					}
					if ( ts > 0.3 * ls )
					{
						s.b = s.a + Ds * ts;
						t.a = s.b + Ns * off;
					}
				}
			}
		}
		// uzavřený mnohoúhelník; pryč vrcholy na přímce a ostré špičky
		std::vector<CCVector2d> poly;
		auto push = [&]( const CCVector2d& P )
		{
			if ( poly.empty() || dist( poly.back(), P ) > 0.005 * sc )
			{
				poly.push_back( P );
			}
		};
		for ( int k = 0; k < m; ++k )
		{
			push( ord[k].s.a );
			push( ord[k].s.b );
		}
		if ( poly.size() > 1 && dist( poly.front(), poly.back() ) <= 0.005 * sc )
		{
			poly.pop_back();
		}
		bool changed = true;
		while ( changed && poly.size() > 3 )
		{
			changed = false;
			const int nv = static_cast<int>( poly.size() );
			for ( int i = 0; i < nv; ++i )
			{
				const CCVector2d u = poly[( i + nv - 1 ) % nv] - poly[i];
				const CCVector2d w = poly[( i + 1 ) % nv] - poly[i];
				const double lu = u.norm();
				const double lw = w.norm();
				if ( lu < 1e-9 || lw < 1e-9 )
				{
					poly.erase( poly.begin() + i );
					changed = true;
					break;
				}
				const double c = u.dot( w ) / ( lu * lw );
				if ( c > std::cos( 12.0 * Deg ) || c < std::cos( 178.0 * Deg ) )
				{
					poly.erase( poly.begin() + i );
					changed = true;
					break;
				}
			}
		}
		std::vector<Seg> outl;
		const int nv = static_cast<int>( poly.size() );
		for ( int i = 0; i < nv && nv >= 2; ++i )
		{
			outl.push_back( { poly[i], poly[( i + 1 ) % nv] } );
		}
		return outl;
	}
}
