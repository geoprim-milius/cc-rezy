#include "RezLayer.h"

#include <cmath>
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#include <ccGenericGLDisplay.h>

#include <QFont>
#include <QFontMetrics>
#include <QImage>
#include <QOpenGLTexture>
#include <QPainter>
#include <QOpenGLFunctions_2_1>

#include <algorithm>
#include <cmath>

void RezLayer::drawMeOnly( CC_DRAW_CONTEXT& context )
{
	if ( !MACRO_Draw2D( context ) || !MACRO_Foreground( context ) || !context.display || items.empty() )
	{
		return;
	}
	QOpenGLFunctions_2_1* gl = context.glFunctions<QOpenGLFunctions_2_1>();
	if ( !gl )
	{
		return;
	}
	ccGLCameraParameters cam;
	context.display->getGLCameraParameters( cam );
	const double cx = cam.viewport[2] / 2.0;
	const double cy = cam.viewport[3] / 2.0;

	QFont base = context.display->getTextDisplayFont();
	for ( const Item& it : items )
	{
		if ( !it.segs.empty() )
		{
			gl->glColor3ub( it.color.r, it.color.g, it.color.b );
			gl->glLineWidth( 1.5f );
			gl->glBegin( GL_LINES );
			for ( size_t k = 0; k + 1 < it.segs.size(); k += 2 )
			{
				CCVector3d A;
				CCVector3d B;
				if ( cam.project( it.segs[k], A ) && cam.project( it.segs[k + 1], B ) )
				{
					gl->glVertex2d( A.x - cx, A.y - cy );
					gl->glVertex2d( B.x - cx, B.y - cy );
				}
			}
			gl->glEnd();
			gl->glLineWidth( 1.0f );
			if ( it.text.isEmpty() )
			{
				continue;
			}
		}
		CCVector3d S;
		if ( !cam.project( it.P, S ) )
		{
			continue;
		}
		if ( S.x < -50 || S.y < -50 || S.x > cam.viewport[2] + 50 || S.y > cam.viewport[3] + 50 )
		{
			continue;
		}
		const ccColor::Rgba col( it.color.r, it.color.g, it.color.b, 255 );
		if ( it.pin )
		{
			// špendlík: jehla šikmo nahoru, kulatá hlavička
			const double x = S.x - cx;
			const double y = S.y - cy;
			const double hx = x + 14.0;
			const double hy = y + 26.0;
			gl->glLineWidth( 3.0f );
			gl->glColor3ub( 40, 40, 40 );
			gl->glBegin( GL_LINES );
			gl->glVertex2d( x, y );
			gl->glVertex2d( hx, hy );
			gl->glEnd();
			gl->glLineWidth( 1.0f );
			gl->glColor3ub( 235, 40, 40 );
			gl->glBegin( GL_TRIANGLE_FAN );
			gl->glVertex2d( hx, hy );
			for ( int k = 0; k <= 24; ++k )
			{
				const double a = k * 2.0 * M_PI / 24.0;
				gl->glVertex2d( hx + 8.0 * std::cos( a ), hy + 8.0 * std::sin( a ) );
			}
			gl->glEnd();
			gl->glColor3ub( 255, 255, 255 );
			gl->glPointSize( 4.0f );
			gl->glBegin( GL_POINTS );
			gl->glVertex2d( hx - 2.5, hy + 2.5 );
			gl->glEnd();
			gl->glPointSize( 1.0f );
			continue;
		}
		if ( it.marker )
		{
			// křížek ve 2D (souřadnice od středu okna)
			const double x = S.x - cx;
			const double y = S.y - cy;
			const double r = 4.0;
			gl->glColor3ub( it.markerColor.r, it.markerColor.g, it.markerColor.b );
			gl->glLineWidth( 1.5f );
			gl->glBegin( GL_LINES );
			gl->glVertex2d( x - r, y - r );
			gl->glVertex2d( x + r, y + r );
			gl->glVertex2d( x - r, y + r );
			gl->glVertex2d( x + r, y - r );
			gl->glEnd();
			gl->glLineWidth( 1.0f );
		}
		if ( it.text.isEmpty() )
		{
			continue;
		}
		QFont f = base;
		if ( it.height > 0.0 )
		{
			// výška písma podle měřítka
			CCVector3d S2;
			if ( !cam.project( it.P + it.up * it.height, S2 ) )
			{
				continue;
			}
			// skutečná velikost, ale aspoň čitelná
			const double px = std::sqrt( ( S2.x - S.x ) * ( S2.x - S.x ) + ( S2.y - S.y ) * ( S2.y - S.y ) );
			const int fpx = static_cast<int>( std::max( 11.0, std::min( 300.0, px * 1.35 ) ) );
			f.setPixelSize( fpx );
			CCVector3d S3;
			if ( it.dir.norm2() > 1e-12 && cam.project( it.P + it.dir * it.height, S3 ) && ( std::abs( S3.x - S.x ) + std::abs( S3.y - S.y ) ) > 1e-6 )
			{
				// natočený text: vykreslit do obrázku a ten natočit (displayText natáčet neumí)
				const double th = std::atan2( S3.y - S.y, S3.x - S.x );
				QFontMetrics fm( f );
				const int w = fm.horizontalAdvance( it.text ) + 2;
				const int h = fm.height();
				QImage img( w, h, QImage::Format_RGBA8888_Premultiplied );
				img.fill( Qt::transparent );
				{
					QPainter pnt( &img );
					pnt.setFont( f );
					pnt.setPen( QColor( it.color.r, it.color.g, it.color.b ) );
					pnt.drawText( 1, fm.ascent(), it.text );
				}
				QOpenGLTexture tex( img.mirrored() );
				tex.setMinificationFilter( QOpenGLTexture::Linear );
				tex.setMagnificationFilter( QOpenGLTexture::Linear );
				const double c = std::cos( th );
				const double s = std::sin( th );
				const double ox = S.x - cx;
				const double oy = S.y - cy;
				const double y0 = -fm.descent();
				const double y1 = h - fm.descent();
				auto V = [&]( double u, double v ) { gl->glVertex2d( ox + u * c - v * s, oy + u * s + v * c ); };
				gl->glPushAttrib( GL_ENABLE_BIT | GL_COLOR_BUFFER_BIT | GL_TEXTURE_BIT );
				gl->glEnable( GL_TEXTURE_2D );
				gl->glEnable( GL_BLEND );
				gl->glBlendFunc( GL_ONE, GL_ONE_MINUS_SRC_ALPHA );
				tex.bind();
				gl->glColor4f( 1.f, 1.f, 1.f, 1.f );
				gl->glBegin( GL_QUADS );
				gl->glTexCoord2d( 0, 0 );
				V( 0, y0 );
				gl->glTexCoord2d( 1, 0 );
				V( w, y0 );
				gl->glTexCoord2d( 1, 1 );
				V( w, y1 );
				gl->glTexCoord2d( 0, 1 );
				V( 0, y1 );
				gl->glEnd();
				tex.release();
				gl->glPopAttrib();
				continue;
			}
			// displayText kreslí písmo pod zadaný bod → posun nahoru, aby P byl levý dolní roh textu (jako v DGN)
			context.display->displayText( it.text, static_cast<int>( S.x ), static_cast<int>( S.y + 0.8 * fpx ), ccGenericGLDisplay::ALIGN_HLEFT | ccGenericGLDisplay::ALIGN_VBOTTOM, 0.0f, &col, &f );
		}
		else
		{
			// číslo bodu a za ním kód (každý svou barvou), případně s tmavým podkladem
			f.setPointSize( std::max( 7, f.pointSize() ) );
			f.setBold( true );
			const float bk = it.background ? 0.55f : 0.0f;
			const int x = static_cast<int>( S.x ) + 7;
			const int y = static_cast<int>( S.y ) + 4;
			context.display->displayText( it.text, x, y, ccGenericGLDisplay::ALIGN_HLEFT | ccGenericGLDisplay::ALIGN_VBOTTOM, bk, &col, &f );
			if ( !it.text2.isEmpty() )
			{
				QFontMetrics fm( f );
				const int w = fm.horizontalAdvance( it.text + QStringLiteral( "  " ) );
				const ccColor::Rgba col2( it.color2.r, it.color2.g, it.color2.b, 255 );
				QFont f2 = f;
				f2.setBold( false );
				context.display->displayText( it.text2, x + w, y, ccGenericGLDisplay::ALIGN_HLEFT | ccGenericGLDisplay::ALIGN_VBOTTOM, bk, &col2, &f2 );
			}
			if ( !it.text3.isEmpty() )
			{
				// výška pod číslem
				QFontMetrics fm( f );
				const ccColor::Rgba col3( it.color3.r, it.color3.g, it.color3.b, 255 );
				QFont f3 = f;
				f3.setBold( false );
				context.display->displayText( it.text3, x, y - fm.height(), ccGenericGLDisplay::ALIGN_HLEFT | ccGenericGLDisplay::ALIGN_VBOTTOM, bk, &col3, &f3 );
			}
		}
	}
}
