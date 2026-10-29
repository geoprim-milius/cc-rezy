#include "Drawing.h"

#include <QSettings>

namespace Drawing
{
	QString categoryName( int cat )
	{
		switch ( cat )
		{
		case Walls:
			return QStringLiteral( "Zdi" );
		case Lintels:
			return QStringLiteral( "Překlady" );
		case Beams:
			return QStringLiteral( "Trámy" );
		case Details:
			return QStringLiteral( "Drobné prvky" );
		default:
			return QStringLiteral( "?" );
		}
	}

	QColor displayColor( int cat, bool hidden )
	{
		QColor c;
		switch ( cat )
		{
		case Walls:
			c = QColor( 230, 30, 30 );
			break;
		case Lintels:
			c = QColor( 30, 90, 255 );
			break;
		case Beams:
			c = QColor( 200, 0, 220 );
			break;
		case Details:
			c = QColor( 0, 170, 60 );
			break;
		default:
			c = Qt::white;
		}
		if ( hidden )
		{
			// zakryté prvky světlejší
			c = c.lighter( 160 );
		}
		return c;
	}

	SymbologyTable SymbologyTable::defaults()
	{
		// výchozí hodnoty podle výkresu vykresy_old.dgn (Moskevská půda)
		// styl čáry: -1 plná, -2 přerušovaná (trámy), -3 zakrytá
		SymbologyTable t;
		auto set = []( DgnV7::Symbology& s, int level, int color, int weight, int ls )
		{
			s.level = level;
			s.color = color;
			s.weight = weight;
			s.style = 0;
			s.hasLineStyle = true;
			s.lineStyleId = ls;
		};
		set( t.sym[Walls][0], 2, 196, 1, -1 );
		set( t.sym[Walls][1], 2, 196, 1, -3 );
		set( t.sym[Lintels][0], 3, 83, 0, -1 );
		set( t.sym[Lintels][1], 3, 83, 0, -3 );
		set( t.sym[Beams][0], 5, 6, 0, -1 );
		set( t.sym[Beams][1], 5, 6, 0, -2 );
		set( t.sym[Details][0], 6, 0, 0, -1 );
		set( t.sym[Details][1], 6, 0, 0, -3 );
		return t;
	}

	SymbologyTable SymbologyTable::load()
	{
		SymbologyTable t = defaults();
		QSettings s;
		s.beginGroup( QStringLiteral( "qRezy/symbology" ) );
		for ( int c = 0; c < CategoryCount; ++c )
		{
			for ( int h = 0; h < 2; ++h )
			{
				const QString k = QStringLiteral( "c%1_%2_" ).arg( c ).arg( h );
				DgnV7::Symbology& y = t.sym[c][h];
				y.level = s.value( k + "level", y.level ).toInt();
				y.color = s.value( k + "color", y.color ).toInt();
				y.weight = s.value( k + "weight", y.weight ).toInt();
				y.style = s.value( k + "style", y.style ).toInt();
				y.hasLineStyle = s.value( k + "hasLs", y.hasLineStyle ).toBool();
				y.lineStyleId = s.value( k + "ls", y.lineStyleId ).toInt();
			}
		}
		s.endGroup();
		return t;
	}

	void SymbologyTable::save() const
	{
		QSettings s;
		s.beginGroup( QStringLiteral( "qRezy/symbology" ) );
		for ( int c = 0; c < CategoryCount; ++c )
		{
			for ( int h = 0; h < 2; ++h )
			{
				const QString k = QStringLiteral( "c%1_%2_" ).arg( c ).arg( h );
				const DgnV7::Symbology& y = sym[c][h];
				s.setValue( k + "level", y.level );
				s.setValue( k + "color", y.color );
				s.setValue( k + "weight", y.weight );
				s.setValue( k + "style", y.style );
				s.setValue( k + "hasLs", y.hasLineStyle );
				s.setValue( k + "ls", y.lineStyleId );
			}
		}
		s.endGroup();
	}
}
