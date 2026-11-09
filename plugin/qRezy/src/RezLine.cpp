#include "RezLine.h"

#include <QOpenGLFunctions_2_1>
#include <QSettings>

#include <algorithm>

namespace
{
	int g_dashFactor = -1;
}

int RezLine::dashFactor()
{
	if ( g_dashFactor < 0 )
	{
		g_dashFactor = std::max( 1, std::min( 8, QSettings().value( QStringLiteral( "qRezy/dashFactor" ), 1 ).toInt() ) );
	}
	return g_dashFactor;
}

void RezLine::setDashFactor( int f )
{
	g_dashFactor = std::max( 1, std::min( 8, f ) );
	QSettings().setValue( QStringLiteral( "qRezy/dashFactor" ), g_dashFactor );
}

void RezLine::drawMeOnly( CC_DRAW_CONTEXT& context )
{
	QOpenGLFunctions_2_1* gl = context.glFunctions<QOpenGLFunctions_2_1>();
	const bool stipple = m_dashed && gl && MACRO_Draw3D( context );
	if ( stipple )
	{
		gl->glPushAttrib( GL_LINE_BIT );
		gl->glEnable( GL_LINE_STIPPLE );
		gl->glLineStipple( dashFactor(), 0x00FF ); // čárkovaně: 8 px čárka, 8 px mezera × faktor
	}

	ccPolyline::drawMeOnly( context );

	if ( stipple )
	{
		gl->glPopAttrib();
	}
}
