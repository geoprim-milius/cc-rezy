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
		std::vector<char> tried( n, 0 );
		std::vector<int> stamp( n, 0 );
		int cur = 0;
		const double r = std::max( 0.15 * sc, 5.0 * cell );

		// body v pásu kolem přímky (P0, D) mezi t0 a t1 (jen nepoužité)
		auto band = [&]( const CCVector2d& P0, const CCVector2d& D, double t0, double t1, double tolB, std::vector<int>& res )
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
					if ( used[i] || stamp[i] == cur )
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
		for ( int s = 0; s < n; ++s )
		{
			if ( used[s] || tried[s] )
			{
				continue;
			}
			tried[s] = 1;
			nb.clear();
			H.around( q[s], 2, [&]( int i )
			{
				if ( !used[i] && dist( q[i], q[s] ) <= r )
				{
					nb.push_back( i );
				}
			} );
			if ( nb.size() < 6 )
			{
				continue;
			}
			CCVector2d P0;
			CCVector2d D;
			double pv = 0.0;
			if ( !fit( q, nb, P0, D, pv ) || std::sqrt( pv ) > tol )
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
							band( P0, D, tMax, tMax + gap, tol, win );
						else
							band( P0, D, tMin - gap, tMin, tol, win );
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
				band( P0, D, tMin, tMax, tol, mem );
				if ( mem.size() < 6 )
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
			if ( L < p.minLen || static_cast<double>( mem.size() ) < std::max( 8.0, 0.3 * L / cell ) )
			{
				continue;
			}
			out.push_back( { P0 + D * tMin, P0 + D * tMax } );
			// spotřebovat body líce (i trochu širší pás – ať nevznikne zdvojená čára)
			band( P0, D, tMin - tol, tMax + tol, 2.0 * tol, mem );
			for ( int i : mem )
			{
				used[i] = 1;
			}
		}
		std::sort( out.begin(), out.end(), []( const Seg& x, const Seg& y ) { return dist( x.a, x.b ) > dist( y.a, y.b ); } );
		return out;
	}

	void squareUp( std::vector<Seg>& segs )
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
		// zpřesnit: vážený průměr úhlů do ±2° od vrcholu
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
			if ( std::abs( d ) <= 2.0 )
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
			if ( std::abs( d ) < 3.0 * Deg && std::abs( d ) > 1e-12 )
			{
				const CCVector2d m = ( s.a + s.b ) * 0.5;
				const double half = 0.5 * dist( s.a, s.b );
				const double na = a - d;
				const CCVector2d u( std::cos( na ), std::sin( na ) );
				s.a = m - u * half;
				s.b = m + u * half;
			}
		}
	}

	void joinCorners( std::vector<Seg>& segs, double reach )
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
				for ( int e = 0; e < 2; ++e )
				{
					const CCVector2d E = e == 0 ? ai : bi;
					const double dE = dist( I, E );
					if ( dE > reach || ( e == 0 ? si > 0.5 : si < 0.5 ) )
					{
						continue;
					}
					const double dA = dist( I, aj );
					const double dB = dist( I, bj );
					if ( std::min( dA, dB ) <= reach )
					{
						cands.push_back( { dE + std::min( dA, dB ), i, e, j, dA <= dB ? 0 : 1, I } );
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
	}

	std::vector<Seg> room( const std::vector<CCVector2d>& pts, const CCVector2d& inside, const Params& p, double& openRays )
	{
		openRays = 1.0;
		const double sc = p.scale;
		const double R = 25.0 * sc;
		const double g = 0.02 * sc;
		const int nx = static_cast<int>( 2.0 * R / g ) + 1;
		const CCVector2d O = inside - CCVector2d( R, R );
		std::vector<uint8_t> occ( static_cast<size_t>( nx ) * nx, 0 );
		auto cellOf = [&]( const CCVector2d& P, int& ix, int& iy ) -> bool
		{
			ix = static_cast<int>( std::floor( ( P.x - O.x ) / g ) );
			iy = static_cast<int>( std::floor( ( P.y - O.y ) / g ) );
			return ix >= 0 && iy >= 0 && ix < nx && iy < nx;
		};
		for ( const CCVector2d& P : pts )
		{
			int ix;
			int iy;
			if ( cellOf( P, ix, iy ) )
			{
				uint8_t& c = occ[static_cast<size_t>( iy ) * nx + ix];
				if ( c < 255 )
				{
					++c;
				}
			}
		}
		// obsazenost rozšířit o 1 buňku (paprsek nesmí proklouznout mezerou mezi body líce)
		{
			std::vector<uint8_t> dil( occ.size(), 0 );
			for ( int y = 0; y < nx; ++y )
			{
				for ( int x = 0; x < nx; ++x )
				{
					if ( !occ[static_cast<size_t>( y ) * nx + x] )
					{
						continue;
					}
					for ( int dy = -1; dy <= 1; ++dy )
					{
						for ( int dx = -1; dx <= 1; ++dx )
						{
							const int xx = x + dx;
							const int yy = y + dy;
							if ( xx >= 0 && yy >= 0 && xx < nx && yy < nx )
							{
								dil[static_cast<size_t>( yy ) * nx + xx] = 1;
							}
						}
					}
				}
			}
			occ.swap( dil );
		}
		// paprsky z místa kliknutí – první obsazená buňka = viditelná zeď
		const int rays = 1440;
		auto occAt = [&]( const CCVector2d& d, double s, bool& inside2 ) -> bool
		{
			int ix;
			int iy;
			inside2 = cellOf( inside + d * s, ix, iy );
			return inside2 && occ[static_cast<size_t>( iy ) * nx + ix];
		};
		// další zásah od vzdálenosti s0 (s0 < 0 = od začátku; jinak nejdřív opustit překážku)
		auto cast = [&]( int k, double s0 ) -> double
		{
			const double a = 2.0 * Pi * k / rays;
			const CCVector2d d( std::cos( a ), std::sin( a ) );
			double s = s0 < 0 ? g : s0;
			bool in = true;
			if ( s0 >= 0 )
			{
				while ( s < R && occAt( d, s, in ) )
				{
					s += 0.5 * g;
				}
			}
			for ( ; s < R; s += 0.5 * g )
			{
				if ( occAt( d, s, in ) )
				{
					return s;
				}
				if ( !in )
				{
					break;
				}
			}
			return -1.0; // bez zdi
		};
		std::vector<double> hitS( rays );
		for ( int k = 0; k < rays; ++k )
		{
			hitS[k] = cast( k, -1.0 );
		}
		// samostatné překážky v místnosti (sloup, nábytek): úzký úsek paprsků blíž než obě strany → paprsky pustit dál
		const double jump = 0.3 * sc;
		for ( int iter = 0; iter < 4; ++iter )
		{
			bool any = false;
			int k = 0;
			while ( k < rays )
			{
				if ( hitS[k] < 0 )
				{
					++k;
					continue;
				}
				int e = k;
				while ( e + 1 < rays && hitS[e + 1] >= 0 && std::abs( hitS[e + 1] - hitS[e] ) < jump )
				{
					++e;
				}
				const int width = e - k + 1;
				const double dRun = *std::min_element( hitS.begin() + k, hitS.begin() + e + 1 );
				const double before = hitS[( k + rays - 1 ) % rays];
				const double after = hitS[( e + 1 ) % rays];
				const bool farther = ( before < 0 || before > dRun + 0.5 * sc ) && ( after < 0 || after > dRun + 0.5 * sc );
				if ( width < rays / 36 && farther && width < rays ) // do 10°
				{
					for ( int j = k; j <= e; ++j )
					{
						hitS[j] = cast( j, hitS[j] );
					}
					any = true;
				}
				k = e + 1;
			}
			if ( !any )
			{
				break;
			}
		}
		std::vector<uint8_t> vis( static_cast<size_t>( nx ) * nx, 0 );
		int open = 0;
		for ( int k = 0; k < rays; ++k )
		{
			if ( hitS[k] < 0 )
			{
				++open;
				continue;
			}
			const double a = 2.0 * Pi * k / rays;
			int ix;
			int iy;
			if ( !cellOf( inside + CCVector2d( std::cos( a ), std::sin( a ) ) * hitS[k], ix, iy ) )
			{
				continue;
			}
			for ( int dy = -2; dy <= 2; ++dy )
			{
				for ( int dx = -2; dx <= 2; ++dx )
				{
					const int x = ix + dx;
					const int y = iy + dy;
					if ( x >= 0 && y >= 0 && x < nx && y < nx )
					{
						vis[static_cast<size_t>( y ) * nx + x] = 1;
					}
				}
			}
		}
		openRays = static_cast<double>( open ) / rays;
		std::vector<CCVector2d> seen;
		for ( const CCVector2d& P : pts )
		{
			int ix;
			int iy;
			if ( cellOf( P, ix, iy ) && vis[static_cast<size_t>( iy ) * nx + ix] )
			{
				seen.push_back( P );
			}
		}
		Params pr = p;
		pr.minLen = std::min( p.minLen, 0.15 * sc );
		std::vector<Seg> segs = extract( seen, pr );
		if ( p.square )
		{
			squareUp( segs );
		}
		if ( segs.size() < 2 )
		{
			return segs;
		}
		// orientace proti směru hodinek kolem bodu, řazení podle úhlu začátku
		auto ang = [&]( const CCVector2d& P ) { return std::atan2( P.y - inside.y, P.x - inside.x ); };
		for ( Seg& s : segs )
		{
			if ( wrapPi( ang( s.b ) - ang( s.a ) ) < 0 )
			{
				std::swap( s.a, s.b );
			}
		}
		std::sort( segs.begin(), segs.end(), [&]( const Seg& x, const Seg& y ) { return ang( x.a ) < ang( y.a ); } );
		// úseky mířící ke kliknutí (v úzkém úhlu – trámy, ostění, hrany nábytku) nejsou líc zdi místnosti
		segs.erase( std::remove_if( segs.begin(), segs.end(), [&]( const Seg& g )
		            {
			            const CCVector2d mid = ( g.a + g.b ) * 0.5;
			            CCVector2d rad = mid - inside;
			            const double lr = rad.norm();
			            const double lg = dist( g.a, g.b );
			            if ( lr < 1e-9 || lg < 1e-9 )
			            {
				            return true;
			            }
			            const double c = std::abs( rad.dot( g.b - g.a ) ) / ( lr * lg );
			            const double w = std::abs( wrapPi( ang( g.b ) - ang( g.a ) ) );
			            return c > std::cos( 35.0 * Deg ) && w < 8.0 * Deg;
		            } ),
		            segs.end() );
		if ( segs.size() < 2 )
		{
			return segs;
		}
		// úseky viděné skrz otvor (dál než oba sousedé a v úzkém úhlu) vynechat – obrys pak jde přes otvor
		{
			auto segDist = [&]( const Seg& g )
			{
				const CCVector2d d = g.b - g.a;
				const double L2 = d.dot( d );
				double t = L2 > 1e-18 ? ( inside - g.a ).dot( d ) / L2 : 0.0;
				t = std::max( 0.0, std::min( 1.0, t ) );
				return dist( g.a + d * t, inside );
			};
			bool changed = true;
			while ( changed && segs.size() >= 4 )
			{
				changed = false;
				const int m0 = static_cast<int>( segs.size() );
				for ( int k = 0; k < m0; ++k )
				{
					const Seg& g = segs[k];
					const double w = wrapPi( ang( g.b ) - ang( g.a ) );
					const double dk = segDist( g );
					const double dp = segDist( segs[( k + m0 - 1 ) % m0] );
					const double dn = segDist( segs[( k + 1 ) % m0] );
					if ( w < 25.0 * Deg && dk > 1.2 * std::max( dp, dn ) + 0.1 * sc )
					{
						segs.erase( segs.begin() + k );
						changed = true;
						break;
					}
				}
			}
		}
		// otvor (okno, dveře – vidět skrz) nebo nábytek před zdí: úseky mezi dvěma částmi téže zdi vynechat
		// a zeď vést rovně přes (jen do šířky 2,5 m)
		{
			const double lineTol = 3.0 * p.tol + 0.01 * sc;
			bool changed = true;
			while ( changed && segs.size() >= 3 )
			{
				changed = false;
				const int m0 = static_cast<int>( segs.size() );
				for ( int i = 0; i < m0 && !changed; ++i )
				{
					const Seg& A = segs[i];
					const double La = dist( A.a, A.b );
					if ( La < 1e-9 )
					{
						continue;
					}
					const CCVector2d Da = ( A.b - A.a ) / La;
					const CCVector2d Na( -Da.y, Da.x );
					for ( int step = 2; step <= 5 && step < m0; ++step )
					{
						const int j = ( i + step ) % m0;
						const Seg& B = segs[j];
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
						if ( span < -lineTol || span > 2.5 * sc )
						{
							continue;
						}
						// sloučit A a B do jedné zdi, mezilehlé úseky pryč
						Seg merged{ A.a, A.a + Da * ( ( B.b - A.a ).dot( Da ) ) };
						std::vector<Seg> ns;
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
								ns.push_back( segs[k] );
						}
						segs.swap( ns );
						changed = true;
						break;
					}
				}
			}
		}
		const int m = static_cast<int>( segs.size() );
		const double cos20 = std::cos( 20.0 * Deg );
		const double reach = 1.0 * sc;
		for ( int k = 0; k < m; ++k )
		{
			Seg& s = segs[k];
			Seg& t = segs[( k + 1 ) % m];
			const CCVector2d ds = s.b - s.a;
			const CCVector2d dt = t.b - t.a;
			const double ls = ds.norm();
			const double lt = dt.norm();
			CCVector2d I;
			bool joined = false;
			if ( ls > 1e-9 && lt > 1e-9 && std::abs( ds.dot( dt ) / ( ls * lt ) ) < cos20 && intersect( s.a, s.b, t.a, t.b, I ) )
			{
				const double ps = ( I - s.a ).dot( ds ) / ( ls * ls );
				const double pt = ( I - t.a ).dot( dt ) / ( lt * lt );
				if ( dist( I, s.b ) < reach && dist( I, t.a ) < reach && ps > 0.5 && pt < 0.5 )
				{
					s.b = I;
					t.a = I;
					joined = true;
				}
			}
			(void)joined; // nenapojené konce spojí obrys rovnou čarou (ústupek, výklenek, otvor)
		}
		// obrys jako uzavřený mnohoúhelník: vrcholy v pořadí, pak odstranit „zuby“ (ostré špičky – trámy, nábytek,
		// pohled skrz otvor) a vrcholy na přímce
		std::vector<CCVector2d> poly;
		auto push = [&]( const CCVector2d& P )
		{
			if ( poly.empty() || dist( poly.back(), P ) > 0.01 * sc )
			{
				poly.push_back( P );
			}
		};
		for ( int k = 0; k < m; ++k )
		{
			push( segs[k].a );
			push( segs[k].b );
		}
		if ( poly.size() > 1 && dist( poly.front(), poly.back() ) <= 0.01 * sc )
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
				const CCVector2d& P = poly[( i + nv - 1 ) % nv];
				const CCVector2d& V = poly[i];
				const CCVector2d& N = poly[( i + 1 ) % nv];
				const CCVector2d u = P - V;
				const CCVector2d w = N - V;
				const double lu = u.norm();
				const double lw = w.norm();
				if ( lu < 1e-9 || lw < 1e-9 )
				{
					poly.erase( poly.begin() + i );
					changed = true;
					break;
				}
				const double c = u.dot( w ) / ( lu * lw );
				// špička (úhel < 25°) nebo vrchol na přímce (> 177°)
				if ( c > std::cos( 25.0 * Deg ) || c < std::cos( 177.0 * Deg ) )
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
