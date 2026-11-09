#include "Icons.h"

#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
#include <QSettings>

#include <cmath>

namespace Icons
{
	namespace
	{
		// barvy ikon podle vzhledu (qRezy/theme: "", "acad", "geoprim")
		QColor Ink( 40, 40, 40 );
		QColor Accent( 220, 50, 40 );
		QColor Blue( 30, 110, 220 );
		QColor Green( 30, 150, 60 );

		void applyTheme()
		{
			const QString t = QSettings().value( QStringLiteral( "qRezy/theme" ) ).toString();
			if ( t == QLatin1String( "acad" ) )
			{
				Ink = QColor( 220, 226, 234 );
				Accent = QColor( 240, 90, 80 );
				Blue = QColor( 80, 165, 255 );
				Green = QColor( 90, 200, 120 );
			}
			else if ( t == QLatin1String( "geoprim" ) )
			{
				Ink = QColor( 236, 231, 223 );
				Accent = QColor( 214, 120, 90 );
				Blue = QColor( 201, 164, 106 );
				Green = QColor( 168, 190, 140 );
			}
			else
			{
				Ink = QColor( 40, 40, 40 );
				Accent = QColor( 220, 50, 40 );
				Blue = QColor( 30, 110, 220 );
				Green = QColor( 30, 150, 60 );
			}
		}

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
			else if ( n == "project" )
			{
				// složka s mračnem (rozdělaná práce)
				p.setPen( QPen( Ink, 1.6 ) );
				p.setBrush( QColor( 250, 210, 110 ) );
				QPainterPath path;
				path.moveTo( 3, 9 );
				path.lineTo( 12, 9 );
				path.lineTo( 14, 6 );
				path.lineTo( 29, 6 );
				path.lineTo( 29, 27 );
				path.lineTo( 3, 27 );
				path.closeSubpath();
				p.drawPath( path );
				p.setBrush( Qt::NoBrush );
				p.setPen( QPen( Blue, 2.2 ) );
				for ( const QPointF& c : { QPointF( 10, 17 ), QPointF( 15, 21 ), QPointF( 20, 15 ), QPointF( 23, 21 ), QPointF( 12, 22 ) } )
				{
					p.drawPoint( c );
				}
			}
			else if ( n == "arc" )
			{
				p.setPen( QPen( Blue, 2.4, Qt::SolidLine, Qt::RoundCap ) );
				p.drawArc( QRectF( 5, 8, 22, 22 ), 0, 180 * 16 );
				p.setPen( Qt::NoPen );
				p.setBrush( Ink );
				for ( const QPointF& c : { QPointF( 5, 19 ), QPointF( 27, 19 ), QPointF( 16, 19 ) } )
				{
					p.drawEllipse( c, 2.2, 2.2 );
				}
				p.setBrush( Qt::NoBrush );
			}
			else if ( n == "cloudline" )
			{
				// body mračna podél zdi a proložená čára
				p.setPen( Qt::NoPen );
				p.setBrush( Ink );
				const QPointF pts[] = { { 5, 24 }, { 8, 22.5 }, { 10, 21.8 }, { 13, 19.6 }, { 15, 19.2 }, { 18, 17 }, { 20, 16.4 }, { 23, 14.2 }, { 26, 13 }, { 7, 23.8 }, { 12, 20.9 }, { 17, 18.2 }, { 22, 15.3 }, { 9, 6 }, { 24, 25 } };
				for ( const QPointF& c : pts )
				{
					p.drawEllipse( c, 1.1, 1.1 );
				}
				p.setBrush( Qt::NoBrush );
				p.setPen( QPen( Accent, 2.2, Qt::SolidLine, Qt::RoundCap ) );
				p.drawLine( QPointF( 4, 22.5 ), QPointF( 28, 10.5 ) );
			}
			else if ( n == "open" )
			{
				// otevřít složku (šipka nahoru)
				p.setPen( QPen( Ink, 1.8 ) );
				QPainterPath path;
				path.moveTo( 3, 9 );
				path.lineTo( 12, 9 );
				path.lineTo( 14, 12 );
				path.lineTo( 29, 12 );
				path.lineTo( 29, 27 );
				path.lineTo( 3, 27 );
				path.closeSubpath();
				p.drawPath( path );
				p.setPen( QPen( Blue, 2.4, Qt::SolidLine, Qt::RoundCap ) );
				p.drawLine( QPointF( 16, 24 ), QPointF( 16, 15 ) );
				p.drawLine( QPointF( 12.5, 18.5 ), QPointF( 16, 15 ) );
				p.drawLine( QPointF( 19.5, 18.5 ), QPointF( 16, 15 ) );
			}
			else if ( n == "layers" )
			{
				p.setPen( QPen( Ink, 1.8 ) );
				for ( int i = 2; i >= 0; --i )
				{
					QPolygonF g;
					g << QPointF( 16, 5 + i * 6 ) << QPointF( 28, 11 + i * 6 ) << QPointF( 16, 17 + i * 6 ) << QPointF( 4, 11 + i * 6 );
					p.setBrush( i == 0 ? QBrush( Blue ) : QBrush( Qt::NoBrush ) );
					p.drawPolygon( g );
				}
				p.setBrush( Qt::NoBrush );
			}
			else if ( n == "theme" )
			{
				p.setPen( QPen( Ink, 2 ) );
				p.drawEllipse( QRectF( 5, 5, 22, 22 ) );
				p.setBrush( Ink );
				p.drawPie( QRectF( 5, 5, 22, 22 ), 90 * 16, 180 * 16 );
				p.setBrush( Qt::NoBrush );
			}
			else if ( n == "bgdark" || n == "bglight" )
			{
				const bool dk = n == "bgdark";
				p.setPen( QPen( Ink, 1.6 ) );
				p.setBrush( dk ? QColor( 25, 30, 38 ) : QColor( 245, 245, 245 ) );
				p.drawRect( QRectF( 3.5, 6.5, 25, 19 ) );
				p.setPen( Qt::NoPen );
				p.setBrush( dk ? QColor( 230, 230, 230 ) : QColor( 40, 40, 40 ) );
				const QPointF pts[] = { { 8, 11 }, { 13, 19 }, { 18, 13 }, { 23, 20 }, { 11, 15 }, { 21, 10 }, { 16, 22 }, { 25, 15 } };
				for ( const QPointF& c : pts )
				{
					p.drawEllipse( c, 1.3, 1.3 );
				}
				p.setBrush( Qt::NoBrush );
			}
			else if ( n == "workmode" )
			{
				p.setPen( QPen( Ink, 1.8 ) );
				p.drawRect( QRectF( 3.5, 5.5, 25, 21 ) );
				p.setPen( Qt::NoPen );
				p.setBrush( Blue );
				p.drawRect( QRectF( 4.5, 6.5, 23, 5 ) );
				p.setBrush( Qt::NoBrush );
				p.setPen( QPen( Ink, 1.4, Qt::DashLine ) );
				p.drawLine( QPointF( 9, 21 ), QPointF( 23, 16 ) );
			}
			else if ( n == "rename" )
			{
				p.setPen( QPen( Ink, 1.6 ) );
				p.drawRect( QRectF( 3.5, 10.5, 25, 12 ) );
				p.setPen( QPen( Blue, 2.2 ) );
				p.drawLine( QPointF( 9, 8 ), QPointF( 9, 25 ) );
				p.drawLine( QPointF( 6.5, 8 ), QPointF( 11.5, 8 ) );
				p.drawLine( QPointF( 6.5, 25 ), QPointF( 11.5, 25 ) );
			}
			else if ( n == "select" )
			{
				// šipka výběru
				QPainterPath path;
				path.moveTo( 8, 3 );
				path.lineTo( 8, 25 );
				path.lineTo( 13, 20 );
				path.lineTo( 17, 29 );
				path.lineTo( 20, 27.5 );
				path.lineTo( 16, 19 );
				path.lineTo( 23, 19 );
				path.closeSubpath();
				p.setPen( QPen( Ink, 1.6 ) );
				p.setBrush( Qt::white );
				p.drawPath( path );
				p.setBrush( Qt::NoBrush );
			}
			else if ( n == "modify" )
			{
				// čára, konec se přesouvá na nové místo
				p.drawLine( QPointF( 4, 26 ), QPointF( 16, 14 ) );
				p.setPen( QPen( Blue, 2.2, Qt::DashLine ) );
				p.drawLine( QPointF( 4, 26 ), QPointF( 27, 22 ) );
				p.setPen( QPen( Accent, 2 ) );
				p.drawRect( QRectF( 13.5, 11.5, 5, 5 ) );
				p.setPen( QPen( Blue, 2 ) );
				arrowHead( p, QPointF( 27, 21 ), QPointF( 18, 16 ), 6 );
			}
			else if ( n == "insvertex" )
			{
				p.drawLine( QPointF( 3, 24 ), QPointF( 16, 8 ) );
				p.drawLine( QPointF( 16, 8 ), QPointF( 29, 24 ) );
				p.setPen( QPen( Ink, 1.2, Qt::DashLine ) );
				p.drawLine( QPointF( 3, 24 ), QPointF( 29, 24 ) );
				p.setPen( QPen( QColor( 40, 160, 60 ), 2.2 ) );
				p.drawRect( QRectF( 13, 5, 6, 6 ) );
			}
			else if ( n == "delvertex" )
			{
				p.setPen( QPen( Ink, 1.2, Qt::DashLine ) );
				p.drawLine( QPointF( 3, 24 ), QPointF( 16, 8 ) );
				p.drawLine( QPointF( 16, 8 ), QPointF( 29, 24 ) );
				p.setPen( QPen( Ink, 2.2 ) );
				p.drawLine( QPointF( 3, 24 ), QPointF( 29, 24 ) );
				p.setPen( QPen( Accent, 2.2 ) );
				p.drawLine( QPointF( 12, 4 ), QPointF( 20, 12 ) );
				p.drawLine( QPointF( 20, 4 ), QPointF( 12, 12 ) );
			}
			else if ( n == "newfloor" )
			{
				// dvě patra nad sebou + plus
				p.setPen( QPen( Ink, 1.6 ) );
				p.drawRect( QRectF( 3, 17, 18, 11 ) );
				p.setPen( QPen( Blue, 1.8 ) );
				p.drawRect( QRectF( 3, 5, 18, 11 ) );
				p.setPen( QPen( QColor( 40, 160, 60 ), 2.4 ) );
				p.drawLine( QPointF( 26, 3 ), QPointF( 26, 13 ) );
				p.drawLine( QPointF( 21, 8 ), QPointF( 31, 8 ) );
			}
			else if ( n == "auxsec" )
			{
				// tenká čára řezu + metr (jen měření)
				p.setPen( QPen( QColor( 0, 170, 170 ), 1.6, Qt::DashLine ) );
				p.drawLine( QPointF( 4, 28 ), QPointF( 28, 4 ) );
				p.setPen( QPen( Ink, 1.6 ) );
				p.setBrush( QColor( 250, 220, 120 ) );
				p.drawRect( QRectF( 14, 8, 6, 20 ) );
				p.setBrush( Qt::NoBrush );
				for ( int i = 0; i < 4; ++i )
				{
					p.drawLine( QPointF( 14, 11 + i * 4 ), QPointF( i % 2 ? 16.5 : 18, 11 + i * 4 ) );
				}
			}
			else if ( n == "points" )
			{
				p.setPen( QPen( QColor( 200, 40, 200 ), 2 ) );
				for ( const QPointF& c : { QPointF( 8, 9 ), QPointF( 22, 14 ), QPointF( 11, 24 ) } )
				{
					p.drawLine( c + QPointF( -3, -3 ), c + QPointF( 3, 3 ) );
					p.drawLine( c + QPointF( -3, 3 ), c + QPointF( 3, -3 ) );
				}
				p.setPen( QPen( Ink, 1 ) );
				QFont f = p.font();
				f.setPixelSize( 8 );
				p.setFont( f );
				p.drawText( QPointF( 13, 9 ), QStringLiteral( "1" ) );
				p.drawText( QPointF( 24, 24 ), QStringLiteral( "2" ) );
			}
			else if ( n == "breakline" )
			{
				// dvě rozdělené čáry a červená řezná čára
				p.setPen( QPen( Ink, 1.8 ) );
				p.drawLine( QPointF( 3, 10 ), QPointF( 14, 10 ) );
				p.drawLine( QPointF( 18, 10 ), QPointF( 29, 10 ) );
				p.drawLine( QPointF( 3, 22 ), QPointF( 14, 22 ) );
				p.drawLine( QPointF( 18, 22 ), QPointF( 29, 22 ) );
				p.setPen( QPen( QColor( 230, 60, 60 ), 1.6, Qt::DashLine ) );
				p.drawLine( QPointF( 16, 3 ), QPointF( 16, 29 ) );
			}
			else if ( n == "pin" )
			{
				// špendlík: jehla a kulatá hlavička
				p.setPen( QPen( Ink, 1.8 ) );
				p.drawLine( QPointF( 16, 16 ), QPointF( 16, 30 ) );
				p.setPen( QPen( QColor( 150, 20, 20 ), 1.2 ) );
				p.setBrush( QColor( 230, 50, 50 ) );
				p.drawEllipse( QPointF( 16, 10 ), 6.5, 6.5 );
				p.setBrush( Qt::NoBrush );
			}
			else if ( n == "kota" )
			{
				// kačena: základní čára, šipka do V, svislice s poličkou, text
				p.setPen( QPen( Ink, 1.6 ) );
				p.drawLine( QPointF( 2, 27 ), QPointF( 20, 27 ) );
				p.drawLine( QPointF( 6, 21 ), QPointF( 11, 27 ) );
				p.drawLine( QPointF( 16, 21 ), QPointF( 11, 27 ) );
				p.drawLine( QPointF( 11, 27 ), QPointF( 11, 14 ) );
				p.drawLine( QPointF( 11, 14 ), QPointF( 30, 14 ) );
				p.setPen( QPen( Blue, 1 ) );
				QFont f = p.font();
				f.setPixelSize( 9 );
				f.setBold( true );
				p.setFont( f );
				p.drawText( QPointF( 12, 11 ), QStringLiteral( "+3.3" ) );
			}
			else if ( n == "text" )
			{
				p.setPen( QPen( Ink, 1 ) );
				QFont f = p.font();
				f.setPixelSize( 22 );
				f.setBold( true );
				p.setFont( f );
				p.drawText( QRectF( 2, 2, 28, 28 ), Qt::AlignCenter, QStringLiteral( "A" ) );
				p.setPen( QPen( Blue, 2 ) );
				p.drawLine( QPointF( 5, 29 ), QPointF( 27, 29 ) );
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
		applyTheme();
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
