#include "View3D.h"
#include "RezLine.h"

#include "ccGLUtils.h"
#include "ccGLWindowInterface.h"
#include "ccMainAppInterface.h"
#include "ccGuiParameters.h"

#include <ccPointCloud.h>
#include <ccPolyline.h>

#include <CloudSamplingTools.h>
#include <ReferenceCloud.h>

#include <QCheckBox>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPushButton>
#include <QSlider>
#include <QOpenGLFunctions_2_1>
#include <QWheelEvent>
#include <QSettings>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>
#include <functional>

//! skupina, jejíž potomci (mračno) se kreslí průhledně
class BlendGroup : public ccHObject
{
public:
	BlendGroup()
		: ccHObject( "blend" )
	{
		setVisible( true );
		setEnabled( true );
	}
	float alpha = 1.0f;

	void draw( CC_DRAW_CONTEXT& context ) override
	{
		QOpenGLFunctions_2_1* gl = context.glFunctions<QOpenGLFunctions_2_1>();
		const bool blend = gl && MACRO_Draw3D( context ) && alpha < 0.999f;
		if ( blend )
		{
			gl->glPushAttrib( GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT );
			gl->glEnable( GL_BLEND );
			gl->glBlendColor( 0.0f, 0.0f, 0.0f, alpha );
			gl->glBlendFunc( GL_CONSTANT_ALPHA, GL_ONE_MINUS_CONSTANT_ALPHA );
			gl->glDepthMask( GL_FALSE ); // body se navzájem nezakrývají („rentgen“)
		}
		ccHObject::draw( context );
		if ( blend )
		{
			gl->glPopAttrib();
		}
	}
};

//! posuvník se dvěma jezdci (od – do)
class RangeSlider : public QWidget
{
public:
	explicit RangeSlider( QWidget* parent = nullptr )
		: QWidget( parent )
	{
		setMinimumHeight( 22 );
		setMinimumWidth( 120 );
		setSizePolicy( QSizePolicy::Expanding, QSizePolicy::Fixed );
		setMouseTracking( true );
	}

	std::function<void()> onChanged;

	void setRange( double mn, double mx )
	{
		m_min = mn;
		m_max = std::max( mx, mn + 0.01 );
		m_v0 = clampV( m_v0 );
		m_v1 = clampV( m_v1 );
		update();
	}
	void setValues( double v0, double v1 )
	{
		m_v0 = clampV( std::min( v0, v1 ) );
		m_v1 = clampV( std::max( v0, v1 ) );
		update();
	}
	double v0() const { return m_v0; }
	double v1() const { return m_v1; }
	double minimum() const { return m_min; }
	double maximum() const { return m_max; }

protected:
	void paintEvent( QPaintEvent* ) override
	{
		QPainter p( this );
		p.setRenderHint( QPainter::Antialiasing );
		const QPalette& pal = palette();
		const int cy = height() / 2;
		const QColor dis = pal.color( QPalette::Mid );
		p.setPen( Qt::NoPen );
		p.setBrush( dis );
		p.drawRoundedRect( QRectF( Margin, cy - 2, width() - 2 * Margin, 4 ), 2, 2 );
		const double x0 = toX( m_v0 );
		const double x1 = toX( m_v1 );
		p.setBrush( isEnabled() ? pal.color( QPalette::Highlight ) : dis );
		p.drawRect( QRectF( x0, cy - 3, x1 - x0, 6 ) );
		// nula = místo kliknutí
		if ( m_min < 0 && m_max > 0 )
		{
			p.setPen( QPen( pal.color( QPalette::Text ), 1 ) );
			const double xz = toX( 0.0 );
			p.drawLine( QPointF( xz, cy - 7 ), QPointF( xz, cy + 7 ) );
		}
		p.setPen( QPen( pal.color( QPalette::Dark ), 1 ) );
		p.setBrush( pal.color( QPalette::Button ) );
		for ( double x : { x0, x1 } )
		{
			p.drawRoundedRect( QRectF( x - 5, cy - 8, 10, 16 ), 3, 3 );
		}
	}
	void mousePressEvent( QMouseEvent* e ) override
	{
		const double x = e->pos().x();
		const double d0 = std::abs( x - toX( m_v0 ) );
		const double d1 = std::abs( x - toX( m_v1 ) );
		if ( std::abs( d0 - d1 ) < 1.0 )
		{
			m_drag = ( x < toX( m_v0 ) ) ? 0 : 1; // jezdci na sobě → podle směru
		}
		else
		{
			m_drag = ( d0 < d1 ) ? 0 : 1;
		}
		moveTo( x );
	}
	void mouseMoveEvent( QMouseEvent* e ) override
	{
		if ( m_drag >= 0 && ( e->buttons() & Qt::LeftButton ) )
		{
			moveTo( e->pos().x() );
		}
	}
	void mouseReleaseEvent( QMouseEvent* ) override
	{
		m_drag = -1;
	}
	void wheelEvent( QWheelEvent* e ) override
	{
		e->ignore();
	}

private:
	static constexpr int Margin = 8;
	double clampV( double v ) const { return std::max( m_min, std::min( m_max, v ) ); }
	double toX( double v ) const
	{
		return Margin + ( v - m_min ) / ( m_max - m_min ) * ( width() - 2 * Margin );
	}
	double toV( double x ) const
	{
		return m_min + ( x - Margin ) / std::max( 1, width() - 2 * Margin ) * ( m_max - m_min );
	}
	void moveTo( double x )
	{
		// krok 5 cm
		double v = std::round( clampV( toV( x ) ) * 20.0 ) / 20.0;
		v = clampV( v );
		if ( m_drag == 0 )
		{
			m_v0 = std::min( v, m_v1 - 0.05 );
		}
		else
		{
			m_v1 = std::max( v, m_v0 + 0.05 );
		}
		m_v0 = clampV( m_v0 );
		m_v1 = clampV( m_v1 );
		update();
		if ( onChanged )
		{
			onChanged();
		}
	}

	double m_min = -10.0;
	double m_max = 10.0;
	double m_v0 = -1.5;
	double m_v1 = 1.5;
	int m_drag = -1;
};

namespace
{
	const char* const AxisKey[3] = { "U", "V", "Z" };
	const double AxisDefault[3][2] = { { -1.5, 1.5 }, { -1.5, 1.5 }, { -2.0, 2.0 } };

	QString fmtM( double v )
	{
		return QString::number( v, 'f', 2 ).replace( '.', ',' );
	}
}

View3D::View3D( ccMainAppInterface* app, QWidget* parent, bool auxOnly )
	: QDockWidget( auxOnly ? QStringLiteral( "Řezy – pomocný řez (měření)" ) : QStringLiteral( "Řezy – 3D náhled" ), parent )
	, m_app( app )
	, m_auxOnly( auxOnly )
{
	setObjectName( auxOnly ? QStringLiteral( "qRezyAuxView" ) : QStringLiteral( "qRezy3DView" ) );

	auto* w = new QWidget;
	auto* lay = new QVBoxLayout( w );
	lay->setContentsMargins( 2, 2, 2, 2 );

	// posuvníky ořezu (od – do, metry od místa kliknutí)
	QSettings st;
	auto* grid = new QGridLayout;
	grid->setContentsMargins( 4, 0, 4, 0 );
	grid->setVerticalSpacing( 0 );
	const QString names[3] = { QStringLiteral( "Šířka" ), QStringLiteral( "Hloubka" ), QStringLiteral( "Výška" ) };
	const QString tips[3] = { QStringLiteral( "Do stran (vodorovně na obrazovce řezu/půdorysu)" ),
	                          QStringLiteral( "Do hloubky (kolmo, ve směru pohledu)" ),
	                          QStringLiteral( "Výška od místa kliknutí (horní jezdec doleva = uříznout střechu)" ) };
	for ( int i = 0; i < 3; ++i )
	{
		m_rs[i] = new RangeSlider;
		m_rs[i]->setToolTip( tips[i] );
		m_rs[i]->setEnabled( false );
		const QString k = QStringLiteral( "qRezy/3d%1" ).arg( QLatin1String( AxisKey[i] ) );
		m_rs[i]->setRange( -50.0, 50.0 );
		m_rs[i]->setValues( st.value( k + QStringLiteral( "0" ), AxisDefault[i][0] ).toDouble(), st.value( k + QStringLiteral( "1" ), AxisDefault[i][1] ).toDouble() );
		m_rsLabel[i] = new QLabel;
		m_rsLabel[i]->setMinimumWidth( 110 );
		auto* nl = new QLabel( names[i] );
		nl->setToolTip( tips[i] );
		grid->addWidget( nl, i, 0 );
		grid->addWidget( m_rs[i], i, 1 );
		grid->addWidget( m_rsLabel[i], i, 2 );
		m_rs[i]->onChanged = [this]()
		{
			updateValueLabels();
			applyClip();
		};
	}
	grid->setColumnStretch( 1, 1 );
	auto* side = new QVBoxLayout;
	m_showLines = new QCheckBox( QStringLiteral( "čáry" ) );
	m_showLines->setChecked( true );
	m_showLines->setToolTip( QStringLiteral( "Zobrazit nakreslené čáry" ) );
	auto* resetBtn = new QPushButton( QStringLiteral( "výchozí" ) );
	resetBtn->setToolTip( QStringLiteral( "Šířka a hloubka ±1,5 m, výška ±2 m" ) );
	auto* allBtn = new QPushButton( QStringLiteral( "vše" ) );
	allBtn->setToolTip( QStringLiteral( "Celé mračno" ) );
	side->addWidget( m_showLines );
	side->addWidget( resetBtn );
	side->addWidget( allBtn );
	m_alpha = new QSlider( Qt::Horizontal );
	m_alpha->setRange( 5, 100 );
	m_alpha->setValue( st.value( auxOnly ? QStringLiteral( "qRezy/auxAlpha" ) : QStringLiteral( "qRezy/3dAlpha" ), 100 ).toInt() );
	m_alpha->setToolTip( QStringLiteral( "Neprůhlednost mračna (vlevo = průhledné, čáry zůstanou plné)" ) );
	m_alpha->setMaximumWidth( 90 );
	side->addWidget( new QLabel( QStringLiteral( "průhlednost" ) ) );
	side->addWidget( m_alpha );
	m_backBtn = new QPushButton( QStringLiteral( "3D" ) );
	m_backBtn->setToolTip( QStringLiteral( "Zpět z pomocného řezu do 3D náhledu" ) );
	m_backBtn->setVisible( false );
	side->addWidget( m_backBtn );
	auto* top = new QHBoxLayout;
	top->addLayout( grid, 1 );
	top->addLayout( side );
	lay->addLayout( top );
	updateValueLabels();

	m_info = new QLabel( QStringLiteral( "Prostřední klik do řezu = ukázat místo. Levé tažení = otáčení, kolečko = zoom." ) );
	m_info->setWordWrap( true );
	lay->addWidget( m_info );

	m_app->createGLWindow( m_win, m_widget );
	if ( m_win && m_widget )
	{
		m_widget->setMinimumSize( 300, 250 );
		lay->addWidget( m_widget, 1 );
		m_win->setPerspectiveState( true, true );
		m_win->setPivotVisibility( ccGLWindowInterface::PIVOT_SHOW_ON_MOVE );
	}
	else
	{
		m_info->setText( QStringLiteral( "3D okno se nepodařilo vytvořit." ) );
	}
	setWidget( w );

	connect( m_backBtn, &QPushButton::clicked, this, [this]() { leaveAux( true ); } );
	connect( m_alpha, &QSlider::valueChanged, this, [this]( int v )
	{
		QSettings().setValue( m_auxOnly ? QStringLiteral( "qRezy/auxAlpha" ) : QStringLiteral( "qRezy/3dAlpha" ), v );
		if ( m_blend )
		{
			m_blend->alpha = v / 100.0f;
		}
		if ( m_win )
		{
			m_win->redraw();
		}
	} );
	if ( m_widget )
	{
		m_widget->installEventFilter( this );
	}
	connect( resetBtn, &QPushButton::clicked, this, [this]()
	{
		for ( int i = 0; i < 3; ++i )
		{
			m_rs[i]->setValues( AxisDefault[i][0], AxisDefault[i][1] );
		}
		updateValueLabels();
		applyClip();
	} );
	connect( allBtn, &QPushButton::clicked, this, [this]()
	{
		for ( int i = 0; i < 3; ++i )
		{
			m_rs[i]->setValues( m_rs[i]->minimum(), m_rs[i]->maximum() );
		}
		updateValueLabels();
		applyClip();
	} );
	connect( m_showLines, &QCheckBox::toggled, this, [this]( bool on )
	{
		if ( m_linesGroup )
		{
			m_linesGroup->setEnabled( on );
		}
		if ( m_win )
		{
			m_win->redraw();
		}
	} );
}

void View3D::saveSettings() const
{
	QSettings s;
	for ( int i = 0; i < 3; ++i )
	{
		const QString k = QStringLiteral( "qRezy/3d%1" ).arg( QLatin1String( AxisKey[i] ) );
		s.setValue( k + QStringLiteral( "0" ), m_rs[i]->v0() );
		s.setValue( k + QStringLiteral( "1" ), m_rs[i]->v1() );
	}
}

void View3D::updateValueLabels()
{
	for ( int i = 0; i < 3; ++i )
	{
		m_rsLabel[i]->setText( QStringLiteral( "%1 … %2 m" ).arg( fmtM( m_rs[i]->v0() ), fmtM( m_rs[i]->v1() ) ) );
	}
}

void View3D::updateRanges()
{
	if ( !m_clone || !m_hasFocus )
	{
		return;
	}
	// rozsah = celé mračno v osách pohledu, relativně k bodu (metry)
	const ccBBox box = m_clone->getOwnBB();
	const CCVector3 mn = box.minCorner();
	const CCVector3 mx = box.maxCorner();
	const double c = std::cos( m_angle );
	const double s = std::sin( m_angle );
	const CCVector3d axes[3] = { CCVector3d( c, s, 0 ), CCVector3d( -s, c, 0 ), CCVector3d( 0, 0, 1 ) };
	for ( int i = 0; i < 3; ++i )
	{
		double lo = 1e30;
		double hi = -1e30;
		for ( int k = 0; k < 8; ++k )
		{
			const CCVector3d P( ( k & 1 ) ? mx.x : mn.x, ( k & 2 ) ? mx.y : mn.y, ( k & 4 ) ? mx.z : mn.z );
			const double v = ( P - m_focus ).dot( axes[i] ) / m_scale;
			lo = std::min( lo, v );
			hi = std::max( hi, v );
		}
		lo = std::floor( std::min( lo, -0.5 ) * 10.0 ) / 10.0;
		hi = std::ceil( std::max( hi, 0.5 ) * 10.0 ) / 10.0;
		m_rs[i]->setRange( lo, hi );
		m_rs[i]->setEnabled( true );
	}
	updateValueLabels();
}

View3D::~View3D()
{
	for ( ccPolyline*& c : m_cross )
	{
		if ( c )
		{
			if ( m_win )
			{
				m_win->removeFromOwnDB( c );
			}
			delete c;
			c = nullptr;
		}
	}
	if ( m_measPoly )
	{
		if ( m_win )
		{
			m_win->removeFromOwnDB( m_measPoly );
		}
		delete m_measPoly;
		m_measPoly = nullptr;
	}
	clearLines();
	release();
	if ( m_blend )
	{
		if ( m_win )
		{
			m_win->removeFromOwnDB( m_blend );
		}
		delete m_blend;
		m_blend = nullptr;
	}
	if ( m_win )
	{
		m_app->destroyGLWindow( m_win );
		m_win = nullptr;
	}
}

void View3D::release()
{
	if ( m_clone && m_blend )
	{
		m_blend->detachChild( m_clone );
	}
	delete m_clone;
	m_clone = nullptr;
	m_sourceID = 0;
}

void View3D::clearLines()
{
	if ( m_linesGroup )
	{
		if ( m_win )
		{
			m_win->removeFromOwnDB( m_linesGroup );
		}
		delete m_linesGroup;
		m_linesGroup = nullptr;
	}
}

bool View3D::setSource( ccPointCloud* cloud, unsigned maxPoints )
{
	if ( !m_win || !cloud )
	{
		return false;
	}
	if ( m_clone && m_sourceID == cloud->getUniqueID() )
	{
		return true;
	}
	release();

	// zředěná kopie (pro náhled stačí)
	CCCoreLib::ReferenceCloud* sel = nullptr;
	if ( cloud->size() > maxPoints )
	{
		sel = CCCoreLib::CloudSamplingTools::subsampleCloudRandomly( cloud, maxPoints );
	}
	else
	{
		sel = new CCCoreLib::ReferenceCloud( cloud );
		sel->addPointIndex( 0, cloud->size() );
	}
	if ( !sel )
	{
		return false;
	}
	m_clone = cloud->partialClone( sel, nullptr, false );
	delete sel;
	if ( !m_clone )
	{
		m_app->dispToConsole( QStringLiteral( "[Řezy] Na 3D náhled nestačí paměť." ), ccMainAppInterface::WRN_CONSOLE_MESSAGE );
		return false;
	}
	m_clone->setName( cloud->getName() + QStringLiteral( " (3D náhled)" ) );
	m_clone->setGlobalShift( cloud->getGlobalShift() );
	m_clone->setGlobalScale( cloud->getGlobalScale() );
	m_scale = cloud->getGlobalScale();
	if ( cloud->hasColors() )
	{
		m_clone->showColors( true );
	}
	m_clone->setPointSize( 2 );
	m_clone->setEnabled( true );
	m_clone->setVisible( true );
	m_clone->setDisplay( m_win );
	if ( !m_blend )
	{
		m_blend = new BlendGroup;
		m_blend->setDisplay( m_win );
		m_win->addToOwnDB( m_blend );
	}
	m_blend->alpha = m_alpha->value() / 100.0f;
	m_blend->addChild( m_clone, ccHObject::DP_NONE );
	m_sourceID = cloud->getUniqueID();
	m_hasFocus = false;
	m_win->zoomGlobal();
	m_win->redraw();
	m_info->setText( QStringLiteral( "%1 bodů v náhledu. Prostřední klik do řezu = ukázat místo (posuvníky ořezu se zapnou)." ).arg( m_clone->size() ) );
	return true;
}

void View3D::setLines( const std::vector<Line>& lines )
{
	if ( !m_win )
	{
		return;
	}
	clearLines();
	m_linesGroup = new ccHObject( "lines" );
	for ( const Line& l : lines )
	{
		if ( l.pts.size() < 2 )
		{
			continue;
		}
		auto* verts = new ccPointCloud( "v" );
		verts->reserve( static_cast<unsigned>( l.pts.size() ) );
		for ( const CCVector3d& P : l.pts )
		{
			verts->addPoint( P.toFloat() );
		}
		verts->setEnabled( false );
		auto* poly = new RezLine( verts );
		poly->addChild( verts );
		poly->addPointIndex( 0, static_cast<unsigned>( l.pts.size() ) );
		poly->setColor( l.color );
		poly->showColors( true );
		poly->setWidth( 2 );
		poly->setDashed( l.dashed );
		m_linesGroup->addChild( poly );
	}
	m_linesGroup->setDisplay_recursive( m_win );
	m_linesGroup->setEnabled( m_showLines->isChecked() );
	m_win->addToOwnDB( m_linesGroup );
	applyClip();
	m_win->redraw();
}

void View3D::applyClip()
{
	if ( !m_win || !m_clone || !m_hasFocus )
	{
		return;
	}
	if ( !m_aux )
	{
		saveSettings();
	}
	const CCVector3d& P = m_focus;
	const double c = std::cos( m_angle );
	const double s = std::sin( m_angle );
	const CCVector3d axes[3] = { CCVector3d( c, s, 0 ), CCVector3d( -s, c, 0 ), CCVector3d( 0, 0, 1 ) };

	m_clone->removeAllClipPlanes();
	m_clipSet.clear();
	for ( int i = 0; i < 3; ++i )
	{
		const CCVector3d& N = axes[i];
		const double d = N.dot( P );
		const double lo = d + m_rs[i]->v0() * m_scale;
		const double hi = d + m_rs[i]->v1() * m_scale;
		ccClipPlane pos;
		pos.equation = Tuple4Tpl<double>( N.x, N.y, N.z, -lo );
		ccClipPlane neg;
		neg.equation = Tuple4Tpl<double>( -N.x, -N.y, -N.z, hi );
		m_clipSet.push_back( pos );
		m_clipSet.push_back( neg );
	}
	for ( const ccClipPlane& cp : m_clipSet )
	{
		m_clone->addClipPlanes( cp );
	}
	// stejný ořez i pro nakreslené čáry
	if ( m_linesGroup )
	{
		for ( unsigned i = 0; i < m_linesGroup->getChildrenNumber(); ++i )
		{
			ccHObject* ch = m_linesGroup->getChild( i );
			ch->removeAllClipPlanes();
			for ( const ccClipPlane& cp : m_clipSet )
			{
				ch->addClipPlanes( cp );
			}
		}
	}
	m_clone->prepareDisplayForRefresh();
	m_win->redraw();
}

void View3D::focusOn( const CCVector3d& P, double viewAngle )
{
	if ( !m_win || !m_clone )
	{
		return;
	}
	if ( m_aux )
	{
		leaveAux( false );
	}
	m_focus = P;
	m_angle = viewAngle;
	m_hasFocus = true;
	updateRanges();
	applyClip();

	// pohled šikmo shora, ve směru natočení pohledu
	const double r = std::max( { std::abs( m_rs[0]->v0() ), std::abs( m_rs[0]->v1() ), std::abs( m_rs[1]->v0() ), std::abs( m_rs[1]->v1() ) } ) * m_scale;
	const double zLo = P.z + m_rs[2]->v0() * m_scale;
	const double zHi = P.z + m_rs[2]->v1() * m_scale;
	CCVector3 mn( static_cast<PointCoordinateType>( P.x - r ), static_cast<PointCoordinateType>( P.y - r ), static_cast<PointCoordinateType>( zLo ) );
	CCVector3 mx( static_cast<PointCoordinateType>( P.x + r ), static_cast<PointCoordinateType>( P.y + r ), static_cast<PointCoordinateType>( zHi ) );
	ccBBox box( mn, mx, true );

	m_win->setPerspectiveState( true, true );
	ccGLMatrixd rotZ;
	rotZ.initFromParameters( -viewAngle, CCVector3d( 0, 0, 1 ), CCVector3d( 0, 0, 0 ) );
	ccGLMatrixd view = ccGLUtils::GenerateViewMat( CC_ISO_VIEW_1 ) * rotZ;
	m_win->setBaseViewMat( view );
	m_win->updateConstellationCenterAndZoom( &box );
	m_win->setPivotPoint( CCVector3d( P.x, P.y, P.z ), false, false );
	m_win->redraw();
	if ( !isVisible() )
	{
		show();
	}
}

// ---------------------------------------------------------------- pomocný řez

void View3D::auxSection( const CCVector3d& A, const CCVector3d& B, const CCVector3d& N, double planZ, double zeroGlobal, double thickness )
{
	if ( !m_win || !m_clone )
	{
		return;
	}
	const CCVector3d V( 0, 0, 1 );
	CCVector3d n( N.x, N.y, 0.0 );
	if ( n.norm() < 1e-9 )
	{
		return;
	}
	n.normalize();
	const CCVector3d U = V.cross( n );
	const double uA = A.dot( U );
	const double uB = B.dot( U );
	const CCVector3d left = ( uA <= uB ) ? A : B;
	const double len = std::abs( uB - uA );

	m_aux = true;
	m_auxA = CCVector3d( left.x, left.y, planZ );
	m_auxU = U;
	m_auxN = n;
	m_zeroG = zeroGlobal;
	m_measStep = 0;
	m_focus = m_auxA + U * ( len / 2.0 );
	m_angle = std::atan2( U.y, U.x );
	m_hasFocus = true;
	updateRanges();
	const double halfLen = len / 2.0 / m_scale + 0.3;
	const double halfT = std::max( 0.005, thickness / 2.0 );
	m_rs[0]->setValues( -halfLen, halfLen );
	m_rs[1]->setValues( -halfT, halfT );
	m_rs[2]->setValues( m_rs[2]->minimum(), m_rs[2]->maximum() );
	updateValueLabels();
	applyClip();

	// pravoúhlý pohled proti N
	ccGLMatrixd view;
	view.toIdentity();
	double* m = view.data();
	m[0] = U.x;
	m[4] = U.y;
	m[8] = U.z;
	m[1] = V.x;
	m[5] = V.y;
	m[9] = V.z;
	m[2] = n.x;
	m[6] = n.y;
	m[10] = n.z;
	m_win->setPerspectiveState( false, true );
	m_win->setBaseViewMat( view );
	m_win->setInteractionMode( ccGLWindowInterface::MODE_PAN_ONLY );
	m_win->setPivotVisibility( ccGLWindowInterface::PIVOT_HIDE );
	m_win->setWindowCursor( Qt::BlankCursor ); // nitkový kříž kreslíme sami
	const ccBBox bb = m_clone->getOwnBB();
	const CCVector3d P0 = m_auxA;
	const CCVector3d P1 = m_auxA + U * len;
	CCVector3 mn( static_cast<PointCoordinateType>( std::min( P0.x, P1.x ) ), static_cast<PointCoordinateType>( std::min( P0.y, P1.y ) ), bb.minCorner().z );
	CCVector3 mx( static_cast<PointCoordinateType>( std::max( P0.x, P1.x ) ), static_cast<PointCoordinateType>( std::max( P0.y, P1.y ) ), bb.maxCorner().z );
	ccBBox box( mn, mx, true );
	m_win->updateConstellationCenterAndZoom( &box );
	if ( m_measPoly )
	{
		m_measPoly->setEnabled( false );
	}
	m_backBtn->setVisible( !m_auxOnly );
	setWindowTitle( QStringLiteral( "Řezy – pomocný řez (měření)" ) );
	m_info->setText( QStringLiteral( "<b>Pomocný řez</b> – klikněte 2 body = vzdálenost a převýšení; u kurzoru výška nad ±0,000 a v Bpv. "
	                                 "Pravé tažení = posun, kolečko = zoom." ) );
	m_win->redraw();
	if ( !isVisible() )
	{
		show();
	}
	raise();
}

void View3D::leaveAux( bool refocus )
{
	if ( !m_aux )
	{
		return;
	}
	m_aux = false;
	m_measStep = 0;
	updateCross( QPoint(), false );
	if ( m_measPoly )
	{
		m_measPoly->setEnabled( false );
	}
	m_backBtn->setVisible( false );
	setWindowTitle( QStringLiteral( "Řezy – 3D náhled" ) );
	if ( m_win )
	{
		m_win->setInteractionMode( ccGLWindowInterface::MODE_TRANSFORM_CAMERA );
		m_win->setWindowCursor( Qt::ArrowCursor );
		m_win->setPivotVisibility( ccGLWindowInterface::PIVOT_SHOW_ON_MOVE );
		m_win->setPerspectiveState( true, true );
	}
	// zpět uložené hodnoty posuvníků 3D náhledu
	QSettings st;
	for ( int i = 0; i < 3; ++i )
	{
		const QString k = QStringLiteral( "qRezy/3d%1" ).arg( QLatin1String( AxisKey[i] ) );
		m_rs[i]->setValues( st.value( k + QStringLiteral( "0" ), AxisDefault[i][0] ).toDouble(), st.value( k + QStringLiteral( "1" ), AxisDefault[i][1] ).toDouble() );
	}
	updateValueLabels();
	m_info->setText( QStringLiteral( "Prostřední klik do řezu = ukázat místo. Levé tažení = otáčení, kolečko = zoom." ) );
	if ( refocus && m_hasFocus )
	{
		focusOn( m_focus, m_angle );
	}
}

bool View3D::auxPoint( const QPoint& pos, CCVector3d& P ) const
{
	if ( !m_win )
	{
		return false;
	}
	ccGLCameraParameters cam;
	m_win->getGLCameraParameters( cam );
	const QPointF g = m_win->toCornerGLCoordinates( pos.x(), pos.y() );
	CCVector3d Q;
	if ( !cam.unproject( CCVector3d( g.x(), g.y(), 0.5 ), Q ) )
	{
		return false;
	}
	// průmět do roviny řezu
	P = Q - m_auxN * ( ( Q - m_auxA ).dot( m_auxN ) );
	return true;
}

QString View3D::heightText( const CCVector3d& P ) const
{
	const double zG = m_clone ? m_clone->toGlobal3d( P ).z : P.z;
	const double rel = zG - m_zeroG;
	QString r = QString::number( std::abs( rel ), 'f', 3 ).replace( '.', ',' );
	r = ( std::abs( rel ) < 0.0005 ) ? QStringLiteral( "±0,000" ) : ( ( rel > 0 ? QStringLiteral( "+" ) : QStringLiteral( "-" ) ) + r );
	return QStringLiteral( "%1 (Bpv %2)" ).arg( r, QString::number( zG, 'f', 3 ).replace( '.', ',' ) );
}

void View3D::updateMeasure( bool live )
{
	if ( !m_win )
	{
		return;
	}
	QString txt = QStringLiteral( "<b>Pomocný řez</b> – kurzor: výška %1" ).arg( heightText( m_mCursor ) );
	if ( m_measStep >= 1 )
	{
		const CCVector3d B = ( m_measStep == 1 ) ? m_mCursor : m_m2;
		const double du = std::abs( ( B - m_m1 ).dot( m_auxU ) ) / m_scale;
		const double dz = ( B.z - m_m1.z ) / m_scale;
		const double d = std::sqrt( du * du + dz * dz );
		auto f = []( double v ) { return QString::number( v, 'f', 3 ).replace( '.', ',' ); };
		txt += QStringLiteral( "<br>1. bod: %1 · 2. bod: %2<br><b>vodorovně %3 m · svisle %4 m · šikmo %5 m</b>" )
		           .arg( heightText( m_m1 ), heightText( B ), f( du ), f( std::abs( dz ) ), f( d ) );

		// čára měření (kousek před rovinou, aby ji body nezakryly)
		if ( !m_measPoly )
		{
			auto* verts = new ccPointCloud( "measure" );
			verts->reserve( 2 );
			verts->addPoint( CCVector3( 0, 0, 0 ) );
			verts->addPoint( CCVector3( 0, 0, 0 ) );
			verts->setEnabled( false );
			m_measPoly = new ccPolyline( verts );
			m_measPoly->addChild( verts );
			m_measPoly->addPointIndex( 0, 2 );
			m_measPoly->setColor( ccColor::Rgb( 255, 220, 0 ) );
			m_measPoly->showColors( true );
			m_measPoly->setWidth( 3 );
			m_measPoly->setDisplay( m_win );
			m_win->addToOwnDB( m_measPoly );
		}
		auto* verts = static_cast<ccPointCloud*>( m_measPoly->getAssociatedCloud() );
		const CCVector3d front = m_auxN * ( 2.0 * m_scale );
		*const_cast<CCVector3*>( verts->getPointPersistentPtr( 0 ) ) = ( m_m1 + front ).toFloat();
		*const_cast<CCVector3*>( verts->getPointPersistentPtr( 1 ) ) = ( B + front ).toFloat();
		verts->invalidateBoundingBox();
		m_measPoly->setEnabled( true );
		m_measPoly->prepareDisplayForRefresh();
	}
	m_info->setText( txt );
	m_win->redraw( !live || m_measStep == 0, false );
}

bool View3D::eventFilter( QObject* obj, QEvent* event )
{
	if ( obj != m_widget || !m_aux )
	{
		return QDockWidget::eventFilter( obj, event );
	}
	switch ( event->type() )
	{
	case QEvent::MouseButtonPress:
	case QEvent::MouseButtonDblClick:
	{
		auto* e = static_cast<QMouseEvent*>( event );
		if ( e->button() != Qt::LeftButton )
		{
			break;
		}
		CCVector3d P;
		if ( auxPoint( e->pos(), P ) )
		{
			m_mCursor = P;
			if ( m_measStep == 1 )
			{
				m_m2 = P;
				m_measStep = 2;
			}
			else
			{
				m_m1 = P;
				m_measStep = 1;
			}
			updateMeasure( false );
		}
		return true; // levé tlačítko neotáčí
	}
	case QEvent::MouseButtonRelease:
	{
		auto* e = static_cast<QMouseEvent*>( event );
		if ( e->button() == Qt::LeftButton )
		{
			return true;
		}
		break;
	}
	case QEvent::MouseMove:
	{
		auto* e = static_cast<QMouseEvent*>( event );
		CCVector3d P;
		updateCross( e->pos(), true );
		m_win->setWindowCursor( Qt::BlankCursor );
		if ( e->buttons() == Qt::NoButton && auxPoint( e->pos(), P ) )
		{
			m_mCursor = P;
			updateMeasure( true );
		}
		else
		{
			m_win->redraw( true, false );
		}
		break;
	}
	case QEvent::Leave:
		updateCross( QPoint(), false );
		m_win->redraw( true, false );
		break;
	default:
		break;
	}
	return QDockWidget::eventFilter( obj, event );
}

void View3D::updateCross( const QPoint& pos, bool show )
{
	if ( !m_win )
	{
		return;
	}
	if ( !m_cross[0] )
	{
		for ( ccPolyline*& c : m_cross )
		{
			auto* verts = new ccPointCloud( "cross" );
			verts->reserve( 2 );
			verts->addPoint( CCVector3( 0, 0, 0 ) );
			verts->addPoint( CCVector3( 0, 0, 0 ) );
			verts->setEnabled( false );
			c = new ccPolyline( verts );
			c->addChild( verts );
			c->addPointIndex( 0, 2 );
			c->set2DMode( true );
			c->setForeground( true );
			c->showColors( true );
			c->setWidth( 1 );
			c->setDisplay( m_win );
			m_win->addToOwnDB( c );
		}
	}
	if ( !show )
	{
		m_cross[0]->setEnabled( false );
		m_cross[1]->setEnabled( false );
		return;
	}
	// ±45 px jako v kreslení; na světlém pozadí černý
	const ccColor::Rgbub& bg = ccGui::Parameters().backgroundCol;
	const double lum = 0.299 * bg.r + 0.587 * bg.g + 0.114 * bg.b;
	const ccColor::Rgb col = ( lum > 140.0 ) ? ccColor::Rgb( 0, 0, 0 ) : ccColor::Rgb( 255, 255, 255 );
	ccGLCameraParameters cam;
	m_win->getGLCameraParameters( cam );
	const QPointF g = m_win->toCornerGLCoordinates( pos.x(), pos.y() );
	const double cx = g.x() - cam.viewport[2] / 2.0;
	const double cy = g.y() - cam.viewport[3] / 2.0;
	const double r = 45.0 * m_win->getDevicePixelRatio();
	const CCVector3 ends[2][2] = { { CCVector3( static_cast<PointCoordinateType>( cx - r ), static_cast<PointCoordinateType>( cy ), 0 ), CCVector3( static_cast<PointCoordinateType>( cx + r ), static_cast<PointCoordinateType>( cy ), 0 ) },
	                               { CCVector3( static_cast<PointCoordinateType>( cx ), static_cast<PointCoordinateType>( cy - r ), 0 ), CCVector3( static_cast<PointCoordinateType>( cx ), static_cast<PointCoordinateType>( cy + r ), 0 ) } };
	for ( int i = 0; i < 2; ++i )
	{
		auto* verts = static_cast<ccPointCloud*>( m_cross[i]->getAssociatedCloud() );
		*const_cast<CCVector3*>( verts->getPointPersistentPtr( 0 ) ) = ends[i][0];
		*const_cast<CCVector3*>( verts->getPointPersistentPtr( 1 ) ) = ends[i][1];
		m_cross[i]->setColor( col );
		m_cross[i]->setEnabled( true );
	}
}
