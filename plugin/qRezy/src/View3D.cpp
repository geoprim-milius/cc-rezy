#include "View3D.h"
#include "RezLine.h"

#include "ccGLUtils.h"
#include "ccGLWindowInterface.h"
#include "ccMainAppInterface.h"

#include <ccPointCloud.h>

#include <CloudSamplingTools.h>
#include <ReferenceCloud.h>

#include <QCheckBox>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPushButton>
#include <QWheelEvent>
#include <QSettings>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>
#include <functional>

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

View3D::View3D( ccMainAppInterface* app, QWidget* parent )
	: QDockWidget( QStringLiteral( "Řezy – 3D náhled" ), parent )
	, m_app( app )
{
	setObjectName( QStringLiteral( "qRezy3DView" ) );

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
	clearLines();
	release();
	if ( m_win )
	{
		m_app->destroyGLWindow( m_win );
		m_win = nullptr;
	}
}

void View3D::release()
{
	if ( m_clone && m_win )
	{
		m_win->removeFromOwnDB( m_clone );
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
	m_win->addToOwnDB( m_clone );
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
	saveSettings();
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
