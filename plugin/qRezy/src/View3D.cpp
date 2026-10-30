#include "View3D.h"
#include "RezLine.h"

#include "ccGLUtils.h"
#include "ccGLWindowInterface.h"
#include "ccMainAppInterface.h"

#include <ccPointCloud.h>

#include <CloudSamplingTools.h>
#include <ReferenceCloud.h>

#include <QCheckBox>
#include <QDoubleSpinBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QSettings>
#include <QVBoxLayout>

#include <cmath>

namespace
{
	QDoubleSpinBox* spin( double v, const QString& tip )
	{
		auto* s = new QDoubleSpinBox;
		s->setRange( 0.1, 200.0 );
		s->setDecimals( 1 );
		s->setSingleStep( 0.5 );
		s->setValue( v );
		s->setSuffix( QStringLiteral( " m" ) );
		s->setToolTip( tip );
		s->setKeyboardTracking( false );
		return s;
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

	// ovládání ořezu
	QSettings st;
	auto* row = new QHBoxLayout;
	m_radius = spin( st.value( QStringLiteral( "qRezy/3dRadius" ), 1.5 ).toDouble(), QStringLiteral( "Do stran od místa kliknutí" ) );
	m_above = spin( st.value( QStringLiteral( "qRezy/3dAbove" ), 2.0 ).toDouble(), QStringLiteral( "Nahoru od výšky řezu (menší = uříznutá střecha)" ) );
	m_below = spin( st.value( QStringLiteral( "qRezy/3dBelow" ), 2.0 ).toDouble(), QStringLiteral( "Dolů od výšky řezu" ) );
	m_clipSides = new QCheckBox( QStringLiteral( "do stran" ) );
	m_clipSides->setChecked( st.value( QStringLiteral( "qRezy/3dSides" ), true ).toBool() );
	m_clipSides->setToolTip( QStringLiteral( "Vypnuto = celé mračno (jen ořez shora a zdola)" ) );
	m_showLines = new QCheckBox( QStringLiteral( "čáry" ) );
	m_showLines->setChecked( true );
	m_showLines->setToolTip( QStringLiteral( "Zobrazit nakreslené čáry" ) );
	row->addWidget( m_clipSides );
	row->addWidget( m_radius );
	row->addWidget( new QLabel( QStringLiteral( "↑" ) ) );
	row->addWidget( m_above );
	row->addWidget( new QLabel( QStringLiteral( "↓" ) ) );
	row->addWidget( m_below );
	row->addWidget( m_showLines );
	row->addStretch( 1 );
	lay->addLayout( row );

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

	auto changed = [this]()
	{
		QSettings s;
		s.setValue( QStringLiteral( "qRezy/3dRadius" ), m_radius->value() );
		s.setValue( QStringLiteral( "qRezy/3dAbove" ), m_above->value() );
		s.setValue( QStringLiteral( "qRezy/3dBelow" ), m_below->value() );
		s.setValue( QStringLiteral( "qRezy/3dSides" ), m_clipSides->isChecked() );
		applyClip();
	};
	for ( QDoubleSpinBox* sb : { m_radius, m_above, m_below } )
	{
		connect( sb, qOverload<double>( &QDoubleSpinBox::valueChanged ), this, changed );
	}
	connect( m_clipSides, &QCheckBox::toggled, this, changed );
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
	m_info->setText( QStringLiteral( "%1 bodů v náhledu. Prostřední klik do řezu = ukázat místo." ).arg( m_clone->size() ) );
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
	const CCVector3d& P = m_focus;
	const double r = m_radius->value() * m_scale;
	const double zLo = P.z - m_below->value() * m_scale;
	const double zHi = P.z + m_above->value() * m_scale;

	m_clone->removeAllClipPlanes();
	m_clipSet.clear();
	if ( m_clipSides->isChecked() )
	{
		// hranol kolem bodu, natočený podle pohledu
		const double c = std::cos( m_angle );
		const double s = std::sin( m_angle );
		const CCVector3d axes[2] = { CCVector3d( c, s, 0 ), CCVector3d( -s, c, 0 ) };
		for ( const CCVector3d& N : axes )
		{
			const double d = N.dot( P );
			ccClipPlane pos;
			pos.equation = Tuple4Tpl<double>( N.x, N.y, N.z, -( d - r ) );
			ccClipPlane neg;
			neg.equation = Tuple4Tpl<double>( -N.x, -N.y, -N.z, d + r );
			m_clipSet.push_back( pos );
			m_clipSet.push_back( neg );
		}
	}
	ccClipPlane lo;
	lo.equation = Tuple4Tpl<double>( 0, 0, 1, -zLo );
	ccClipPlane hi;
	hi.equation = Tuple4Tpl<double>( 0, 0, -1, zHi );
	m_clipSet.push_back( lo );
	m_clipSet.push_back( hi );
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
	applyClip();

	// pohled šikmo shora, ve směru natočení pohledu
	const double r = m_radius->value() * m_scale;
	const double zLo = P.z - m_below->value() * m_scale;
	const double zHi = P.z + m_above->value() * m_scale;
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
