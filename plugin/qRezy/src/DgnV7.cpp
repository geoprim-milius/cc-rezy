#include "DgnV7.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>
#include <iterator>

namespace DgnV7
{
	namespace
	{
		int clampi( int v, int lo, int hi )
		{
			return v < lo ? lo : ( v > hi ? hi : v );
		}

		// --- čtení/zápis „middle-endian“ 32bit čísel (VAX pořadí: horní slovo první, každé slovo little-endian)
		int32_t readI32( const uint8_t* p )
		{
			const uint32_t hi = static_cast<uint32_t>( p[0] ) | ( static_cast<uint32_t>( p[1] ) << 8 );
			const uint32_t lo = static_cast<uint32_t>( p[2] ) | ( static_cast<uint32_t>( p[3] ) << 8 );
			return static_cast<int32_t>( ( hi << 16 ) | lo );
		}

		void putI32( std::vector<uint8_t>& out, int32_t v )
		{
			const uint32_t u = static_cast<uint32_t>( v );
			const uint32_t hi = u >> 16;
			const uint32_t lo = u & 0xFFFF;
			out.push_back( static_cast<uint8_t>( hi & 0xFF ) );
			out.push_back( static_cast<uint8_t>( hi >> 8 ) );
			out.push_back( static_cast<uint8_t>( lo & 0xFF ) );
			out.push_back( static_cast<uint8_t>( lo >> 8 ) );
		}

		void putU16( std::vector<uint8_t>& out, uint16_t v )
		{
			out.push_back( static_cast<uint8_t>( v & 0xFF ) );
			out.push_back( static_cast<uint8_t>( v >> 8 ) );
		}

		void setU16( std::vector<uint8_t>& out, size_t pos, uint16_t v )
		{
			out[pos] = static_cast<uint8_t>( v & 0xFF );
			out[pos + 1] = static_cast<uint8_t>( v >> 8 );
		}

		// rozsah (range) se ukládá jako unsigned s převráceným znaménkovým bitem
		void putRange( std::vector<uint8_t>& out, int32_t v )
		{
			putI32( out, static_cast<int32_t>( static_cast<uint32_t>( v ) ^ 0x80000000u ) );
		}

		// VAX D-float -> IEEE double (stejně jako GDAL DGN2IEEEDouble)
		double vaxToDouble( const uint8_t* src )
		{
			uint8_t d[8];
			d[2] = src[0]; d[3] = src[1]; d[0] = src[2]; d[1] = src[3];
			d[6] = src[4]; d[7] = src[5]; d[4] = src[6]; d[5] = src[7];
			uint32_t hi = static_cast<uint32_t>( d[0] ) | ( static_cast<uint32_t>( d[1] ) << 8 ) | ( static_cast<uint32_t>( d[2] ) << 16 ) | ( static_cast<uint32_t>( d[3] ) << 24 );
			uint32_t lo = static_cast<uint32_t>( d[4] ) | ( static_cast<uint32_t>( d[5] ) << 8 ) | ( static_cast<uint32_t>( d[6] ) << 16 ) | ( static_cast<uint32_t>( d[7] ) << 24 );

			const uint32_t sign = hi & 0x80000000u;
			uint32_t exponent = ( hi >> 23 ) & 0xFFu;
			if ( exponent )
			{
				exponent = exponent - 129 + 1023;
			}
			const uint32_t rnd = lo & 0x7u;
			lo = lo >> 3;
			lo = ( lo & 0x1FFFFFFFu ) | ( hi << 29 );
			if ( rnd )
			{
				lo |= 1u;
			}
			hi = hi >> 3;
			hi &= 0x000FFFFFu;
			hi |= ( exponent << 20 ) | sign;

			const uint64_t bits = ( static_cast<uint64_t>( hi ) << 32 ) | lo;
			double out = 0.0;
			std::memcpy( &out, &bits, sizeof( out ) );
			return out;
		}

		// IEEE double -> VAX D-float (opak vaxToDouble, jako GDAL IEEE2DGNDouble)
		void doubleToVax( double v, uint8_t* dst )
		{
			uint64_t bits = 0;
			std::memcpy( &bits, &v, sizeof( bits ) );
			uint32_t hi = static_cast<uint32_t>( bits >> 32 );
			uint32_t lo = static_cast<uint32_t>( bits & 0xFFFFFFFFu );
			const uint32_t sign = hi & 0x80000000u;
			int32_t exponent = static_cast<int32_t>( ( hi >> 20 ) & 0x7FFu );
			if ( exponent )
			{
				exponent = exponent - 1023 + 129;
			}
			if ( v == 0.0 || exponent <= 0 )
			{
				hi = 0;
				lo = 0;
			}
			else if ( exponent > 255 )
			{
				hi = sign | 0x7FFFFFFFu;
				lo = 0xFFFFFFFFu;
			}
			else
			{
				hi = hi << 3;
				hi = hi | ( lo >> 29 );
				hi = hi & 0x007FFFFFu;
				hi = hi | ( static_cast<uint32_t>( exponent ) << 23 ) | sign;
				lo = lo << 3;
			}
			uint8_t d[8];
			for ( int i = 0; i < 4; ++i )
			{
				d[i] = static_cast<uint8_t>( ( hi >> ( 8 * i ) ) & 0xFF );
				d[4 + i] = static_cast<uint8_t>( ( lo >> ( 8 * i ) ) & 0xFF );
			}
			dst[0] = d[2]; dst[1] = d[3]; dst[2] = d[0]; dst[3] = d[1];
			dst[4] = d[6]; dst[5] = d[7]; dst[6] = d[4]; dst[7] = d[5];
		}

		// negrafické prvky, které ze seedu přebíráme
		bool keepType( int type )
		{
			switch ( type )
			{
			case 1:  // knihovna buněk
			case 5:  // tabulka barev / skupinová data
			case 8:  // nastavení digitizéru
			case 9:  // TCB (řídicí blok výkresu)
			case 10: // symbologie hladin
			case 26: // připojené reference
			case 66: // aplikační prvky (názvy hladin, nastavení)
				return true;
			default:
				return false;
			}
		}
	}

	bool Writer::loadSeed( const std::string& path, std::string& error )
	{
		std::ifstream f( path, std::ios::binary );
		if ( !f )
		{
			error = "Nelze otevřít seed soubor: " + path;
			return false;
		}
		std::vector<uint8_t> bytes( ( std::istreambuf_iterator<char>( f ) ), std::istreambuf_iterator<char>() );
		return loadSeedBytes( bytes, error );
	}

	bool Writer::loadSeedBytes( const std::vector<uint8_t>& b, std::string& error )
	{
		m_header.clear();

		if ( b.size() < 1536 || ( b[1] & 0x7F ) != 9 )
		{
			error = "Seed není výkres MicroStation V7 (chybí řídicí blok TCB).";
			return false;
		}

		m_is3D = ( b[0] & 0x40 ) != 0;
		const int32_t subPerMaster = readI32( &b[1112] );
		const int32_t uorPerSub = readI32( &b[1116] );
		if ( subPerMaster <= 0 || uorPerSub <= 0 )
		{
			error = "Seed má neplatné pracovní jednotky.";
			return false;
		}
		m_uorPerMaster = static_cast<double>( subPerMaster ) * uorPerSub;
		for ( int i = 0; i < 3; ++i )
		{
			m_origin[i] = vaxToDouble( &b[1240 + 8 * i] );
			m_seedOrigin[i] = m_origin[i];
		}

		size_t p = 0;
		bool keepingComplex = false;
		while ( p + 4 <= b.size() )
		{
			if ( b[p] == 0xFF && b[p + 1] == 0xFF )
			{
				break; // konec výkresu
			}
			const int type = b[p + 1] & 0x7F;
			const bool deleted = ( b[p + 1] & 0x80 ) != 0;
			const bool complexChild = ( b[p] & 0x80 ) != 0;
			const size_t words = static_cast<size_t>( b[p + 2] ) | ( static_cast<size_t>( b[p + 3] ) << 8 );
			const size_t len = 4 + 2 * words;
			if ( p + len > b.size() )
			{
				error = "Seed je poškozený (prvek přesahuje konec souboru).";
				return false;
			}

			bool keep = false;
			if ( !deleted )
			{
				if ( complexChild )
				{
					keep = keepingComplex;
				}
				else
				{
					keep = keepType( type );
					keepingComplex = keep;
				}
			}
			if ( keep )
			{
				m_header.insert( m_header.end(), b.begin() + p, b.begin() + p + len );
			}
			p += len;
		}

		if ( m_header.empty() )
		{
			error = "V seedu nebyla nalezena hlavička výkresu.";
			return false;
		}
		return true;
	}

	void Writer::setOriginMaster( double x, double y, double z )
	{
		const double v[3] = { x, y, z };
		for ( int i = 0; i < 3; ++i )
		{
			m_seedOrigin[i] = v[i] * m_uorPerMaster;
			m_origin[i] = m_seedOrigin[i];
		}
		m_originSet = true;
	}

	bool Writer::toUOR( double v, int axis, int32_t& out ) const
	{
		const double raw = std::round( v * m_uorPerMaster + m_origin[axis] );
		if ( raw < -2147483647.0 || raw > 2147483647.0 )
		{
			return false;
		}
		out = static_cast<int32_t>( raw );
		return true;
	}

	bool Writer::appendText( std::vector<uint8_t>& out, const Element& e, std::string& error ) const
	{
		if ( e.pts.empty() || e.text.empty() || m_is3D )
		{
			return true; // text do 3D výkresu zatím nepíšeme
		}
		const std::string txt = e.text.substr( 0, 255 );
		int32_t x = 0;
		int32_t y = 0;
		if ( !toUOR( e.pts[0].x, 0, x ) || !toUOR( e.pts[0].y, 1, y ) )
		{
			error = "Souřadnice textu jsou mimo rozsah výkresu V7.";
			return false;
		}
		const double h = e.textHeight * m_uorPerMaster;
		const double wd = h * 0.8 * txt.size();
		// rozsah natočeného obdélníku textu
		const double ra = e.textAngleDeg * 3.14159265358979323846 / 180.0;
		const double ca = std::cos( ra );
		const double sa = std::sin( ra );
		double minX = 0;
		double maxX = 0;
		double minY = 0;
		double maxY = 0;
		for ( int k = 0; k < 4; ++k )
		{
			const double u = ( k & 1 ) ? wd : 0.0;
			const double v = ( k & 2 ) ? h : 0.0;
			const double px = u * ca - v * sa;
			const double py = u * sa + v * ca;
			minX = std::min( minX, px );
			maxX = std::max( maxX, px );
			minY = std::min( minY, py );
			maxY = std::max( maxY, py );
		}

		const size_t start = out.size();
		out.push_back( static_cast<uint8_t>( clampi( e.sym.level, 1, 63 ) ) );
		out.push_back( 17 );
		putU16( out, 0 );
		putRange( out, x + static_cast<int32_t>( std::floor( minX ) ) );
		putRange( out, y + static_cast<int32_t>( std::floor( minY ) ) );
		putRange( out, 0 );
		putRange( out, x + static_cast<int32_t>( std::ceil( maxX ) ) );
		putRange( out, y + static_cast<int32_t>( std::ceil( maxY ) ) );
		putRange( out, 0 );
		putU16( out, 0 ); // grafická skupina
		const size_t attIndexPos = out.size();
		putU16( out, 0 );
		putU16( out, 0x0200 );
		out.push_back( static_cast<uint8_t>( ( clampi( e.sym.weight, 0, 31 ) << 3 ) | clampi( e.sym.style, 0, 7 ) ) );
		out.push_back( static_cast<uint8_t>( clampi( e.sym.color, 0, 255 ) ) );

		out.push_back( static_cast<uint8_t>( clampi( e.font, 0, 255 ) ) );
		out.push_back( static_cast<uint8_t>( clampi( e.justification, 0, 255 ) ) );
		// násobky délky a výšky písma (jednotky UOR * 1000 / 6)
		const int32_t mult = static_cast<int32_t>( h * 1000.0 / 6.0 + 0.5 );
		putI32( out, mult );
		putI32( out, mult );
		putI32( out, static_cast<int32_t>( std::lround( e.textAngleDeg * 360000.0 ) ) ); // natočení v 1/360000 °
		putI32( out, x );
		putI32( out, y );
		out.push_back( static_cast<uint8_t>( txt.size() ) );
		out.push_back( 0 ); // editovatelná pole
		out.insert( out.end(), txt.begin(), txt.end() );
		if ( ( out.size() - start ) % 2 )
		{
			out.push_back( 0 );
		}
		setU16( out, attIndexPos, static_cast<uint16_t>( ( out.size() - start - 32 ) / 2 ) );
		setU16( out, start + 2, static_cast<uint16_t>( ( out.size() - start - 4 ) / 2 ) );
		return true;
	}

	void Writer::appendLineStyle( std::vector<uint8_t>& out, const Symbology& sym ) const
	{
		if ( !sym.hasLineStyle )
		{
			return;
		}
		static const uint8_t tail[] = { 0x01, 0x00, 0x00, 0x00, 0x4c, 0x3e, 0xcc, 0xcc, 0xcc, 0xcc, 0xd0, 0xcc, 0x00, 0x00, 0x00, 0x00 };
		putU16( out, 0x100B ); // uživatelská linkage, 11 slov za hlavičkou
		putU16( out, 0x79F9 ); // id: styl čáry
		const uint32_t id = static_cast<uint32_t>( sym.lineStyleId );
		out.push_back( static_cast<uint8_t>( id & 0xFF ) );
		out.push_back( static_cast<uint8_t>( ( id >> 8 ) & 0xFF ) );
		out.push_back( static_cast<uint8_t>( ( id >> 16 ) & 0xFF ) );
		out.push_back( static_cast<uint8_t>( ( id >> 24 ) & 0xFF ) );
		out.insert( out.end(), std::begin( tail ), std::end( tail ) );
	}

	bool Writer::appendArc( std::vector<uint8_t>& out, const Element& e, std::string& error ) const
	{
		const double cx = e.center.x * m_uorPerMaster + m_origin[0];
		const double cy = e.center.y * m_uorPerMaster + m_origin[1];
		const double r = e.radius * m_uorPerMaster;
		int32_t lo[2];
		int32_t hi[2];
		if ( !toUOR( e.center.x - e.radius, 0, lo[0] ) || !toUOR( e.center.y - e.radius, 1, lo[1] ) || !toUOR( e.center.x + e.radius, 0, hi[0] ) || !toUOR( e.center.y + e.radius, 1, hi[1] ) )
		{
			error = "Souřadnice oblouku jsou mimo rozsah výkresu V7.";
			return false;
		}
		double a0 = std::fmod( e.startDeg, 360.0 );
		if ( a0 < 0 )
		{
			a0 += 360.0;
		}
		const size_t start = out.size();
		out.push_back( static_cast<uint8_t>( clampi( e.sym.level, 1, 63 ) ) );
		out.push_back( 16 );
		putU16( out, 0 );
		putRange( out, lo[0] );
		putRange( out, lo[1] );
		putRange( out, 0 );
		putRange( out, hi[0] );
		putRange( out, hi[1] );
		putRange( out, 0 );
		putU16( out, 0 ); // grafická skupina
		const size_t attIndexPos = out.size();
		putU16( out, 0 );
		putU16( out, e.sym.hasLineStyle ? 0x0A00 : 0x0200 );
		out.push_back( static_cast<uint8_t>( ( clampi( e.sym.weight, 0, 31 ) << 3 ) | clampi( e.sym.style, 0, 7 ) ) );
		out.push_back( static_cast<uint8_t>( clampi( e.sym.color, 0, 255 ) ) );
		// úhly v 1/360000 stupně, rozsah vždy kladný (proti směru hodinových ručiček)
		putI32( out, static_cast<int32_t>( std::round( a0 * 360000.0 ) ) );
		putI32( out, static_cast<int32_t>( std::round( std::min( 360.0, std::abs( e.sweepDeg ) ) * 360000.0 ) ) );
		uint8_t buf[8];
		doubleToVax( r, buf );
		out.insert( out.end(), buf, buf + 8 ); // hlavní poloosa
		out.insert( out.end(), buf, buf + 8 ); // vedlejší poloosa
		putI32( out, 0 );                      // natočení
		doubleToVax( cx, buf );
		out.insert( out.end(), buf, buf + 8 );
		doubleToVax( cy, buf );
		out.insert( out.end(), buf, buf + 8 );
		setU16( out, attIndexPos, static_cast<uint16_t>( ( out.size() - start - 32 ) / 2 ) );
		appendLineStyle( out, e.sym );
		setU16( out, start + 2, static_cast<uint16_t>( ( out.size() - start - 4 ) / 2 ) );
		return true;
	}

	bool Writer::appendElement( std::vector<uint8_t>& out, const Element& e, std::string& error ) const
	{
		if ( e.isText )
		{
			return appendText( out, e, error );
		}
		if ( e.isArc && !m_is3D && e.radius > 0.0 )
		{
			return appendArc( out, e, error );
		}
		if ( e.pts.size() < 2 )
		{
			return true; // nic ke kreslení
		}
		if ( e.pts.size() > 101 )
		{
			// lomená čára má v V7 max. 101 vrcholů – rozdělíme ji
			for ( size_t start = 0; start + 1 < e.pts.size(); start += 100 )
			{
				Element part;
				part.sym = e.sym;
				const size_t end = std::min( e.pts.size(), start + 101 );
				part.pts.assign( e.pts.begin() + start, e.pts.begin() + end );
				if ( !appendElement( out, part, error ) )
				{
					return false;
				}
			}
			return true;
		}

		const int dims = m_is3D ? 3 : 2;
		std::vector<int32_t> coords;
		coords.reserve( e.pts.size() * dims );
		int32_t lo[3] = { INT32_MAX, INT32_MAX, 0 };
		int32_t hi[3] = { INT32_MIN, INT32_MIN, 0 };
		for ( const Point2& P : e.pts )
		{
			int32_t x = 0;
			int32_t y = 0;
			if ( !toUOR( P.x, 0, x ) || !toUOR( P.y, 1, y ) )
			{
				error = "Souřadnice jsou mimo rozsah výkresu V7 (zkontrolujte pracovní jednotky a globální počátek seedu).";
				return false;
			}
			coords.push_back( x );
			coords.push_back( y );
			if ( m_is3D )
			{
				int32_t z = 0;
				toUOR( 0.0, 2, z );
				coords.push_back( z );
				lo[2] = std::min( lo[2], z );
				hi[2] = std::max( hi[2], z );
			}
			lo[0] = std::min( lo[0], x );
			lo[1] = std::min( lo[1], y );
			hi[0] = std::max( hi[0], x );
			hi[1] = std::max( hi[1], y );
		}

		const bool isLine = ( e.pts.size() == 2 );
		const size_t start = out.size();

		// hlavička prvku
		out.push_back( static_cast<uint8_t>( clampi( e.sym.level, 1, 63 ) ) );
		out.push_back( static_cast<uint8_t>( isLine ? 3 : 4 ) );
		putU16( out, 0 ); // délka – doplní se na konci
		for ( int i = 0; i < 3; ++i )
		{
			putRange( out, lo[i] );
		}
		for ( int i = 0; i < 3; ++i )
		{
			putRange( out, hi[i] );
		}
		putU16( out, 0 ); // grafická skupina
		const size_t attIndexPos = out.size();
		putU16( out, 0 ); // index atributů – doplní se
		const uint16_t props = e.sym.hasLineStyle ? 0x0A00 : 0x0200; // 0x0800 = má atributy, 0x0200 = nový prvek
		putU16( out, props );
		const uint8_t styleWeight = static_cast<uint8_t>( ( clampi( e.sym.weight, 0, 31 ) << 3 ) | ( clampi( e.sym.style, 0, 7 ) ) );
		out.push_back( styleWeight );
		out.push_back( static_cast<uint8_t>( clampi( e.sym.color, 0, 255 ) ) );

		// geometrie
		if ( !isLine )
		{
			putU16( out, static_cast<uint16_t>( e.pts.size() ) );
		}
		for ( int32_t c : coords )
		{
			putI32( out, c );
		}

		// odkaz na atributy (počet slov od offsetu 32)
		setU16( out, attIndexPos, static_cast<uint16_t>( ( out.size() - start - 32 ) / 2 ) );

		// vlastní styl čáry (stejný zápis, jaký používá MicroStation)
		appendLineStyle( out, e.sym );

		// délka prvku ve slovech (bez prvních dvou slov)
		const size_t total = out.size() - start;
		setU16( out, start + 2, static_cast<uint16_t>( ( total - 4 ) / 2 ) );
		return true;
	}

	bool Writer::writeBytes( std::vector<uint8_t>& out, const std::vector<Element>& elements, std::string& error ) const
	{
		if ( m_header.empty() )
		{
			error = "Není načten seed výkresu.";
			return false;
		}
		out = m_header;

		// souřadnice se nevejdou do 32 bitů (např. S-JTSK × 10 000 UOR/m) → posunout globální počátek
		// výkresu k datům (zaokrouhleno na 10 km); souřadnice v MicroStationu zůstanou skutečné
		for ( int i = 0; i < 3; ++i )
		{
			m_origin[i] = m_seedOrigin[i];
		}
		double mn[2] = { 1e300, 1e300 };
		double mx[2] = { -1e300, -1e300 };
		for ( const Element& e : elements )
		{
			for ( const Point2& P : e.pts )
			{
				mn[0] = std::min( mn[0], P.x );
				mx[0] = std::max( mx[0], P.x );
				mn[1] = std::min( mn[1], P.y );
				mx[1] = std::max( mx[1], P.y );
			}
		}
		bool shifted = false;
		for ( int i = 0; i < 2 && mn[i] <= mx[i]; ++i )
		{
			const double lo = mn[i] * m_uorPerMaster + m_origin[i];
			const double hi = mx[i] * m_uorPerMaster + m_origin[i];
			if ( lo < -2.0e9 || hi > 2.0e9 )
			{
				const double center = std::round( ( mn[i] + mx[i] ) / 2.0 / 10000.0 ) * 10000.0;
				m_origin[i] = -center * m_uorPerMaster;
				shifted = true;
			}
		}
		if ( ( shifted || m_originSet ) && out.size() >= 1240 + 24 )
		{
			for ( int i = 0; i < 3; ++i )
			{
				doubleToVax( m_origin[i], &out[1240 + 8 * i] );
			}
		}

		for ( const Element& e : elements )
		{
			if ( !appendElement( out, e, error ) )
			{
				return false;
			}
		}
		out.push_back( 0xFF );
		out.push_back( 0xFF );
		const size_t padded = ( ( out.size() + 511 ) / 512 ) * 512;
		out.resize( padded, 0 );
		return true;
	}

	bool Writer::write( const std::string& path, const std::vector<Element>& elements, std::string& error ) const
	{
		std::vector<uint8_t> bytes;
		if ( !writeBytes( bytes, elements, error ) )
		{
			return false;
		}
		std::ofstream f( path, std::ios::binary | std::ios::trunc );
		if ( !f )
		{
			error = "Nelze zapsat soubor: " + path;
			return false;
		}
		f.write( reinterpret_cast<const char*>( bytes.data() ), static_cast<std::streamsize>( bytes.size() ) );
		return static_cast<bool>( f );
	}

	bool readDesign( const std::vector<uint8_t>& b, std::vector<ReadItem>& out, ReadStats& stats, std::string& error )
	{
		if ( b.size() < 1536 || ( b[1] & 0x7F ) != 9 )
		{
			error = "Soubor není výkres MicroStation V7 (chybí řídicí blok TCB). Výkres V8 uložte v MicroStationu jako V7.";
			return false;
		}
		const bool is3D = ( b[0] & 0x40 ) != 0;
		stats.is3D = is3D;
		const int32_t subPerMaster = readI32( &b[1112] );
		const int32_t uorPerSub = readI32( &b[1116] );
		if ( subPerMaster <= 0 || uorPerSub <= 0 )
		{
			error = "Výkres má neplatné pracovní jednotky.";
			return false;
		}
		const double uor = static_cast<double>( subPerMaster ) * uorPerSub;
		double origin[3];
		for ( int i = 0; i < 3; ++i )
		{
			origin[i] = vaxToDouble( &b[1240 + 8 * i] );
		}
		const int dim = is3D ? 3 : 2;
		auto pt = [&]( size_t off ) -> Point2
		{
			// celočíselné UOR → hlavní jednotky
			return { ( readI32( &b[off] ) - origin[0] ) / uor, ( readI32( &b[off + 4] ) - origin[1] ) / uor };
		};
		auto ptD = [&]( size_t off ) -> Point2
		{
			// VAX double v UOR
			return { ( vaxToDouble( &b[off] ) - origin[0] ) / uor, ( vaxToDouble( &b[off + 8] ) - origin[1] ) / uor };
		};

		std::vector<std::pair<int, int>> skipped;
		auto skip = [&skipped]( int t )
		{
			for ( auto& s : skipped )
			{
				if ( s.first == t )
				{
					++s.second;
					return;
				}
			}
			skipped.push_back( { t, 1 } );
		};

		size_t p = 0;
		bool parentUsable = false; //!< složky komplexního prvku / buňky
		while ( p + 4 <= b.size() )
		{
			if ( b[p] == 0xFF && b[p + 1] == 0xFF )
			{
				break;
			}
			const int type = b[p + 1] & 0x7F;
			const bool deleted = ( b[p + 1] & 0x80 ) != 0;
			const bool complexChild = ( b[p] & 0x80 ) != 0;
			const int level = b[p] & 0x3F;
			const size_t words = static_cast<size_t>( b[p + 2] ) | ( static_cast<size_t>( b[p + 3] ) << 8 );
			const size_t len = 4 + 2 * words;
			if ( p + len > b.size() )
			{
				break;
			}
			const size_t e = p;
			p += len;
			if ( deleted || type == 9 || type == 8 || type == 10 )
			{
				continue;
			}
			if ( !complexChild )
			{
				// nadřazený prvek: buňka (2), komplexní řetězec (12) a tvar (14) – složky se použijí
				parentUsable = ( type == 2 || type == 12 || type == 14 );
				if ( type == 2 || type == 12 || type == 14 )
				{
					continue;
				}
			}
			else if ( !parentUsable )
			{
				continue; // složky sdílené buňky, rastru, …
			}
			++stats.total;
			if ( len < 36 )
			{
				continue;
			}
			ReadItem it;
			it.type = type;
			it.level = level;
			it.weight = b[e + 34] >> 3;
			it.color = b[e + 35];
			// vlastní styl čáry v připojených datech (linkage 0x79F9)
			const size_t attIndex = static_cast<size_t>( b[e + 30] ) | ( static_cast<size_t>( b[e + 31] ) << 8 );
			const size_t attStart = e + 32 + 2 * attIndex;
			if ( ( b[e + 33] & 0x08 ) && attStart < e + len )
			{
				for ( size_t q = attStart; q + 8 <= e + len; q += 2 )
				{
					if ( b[q] == 0xF9 && b[q + 1] == 0x79 )
					{
						it.lineStyleId = static_cast<int32_t>( static_cast<uint32_t>( b[q + 2] ) | ( static_cast<uint32_t>( b[q + 3] ) << 8 ) | ( static_cast<uint32_t>( b[q + 4] ) << 16 ) | ( static_cast<uint32_t>( b[q + 5] ) << 24 ) );
						break;
					}
				}
			}
			const size_t vs = static_cast<size_t>( 4 * dim ); // velikost bodu
			bool ok = false;
			switch ( type )
			{
			case 3: // úsečka
				if ( len >= 36 + 2 * vs )
				{
					it.pts.push_back( pt( e + 36 ) );
					it.pts.push_back( pt( e + 36 + vs ) );
					ok = true;
				}
				break;
			case 4: // lomená čára
			case 6: // tvar
			{
				if ( len < 38 )
					break;
				const size_t n = static_cast<size_t>( b[e + 36] ) | ( static_cast<size_t>( b[e + 37] ) << 8 );
				if ( n < 2 || 38 + n * vs > len )
					break;
				for ( size_t k = 0; k < n; ++k )
				{
					it.pts.push_back( pt( e + 38 + k * vs ) );
				}
				ok = true;
				break;
			}
			case 15: // elipsa / kružnice (2D)
			case 16: // oblouk (2D)
			{
				if ( is3D )
					break; // 3D oblouky (natočení kvaternionem) zatím ne
				size_t o = e + 36;
				double start = 0.0;
				double sweep = 360.0;
				if ( type == 16 )
				{
					start = readI32( &b[o] ) / 360000.0;
					uint8_t sw[4] = { b[o + 4], b[o + 5], b[o + 6], b[o + 7] };
					const bool neg = ( sw[1] & 0x80 ) != 0;
					sw[1] &= 0x7F;
					const int32_t sv = readI32( sw );
					sweep = sv == 0 ? 360.0 : ( neg ? -sv : sv ) / 360000.0;
					o += 8;
				}
				if ( len < ( o - e ) + 36 )
					break;
				const double a1 = vaxToDouble( &b[o] ) / uor;
				const double a2 = vaxToDouble( &b[o + 8] ) / uor;
				const double rot = readI32( &b[o + 16] ) / 360000.0;
				const Point2 c = ptD( o + 20 );
				if ( a1 <= 0.0 )
					break;
				if ( std::abs( a1 - a2 ) <= 1e-6 * std::max( 1.0, a1 ) )
				{
					it.isArc = true;
					it.center = c;
					it.radius = a1;
					it.startDeg = start + rot;
					it.sweepDeg = sweep;
				}
				else
				{
					// skutečná elipsa → lomená čára
					const double r = rot * 3.14159265358979323846 / 180.0;
					const int n = std::max( 8, static_cast<int>( std::ceil( std::abs( sweep ) / 7.5 ) ) );
					for ( int k = 0; k <= n; ++k )
					{
						const double t = ( start + sweep * k / n ) * 3.14159265358979323846 / 180.0;
						const double x = a1 * std::cos( t );
						const double y = a2 * std::sin( t );
						it.pts.push_back( { c.x + x * std::cos( r ) - y * std::sin( r ), c.y + x * std::sin( r ) + y * std::cos( r ) } );
					}
					it.type = 4;
				}
				ok = true;
				break;
			}
			case 17: // text
			{
				const size_t o = e + 36;
				if ( is3D )
				{
					// 3D: font, just, lengthMult, heightMult, quat[4], origin xyz, nchars, edf, text
					if ( len < 36 + 2 + 8 + 16 + 12 + 2 )
						break;
					it.textHeight = readI32( &b[o + 6] ) * 6.0 / 1000.0 / uor;
					it.pts.push_back( pt( o + 26 ) );
					const size_t nc = b[o + 38];
					if ( o + 40 + nc <= e + len )
						it.text.assign( reinterpret_cast<const char*>( &b[o + 40] ), nc );
				}
				else
				{
					if ( len < 36 + 24 )
						break;
					it.textHeight = readI32( &b[o + 6] ) * 6.0 / 1000.0 / uor;
					it.pts.push_back( pt( o + 14 ) );
					const size_t nc = b[o + 22];
					if ( o + 24 + nc <= e + len )
						it.text.assign( reinterpret_cast<const char*>( &b[o + 24] ), nc );
				}
				it.isText = !it.text.empty();
				ok = it.isText;
				break;
			}
			default:
				break;
			}
			if ( ok )
			{
				out.push_back( it );
				++stats.used;
			}
			else
			{
				skip( type );
			}
		}
		stats.skippedTypes = skipped;
		return true;
	}
}
