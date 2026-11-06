#include "Tiff.h"

#include <QFile>
#include <QTextStream>

#include <algorithm>

#include <unordered_map>

namespace Tiff
{
	namespace
	{
		struct BitWriter
		{
			std::vector<uint8_t>& out;
			uint32_t acc = 0;
			int nacc = 0;
			void put( int code, int nbits )
			{
				// MSB napřed (TIFF LZW)
				acc = ( acc << nbits ) | static_cast<uint32_t>( code );
				nacc += nbits;
				while ( nacc >= 8 )
				{
					out.push_back( static_cast<uint8_t>( ( acc >> ( nacc - 8 ) ) & 0xFF ) );
					nacc -= 8;
				}
				acc &= ( 1u << nacc ) - 1u;
			}
			void flush()
			{
				if ( nacc > 0 )
				{
					out.push_back( static_cast<uint8_t>( ( acc << ( 8 - nacc ) ) & 0xFF ) );
					nacc = 0;
					acc = 0;
				}
			}
		};

		void put16( std::vector<uint8_t>& b, uint16_t v )
		{
			b.push_back( static_cast<uint8_t>( v & 0xFF ) );
			b.push_back( static_cast<uint8_t>( v >> 8 ) );
		}
		void put32( std::vector<uint8_t>& b, uint32_t v )
		{
			for ( int i = 0; i < 4; ++i )
			{
				b.push_back( static_cast<uint8_t>( ( v >> ( 8 * i ) ) & 0xFF ) );
			}
		}
		void set32( std::vector<uint8_t>& b, size_t pos, uint32_t v )
		{
			for ( int i = 0; i < 4; ++i )
			{
				b[pos + static_cast<size_t>( i )] = static_cast<uint8_t>( ( v >> ( 8 * i ) ) & 0xFF );
			}
		}
	}

	std::vector<uint8_t> lzw( const uint8_t* data, size_t n )
	{
		// stejně jako libtiff: kód se rozšíří, když počet položek překročí maxcode; při 4094 položkách Clear
		constexpr int Clear = 256;
		constexpr int Eoi = 257;
		constexpr int First = 258;
		constexpr int CodeMax = 4095;
		std::vector<uint8_t> out;
		out.reserve( n / 2 + 16 );
		BitWriter bw{ out };
		std::unordered_map<uint32_t, int> table;
		table.reserve( 8192 );
		int nbits = 9;
		int maxcode = 511;
		int freeEnt = First;
		bw.put( Clear, nbits );
		int ent = -1;
		auto reset = [&]()
		{
			table.clear();
			freeEnt = First;
			bw.put( Clear, nbits );
			nbits = 9;
			maxcode = 511;
		};
		for ( size_t i = 0; i < n; ++i )
		{
			const int c = data[i];
			if ( ent < 0 )
			{
				ent = c;
				continue;
			}
			const uint32_t key = ( static_cast<uint32_t>( ent ) << 8 ) | static_cast<uint32_t>( c );
			auto it = table.find( key );
			if ( it != table.end() )
			{
				ent = it->second;
				continue;
			}
			bw.put( ent, nbits );
			if ( freeEnt == CodeMax - 1 )
			{
				reset();
			}
			else
			{
				table.emplace( key, freeEnt++ );
				if ( freeEnt > maxcode )
				{
					++nbits;
					maxcode = ( 1 << nbits ) - 1;
				}
			}
			ent = c;
		}
		if ( ent >= 0 )
		{
			bw.put( ent, nbits );
			++freeEnt;
			if ( freeEnt == CodeMax - 1 )
			{
				reset();
			}
			else if ( freeEnt > maxcode )
			{
				++nbits;
				maxcode = ( 1 << nbits ) - 1;
			}
		}
		bw.put( Eoi, nbits );
		bw.flush();
		return out;
	}

	bool writeRgb( const QString& path, int w, int h, const std::vector<uint8_t>& rgb, double dpi, QString& err )
	{
		if ( w <= 0 || h <= 0 || rgb.size() < static_cast<size_t>( w ) * h * 3 )
		{
			err = QStringLiteral( "Prázdný obrázek" );
			return false;
		}
		const size_t rowBytes = static_cast<size_t>( w ) * 3;
		const int rowsPerStrip = std::max( 1, static_cast<int>( 65536 / rowBytes ) );
		const int strips = ( h + rowsPerStrip - 1 ) / rowsPerStrip;

		std::vector<uint8_t> b;
		b.reserve( rgb.size() / 3 + 4096 );
		// hlavička: little endian, IFD hned za ní
		b.push_back( 'I' );
		b.push_back( 'I' );
		put16( b, 42 );
		put32( b, 8 );
		struct Entry
		{
			uint16_t tag;
			uint16_t type;
			uint32_t count;
			uint32_t value;
		};
		const int nEntries = 13;
		const size_t ifdPos = b.size();
		const size_t ifdSize = 2 + nEntries * 12 + 4;
		b.resize( ifdPos + ifdSize, 0 );
		// pomocná data za IFD
		const size_t bpsPos = b.size();
		put16( b, 8 );
		put16( b, 8 );
		put16( b, 8 );
		const size_t resPos = b.size();
		const uint32_t dpiI = static_cast<uint32_t>( std::max( 1.0, dpi ) * 100.0 );
		put32( b, dpiI );
		put32( b, 100 );
		const size_t offPos = b.size();
		b.resize( b.size() + static_cast<size_t>( strips ) * 4, 0 );
		const size_t cntPos = b.size();
		b.resize( b.size() + static_cast<size_t>( strips ) * 4, 0 );
		// pruhy
		for ( int s = 0; s < strips; ++s )
		{
			const int r0 = s * rowsPerStrip;
			const int r1 = std::min( h, r0 + rowsPerStrip );
			const std::vector<uint8_t> z = lzw( rgb.data() + static_cast<size_t>( r0 ) * rowBytes, static_cast<size_t>( r1 - r0 ) * rowBytes );
			if ( b.size() & 1 )
			{
				b.push_back( 0 );
			}
			set32( b, offPos + static_cast<size_t>( s ) * 4, static_cast<uint32_t>( b.size() ) );
			set32( b, cntPos + static_cast<size_t>( s ) * 4, static_cast<uint32_t>( z.size() ) );
			b.insert( b.end(), z.begin(), z.end() );
			if ( b.size() > 0xF0000000u )
			{
				err = QStringLiteral( "Obrázek je příliš velký (přes 4 GB) – zvětšete velikost pixelu" );
				return false;
			}
		}
		const Entry entries[nEntries] = {
			{ 256, 4, 1, static_cast<uint32_t>( w ) },                                   // šířka
			{ 257, 4, 1, static_cast<uint32_t>( h ) },                                   // výška
			{ 258, 3, 3, static_cast<uint32_t>( bpsPos ) },                              // 8,8,8
			{ 259, 3, 1, 5 },                                                            // LZW
			{ 262, 3, 1, 2 },                                                            // RGB
			{ 273, 4, static_cast<uint32_t>( strips ), static_cast<uint32_t>( strips == 1 ? 0 : offPos ) },
			{ 277, 3, 1, 3 },                                                            // vzorky na pixel
			{ 278, 4, 1, static_cast<uint32_t>( rowsPerStrip ) },
			{ 279, 4, static_cast<uint32_t>( strips ), static_cast<uint32_t>( strips == 1 ? 0 : cntPos ) },
			{ 282, 5, 1, static_cast<uint32_t>( resPos ) },
			{ 283, 5, 1, static_cast<uint32_t>( resPos ) },
			{ 284, 3, 1, 1 },                                                            // prokládání pixelů
			{ 296, 3, 1, 2 },                                                            // palce
		};
		size_t p = ifdPos;
		b[p++] = static_cast<uint8_t>( nEntries & 0xFF );
		b[p++] = 0;
		for ( const Entry& e : entries )
		{
			b[p++] = static_cast<uint8_t>( e.tag & 0xFF );
			b[p++] = static_cast<uint8_t>( e.tag >> 8 );
			b[p++] = static_cast<uint8_t>( e.type & 0xFF );
			b[p++] = 0;
			set32( b, p, e.count );
			p += 4;
			uint32_t v = e.value;
			if ( ( e.tag == 273 || e.tag == 279 ) && strips == 1 )
			{
				// jediný pruh: hodnota přímo v položce
				v = e.tag == 273 ? static_cast<uint32_t>( b[offPos] | ( b[offPos + 1] << 8 ) | ( b[offPos + 2] << 16 ) | ( static_cast<uint32_t>( b[offPos + 3] ) << 24 ) )
				                 : static_cast<uint32_t>( b[cntPos] | ( b[cntPos + 1] << 8 ) | ( b[cntPos + 2] << 16 ) | ( static_cast<uint32_t>( b[cntPos + 3] ) << 24 ) );
			}
			if ( e.type == 3 && e.count == 1 )
			{
				// SHORT v levé části pole
				b[p] = static_cast<uint8_t>( v & 0xFF );
				b[p + 1] = static_cast<uint8_t>( ( v >> 8 ) & 0xFF );
				b[p + 2] = 0;
				b[p + 3] = 0;
			}
			else
			{
				set32( b, p, v );
			}
			p += 4;
		}
		set32( b, p, 0 ); // další IFD není

		QFile f( path );
		if ( !f.open( QIODevice::WriteOnly | QIODevice::Truncate ) || f.write( reinterpret_cast<const char*>( b.data() ), static_cast<qint64>( b.size() ) ) != static_cast<qint64>( b.size() ) )
		{
			err = QStringLiteral( "Soubor nelze zapsat: %1" ).arg( path );
			return false;
		}
		return true;
	}

	bool writeWorld( const QString& path, double px, double xUL, double yUL, QString& err )
	{
		QFile f( path );
		if ( !f.open( QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text ) )
		{
			err = QStringLiteral( "Soubor nelze zapsat: %1" ).arg( path );
			return false;
		}
		QTextStream ts( &f );
		ts.setRealNumberNotation( QTextStream::FixedNotation );
		ts.setRealNumberPrecision( 6 );
		ts << px << "\n" << 0.0 << "\n" << 0.0 << "\n" << -px << "\n";
		ts.setRealNumberPrecision( 4 );
		ts << xUL << "\n" << yUL << "\n";
		return true;
	}
}
