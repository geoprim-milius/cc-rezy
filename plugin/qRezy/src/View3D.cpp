#include "View3D.h"

#include "ccGLWindowInterface.h"
#include "ccMainAppInterface.h"
#include "ccGLUtils.h"

#include <ccPointCloud.h>

#include <CloudSamplingTools.h>
#include <ReferenceCloud.h>

#include <QLabel>
#include <QVBoxLayout>

#include <cmath>

View3D::View3D( ccMainAppInterface* app, QWidget* parent )
	: QDockWidget( QStringLiteral( "Řezy – 3D náhled" ), parent )
	, m_app( app )
{
	setObjectName( QStringLiteral( "qRezy3DView" ) );

	auto* w = new QWidget;
	auto* lay = new QVBoxLayout( w );
	lay->setContentsMargins( 0, 0, 0, 0 );

	m_info = new QLabel( QStringLiteral( "Klikněte prostředním tlačítkem do půdorysu – zde se ukáže okolí ve 3D. Levé tažení = otáčení." ) );
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
}

View3D::~View3D()
{
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
	m_win->zoomGlobal();
	m_win->redraw();
	m_info->setText( QStringLiteral( "%1 bodů v náhledu. Prostřední klik do půdorysu = ukázat místo." ).arg( m_clone->size() ) );
	return true;
}

void View3D::focusOn( const CCVector3d& P, double radius, double viewAngle )
{
	if ( !m_win || !m_clone )
	{
		return;
	}

	// ořez: hranol radius × radius kolem bodu (natočený podle půdorysu), na celou výšku
	m_clone->removeAllClipPlanes();
	const double c = std::cos( viewAngle );
	const double s = std::sin( viewAngle );
	const CCVector3d axes[2] = { CCVector3d( c, s, 0 ), CCVector3d( -s, c, 0 ) };
	for ( const CCVector3d& N : axes )
	{
		const double d = N.dot( P );
		ccClipPlane pos;
		pos.equation = Tuple4Tpl<double>( N.x, N.y, N.z, -( d - radius ) );
		ccClipPlane neg;
		neg.equation = Tuple4Tpl<double>( -N.x, -N.y, -N.z, d + radius );
		m_clone->addClipPlanes( pos );
		m_clone->addClipPlanes( neg );
	}

	// výškově: od řezu 2× poloměr dolů i nahoru
	const double zLo = P.z - 2.0 * radius;
	const double zHi = P.z + 2.0 * radius;
	{
		ccClipPlane lo;
		lo.equation = Tuple4Tpl<double>( 0, 0, 1, -zLo );
		ccClipPlane hi;
		hi.equation = Tuple4Tpl<double>( 0, 0, -1, zHi );
		m_clone->addClipPlanes( lo );
		m_clone->addClipPlanes( hi );
	}

	// pohled šikmo shora, ve směru natočení půdorysu
	CCVector3 mn( static_cast<PointCoordinateType>( P.x - radius ), static_cast<PointCoordinateType>( P.y - radius ), static_cast<PointCoordinateType>( zLo ) );
	CCVector3 mx( static_cast<PointCoordinateType>( P.x + radius ), static_cast<PointCoordinateType>( P.y + radius ), static_cast<PointCoordinateType>( zHi ) );
	ccBBox box( mn, mx, true );

	m_win->setPerspectiveState( true, true );
	ccGLMatrixd rotZ;
	rotZ.initFromParameters( -viewAngle, CCVector3d( 0, 0, 1 ), CCVector3d( 0, 0, 0 ) );
	ccGLMatrixd view = ccGLUtils::GenerateViewMat( CC_ISO_VIEW_1 ) * rotZ;
	m_win->setBaseViewMat( view );
	m_win->updateConstellationCenterAndZoom( &box );
	m_win->setPivotPoint( CCVector3d( P.x, P.y, P.z ), false, false );
	m_clone->prepareDisplayForRefresh();
	m_win->redraw();
	if ( !isVisible() )
	{
		show();
	}
}
