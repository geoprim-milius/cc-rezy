#include "Drawing.h"

#include <QSettings>
#include <QStringList>

#include <algorithm>

namespace Drawing
{
	namespace
	{
		std::vector<Layer> g_layers;
		bool g_loaded = false;

		DgnV7::Symbology mk( int level, int color, int weight, int ls )
		{
			DgnV7::Symbology s;
			s.level = level;
			s.color = color;
			s.weight = weight;
			s.style = 0;
			s.hasLineStyle = ( ls != 0 );
			s.lineStyleId = ls;
			return s;
		}

		QString symToString( const DgnV7::Symbology& s )
		{
			return QStringLiteral( "%1,%2,%3,%4,%5" ).arg( s.level ).arg( s.color ).arg( s.weight ).arg( s.style ).arg( s.hasLineStyle ? s.lineStyleId : 0 );
		}

		bool symFromString( const QString& str, DgnV7::Symbology& s )
		{
			const QStringList p = str.split( ',' );
			if ( p.size() < 5 )
			{
				return false;
			}
			s.level = p[0].toInt();
			s.color = p[1].toInt();
			s.weight = p[2].toInt();
			s.style = p[3].toInt();
			const int ls = p[4].toInt();
			s.hasLineStyle = ( ls != 0 );
			s.lineStyleId = ls;
			return true;
		}

		void load()
		{
			if ( g_loaded )
			{
				return;
			}
			g_loaded = true;
			QSettings st;
			const QStringList recs = st.value( QStringLiteral( "qRezy/layers" ) ).toStringList();
			for ( const QString& r : recs )
			{
				// id | název | #barva | plná | zakrytá
				const QStringList p = r.split( '|' );
				if ( p.size() < 5 )
				{
					continue;
				}
				Layer L;
				L.id = p[0].toInt();
				L.name = p[1];
				L.color = QColor( p[2] );
				if ( !symFromString( p[3], L.sym[0] ) || !symFromString( p[4], L.sym[1] ) )
				{
					continue;
				}
				g_layers.push_back( L );
			}
			if ( !g_layers.empty() )
			{
				return;
			}
			// první spuštění: výchozí hladiny + případná dřívější nastavení (qRezy/symbology)
			g_layers = defaultLayers();
			st.beginGroup( QStringLiteral( "qRezy/symbology" ) );
			for ( Layer& L : g_layers )
			{
				for ( int h = 0; h < 2; ++h )
				{
					const QString k = QStringLiteral( "c%1_%2_" ).arg( L.id ).arg( h );
					if ( !st.contains( k + "level" ) )
					{
						continue;
					}
					DgnV7::Symbology& y = L.sym[h];
					y.level = st.value( k + "level", y.level ).toInt();
					y.color = st.value( k + "color", y.color ).toInt();
					y.weight = st.value( k + "weight", y.weight ).toInt();
					y.style = st.value( k + "style", y.style ).toInt();
					y.hasLineStyle = st.value( k + "hasLs", y.hasLineStyle ).toBool();
					y.lineStyleId = st.value( k + "ls", y.lineStyleId ).toInt();
				}
			}
			st.endGroup();
		}
	}

	std::vector<Layer> defaultLayers()
	{
		// výchozí hodnoty podle výkresu vykresy_old.dgn (Moskevská půda)
		// styl čáry: -1 plná, -2 přerušovaná (trámy), -3 zakrytá
		std::vector<Layer> v;
		v.push_back( { Walls, QStringLiteral( "Zdi" ), QColor( 230, 30, 30 ), { mk( 2, 196, 1, -1 ), mk( 2, 196, 1, -3 ) } } );
		v.push_back( { Lintels, QStringLiteral( "Překlady" ), QColor( 30, 90, 255 ), { mk( 3, 83, 0, -1 ), mk( 3, 83, 0, -3 ) } } );
		v.push_back( { Beams, QStringLiteral( "Trámy" ), QColor( 200, 0, 220 ), { mk( 5, 6, 0, -1 ), mk( 5, 6, 0, -2 ) } } );
		v.push_back( { Details, QStringLiteral( "Drobné prvky" ), QColor( 0, 170, 60 ), { mk( 6, 0, 0, -1 ), mk( 6, 0, 0, -3 ) } } );
		return v;
	}

	const std::vector<Layer>& layers()
	{
		load();
		return g_layers;
	}

	void setLayers( const std::vector<Layer>& list )
	{
		load();
		if ( list.empty() )
		{
			return;
		}
		g_layers = list;
		QStringList recs;
		for ( const Layer& L : g_layers )
		{
			recs << QStringLiteral( "%1|%2|%3|%4|%5" ).arg( L.id ).arg( QString( L.name ).remove( '|' ) ).arg( L.color.name() ).arg( symToString( L.sym[0] ), symToString( L.sym[1] ) );
		}
		QSettings().setValue( QStringLiteral( "qRezy/layers" ), recs );
	}

	const Layer* layerById( int id )
	{
		for ( const Layer& L : layers() )
		{
			if ( L.id == id )
			{
				return &L;
			}
		}
		return nullptr;
	}

	int layerIdAt( int index )
	{
		const std::vector<Layer>& l = layers();
		return ( index >= 0 && index < static_cast<int>( l.size() ) ) ? l[static_cast<size_t>( index )].id : -1;
	}

	int layerIndex( int id )
	{
		const std::vector<Layer>& l = layers();
		for ( size_t i = 0; i < l.size(); ++i )
		{
			if ( l[i].id == id )
			{
				return static_cast<int>( i );
			}
		}
		return -1;
	}

	int newLayerId()
	{
		int m = 3;
		for ( const Layer& L : layers() )
		{
			m = std::max( m, L.id );
		}
		return m + 1;
	}

	QString categoryName( int cat )
	{
		if ( const Layer* L = layerById( cat ) )
		{
			return L->name;
		}
		return QStringLiteral( "Hladina %1" ).arg( cat );
	}

	QColor displayColor( int cat, bool hidden )
	{
		const Layer* L = layerById( cat );
		QColor c = L ? L->color : QColor( 160, 160, 160 );
		if ( hidden )
		{
			// zakryté prvky světlejší
			c = c.lighter( 160 );
		}
		return c;
	}

	const DgnV7::Symbology& SymbologyTable::get( int cat, bool hidden ) const
	{
		static const DgnV7::Symbology fallback;
		for ( const Layer& L : list )
		{
			if ( L.id == cat )
			{
				return L.sym[hidden ? 1 : 0];
			}
		}
		return list.empty() ? fallback : list.front().sym[hidden ? 1 : 0];
	}

	SymbologyTable SymbologyTable::load()
	{
		SymbologyTable t;
		t.list = layers();
		return t;
	}
}
