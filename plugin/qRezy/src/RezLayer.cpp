#include "RezLayer.h"

#include <ccGenericGLDisplay.h>

#include <QFont>
#include <QFontMetrics>
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
			f.setPixelSize( static_cast<int>( std::max( 11.0, std::min( 300.0, px * 1.35 ) ) ) );
			context.display->displayText( it.text, static_cast<int>( S.x ), static_cast<int>( S.y ), ccGenericGLDisplay::ALIGN_HLEFT | ccGenericGLDisplay::ALIGN_VBOTTOM, 0.0f, &col, &f );
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
		}
	}
}
