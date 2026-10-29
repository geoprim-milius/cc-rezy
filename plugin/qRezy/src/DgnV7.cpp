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

	bool Writer::appendElement( std::vector<uint8_t>& out, const Element& e, std::string& error ) const
	{
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
		if ( e.sym.hasLineStyle )
		{
			static const uint8_t tail[] = { 0x01, 0x00, 0x00, 0x00, 0x4c, 0x3e, 0xcc, 0xcc, 0xcc, 0xcc, 0xd0, 0xcc, 0x00, 0x00, 0x00, 0x00 };
			putU16( out, 0x100B ); // uživatelská linkage, 11 slov za hlavičkou
			putU16( out, 0x79F9 ); // id: styl čáry
			const uint32_t id = static_cast<uint32_t>( e.sym.lineStyleId );
			out.push_back( static_cast<uint8_t>( id & 0xFF ) );
			out.push_back( static_cast<uint8_t>( ( id >> 8 ) & 0xFF ) );
			out.push_back( static_cast<uint8_t>( ( id >> 16 ) & 0xFF ) );
			out.push_back( static_cast<uint8_t>( ( id >> 24 ) & 0xFF ) );
			out.insert( out.end(), std::begin( tail ), std::end( tail ) );
		}

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
}
