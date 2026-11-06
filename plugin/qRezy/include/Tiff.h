#pragma once

// Zápis barevného TIFF (RGB 8 bitů, komprese LZW – čte ho MicroStation i GIS) a world file (.tfw).

#include <QString>

#include <cstdint>
#include <vector>

namespace Tiff
{
	//! rgb = w*h*3 bajtů po řádcích shora dolů
	bool writeRgb( const QString& path, int w, int h, const std::vector<uint8_t>& rgb, double dpi, QString& err );
	//! world file: velikost pixelu a střed levého horního pixelu
	bool writeWorld( const QString& path, double px, double xUL, double yUL, QString& err );
	//! LZW podle TIFF 6.0 (pro test)
	std::vector<uint8_t> lzw( const uint8_t* data, size_t n );
}
