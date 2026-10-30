#include "Icons.h"

#include <QPainter>
#include <QPainterPath>
#include <QPixmap>

#include <cmath>

namespace Icons
{
	namespace
	{
		const QColor Ink( 40, 40, 40 );
		const QColor Accent( 220, 50, 40 );
		const QColor Blue( 30, 110, 220 );
		const QColor Green( 30, 150, 60 );

		void arrowHead( QPainter& p, QPointF tip, QPointF from, double size )
		{
			const QPointF d = tip - from;
			const double len = std::hypot( d.x(), d.y() );
			if ( len < 1e-6 )
				return;
			const QPointF u( d.x() / len, d.y() / len );
			const QPointF n( -u.y(), u.x() );
			QPolygonF tri;
			tri << tip << ( tip - u * size + n * size * 0.5 ) << ( tip - u * size - n * size * 0.5 );
			p.setBrush( p.pen().color() );
			p.drawPolygon( tri );
			p.setBrush( Qt::NoBrush );
		}

		void drawIcon( QPainter& p, const QString& n )
		{
			QPen pen( Ink, 2.2, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin );
			p.setPen( pen );

			if ( n == "draw" ) // tužka
			{
				p.setPen( QPen( Ink, 2 ) );
				QPolygonF body;
				body << QPointF( 6, 26 ) << QPointF( 8, 20 ) << QPointF( 22, 6 ) << QPointF( 26, 10 ) << QPointF( 12, 24 );
				p.setBrush( QColor( 250, 200, 60 ) );
				p.drawPolygon( body );
				p.setBrush( Qt::NoBrush );
				p.drawLine( QPointF( 8, 20 ), QPointF( 12, 24 ) );
			}
			else if ( n == "line" )
			{
				p.drawLine( QPointF( 6, 26 ), QPointF( 26, 6 ) );
				p.setPen( QPen( Accent, 2 ) );
				p.drawRect( QRectF( 3.5, 23.5, 5, 5 ) );
				p.drawRect( QRectF( 23.5, 3.5, 5, 5 ) );
			}
			else if ( n == "move" )
			{
				p.drawLine( QPointF( 4, 16 ), QPointF( 28, 16 ) );
				p.drawLine( QPointF( 16, 4 ), QPointF( 16, 28 ) );
				arrowHead( p, QPointF( 29, 16 ), QPointF( 4, 16 ), 7 );
				arrowHead( p, QPointF( 3, 16 ), QPointF( 28, 16 ), 7 );
				arrowHead( p, QPointF( 16, 3 ), QPointF( 16, 28 ), 7 );
				arrowHead( p, QPointF( 16, 29 ), QPointF( 16, 4 ), 7 );
			}
			else if ( n == "copy" )
			{
				p.drawLine( QPointF( 4, 22 ), QPointF( 18, 8 ) );
				p.setPen( QPen( Blue, 2.2 ) );
				p.drawLine( QPointF( 14, 28 ), QPointF( 28, 14 ) );
				p.setPen( QPen( Ink, 1.5, Qt::DashLine ) );
				p.drawLine( QPointF( 11, 15 ), QPointF( 21, 21 ) );
				arrowHead( p, QPointF( 22, 21.5 ), QPointF( 11, 15 ), 6 );
			}
			else if ( n == "offset" )
			{
				p.drawLine( QPointF( 4, 10 ), QPointF( 28, 10 ) );
				p.setPen( QPen( Blue, 2.2 ) );
				p.drawLine( QPointF( 4, 24 ), QPointF( 28, 24 ) );
				p.setPen( QPen( Ink, 1.5 ) );
				p.drawLine( QPointF( 16, 11 ), QPointF( 16, 23 ) );
				arrowHead( p, QPointF( 16, 23 ), QPointF( 16, 12 ), 5 );
				arrowHead( p, QPointF( 16, 11 ), QPointF( 16, 22 ), 5 );
			}
			else if ( n == "extend" )
			{
				p.drawLine( QPointF( 4, 16 ), QPointF( 18, 16 ) );
				p.setPen( QPen( Blue, 2.2, Qt::DashLine ) );
				p.drawLine( QPointF( 18, 16 ), QPointF( 26, 16 ) );
				p.setPen( QPen( Ink, 2.2 ) );
				p.drawLine( QPointF( 28, 6 ), QPointF( 28, 26 ) );
				p.setPen( QPen( Blue, 2 ) );
				arrowHead( p, QPointF( 27, 16 ), QPointF( 18, 16 ), 6 );
			}
			else if ( n == "corner" )
			{
				p.drawLine( QPointF( 6, 26 ), QPointF( 6, 8 ) );
				p.drawLine( QPointF( 10, 6 ), QPointF( 26, 6 ) );
				p.setPen( QPen( Blue, 2.2, Qt::DashLine ) );
				p.drawLine( QPointF( 6, 8 ), QPointF( 6, 6 ) );
				p.drawLine( QPointF( 6, 6 ), QPointF( 10, 6 ) );
				p.setPen( QPen( Accent, 2 ) );
				p.drawEllipse( QPointF( 6, 6 ), 3, 3 );
			}
			else if ( n == "break" )
			{
				// čára s vymazanou částí uprostřed
				p.drawLine( QPointF( 3, 22 ), QPointF( 12, 16 ) );
				p.drawLine( QPointF( 20, 11 ), QPointF( 29, 5 ) );
				p.setPen( QPen( Accent, 1.5, Qt::DashLine ) );
				p.drawLine( QPointF( 12, 16 ), QPointF( 20, 11 ) );
				p.setPen( QPen( Blue, 2 ) );
				p.drawLine( QPointF( 12, 20 ), QPointF( 12, 26 ) );
				p.drawLine( QPointF( 20, 15 ), QPointF( 20, 21 ) );
			}
			else if ( n == "trim" )
			{
				// hranice (svislá) a čáry oříznuté / prodloužené k ní
				p.setPen( QPen( Blue, 2.4 ) );
				p.drawLine( QPointF( 20, 3 ), QPointF( 20, 29 ) );
				p.setPen( QPen( Ink, 2.2 ) );
				p.drawLine( QPointF( 3, 10 ), QPointF( 20, 10 ) );
				p.drawLine( QPointF( 3, 22 ), QPointF( 13, 22 ) );
				p.setPen( QPen( Accent, 1.5, Qt::DashLine ) );
				p.drawLine( QPointF( 20, 10 ), QPointF( 29, 10 ) );
				p.setPen( QPen( Ink, 1.5, Qt::DashLine ) );
				p.drawLine( QPointF( 13, 22 ), QPointF( 20, 22 ) );
			}
			else if ( n == "delete" )
			{
				p.setPen( QPen( Ink, 2.2 ) );
				p.drawLine( QPointF( 4, 22 ), QPointF( 28, 10 ) );
				p.setPen( QPen( Accent, 3 ) );
				p.drawLine( QPointF( 10, 8 ), QPointF( 22, 24 ) );
				p.drawLine( QPointF( 22, 8 ), QPointF( 10, 24 ) );
			}
			else if ( n == "measure" )
			{
				p.setPen( QPen( Ink, 1.8 ) );
				p.setBrush( QColor( 250, 220, 120 ) );
				p.drawRect( QRectF( 3, 11, 26, 10 ) );
				p.setBrush( Qt::NoBrush );
				for ( int i = 0; i < 6; ++i )
				{
					const double x = 6 + i * 4;
					p.drawLine( QPointF( x, 11 ), QPointF( x, i % 2 ? 15 : 17 ) );
				}
			}
			else if ( n == "undo" )
			{
				QPainterPath path;
				path.moveTo( 8, 14 );
				path.cubicTo( 14, 4, 28, 8, 26, 20 );
				path.cubicTo( 25, 26, 18, 28, 14, 26 );
				p.drawPath( path );
				arrowHead( p, QPointF( 6, 12 ), QPointF( 12, 9 ), 8 );
				p.setBrush( Ink );
				QPolygonF t;
				t << QPointF( 4, 8 ) << QPointF( 13, 8 ) << QPointF( 7, 16 );
				p.drawPolygon( t );
				p.setBrush( Qt::NoBrush );
			}
			else if ( n == "ortho" )
			{
				p.drawLine( QPointF( 6, 4 ), QPointF( 6, 26 ) );
				p.drawLine( QPointF( 6, 26 ), QPointF( 28, 26 ) );
				p.setPen( QPen( Accent, 1.8 ) );
				p.drawRect( QRectF( 6, 19, 7, 7 ) );
			}
			else if ( n == "tracking" )
			{
				p.setPen( QPen( Green, 1.6, Qt::DashLine ) );
				p.drawLine( QPointF( 2, 12 ), QPointF( 30, 12 ) );
				p.drawLine( QPointF( 20, 2 ), QPointF( 20, 30 ) );
				p.setPen( QPen( Green, 2.2 ) );
				p.drawLine( QPointF( 4, 8 ), QPointF( 4, 16 ) );
				p.drawLine( QPointF( 0, 12 ), QPointF( 8, 12 ) );
				p.setPen( QPen( Accent, 2 ) );
				p.drawLine( QPointF( 16, 8 ), QPointF( 24, 16 ) );
				p.drawLine( QPointF( 24, 8 ), QPointF( 16, 16 ) );
			}
			else if ( n == "hidden" )
			{
				p.setPen( QPen( Ink, 2.4, Qt::DashLine ) );
				p.drawLine( QPointF( 3, 16 ), QPointF( 29, 16 ) );
			}
			else if ( n == "rotate" )
			{
				p.drawArc( QRectF( 5, 5, 22, 22 ), 30 * 16, 280 * 16 );
				arrowHead( p, QPointF( 26, 10 ), QPointF( 22, 6 ), 7 );
				p.setPen( QPen( Accent, 2 ) );
				p.drawLine( QPointF( 10, 20 ), QPointF( 22, 14 ) );
			}
			else if ( n == "north" )
			{
				p.drawLine( QPointF( 16, 28 ), QPointF( 16, 6 ) );
				arrowHead( p, QPointF( 16, 3 ), QPointF( 16, 20 ), 9 );
				QFont f = p.font();
				f.setPixelSize( 10 );
				f.setBold( true );
				p.setFont( f );
				p.drawText( QRectF( 19, 16, 12, 12 ), Qt::AlignCenter, QStringLiteral( "S" ) );
			}
			else if ( n == "section" )
			{
				p.setPen( QPen( QColor( 0, 170, 170 ), 2.2, Qt::DashDotLine ) );
				p.drawLine( QPointF( 8, 26 ), QPointF( 24, 6 ) );
				p.setPen( QPen( Ink, 2 ) );
				p.drawLine( QPointF( 8, 26 ), QPointF( 2, 21 ) );
				arrowHead( p, QPointF( 1, 20 ), QPointF( 8, 26 ), 6 );
				p.drawLine( QPointF( 24, 6 ), QPointF( 18, 1 ) );
				arrowHead( p, QPointF( 17, 0 ), QPointF( 24, 6 ), 6 );
			}
			else if ( n == "facade" )
			{
				p.setPen( QPen( Ink, 1.8 ) );
				p.drawRect( QRectF( 8, 10, 20, 18 ) );
				p.drawRect( QRectF( 12, 14, 5, 5 ) );
				p.drawRect( QRectF( 20, 14, 5, 5 ) );
				p.drawLine( QPointF( 8, 10 ), QPointF( 18, 3 ) );
				p.drawLine( QPointF( 18, 3 ), QPointF( 28, 10 ) );
				p.setPen( QPen( Blue, 2 ) );
				p.drawLine( QPointF( 2, 6 ), QPointF( 2, 28 ) );
				arrowHead( p, QPointF( 7, 18 ), QPointF( 2, 18 ), 5 );
			}
			else if ( n == "3d" )
			{
				p.setPen( QPen( Ink, 1.8 ) );
				QPolygonF top;
				top << QPointF( 16, 4 ) << QPointF( 28, 10 ) << QPointF( 16, 16 ) << QPointF( 4, 10 );
				p.setBrush( QColor( 200, 220, 250 ) );
				p.drawPolygon( top );
				p.setBrush( QColor( 150, 180, 230 ) );
				QPolygonF left;
				left << QPointF( 4, 10 ) << QPointF( 16, 16 ) << QPointF( 16, 28 ) << QPointF( 4, 22 );
				p.drawPolygon( left );
				p.setBrush( QColor( 110, 150, 210 ) );
				QPolygonF right;
				right << QPointF( 28, 10 ) << QPointF( 16, 16 ) << QPointF( 16, 28 ) << QPointF( 28, 22 );
				p.drawPolygon( right );
				p.setBrush( Qt::NoBrush );
			}
			else if ( n == "save" )
			{
				p.setPen( QPen( Ink, 1.8 ) );
				p.setBrush( QColor( 70, 110, 190 ) );
				p.drawRoundedRect( QRectF( 4, 4, 24, 24 ), 2, 2 );
				p.setBrush( QColor( 235, 235, 235 ) );
				p.drawRect( QRectF( 9, 4, 14, 9 ) );
				p.setBrush( Qt::white );
				p.drawRect( QRectF( 8, 17, 16, 11 ) );
				p.setBrush( Qt::NoBrush );
				QFont f = p.font();
				f.setPixelSize( 8 );
				f.setBold( true );
				p.setFont( f );
				p.drawText( QRectF( 8, 17, 16, 11 ), Qt::AlignCenter, QStringLiteral( "V7" ) );
			}
			else if ( n == "cloud" )
			{
				p.setPen( Qt::NoPen );
				p.setBrush( QColor( 90, 90, 90 ) );
				const double pts[][2] = { { 6, 20 }, { 9, 14 }, { 12, 22 }, { 15, 10 }, { 17, 18 }, { 20, 13 }, { 23, 21 }, { 26, 16 }, { 11, 17 }, { 19, 24 }, { 14, 15 }, { 24, 10 } };
				for ( const auto& q : pts )
					p.drawEllipse( QPointF( q[0], q[1] ), 1.6, 1.6 );
				p.setBrush( Qt::NoBrush );
			}
			else if ( n == "trash" )
			{
				p.setPen( QPen( Ink, 1.8 ) );
				p.drawRect( QRectF( 9, 10, 14, 18 ) );
				p.drawLine( QPointF( 6, 8 ), QPointF( 26, 8 ) );
				p.drawLine( QPointF( 13, 5 ), QPointF( 19, 5 ) );
				p.drawLine( QPointF( 13, 13 ), QPointF( 13, 25 ) );
				p.drawLine( QPointF( 19, 13 ), QPointF( 19, 25 ) );
			}
			else if ( n == "floor" )
			{
				p.drawLine( QPointF( 3, 24 ), QPointF( 29, 24 ) );
				p.setPen( QPen( Accent, 2 ) );
				p.drawLine( QPointF( 16, 6 ), QPointF( 16, 22 ) );
				arrowHead( p, QPointF( 16, 23 ), QPointF( 16, 8 ), 7 );
			}
			else if ( n == "up" || n == "down" )
			{
				const bool up = ( n == "up" );
				p.setPen( QPen( Ink, 1.6 ) );
				p.drawLine( QPointF( 4, 16 ), QPointF( 20, 16 ) );
				p.setPen( QPen( Blue, 2.4 ) );
				p.drawLine( QPointF( 25, up ? 26 : 6 ), QPointF( 25, up ? 8 : 24 ) );
				arrowHead( p, QPointF( 25, up ? 4 : 28 ), QPointF( 25, up ? 20 : 12 ), 8 );
			}
			else if ( n == "plus" || n == "minus" )
			{
				p.setPen( QPen( Ink, 3 ) );
				p.drawLine( QPointF( 7, 16 ), QPointF( 25, 16 ) );
				if ( n == "plus" )
					p.drawLine( QPointF( 16, 7 ), QPointF( 16, 25 ) );
			}
			else
			{
				p.drawEllipse( QRectF( 6, 6, 20, 20 ) );
			}
		}
	}

	QIcon get( const QString& name )
	{
		QIcon icon;
		for ( int size : { 32, 64 } )
		{
			QPixmap pm( size, size );
			pm.fill( Qt::transparent );
			QPainter p( &pm );
			p.setRenderHint( QPainter::Antialiasing, true );
			p.scale( size / 32.0, size / 32.0 );
			drawIcon( p, name );
			p.end();
			icon.addPixmap( pm );
		}
		return icon;
	}

	QIcon colorSwatch( const QColor& c, bool dashed )
	{
		QPixmap pm( 32, 32 );
		pm.fill( Qt::transparent );
		QPainter p( &pm );
		p.setRenderHint( QPainter::Antialiasing, true );
		p.setPen( QPen( c, 5, dashed ? Qt::DashLine : Qt::SolidLine, Qt::FlatCap ) );
		p.drawLine( QPointF( 2, 16 ), QPointF( 30, 16 ) );
		p.end();
		return QIcon( pm );
	}
}
