#include "RezLine.h"

#include <QOpenGLFunctions_2_1>

void RezLine::drawMeOnly( CC_DRAW_CONTEXT& context )
{
	QOpenGLFunctions_2_1* gl = context.glFunctions<QOpenGLFunctions_2_1>();
	const bool stipple = m_dashed && gl && MACRO_Draw3D( context );
	if ( stipple )
	{
		gl->glPushAttrib( GL_LINE_BIT );
		gl->glEnable( GL_LINE_STIPPLE );
		gl->glLineStipple( 4, 0x00FF ); // čárkovaně
	}

	ccPolyline::drawMeOnly( context );

	if ( stipple )
	{
		gl->glPopAttrib();
	}
}
