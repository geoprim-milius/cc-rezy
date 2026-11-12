#include "DrawTool.h"
#include "CloudLinesV15.h"
#include "RezLine.h"
#include "RezLayer.h"

#include "ccGLWindowInterface.h"
#include "ccGuiParameters.h"
#include "ccMainAppInterface.h"

#include <ccGenericPointCloud.h>
#include <ccHObjectCaster.h>
#include <ccPointCloud.h>
#include <ccPolyline.h>

#include <array>

#include <QApplication>
#include <QColor>
#include <QCursor>
#include <QFile>
#include <QFileInfo>
#include <QInputDialog>
#include <QMap>
#include <QSettings>
#include <QTextCodec>
#include <QTextStream>
#include <QRegExp>
#include <QKeyEvent>
#include <QMenu>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QTimer>

#include <algorithm>
#include <cmath>
#include <limits>

namespace
{
	constexpr double SnapPixels = 10.0; //!< dosah uchopení v pixelech
	constexpr double PickPixels = 5.0;  //!< dosah výběru čáry
	constexpr double PickBoxPixels = 4.0; //!< polovina čtverečku v kurzoru
	constexpr int ClickTolerance = 4;   //!< max. posun myši, aby šlo o kliknutí
	constexpr double CrossPixels = 45.0; //!< ramena nitkového kříže
	constexpr double RadToDeg = 57.29577951308232;

	double dist( const CCVector2d& a, const CCVector2d& b )
	{
		return ( a - b ).norm();
	}

	//! nejbližší bod úsečky AB k bodu P
	CCVector2d closestOnSegment( const CCVector2d& A, const CCVector2d& B, const CCVector2d& P )
	{
		const CCVector2d AB = B - A;
		const double l2 = AB.norm2();
		if ( l2 <= 0.0 )
		{
			return A;
		}
		double t = ( P - A ).dot( AB ) / l2;
		t = std::max( 0.0, std::min( 1.0, t ) );
		return A + AB * t;
	}

	//! průsečík úseček (true, pokud se protínají)
	bool segmentIntersection( const CCVector2d& A, const CCVector2d& B, const CCVector2d& C, const CCVector2d& D, CCVector2d& I )
	{
		const CCVector2d r = B - A;
		const CCVector2d s = D - C;
		const double den = r.x * s.y - r.y * s.x;
		if ( std::abs( den ) < 1e-12 )
		{
			return false;
		}
		const CCVector2d q = C - A;
		const double t = ( q.x * s.y - q.y * s.x ) / den;
		const double u = ( q.x * r.y - q.y * r.x ) / den;
		const double eps = 1e-9;
		if ( t < -eps || t > 1 + eps || u < -eps || u > 1 + eps )
		{
			return false;
		}
		I = A + r * t;
		return true;
	}

	//! průsečík přímek (true, pokud nejsou rovnoběžné)
	bool lineIntersection( const CCVector2d& A, const CCVector2d& B, const CCVector2d& C, const CCVector2d& D, CCVector2d& I )
	{
		const CCVector2d r = B - A;
		const CCVector2d s = D - C;
		const double den = r.x * s.y - r.y * s.x;
		if ( std::abs( den ) < 1e-12 * std::max( 1.0, r.norm() * s.norm() ) )
		{
			return false;
		}
		const CCVector2d q = C - A;
		const double t = ( q.x * s.y - q.y * s.x ) / den;
		I = A + r * t;
		return true;
	}

	//! parametr průmětu bodu P na přímku AB (0 = A, 1 = B)
	double paramOn( const CCVector2d& A, const CCVector2d& B, const CCVector2d& P )
	{
		const CCVector2d AB = B - A;
		const double l2 = AB.norm2();
		return l2 > 0.0 ? ( P - A ).dot( AB ) / l2 : 0.0;
	}

	constexpr double TwoPi = 6.283185307179586;

	//! úhel do intervalu (−π, π]
	double wrapPi( double a )
	{
		while ( a > TwoPi / 2 )
			a -= TwoPi;
		while ( a <= -TwoPi / 2 )
			a += TwoPi;
		return a;
	}

	//! kružnice třemi body
	bool circleFrom3( const CCVector2d& A, const CCVector2d& B, const CCVector2d& C, CCVector2d& center )
	{
		const double d = 2.0 * ( A.x * ( B.y - C.y ) + B.x * ( C.y - A.y ) + C.x * ( A.y - B.y ) );
		if ( std::abs( d ) < 1e-12 )
		{
			return false;
		}
		const double a2 = A.norm2();
		const double b2 = B.norm2();
		const double c2 = C.norm2();
		center.x = ( a2 * ( B.y - C.y ) + b2 * ( C.y - A.y ) + c2 * ( A.y - B.y ) ) / d;
		center.y = ( a2 * ( C.x - B.x ) + b2 * ( A.x - C.x ) + c2 * ( B.x - A.x ) ) / d;
		return true;
	}

	//! oblouk ze začátku S přes M do konce E: střed, poloměr, počáteční úhel a rozsah (+ = proti směru hod. ručiček)
	bool arcFrom3( const CCVector2d& S, const CCVector2d& M, const CCVector2d& E, CCVector2d& C, double& r, double& a0, double& sweep )
	{
		if ( !circleFrom3( S, M, E, C ) )
		{
			return false;
		}
		r = ( S - C ).norm();
		a0 = std::atan2( S.y - C.y, S.x - C.x );
		auto ccw = [a0]( double a )
		{
			double d = std::fmod( a - a0, TwoPi );
			if ( d < 0 )
				d += TwoPi;
			return d;
		};
		const double dm = ccw( std::atan2( M.y - C.y, M.x - C.x ) );
		const double de = ccw( std::atan2( E.y - C.y, E.x - C.x ) );
		sweep = ( dm <= de ) ? de : de - TwoPi;
		return true;
	}

	ccColor::Rgb toRgb( const QColor& c )
	{
		return ccColor::Rgb( static_cast<ColorCompType>( c.red() ), static_cast<ColorCompType>( c.green() ), static_cast<ColorCompType>( c.blue() ) );
	}

	QString snapName( DrawTool::SnapType t )
	{
		switch ( t )
		{
		case DrawTool::SnapType::End:
			return QStringLiteral( "konec" );
		case DrawTool::SnapType::Mid:
			return QStringLiteral( "střed" );
		case DrawTool::SnapType::Intersection:
			return QStringLiteral( "průsečík" );
		case DrawTool::SnapType::OnLine:
			return QStringLiteral( "na čáře" );
		default:
			return QString();
		}
	}

	QString num( double v )
	{
		return QString::number( v, 'f', 3 ).replace( '.', ',' );
	}

	//! číslice horní řady klávesnice podle fyzické klávesy (CZ i EN rozložení), jinak -1
	int topRowDigit( const QKeyEvent* e )
	{
		if ( e->modifiers() & ( Qt::KeypadModifier | Qt::ShiftModifier | Qt::ControlModifier | Qt::AltModifier ) )
		{
			return -1;
		}
#ifdef Q_OS_WIN
		const quint32 vk = e->nativeVirtualKey();
		if ( vk >= 0x30 && vk <= 0x39 )
		{
			return static_cast<int>( vk - 0x30 );
		}
		return -1;
#else
		// X11: kódy kláves 10–19 = horní řada 1 … 0
		const quint32 sc = e->nativeScanCode();
		if ( sc >= 10 && sc <= 19 )
		{
			return sc == 19 ? 0 : static_cast<int>( sc - 9 );
		}
		const int k = e->key();
		if ( sc == 0 && k >= Qt::Key_0 && k <= Qt::Key_9 )
		{
			return k - Qt::Key_0;
		}
		return -1;
#endif
	}

	//! vyskakovací menu jako v MicroStationu: položky se volí klávesou horní řady (1 … 0, Q)
	class KeyMenu : public QMenu
	{
	public:
		using QMenu::QMenu;
		std::vector<std::pair<int, QAction*>> keys; //!< číslice (0–9) nebo Qt::Key_Q → akce

	protected:
		bool event( QEvent* ev ) override
		{
			// klávesy menu nesmí sebrat zkratky CloudCompare
			if ( ev->type() == QEvent::ShortcutOverride )
			{
				ev->accept();
				return true;
			}
			return QMenu::event( ev );
		}
		void keyPressEvent( QKeyEvent* e ) override
		{
			int code = topRowDigit( e );
			if ( code < 0 && ( e->modifiers() & Qt::KeypadModifier ) && e->key() >= Qt::Key_0 && e->key() <= Qt::Key_9 )
			{
				code = e->key() - Qt::Key_0;
			}
			if ( code < 0 && e->key() >= Qt::Key_A && e->key() <= Qt::Key_Z )
			{
				code = e->key();
			}
			if ( code >= 0 )
			{
				for ( const auto& k : keys )
				{
					if ( k.first == code )
					{
						if ( k.second->isEnabled() )
						{
							QAction* a = k.second;
							close();
							a->trigger();
						}
						return;
					}
				}
				return;
			}
			QMenu::keyPressEvent( e );
		}
	};

	CCVector3 v3( const CCVector2d& p, double z )
	{
		return CCVector3( static_cast<PointCoordinateType>( p.x ), static_cast<PointCoordinateType>( p.y ), static_cast<PointCoordinateType>( z ) );
	}

	ccPolyline* make2DPolyline( ccPointCloud*& verts, unsigned count, const ccColor::Rgb& col, PointCoordinateType width )
	{
		verts = new ccPointCloud( "preview vertices" );
		verts->reserve( count );
		for ( unsigned i = 0; i < count; ++i )
		{
			verts->addPoint( CCVector3( 0, 0, 0 ) );
		}
		auto* poly = new ccPolyline( verts );
		poly->addChild( verts );
		poly->addPointIndex( 0, count );
		poly->set2DMode( true );
		poly->setForeground( true );
		poly->setWidth( width );
		poly->showColors( true );
		poly->setColor( col );
		poly->setEnabled( false );
		return poly;
	}

	void setVertex( ccPointCloud* verts, unsigned i, const CCVector3& P )
	{
		*const_cast<CCVector3*>( verts->getPointPersistentPtr( i ) ) = P;
	}
}

DrawTool::DrawTool( ccMainAppInterface* app, QObject* parent )
	: QObject( parent )
	, m_app( app )
{
	// trasování: bod se „získá“, když na něm kurzor chvíli stojí
	m_hoverTimer = new QTimer( this );
	m_hoverTimer->setSingleShot( true );
	m_hoverTimer->setInterval( 450 );
	connect( m_hoverTimer, &QTimer::timeout, this, &DrawTool::onHoverTimeout );
	QSettings st;
	m_arcMethod = std::max( 0, std::min( 2, st.value( QStringLiteral( "qRezy/arcMethod" ), 0 ).toInt() ) );
	m_clGap = st.value( QStringLiteral( "qRezy/clGap" ), 0.10 ).toDouble();
	m_clTol = st.value( QStringLiteral( "qRezy/clTol" ), 0.02 ).toDouble();
	m_clSquare = st.value( QStringLiteral( "qRezy/clSquare" ), true ).toBool();
	m_kotaFmt = std::max( 0, std::min( 2, st.value( QStringLiteral( "qRezy/kotaFormat" ), 0 ).toInt() ) );
	m_kotaScale = std::max( 1, st.value( QStringLiteral( "qRezy/kotaScale" ), 70 ).toInt() );
	m_showHeights = st.value( QStringLiteral( "qRezy/ptHeights" ), true ).toBool();
	m_heightFmt = st.value( QStringLiteral( "qRezy/ptHeightFmt" ), 0 ).toInt();
	m_category = Drawing::layerIdAt( 0 );
}

DrawTool::~DrawTool()
{
	stop();
	for ( UndoItem& u : m_undo )
	{
		for ( Action& a : u )
		{
			if ( a.kind == Action::Remove && a.poly && !a.poly->getParent() )
			{
				delete a.poly;
			}
		}
	}
}

void DrawTool::setCloud( ccGenericPointCloud* cloud )
{
	if ( !cloud )
	{
		return;
	}
	m_cloudID = cloud->getUniqueID();
	m_shift = cloud->getGlobalShift();
	m_scale = cloud->getGlobalScale();
	m_planFront = cloud->getOwnBB().maxCorner().z + 1.0;
	if ( m_frame.plan )
	{
		const double z = m_frame.O.z;
		const int id = m_frame.viewId;
		m_frame = DrawFrame();
		m_frame.viewId = id;
		m_frame.O.z = z;
		m_frame.front = m_planFront - z;
		m_frame.back = m_frame.front;
	}
}

bool DrawTool::start( ccGenericPointCloud* cloud )
{
	if ( !m_app || !cloud )
	{
		return false;
	}
	ccGLWindowInterface* win = m_app->getActiveGLWindow();
	if ( !win )
	{
		return false;
	}
	if ( m_win )
	{
		stop();
	}

	m_win = win;
	m_winObject = win->asQObject();
	// čáry půdorysu kreslíme nad mračnem, aby je v pohledu shora nic nezakrylo
	setCloud( cloud );

	m_savedInteraction = static_cast<int>( win->getInteractionMode() );
	win->setPickingMode( ccGLWindowInterface::NO_PICKING );
	win->setInteractionMode( ccGLWindowInterface::INTERACT_PAN
	                       | ccGLWindowInterface::INTERACT_ZOOM_CAMERA
	                       | ccGLWindowInterface::INTERACT_2D_ITEMS
	                       | ccGLWindowInterface::INTERACT_SIG_MOUSE_MOVED );
	win->setWindowCursor( Qt::CrossCursor );


	m_winObject->installEventFilter( this );
	if ( QWidget* w = qobject_cast<QWidget*>( m_winObject ) )
	{
		w->setFocus();
	}
	m_layer = new RezLayer;
	m_layer->setDisplay( m_win );
	m_win->addToOwnDB( m_layer );

	drawingGroup( true );
	updateLineVisibility();
	resetTool();
	return true;
}

void DrawTool::setFrame( const DrawFrame& f )
{
	m_frame = f;
	m_clPrev = nullptr;
	if ( m_frame.plan )
	{
		m_frame.front = m_planFront - m_frame.O.z;
		m_frame.back = m_frame.front;
	}
	if ( m_mode == Mode::RotateView && !m_frame.plan )
	{
		m_mode = Mode::Line;
		Q_EMIT modeChanged( m_mode );
	}
	updateLineVisibility();
	resetTool();
	redraw3D();
}

void DrawTool::setPlanHeight( double zLocal )
{
	if ( !m_frame.plan )
	{
		return;
	}
	m_frame.O.z = zLocal;
	m_frame.front = m_planFront - zLocal;
	m_frame.back = m_frame.front;
}

void DrawTool::updateLineVisibility()
{
	ccHObject* group = drawingGroup( false );
	if ( !group )
	{
		return;
	}
	for ( unsigned i = 0; i < group->getChildrenNumber(); ++i )
	{
		ccHObject* ch = group->getChild( i );
		if ( !ch->isA( CC_TYPES::POLY_LINE ) )
		{
			continue;
		}
		const int view = ch->hasMetaData( Drawing::MetaView ) ? ch->getMetaData( Drawing::MetaView ).toInt() : 0;
		// -1 = čára definice řezu (vidět jen v půdorysu)
		const bool isRef = m_frame.plan && m_refView >= 0 && view == m_refView && view != m_frame.viewId;
		const bool on = ( view == m_frame.viewId ) || ( view == -1 && m_frame.plan ) || isRef;
		ch->setEnabled( on && !( isRef && isText( ch ) ) );
		// referenční patro šedě
		auto* poly = static_cast<ccPolyline*>( ch );
		if ( isRef )
		{
			poly->setTempColor( ccColor::Rgb( 125, 125, 125 ), true );
		}
		else if ( poly->isColorOverridden() )
		{
			poly->enableTempColor( false );
		}
	}
}

void DrawTool::stop()
{
	if ( !m_win )
	{
		return;
	}
	select( nullptr );
	if ( m_winObject )
	{
		m_winObject->removeEventFilter( this );
	}
	clearOverlayPool();
	if ( m_layer )
	{
		m_win->removeFromOwnDB( m_layer );
		delete m_layer;
		m_layer = nullptr;
	}
	m_hoverTimer->stop();
	m_acquired.clear();
	m_acqDirs.clear();
	m_win->setInteractionMode( static_cast<ccGLWindowInterface::INTERACTION_FLAGS>( m_savedInteraction ) );
	m_win->setPickingMode( ccGLWindowInterface::DEFAULT_PICKING );
	m_win->setWindowCursor( Qt::ArrowCursor );
	m_win->redraw();
	m_win = nullptr;
	m_winObject = nullptr;
	m_chain.clear();
	Q_EMIT statusChanged( QStringLiteral( "Kreslení vypnuto" ) );
}

void DrawTool::setCategory( int cat )
{
	m_category = Drawing::layerById( cat ) ? cat : Drawing::layerIdAt( 0 );
	pruneSelection();
	if ( !m_selSet.empty() )
	{
		// změna hladiny vybraných čar
		UndoItem u;
		for ( ccPolyline* p : m_selSet )
		{
			int c0;
			bool h0;
			if ( lineAttrs( p, c0, h0 ) && c0 != m_category )
			{
				Action a;
				a.kind = Action::Attr;
				a.id = p->getUniqueID();
				a.cat = c0;
				a.hidden = h0;
				u.push_back( a );
				applyLook( p, m_category, h0 );
			}
		}
		pushUndo( std::move( u ) );
		redraw3D();
	}
	updateTip();
	updateStatus();
	Q_EMIT categoryChanged( m_category );
}

void DrawTool::setHidden( bool hidden )
{
	m_hidden = hidden;
	pruneSelection();
	if ( !m_selSet.empty() )
	{
		// zakrytí vybraných čar
		UndoItem u;
		for ( ccPolyline* p : m_selSet )
		{
			int c0;
			bool h0;
			if ( lineAttrs( p, c0, h0 ) && h0 != hidden )
			{
				Action a;
				a.kind = Action::Attr;
				a.id = p->getUniqueID();
				a.cat = c0;
				a.hidden = h0;
				u.push_back( a );
				applyLook( p, c0, hidden );
			}
		}
		pushUndo( std::move( u ) );
		redraw3D();
	}
	updateTip();
	updateStatus();
	Q_EMIT hiddenChanged( m_hidden );
}

void DrawTool::setMode( Mode m )
{
	pruneSelection();
	if ( m == Mode::Delete && !m_selSet.empty() )
	{
		// s výběrem se maže hned, nástroj se nemění
		deleteSelection();
		Q_EMIT modeChanged( m_mode );
		return;
	}
	if ( m_mode != Mode::RotateView && m_mode != Mode::SectionDef )
	{
		m_prevMode = m_mode;
	}
	m_mode = m;
	// výběr zůstává jen pro výběr, přesun a kopii
	if ( m != Mode::Select && m != Mode::Move && m != Mode::Copy && m != Mode::Rotate )
	{
		setSelection( {} );
	}
	resetTool();
	if ( ( m == Mode::Move || m == Mode::Copy || m == Mode::Rotate ) && !m_selSet.empty() )
	{
		m_step = 1; // rovnou výchozí bod
		updateTip();
		updateStatus();
	}
	Q_EMIT modeChanged( m );
	if ( m == Mode::CloudBatch )
	{
		QTimer::singleShot( 0, this, [this]()
		{
			if ( m_mode == Mode::CloudBatch )
			{
				detectBatch();
				updateTip();
				updateStatus();
				redraw2D();
			}
		} );
	}
}

void DrawTool::startSectionDef( int kind )
{
	m_defKind = kind;
	setMode( Mode::SectionDef );
}

void DrawTool::setTracking( bool on )
{
	m_trackingOn = on;
	m_acquired.clear();
	m_acqDirs.clear();
	updateTip();
	updateStatus();
	redraw2D();
	Q_EMIT trackingChanged( on );
}

void DrawTool::setOrtho( bool on )
{
	m_ortho = on;
	updateTip();
	updateStatus();
	Q_EMIT orthoChanged( on );
}

void DrawTool::setArcMethod( int m )
{
	m_arcMethod = std::max( 0, std::min( 2, m ) );
	QSettings().setValue( QStringLiteral( "qRezy/arcMethod" ), m_arcMethod );
	if ( m_mode == Mode::Arc )
	{
		resetTool();
	}
}

void DrawTool::setCloudLineParams( double gap, double tol, bool square )
{
	m_clGap = gap;
	m_clTol = tol;
	m_clSquare = square;
}

void DrawTool::resetTool()
{
	m_arcPts.clear();
	m_arcSweep = 0.0;
	m_perpLock = false;
	m_clHas = false;
	m_clJoins.clear();
	m_clBatch.clear();
	m_chain.clear();
	m_typed.clear();
	m_step = 0;
	m_measDone = false;
	m_acquired.clear();
	m_acqDirs.clear();
	select( nullptr );
	updateTip();
	updateStatus();
	redraw2D();
}

// ---------------------------------------------------------------- souřadnice

double DrawTool::viewAngle() const
{
	if ( !m_win )
	{
		return 0.0;
	}
	// 1. řádek matice pohledu = směr osy X obrazovky ve světě, vyjádřený v rovině kreslení
	const double* m = m_win->getBaseViewMat().data();
	const CCVector3d sx( m[0], m[4], m[8] );
	return std::atan2( sx.dot( m_frame.V ), sx.dot( m_frame.U ) );
}

CCVector3d DrawTool::toGlobal( const CCVector2d& local ) const
{
	return CCVector3d( local.x / m_scale - m_shift.x, local.y / m_scale - m_shift.y, 0.0 );
}

double DrawTool::pixelSize() const
{
	return m_win ? m_win->computeActualPixelSize() : 1.0;
}

bool DrawTool::screenToLocal( const QPoint& pos, CCVector2d& out ) const
{
	if ( !m_win )
	{
		return false;
	}
	ccGLCameraParameters camera;
	m_win->getGLCameraParameters( camera );
	const QPointF p = m_win->toCornerGLCoordinates( pos.x(), pos.y() );
	CCVector3d P3;
	if ( !camera.unproject( CCVector3d( p.x(), p.y(), 0.5 ), P3 ) )
	{
		return false;
	}
	out = m_frame.toUV( P3 );
	return true;
}

bool DrawTool::localToScreen( const CCVector2d& P, CCVector2d& centered ) const
{
	if ( !m_win )
	{
		return false;
	}
	ccGLCameraParameters camera;
	m_win->getGLCameraParameters( camera );
	CCVector3d S;
	if ( !camera.project( m_frame.to3D( P ), S ) )
	{
		return false;
	}
	centered = CCVector2d( S.x - camera.viewport[2] / 2.0, S.y - camera.viewport[3] / 2.0 );
	return true;
}

// ---------------------------------------------------------------- výkres

ccHObject* DrawTool::drawingGroup( bool create )
{
	if ( !m_app || !m_app->dbRootObject() )
	{
		return nullptr;
	}
	ccHObject* root = m_app->dbRootObject();
	// skupina je pod mračnem, aby se uložila s projektem a měla stejný posun souřadnic
	ccHObject* cloudObj = m_cloudID ? root->find( m_cloudID ) : nullptr;
	ccHObject* parent = cloudObj ? cloudObj : root;
	for ( unsigned i = 0; i < parent->getChildrenNumber(); ++i )
	{
		ccHObject* ch = parent->getChild( i );
		if ( ch->isA( CC_TYPES::HIERARCHY_OBJECT ) && ch->getName() == Drawing::GroupName )
		{
			return ch;
		}
	}
	if ( !create )
	{
		return nullptr;
	}
	auto* group = new ccHObject( Drawing::GroupName );
	parent->addChild( group );
	m_app->addToDB( group, false, true, false, false );
	return group;
}

std::vector<DrawTool::Segment> DrawTool::collectSegments() const
{
	std::vector<Segment> segs;
	ccHObject* group = const_cast<DrawTool*>( this )->drawingGroup( false );
	if ( !group )
	{
		return segs;
	}
	for ( unsigned i = 0; i < group->getChildrenNumber(); ++i )
	{
		ccHObject* ch = group->getChild( i );
		if ( !ch->isA( CC_TYPES::POLY_LINE ) )
		{
			continue;
		}
		const int view = ch->hasMetaData( Drawing::MetaView ) ? ch->getMetaData( Drawing::MetaView ).toInt() : 0;
		const bool ref = m_frame.plan && m_refSnap && m_refView >= 0 && view == m_refView && view != m_frame.viewId && !isText( ch );
		if ( view != m_frame.viewId && !ref )
		{
			continue;
		}
		auto* poly = static_cast<ccPolyline*>( ch );
		const unsigned n = poly->size();
		const bool arc = isArc( poly );
		for ( unsigned k = 0; k + 1 < n; ++k )
		{
			const CCVector3* A = poly->getPoint( k );
			const CCVector3* B = poly->getPoint( k + 1 );
			segs.push_back( { m_frame.toUV( CCVector3d::fromArray( A->u ) ), m_frame.toUV( CCVector3d::fromArray( B->u ) ), poly, ref, arc } );
		}
	}
	return segs;
}

bool DrawTool::lineExists( ccPolyline* poly ) const
{
	ccHObject* group = const_cast<DrawTool*>( this )->drawingGroup( false );
	return poly && group && group->getChildIndex( poly ) >= 0;
}

bool DrawTool::lineEnds( ccPolyline* poly, CCVector2d& a, CCVector2d& b ) const
{
	if ( !lineExists( poly ) || poly->size() < 2 )
	{
		return false;
	}
	const CCVector3* A = poly->getPoint( 0 );
	const CCVector3* B = poly->getPoint( poly->size() - 1 );
	a = m_frame.toUV( CCVector3d::fromArray( A->u ) );
	b = m_frame.toUV( CCVector3d::fromArray( B->u ) );
	return true;
}

ccPolyline* DrawTool::pickLine( const QPoint& pos, CCVector2d* clicked ) const
{
	CCVector2d P;
	if ( !screenToLocal( pos, P ) )
	{
		return nullptr;
	}
	const double tol = PickPixels * pixelSize();
	ccPolyline* best = nullptr;
	double bestD = tol;
	for ( const Segment& s : collectSegments() )
	{
		if ( s.ref )
		{
			continue; // referenční patro nejde upravovat
		}
		const double d = dist( closestOnSegment( s.a, s.b, P ), P );
		if ( d < bestD )
		{
			bestD = d;
			best = s.poly;
		}
		if ( s.poly && isText( s.poly ) && !isKota( s.poly ) && s.poly != best )
		{
			// text: klik kamkoli na písmena (obdélník od počátku doprava a nahoru)
			// výška na obrazovce: skutečná, ale aspoň 11 px (jako RezLayer)
			const double th = std::max( QSettings().value( QStringLiteral( "qRezy/userTextHeight" ), 0.15 ).toDouble() * m_scale, 11.0 / 1.35 * pixelSize() );
			CCVector2d upv( 0.0, 1.0 );
			if ( s.poly->hasMetaData( "qRezy.textAng" ) )
			{
				const double a = s.poly->getMetaData( "qRezy.textAng" ).toDouble();
				upv = CCVector2d( -std::sin( a ), std::cos( a ) );
			}
			else if ( m_frame.plan )
			{
				const double a = viewAngle();
				upv = CCVector2d( -std::sin( a ), std::cos( a ) );
			}
			const CCVector2d rightv( upv.y, -upv.x );
			const double w = std::max( 1, s.poly->getMetaData( "qRezy.text" ).toString().size() ) * 0.75 * th;
			const CCVector2d q = P - s.a;
			const double tol = ClickTolerance * pixelSize();
			const double x = q.dot( rightv );
			const double y = q.dot( upv );
			if ( x >= -tol && x <= w + tol && y >= -tol && y <= th * 1.2 + tol )
			{
				bestD = 0.0;
				best = s.poly;
			}
		}
		if ( isKota( s.poly ) && s.poly != best )
		{
			// kóta: klik na kteroukoli čáru značky
			const KotaGeom g = kotaGeom( s.poly );
			for ( size_t k = 0; k + 1 < g.segs.size(); k += 2 )
			{
				const double dk = dist( closestOnSegment( m_frame.toUV( g.segs[k] ), m_frame.toUV( g.segs[k + 1] ), P ), P );
				if ( dk < bestD )
				{
					bestD = dk;
					best = s.poly;
				}
			}
		}
	}
	if ( clicked )
	{
		*clicked = P;
	}
	return best;
}

CCVector2d DrawTool::snapPoint( const QPoint& pos, SnapType& type ) const
{
	type = SnapType::None;
	CCVector2d P;
	if ( !screenToLocal( pos, P ) )
	{
		return P;
	}

	const double tol = SnapPixels * pixelSize();
	const std::vector<Segment> segs = collectSegments();

	std::vector<CCVector2d> ends;
	std::vector<CCVector2d> arcCenters; //!< středy oblouků a středy oblouků po délce
	ccPolyline* lastArc = nullptr;
	for ( const Segment& s : segs )
	{
		if ( !s.arc )
		{
			ends.push_back( s.a );
			ends.push_back( s.b );
		}
		else if ( s.poly != lastArc )
		{
			lastArc = s.poly;
			const unsigned n = s.poly->size();
			if ( n >= 3 )
			{
				const CCVector2d A = m_frame.toUV( CCVector3d::fromArray( s.poly->getPoint( 0 )->u ) );
				const CCVector2d M = m_frame.toUV( CCVector3d::fromArray( s.poly->getPoint( n / 2 )->u ) );
				const CCVector2d B = m_frame.toUV( CCVector3d::fromArray( s.poly->getPoint( n - 1 )->u ) );
				ends.push_back( A );
				ends.push_back( B );
				arcCenters.push_back( M );
				CCVector2d C;
				if ( circleFrom3( A, M, B, C ) )
				{
					arcCenters.push_back( C );
				}
			}
		}
	}
	ends.insert( ends.end(), m_chain.begin(), m_chain.end() );
	ends.insert( ends.end(), m_arcPts.begin(), m_arcPts.end() );
	for ( const VisPoint& vp : visiblePoints() )
	{
		ends.push_back( vp.uv );
	}

	double best = tol;
	CCVector2d bestP = P;

	// 1) konce
	for ( const CCVector2d& E : ends )
	{
		const double d = dist( E, P );
		if ( d < best )
		{
			best = d;
			bestP = E;
			type = SnapType::End;
		}
	}
	if ( type != SnapType::None )
	{
		return bestP;
	}

	std::vector<const Segment*> nearSegs;
	for ( const Segment& s : segs )
	{
		if ( dist( closestOnSegment( s.a, s.b, P ), P ) < tol )
		{
			nearSegs.push_back( &s );
		}
	}

	// 2) průsečíky
	for ( size_t i = 0; i < nearSegs.size(); ++i )
	{
		for ( size_t j = i + 1; j < nearSegs.size(); ++j )
		{
			CCVector2d I;
			if ( segmentIntersection( nearSegs[i]->a, nearSegs[i]->b, nearSegs[j]->a, nearSegs[j]->b, I ) )
			{
				const double d = dist( I, P );
				if ( d < best )
				{
					best = d;
					bestP = I;
					type = SnapType::Intersection;
				}
			}
		}
	}
	if ( type != SnapType::None )
	{
		return bestP;
	}

	// 3) středy (oblouk: střed kružnice a střed oblouku)
	for ( const CCVector2d& M : arcCenters )
	{
		const double d = dist( M, P );
		if ( d < best )
		{
			best = d;
			bestP = M;
			type = SnapType::Mid;
		}
	}
	for ( const Segment* s : nearSegs )
	{
		if ( s->arc )
		{
			continue;
		}
		const CCVector2d M = ( s->a + s->b ) * 0.5;
		const double d = dist( M, P );
		if ( d < best * 0.6 )
		{
			best = d;
			bestP = M;
			type = SnapType::Mid;
		}
	}
	if ( type != SnapType::None )
	{
		return bestP;
	}

	// 4) na čáře
	best = tol;
	for ( const Segment* s : nearSegs )
	{
		const CCVector2d C = closestOnSegment( s->a, s->b, P );
		const double d = dist( C, P );
		if ( d < best )
		{
			best = d;
			bestP = C;
			type = SnapType::OnLine;
		}
	}
	return bestP;
}

CCVector2d DrawTool::applyOrtho( const CCVector2d& from, const CCVector2d& to ) const
{
	// orto podle natočení pohledu
	const double ang = viewAngle();
	const CCVector2d v = to - from;
	const CCVector2d ax( std::cos( ang ), std::sin( ang ) );
	const CCVector2d ay( -ax.y, ax.x );
	const double px = v.dot( ax );
	const double py = v.dot( ay );
	return ( std::abs( px ) >= std::abs( py ) ) ? from + ax * px : from + ay * py;
}

CCVector2d DrawTool::cursorPoint( const QPoint& pos, SnapType& type, const CCVector2d* orthoFrom, TrackLines* lines ) const
{
	CCVector2d P = snapPoint( pos, type );
	m_perpRefValid = false;
	if ( m_perpLock && orthoFrom && m_mode == Mode::Line )
	{
		// kolmo na předchozí úsek: kurzor jen po kolmici; uchopený bod se promítne na kolmici (pata),
		// průsečík kolmice s trasovací čarou nebo s nakreslenou čarou má přednost
		CCVector2d raw;
		if ( !screenToLocal( pos, raw ) )
		{
			return P;
		}
		const bool fixedSnap = ( type == SnapType::End || type == SnapType::Mid || type == SnapType::Intersection );
		const CCVector2d base = fixedSnap ? P : raw;
		CCVector2d dir = m_perpDir;
		const CCVector2d Pc = *orthoFrom + dir * ( base - *orthoFrom ).dot( dir );
		if ( fixedSnap )
		{
			m_perpRefValid = true;
			m_perpRef = P;
			m_perpRefType = type;
			type = SnapType::None;
			if ( lines )
			{
				lines->push_back( { *orthoFrom, dir } );
			}
			return Pc;
		}
		const double tol = SnapPixels * pixelSize();
		double best = tol;
		bool found = false;
		CCVector2d bestI = Pc;
		CCVector2d bestA;
		CCVector2d bestD;
		if ( m_trackingOn )
		{
			for ( const auto& r : trackRays( nullptr ) )
			{
				if ( std::abs( r.second.dot( dir ) ) > 0.995 )
				{
					continue; // rovnoběžná s kolmicí
				}
				CCVector2d I;
				if ( lineIntersection( *orthoFrom, *orthoFrom + dir, r.first, r.first + r.second, I ) && dist( I, Pc ) < best )
				{
					best = dist( I, Pc );
					bestI = I;
					bestA = r.first;
					bestD = r.second;
					found = true;
				}
			}
		}
		if ( found )
		{
			type = SnapType::None;
			if ( lines )
			{
				lines->push_back( { bestA, bestD } );
				lines->push_back( { *orthoFrom, dir } );
			}
			return bestI;
		}
		type = SnapType::None;
		best = tol;
		for ( const Segment& sg : collectSegments() )
		{
			CCVector2d I;
			if ( lineIntersection( *orthoFrom, *orthoFrom + dir, sg.a, sg.b, I ) )
			{
				const double t = paramOn( sg.a, sg.b, I );
				if ( t >= -1e-9 && t <= 1 + 1e-9 && dist( I, Pc ) < best )
				{
					best = dist( I, Pc );
					bestI = I;
					type = SnapType::Intersection;
				}
			}
		}
		return bestI;
	}
	if ( type == SnapType::End || type == SnapType::Mid || type == SnapType::Intersection )
	{
		return P; // pevné uchopení má přednost
	}
	CCVector2d raw;
	if ( !screenToLocal( pos, raw ) )
	{
		return P;
	}
	const double tol = SnapPixels * pixelSize();
	const double ang = viewAngle();
	const CCVector2d d0( std::cos( ang ), std::sin( ang ) );
	const CCVector2d d1( -d0.y, d0.x );
	const std::vector<Segment> segs = collectSegments();

	// průsečík přímky (A, dir) s nakreslenou úsečkou poblíž bodu Q
	auto lineVsSegments = [&]( const CCVector2d& A, const CCVector2d& dir, const CCVector2d& Q, CCVector2d& out ) -> bool
	{
		double best = tol;
		bool found = false;
		for ( const Segment& s : segs )
		{
			CCVector2d I;
			if ( !lineIntersection( A, A + dir, s.a, s.b, I ) )
			{
				continue;
			}
			const double t = paramOn( s.a, s.b, I );
			if ( t < -1e-9 || t > 1 + 1e-9 )
			{
				continue;
			}
			const double d = dist( I, Q );
			if ( d < best )
			{
				best = d;
				out = I;
				found = true;
			}
		}
		return found;
	};

	if ( m_ortho && orthoFrom )
	{
		// orto: kurzor se pohybuje po vodorovné/svislé přímce od posledního bodu;
		// na ní se hledá průsečík s pomocnou čárou trasování nebo s nakreslenou čarou
		const CCVector2d Pc = applyOrtho( *orthoFrom, raw );
		CCVector2d dir = Pc - *orthoFrom;
		if ( dir.norm() < 1e-12 )
		{
			type = SnapType::None;
			return Pc;
		}
		dir.normalize();
		double best = tol;
		bool found = false;
		CCVector2d bestI;
		CCVector2d bestA;
		CCVector2d bestD;
		if ( m_trackingOn )
		{
			for ( const auto& r : trackRays( nullptr ) )
			{
				if ( std::abs( r.second.dot( dir ) ) > 0.995 )
				{
					continue; // rovnoběžná s orto
				}
				CCVector2d I;
				if ( lineIntersection( *orthoFrom, *orthoFrom + dir, r.first, r.first + r.second, I ) && dist( I, Pc ) < best )
				{
					best = dist( I, Pc );
					bestI = I;
					bestA = r.first;
					bestD = r.second;
					found = true;
				}
			}
		}
		if ( found )
		{
			type = SnapType::None;
			if ( lines )
			{
				lines->push_back( { bestA, bestD } );
				lines->push_back( { *orthoFrom, dir } );
			}
			return bestI;
		}
		CCVector2d I;
		if ( lineVsSegments( *orthoFrom, dir, Pc, I ) )
		{
			type = SnapType::Intersection;
			return I;
		}
		type = SnapType::None;
		return Pc;
	}

	if ( m_trackingOn )
	{
		// zdroje pomocných čar: získané body + poslední bod (vodorovně/svisle od něj)
		const TrackLines rays = trackRays( orthoFrom );
		CCVector2d T;
		TrackLines tl;
		if ( !rays.empty() && trackingPoint( raw, rays, T, &tl ) )
		{
			// pomocná čára × nakreslená čára
			CCVector2d I;
			if ( tl.size() == 1 && type == SnapType::OnLine && lineVsSegments( tl[0].first, tl[0].second, raw, I ) )
			{
				T = I;
				type = SnapType::Intersection;
			}
			else
			{
				type = SnapType::None;
			}
			if ( lines )
			{
				*lines = tl;
			}
			return T;
		}
	}
	return P;
}

DrawTool::TrackLines DrawTool::trackRays( const CCVector2d* from ) const
{
	// pomocné čáry: z každého získaného bodu ve směru os pohledu a v prodloužení čáry, na které bod leží;
	// přes poslední dva získané body (šikmo); od posledního bodu ve směru os
	TrackLines rays;
	const double ang = viewAngle();
	const CCVector2d d0( std::cos( ang ), std::sin( ang ) );
	const CCVector2d d1( -d0.y, d0.x );
	auto addRay = [&rays]( const CCVector2d& A, CCVector2d d )
	{
		if ( d.norm() < 1e-12 )
		{
			return;
		}
		d.normalize();
		for ( const auto& r : rays )
		{
			// stejná přímka už je
			if ( std::abs( r.second.x * d.y - r.second.y * d.x ) < 1e-6 && std::abs( ( A - r.first ).x * r.second.y - ( A - r.first ).y * r.second.x ) < 1e-6 )
			{
				return;
			}
		}
		rays.push_back( { A, d } );
	};
	for ( size_t i = 0; i < m_acquired.size(); ++i )
	{
		addRay( m_acquired[i], d0 );
		addRay( m_acquired[i], d1 );
		if ( i < m_acqDirs.size() )
		{
			for ( const CCVector2d& d : m_acqDirs[i] )
			{
				addRay( m_acquired[i], d );
			}
		}
	}
	if ( m_acquired.size() >= 2 )
	{
		const CCVector2d& A = m_acquired[m_acquired.size() - 2];
		const CCVector2d& B = m_acquired.back();
		addRay( A, B - A );
	}
	if ( from )
	{
		addRay( *from, d0 );
		addRay( *from, d1 );
	}
	return rays;
}

bool DrawTool::trackingPoint( const CCVector2d& raw, const TrackLines& rays, CCVector2d& out, TrackLines* lines ) const
{
	const double tol = SnapPixels * pixelSize();

	struct Hit
	{
		CCVector2d P;
		CCVector2d dir;
		double dist;
	};
	std::vector<Hit> hits;
	for ( const auto& r : rays )
	{
		const CCVector2d& A = r.first;
		const CCVector2d& d = r.second;
		const double t = ( raw - A ).dot( d );
		const CCVector2d proj = A + d * t;
		const double dd = ( raw - proj ).norm();
		if ( dd < tol )
		{
			hits.push_back( { A, d, dd } );
		}
	}
	if ( hits.empty() )
	{
		return false;
	}

	// dvě pomocné čáry → průsečík (má přednost)
	double bestI = tol * 1.5;
	bool foundI = false;
	for ( size_t i = 0; i < hits.size(); ++i )
	{
		for ( size_t j = i + 1; j < hits.size(); ++j )
		{
			CCVector2d I;
			if ( lineIntersection( hits[i].P, hits[i].P + hits[i].dir, hits[j].P, hits[j].P + hits[j].dir, I ) && ( I - raw ).norm() < bestI )
			{
				bestI = ( I - raw ).norm();
				out = I;
				foundI = true;
				if ( lines )
				{
					lines->clear();
					lines->push_back( { hits[i].P, hits[i].dir } );
					lines->push_back( { hits[j].P, hits[j].dir } );
				}
			}
		}
	}
	if ( foundI )
	{
		return true;
	}

	// jedna pomocná čára → průmět
	const Hit* best = &hits[0];
	for ( const Hit& h : hits )
	{
		if ( h.dist < best->dist )
		{
			best = &h;
		}
	}
	out = best->P + best->dir * ( raw - best->P ).dot( best->dir );
	if ( lines )
	{
		lines->push_back( { best->P, best->dir } );
	}
	return true;
}

void DrawTool::onHoverTimeout()
{
	if ( !m_trackingOn || !m_hoverValid || !m_hasCursor || ( m_lastCand - m_hoverCand ).norm() > 1e-9 )
	{
		return;
	}
	m_acqDirs.resize( m_acquired.size() );
	// získat / zrušit bod
	for ( size_t i = 0; i < m_acquired.size(); ++i )
	{
		if ( ( m_acquired[i] - m_hoverCand ).norm() < 1e-9 )
		{
			m_acquired.erase( m_acquired.begin() + static_cast<long>( i ) );
			m_acqDirs.erase( m_acqDirs.begin() + static_cast<long>( i ) );
			updateTip();
			redraw2D();
			return;
		}
	}
	// směry čar, které v bodě končí nebo mají střed (prodloužení čáry)
	std::vector<CCVector2d> dirs;
	for ( const Segment& sg : collectSegments() )
	{
		if ( sg.arc )
		{
			continue;
		}
		const CCVector2d mid = ( sg.a + sg.b ) * 0.5;
		const double tolP = 1e-6 * m_scale;
		if ( dist( sg.a, m_hoverCand ) < tolP || dist( sg.b, m_hoverCand ) < tolP || dist( mid, m_hoverCand ) < tolP )
		{
			CCVector2d d = sg.b - sg.a;
			if ( d.norm() > 1e-9 )
			{
				d.normalize();
				dirs.push_back( d );
			}
		}
	}
	m_acquired.push_back( m_hoverCand );
	m_acqDirs.push_back( dirs );
	if ( m_acquired.size() > 3 )
	{
		m_acquired.erase( m_acquired.begin() );
		m_acqDirs.erase( m_acqDirs.begin() );
	}
	updateTip();
	redraw2D();
}

// ---------------------------------------------------------------- úpravy

ccPolyline* DrawTool::addLine( const CCVector2d& a, const CCVector2d& b, int cat, bool hidden, UndoItem* undo )
{
	if ( dist( a, b ) < 1e-6 )
	{
		return nullptr;
	}
	return addPolyline( { a, b }, cat, hidden, false, undo );
}

bool DrawTool::isArc( const ccHObject* obj )
{
	return obj && obj->hasMetaData( Drawing::MetaArc ) && obj->getMetaData( Drawing::MetaArc ).toBool();
}

std::vector<CCVector2d> DrawTool::polyPoints( ccPolyline* poly ) const
{
	std::vector<CCVector2d> out;
	if ( !poly )
	{
		return out;
	}
	for ( unsigned k = 0; k < poly->size(); ++k )
	{
		out.push_back( m_frame.toUV( CCVector3d::fromArray( poly->getPoint( k )->u ) ) );
	}
	return out;
}

void DrawTool::setPolyPoints( ccPolyline* poly, const std::vector<CCVector2d>& pts, UndoItem* undo )
{
	auto* verts = poly ? dynamic_cast<ccPointCloud*>( poly->getAssociatedCloud() ) : nullptr;
	if ( !verts || verts->size() != pts.size() || !lineExists( poly ) )
	{
		return;
	}
	if ( undo )
	{
		Action act;
		act.kind = Action::Modify;
		act.id = poly->getUniqueID();
		act.pts = polyPoints( poly );
		undo->push_back( act );
	}
	for ( unsigned k = 0; k < verts->size(); ++k )
	{
		setVertex( verts, k, m_frame.to3D( pts[k] ).toFloat() );
	}
	poly->setMetaData( "qRezy.fx", m_frame.N.x * m_frame.back );
	poly->setMetaData( "qRezy.fy", m_frame.N.y * m_frame.back );
	poly->setMetaData( "qRezy.fz", m_frame.N.z * m_frame.back );
	verts->invalidateBoundingBox();
	poly->prepareDisplayForRefresh();
}

void DrawTool::translateItem( ccPolyline* poly, const CCVector2d& d, bool copy, UndoItem* undo )
{
	if ( !lineExists( poly ) )
	{
		return;
	}
	std::vector<CCVector2d> pts = polyPoints( poly );
	for ( CCVector2d& P : pts )
	{
		P += d;
	}
	if ( pts.empty() )
	{
		return;
	}
	if ( !copy )
	{
		if ( isArc( poly ) )
		{
			setPolyPoints( poly, pts, undo );
		}
		else
		{
			setLine( poly, pts.front(), pts.back(), undo );
		}
		return;
	}
	if ( isKota( poly ) )
	{
		addKota( pts.front(), poly->getMetaData( "qRezy.kotaSide" ).toInt(), poly->getMetaData( "qRezy.kotaFmt" ).toInt(), poly->getMetaData( "qRezy.kotaScale" ).toInt(), undo, poly );
		return;
	}
	if ( isText( poly ) )
	{
		if ( ccPolyline* n = addText( pts.front(), poly->getMetaData( "qRezy.text" ).toString(), undo ) )
		{
			for ( const char* key : { "qRezy.textLevel", "qRezy.textH", "qRezy.textAng" } )
			{
				if ( poly->hasMetaData( key ) )
				{
					n->setMetaData( key, poly->getMetaData( key ) );
				}
			}
		}
		return;
	}
	int c0;
	bool h0;
	lineAttrs( poly, c0, h0 );
	if ( isArc( poly ) )
	{
		addPolyline( pts, c0, h0, true, undo );
	}
	else
	{
		addLine( pts.front(), pts.back(), c0, h0, undo );
	}
}

bool DrawTool::rejectArc( ccPolyline* poly )
{
	if ( !isArc( poly ) )
	{
		return false;
	}
	m_flash = QStringLiteral( "Oblouk tímto nástrojem upravit nejde – lze ho vybrat, přesunout, kopírovat, smazat a změnit hladinu." );
	Q_EMIT statusChanged( m_flash );
	return true;
}

ccPolyline* DrawTool::addPolyline( const std::vector<CCVector2d>& pts, int cat, bool hidden, bool arc, UndoItem* undo )
{
	ccHObject* group = drawingGroup( true );
	if ( !group || pts.size() < 2 )
	{
		return nullptr;
	}

	auto* verts = new ccPointCloud( "vertices" );
	verts->reserve( static_cast<unsigned>( pts.size() ) );
	for ( const CCVector2d& P : pts )
	{
		verts->addPoint( m_frame.to3D( P ).toFloat() );
	}
	verts->setEnabled( false );
	verts->setGlobalShift( m_shift );
	verts->setGlobalScale( m_scale );

	auto* poly = new RezLine( verts );
	poly->addChild( verts );
	poly->addPointIndex( 0, static_cast<unsigned>( pts.size() ) );
	poly->setClosed( false );
	poly->setGlobalShift( m_shift );
	poly->setGlobalScale( m_scale );
	poly->setDashed( hidden );
	poly->setColor( toRgb( Drawing::displayColor( cat, hidden ) ) );
	poly->showColors( true );
	poly->setWidth( 2 );
	poly->setName( Drawing::categoryName( cat ) + ( arc ? QStringLiteral( " – oblouk" ) : QString() ) + ( hidden ? QStringLiteral( " (zakrytá)" ) : QString() ) );
	poly->setMetaData( Drawing::MetaCategory, cat );
	poly->setMetaData( Drawing::MetaHidden, hidden );
	poly->setMetaData( Drawing::MetaView, m_frame.viewId );
	if ( arc )
	{
		poly->setMetaData( Drawing::MetaArc, true );
	}
	// posun čáry k pozorovateli (pro zobrazení ve 3D na skutečném místě)
	poly->setMetaData( "qRezy.fx", m_frame.N.x * m_frame.back );
	poly->setMetaData( "qRezy.fy", m_frame.N.y * m_frame.back );
	poly->setMetaData( "qRezy.fz", m_frame.N.z * m_frame.back );

	group->addChild( poly );
	m_app->addToDB( poly, false, false, false, false );
	if ( undo )
	{
		Action act;
		act.kind = Action::Add;
		act.id = poly->getUniqueID();
		undo->push_back( act );
	}
	return poly;
}

void DrawTool::setLine( ccPolyline* poly, const CCVector2d& a, const CCVector2d& b, UndoItem* undo )
{
	CCVector2d oa;
	CCVector2d ob;
	if ( !lineEnds( poly, oa, ob ) )
	{
		return;
	}
	auto* verts = dynamic_cast<ccPointCloud*>( poly->getAssociatedCloud() );
	if ( !verts || verts->size() < 2 )
	{
		return;
	}
	if ( undo )
	{
		Action act;
		act.kind = Action::Modify;
		act.id = poly->getUniqueID();
		act.a = oa;
		act.b = ob;
		undo->push_back( act );
	}
	setVertex( verts, 0, m_frame.to3D( a ).toFloat() );
	setVertex( verts, verts->size() - 1, m_frame.to3D( b ).toFloat() );
	poly->setMetaData( "qRezy.fx", m_frame.N.x * m_frame.back );
	poly->setMetaData( "qRezy.fy", m_frame.N.y * m_frame.back );
	poly->setMetaData( "qRezy.fz", m_frame.N.z * m_frame.back );
	verts->invalidateBoundingBox();
	poly->prepareDisplayForRefresh();
}

void DrawTool::removeLine( ccPolyline* poly, UndoItem* undo )
{
	if ( !lineExists( poly ) )
	{
		return;
	}
	if ( poly == m_sel )
	{
		select( nullptr );
	}
	if ( isSelected( poly ) )
	{
		restoreLook( poly );
		m_selSet.erase( std::remove( m_selSet.begin(), m_selSet.end(), poly ), m_selSet.end() );
	}
	m_app->removeFromDB( poly, false ); // necháme si objekt kvůli Zpět
	if ( undo )
	{
		Action act;
		act.kind = Action::Remove;
		act.id = poly->getUniqueID();
		act.poly = poly;
		undo->push_back( act );
	}
	else
	{
		delete poly;
	}
}

void DrawTool::pushUndo( UndoItem&& u )
{
	if ( !u.empty() )
	{
		m_undo.push_back( std::move( u ) );
		if ( m_undo.size() > 500 )
		{
			m_undo.erase( m_undo.begin() );
		}
		Q_EMIT drawingChanged();
	}
}

void DrawTool::select( ccPolyline* poly )
{
	ccPolyline* prev = m_sel;
	m_sel = poly;
	if ( prev && prev != poly && lineExists( prev ) )
	{
		restoreLook( prev );
	}
	if ( m_sel )
	{
		m_sel->setColor( ccColor::Rgb( 255, 220, 0 ) );
		m_sel->setWidth( 4 );
	}
	redraw3D();
}

void DrawTool::undo()
{
	if ( m_undo.empty() )
	{
		return;
	}
	select( nullptr );
	setSelection( {} );
	UndoItem u = std::move( m_undo.back() );
	m_undo.pop_back();

	for ( auto it = u.rbegin(); it != u.rend(); ++it )
	{
		Action& a = *it;
		switch ( a.kind )
		{
		case Action::Add:
			if ( ccHObject* obj = m_app->dbRootObject()->find( a.id ) )
			{
				m_app->removeFromDB( obj, true );
			}
			break;
		case Action::Remove:
			if ( a.poly )
			{
				if ( ccHObject* group = drawingGroup( true ) )
				{
					group->addChild( a.poly );
					m_app->addToDB( a.poly, false, false, false, false );
				}
				a.poly = nullptr;
			}
			break;
		case Action::Modify:
			if ( ccHObject* obj = m_app->dbRootObject()->find( a.id ) )
			{
				if ( obj->isA( CC_TYPES::POLY_LINE ) )
				{
					if ( !a.pts.empty() )
						setPolyPoints( static_cast<ccPolyline*>( obj ), a.pts, nullptr );
					else
						setLine( static_cast<ccPolyline*>( obj ), a.a, a.b, nullptr );
				}
			}
			break;
		case Action::Attr:
			if ( ccHObject* obj = m_app->dbRootObject()->find( a.id ) )
			{
				if ( obj->isA( CC_TYPES::POLY_LINE ) )
				{
					applyLook( static_cast<ccPolyline*>( obj ), a.cat, a.hidden );
				}
			}
			break;
		}
	}

	if ( m_mode == Mode::Line && !m_chain.empty() )
	{
		// zrušená úsečka rozkreslené čáry vrací i poslední bod
		m_chain.pop_back();
		if ( m_chain.size() < 1 )
		{
			m_chain.clear();
		}
	}
	Q_EMIT drawingChanged();
	redraw3D();
	updateTip();
	updateStatus();
}

// ---------------------------------------------------------------- oblouk

std::vector<CCVector2d> DrawTool::arcPoints( const CCVector2d& C, double r, double a0, double sweep ) const
{
	// krok podle odchylky tětivy od oblouku (~0,5 mm), sudý počet úseků → prostřední vrchol = střed oblouku
	const double tol = 0.0005 * m_scale;
	double stepAng = ( r > tol ) ? 2.0 * std::acos( std::max( -1.0, 1.0 - tol / r ) ) : 0.2;
	stepAng = std::max( 0.5 / RadToDeg, std::min( 5.0 / RadToDeg, stepAng ) );
	int n = static_cast<int>( std::ceil( std::abs( sweep ) / stepAng ) );
	n = std::max( 8, std::min( 720, n ) );
	if ( n % 2 )
	{
		++n;
	}
	std::vector<CCVector2d> pts;
	pts.reserve( static_cast<size_t>( n ) + 1 );
	for ( int i = 0; i <= n; ++i )
	{
		const double a = a0 + sweep * i / n;
		pts.push_back( C + CCVector2d( std::cos( a ), std::sin( a ) ) * r );
	}
	return pts;
}

bool DrawTool::arcPreview( std::vector<CCVector2d>& pts ) const
{
	if ( m_mode != Mode::Arc || m_arcPts.size() != 2 || !m_hasCursor )
	{
		return false;
	}
	const CCVector2d S = m_arcPts[0];
	const CCVector2d P1 = m_arcPts[1];
	const CCVector2d Q = m_cursor;
	CCVector2d C;
	double r = 0.0;
	double a0 = 0.0;
	double sweep = 0.0;
	switch ( m_arcMethod )
	{
	case 0: // začátek – střed oblouku – konec (oblouk prochází všemi třemi body)
		if ( !arcFrom3( S, P1, Q, C, r, a0, sweep ) )
		{
			return false;
		}
		break;
	case 1: // začátek – konec – střed oblouku (třetí bod leží na oblouku)
		if ( !arcFrom3( S, Q, P1, C, r, a0, sweep ) )
		{
			return false;
		}
		break;
	default: // začátek – střed kružnice – konec (směr podle pohybu myši)
	{
		C = P1;
		r = dist( S, C );
		a0 = std::atan2( S.y - C.y, S.x - C.x );
		sweep = m_arcSweep;
		break;
	}
	}
	if ( r < 1e-6 || std::abs( sweep ) < 1e-4 )
	{
		return false;
	}
	pts = arcPoints( C, r, a0, sweep );
	return true;
}

// ---------------------------------------------------------------- čára z mračna

bool DrawTool::detectCloudLine( const CCVector2d& click )
{
	m_clHas = false;
	m_clJoin = false;
	ccGenericPointCloud* cloud = nullptr;
	if ( m_app && m_app->dbRootObject() && m_cloudID )
	{
		cloud = ccHObjectCaster::ToGenericPointCloud( m_app->dbRootObject()->find( m_cloudID ) );
	}
	if ( !cloud )
	{
		m_flash = QStringLiteral( "Čára z mračna: není vybrané mračno" );
		return false;
	}
	const std::vector<std::array<double, 4>> planes = m_sliceProvider ? m_sliceProvider() : std::vector<std::array<double, 4>>();
	const double sc = m_scale;
	const double R = 0.30 * sc;          // okolí kliknutí
	const double tol = std::max( 0.003, m_clTol ) * sc;
	const double gap = std::max( 0.01, m_clGap ) * sc;
	const double reach = 40.0 * sc;      // max. délka hledání podél přímky

	// body řezu v rovině kreslení (jen v dosahu)
	std::vector<CCVector2d> nearPts;
	std::vector<CCVector2d> all;
	const unsigned n = cloud->size();
	nearPts.reserve( 4096 );
	all.reserve( 65536 );
	for ( unsigned i = 0; i < n; ++i )
	{
		const CCVector3* P = cloud->getPoint( i );
		bool in = true;
		for ( const auto& e : planes )
		{
			if ( e[0] * P->x + e[1] * P->y + e[2] * P->z + e[3] < 0.0 )
			{
				in = false;
				break;
			}
		}
		if ( !in )
		{
			continue;
		}
		const CCVector2d uv = m_frame.toUV( CCVector3d( P->x, P->y, P->z ) );
		const double d = dist( uv, click );
		if ( d > reach )
		{
			continue;
		}
		all.push_back( uv );
		if ( d <= R )
		{
			nearPts.push_back( uv );
		}
	}
	if ( nearPts.size() < 12 )
	{
		m_flash = QStringLiteral( "Čára z mračna: u kliknutí je málo bodů řezu (%1) – klikněte blíž k bodům" ).arg( nearPts.size() );
		return false;
	}

	// RANSAC: přímka s nejvíce body v toleranci (pevné „náhodné“ pořadí kvůli opakovatelnosti)
	unsigned seed = 12345u;
	auto rnd = [&seed]( size_t m ) { seed = seed * 1103515245u + 12345u; return static_cast<size_t>( ( seed >> 8 ) % m ); };
	size_t bestCount = 0;
	CCVector2d bestP;
	CCVector2d bestD;
	for ( int it = 0; it < 400; ++it )
	{
		const CCVector2d& A = nearPts[rnd( nearPts.size() )];
		const CCVector2d& B = nearPts[rnd( nearPts.size() )];
		CCVector2d d = B - A;
		if ( d.norm() < 0.05 * sc )
		{
			continue;
		}
		d.normalize();
		const CCVector2d nn( -d.y, d.x );
		size_t cnt = 0;
		for ( const CCVector2d& Q : nearPts )
		{
			if ( std::abs( ( Q - A ).dot( nn ) ) <= tol )
			{
				++cnt;
			}
		}
		// při shodě přednost přímce blíž ke kliknutí
		if ( cnt > bestCount || ( cnt == bestCount && cnt > 0 && std::abs( ( click - A ).dot( nn ) ) < std::abs( ( click - bestP ).dot( CCVector2d( -bestD.y, bestD.x ) ) ) ) )
		{
			bestCount = cnt;
			bestP = A;
			bestD = d;
		}
	}
	if ( bestCount < 10 )
	{
		m_flash = QStringLiteral( "Čára z mračna: body u kliknutí netvoří přímku" );
		return false;
	}

	// zpřesnění (nejmenší čtverce kolmo k přímce) přes body v pásu, růst podél přímky s max. mezerou
	CCVector2d P0 = bestP;
	CCVector2d D = bestD;
	double tMin = 0.0;
	double tMax = 0.0;
	for ( int pass = 0; pass < 3; ++pass )
	{
		const CCVector2d nn( -D.y, D.x );
		std::vector<std::pair<double, CCVector2d>> band;
		for ( const CCVector2d& Q : all )
		{
			if ( std::abs( ( Q - P0 ).dot( nn ) ) <= tol )
			{
				band.push_back( { ( Q - P0 ).dot( D ), Q } );
			}
		}
		if ( band.size() < 10 )
		{
			break;
		}
		std::sort( band.begin(), band.end(), []( const std::pair<double, CCVector2d>& x, const std::pair<double, CCVector2d>& y ) { return x.first < y.first; } );
		const double t0 = ( click - P0 ).dot( D );
		size_t k0 = std::lower_bound( band.begin(), band.end(), t0, []( const std::pair<double, CCVector2d>& x, double v ) { return x.first < v; } ) - band.begin();
		if ( k0 >= band.size() )
		{
			k0 = band.size() - 1;
		}
		size_t lo = k0;
		while ( lo > 0 && band[lo].first - band[lo - 1].first <= gap )
		{
			--lo;
		}
		size_t hi = k0;
		while ( hi + 1 < band.size() && band[hi + 1].first - band[hi].first <= gap )
		{
			++hi;
		}
		if ( hi - lo + 1 < 10 )
		{
			m_flash = QStringLiteral( "Čára z mračna: příliš krátký úsek" );
			return false;
		}
		// PCA přes vybraný úsek
		CCVector2d m( 0, 0 );
		for ( size_t k = lo; k <= hi; ++k )
		{
			m += band[k].second;
		}
		m /= static_cast<double>( hi - lo + 1 );
		double sxx = 0.0;
		double sxy = 0.0;
		double syy = 0.0;
		for ( size_t k = lo; k <= hi; ++k )
		{
			const CCVector2d q = band[k].second - m;
			sxx += q.x * q.x;
			sxy += q.x * q.y;
			syy += q.y * q.y;
		}
		const double ang = 0.5 * std::atan2( 2.0 * sxy, sxx - syy );
		CCVector2d Dn( std::cos( ang ), std::sin( ang ) );
		if ( Dn.dot( D ) < 0 )
		{
			Dn = Dn * -1.0;
		}
		P0 = m;
		D = Dn;
		tMin = 1e300;
		tMax = -1e300;
		for ( size_t k = lo; k <= hi; ++k )
		{
			const double t = ( band[k].second - P0 ).dot( D );
			tMin = std::min( tMin, t );
			tMax = std::max( tMax, t );
		}
	}
	if ( tMax - tMin < 0.05 * sc )
	{
		m_flash = QStringLiteral( "Čára z mračna: příliš krátký úsek" );
		return false;
	}

	// napojení v rohu na okolní čáry – na obou koncích, nejen na předchozí čáru (a srovnání do pravého úhlu)
	m_clJoins.clear();
	const double reachJ = 0.6 * sc;
	std::vector<Segment> others;
	for ( const Segment& sg : collectSegments() )
	{
		if ( !sg.ref && !sg.arc && sg.poly && !isText( sg.poly ) && sg.poly->size() == 2 && dist( sg.a, sg.b ) > 1e-9 )
		{
			others.push_back( sg );
		}
	}
	if ( m_clSquare )
	{
		// téměř kolmá čára (do 3°), která se s novou potká u konce → přesně kolmo (přednost předchozí čáře)
		const CCVector2d A = P0 + D * tMin;
		const CCVector2d B = P0 + D * tMax;
		const Segment* ref = nullptr;
		double refScore = 1e300;
		for ( const Segment& sg : others )
		{
			CCVector2d pd = sg.b - sg.a;
			pd.normalize();
			if ( std::abs( pd.dot( D ) ) >= std::sin( 3.0 / RadToDeg ) )
			{
				continue;
			}
			CCVector2d I;
			if ( !lineIntersection( A, B, sg.a, sg.b, I ) )
			{
				continue;
			}
			const double dNew = std::min( dist( I, A ), dist( I, B ) );
			const double dOld = std::min( dist( I, sg.a ), dist( I, sg.b ) );
			const double t = paramOn( sg.a, sg.b, I );
			if ( dNew > reachJ || ( dOld > reachJ && ( t < 0.0 || t > 1.0 ) ) )
			{
				continue;
			}
			const double score = dNew + ( sg.poly == m_clPrev ? -1e6 : 0.0 );
			if ( score < refScore )
			{
				refScore = score;
				ref = &sg;
			}
		}
		if ( ref )
		{
			CCVector2d pd = ref->b - ref->a;
			pd.normalize();
			const CCVector2d mid = P0 + D * ( 0.5 * ( tMin + tMax ) );
			const double half = 0.5 * ( tMax - tMin );
			CCVector2d Dq( -pd.y, pd.x );
			if ( Dq.dot( D ) < 0 )
			{
				Dq = Dq * -1.0;
			}
			D = Dq;
			P0 = mid;
			tMin = -half;
			tMax = half;
		}
	}
	const double tMid = 0.5 * ( tMin + tMax );
	const double epsJ = 0.01 * sc;
	// konec staré čáry je volný = nekončí na něm ani na něj nenavazuje jiná čára (roh už hotový → neprodlužovat)
	auto endFree = [&]( const Segment& sg, const CCVector2d& P )
	{
		for ( const Segment& o : others )
		{
			if ( o.poly != sg.poly && dist( closestOnSegment( o.a, o.b, P ), P ) < epsJ )
			{
				return false;
			}
		}
		return true;
	};
	// prodloužení P→Q nesmí přetnout jinou čáru (např. druhý líc zdi) – jinak by spojilo přes mezeru
	auto crosses = [&]( const CCVector2d& P, const CCVector2d& Q, const void* skip1, const void* skip2 )
	{
		if ( dist( P, Q ) < epsJ )
		{
			return false;
		}
		for ( const Segment& o : others )
		{
			if ( o.poly == skip1 || o.poly == skip2 )
			{
				continue;
			}
			CCVector2d X;
			if ( !lineIntersection( P, Q, o.a, o.b, X ) )
			{
				continue;
			}
			const double u = paramOn( P, Q, X );
			const double v = paramOn( o.a, o.b, X );
			const double mu = epsJ / dist( P, Q );
			const double mv = epsJ / dist( o.a, o.b );
			if ( u > mu && u < 1.0 - mu && v > -mv && v < 1.0 + mv )
			{
				return true;
			}
		}
		return false;
	};
	for ( int e = 0; e < 2; ++e )
	{
		const CCVector2d A = P0 + D * tMin;
		const CCVector2d B = P0 + D * tMax;
		const CCVector2d E = e == 0 ? A : B;
		const Segment* best = nullptr;
		double bestScore = 1e300;
		CCVector2d bestI;
		int bestEnd = -1; // -1 = T (stará čára se nemění)
		for ( const Segment& sg : others )
		{
			CCVector2d pd = sg.b - sg.a;
			const double Lo = pd.norm();
			pd = pd / Lo;
			if ( std::abs( pd.dot( D ) ) > std::cos( 20.0 / RadToDeg ) )
			{
				continue; // jen různoběžné čáry
			}
			CCVector2d I;
			if ( !lineIntersection( A, B, sg.a, sg.b, I ) )
			{
				continue;
			}
			const double ti = ( I - P0 ).dot( D );
			const double dE = dist( I, E );
			if ( dE > reachJ || ( e == 0 ? ti > tMid : ti < tMid ) )
			{
				continue;
			}
			const double dA = dist( I, sg.a );
			const double dB = dist( I, sg.b );
			const double t = paramOn( sg.a, sg.b, I );
			if ( crosses( E, I, sg.poly, nullptr ) )
			{
				continue; // mezi koncem nové čáry a průsečíkem leží jiná čára
			}
			double score;
			int end;
			const bool outside = t < 0.0 || t > 1.0;
			if ( std::min( dA, dB ) <= reachJ && outside )
			{
				// L: prodloužit starou čáru – jen volný konec a bez přetnutí jiné čáry
				const CCVector2d& Eo = dA <= dB ? sg.a : sg.b;
				if ( !endFree( sg, Eo ) || crosses( Eo, I, sg.poly, nullptr ) )
				{
					continue;
				}
				score = dE + std::min( dA, dB );
				end = dA <= dB ? 0 : 1;
			}
			else if ( std::min( dA, dB ) <= reachJ )
			{
				// průsečík na staré čáře u konce: zkrátit ji jen když je konec volný, jinak T (hotový roh nerozbít)
				score = dE + std::min( dA, dB );
				end = endFree( sg, dA <= dB ? sg.a : sg.b ) ? ( dA <= dB ? 0 : 1 ) : -1;
			}
			else if ( t > 0.0 && t < 1.0 )
			{
				score = dE + reachJ; // T – konec nové čáry k čáře
				end = -1;
			}
			else
			{
				continue;
			}
			if ( score < bestScore )
			{
				bestScore = score;
				best = &sg;
				bestI = I;
				bestEnd = end;
			}
		}
		if ( !best )
		{
			continue;
		}
		const double ti = ( bestI - P0 ).dot( D );
		if ( e == 0 )
			tMin = ti;
		else
			tMax = ti;
		if ( bestEnd >= 0 )
		{
			auto it = std::find_if( m_clJoins.begin(), m_clJoins.end(), [best]( const ClJoin& j ) { return j.poly == best->poly; } );
			if ( it == m_clJoins.end() )
			{
				m_clJoins.push_back( { best->poly, best->a, best->b } );
				it = m_clJoins.end() - 1;
			}
			( bestEnd == 0 ? it->a : it->b ) = bestI;
		}
		m_clJoin = true;
	}
	if ( tMax - tMin < 0.05 * sc )
	{
		m_flash = QStringLiteral( "Čára z mračna: příliš krátký úsek" );
		m_clJoins.clear();
		m_clJoin = false;
		return false;
	}
	m_clA = P0 + D * tMin;
	m_clB = P0 + D * tMax;
	m_clHas = true;
	return true;
}

void DrawTool::acceptCloudLine()
{
	if ( !m_clHas )
	{
		return;
	}
	UndoItem u;
	for ( const ClJoin& j : m_clJoins )
	{
		if ( lineExists( j.poly ) )
		{
			setLine( j.poly, j.a, j.b, &u );
		}
	}
	m_clJoins.clear();
	ccPolyline* p = addLine( m_clA, m_clB, m_category, m_hidden, &u );
	pushUndo( std::move( u ) );
	m_clPrev = p;
	m_clHas = false;
	m_clJoin = false;
}

void DrawTool::slicePoints2D( std::vector<CCVector2d>& out, const CCVector2d* center, double reach ) const
{
	out.clear();
	ccGenericPointCloud* cloud = nullptr;
	if ( m_app && m_app->dbRootObject() && m_cloudID )
	{
		cloud = ccHObjectCaster::ToGenericPointCloud( m_app->dbRootObject()->find( m_cloudID ) );
	}
	if ( !cloud )
	{
		return;
	}
	const std::vector<std::array<double, 4>> planes = m_sliceProvider ? m_sliceProvider() : std::vector<std::array<double, 4>>();
	const unsigned n = cloud->size();
	for ( unsigned i = 0; i < n; ++i )
	{
		const CCVector3* P = cloud->getPoint( i );
		bool in = true;
		for ( const auto& e : planes )
		{
			if ( e[0] * P->x + e[1] * P->y + e[2] * P->z + e[3] < 0.0 )
			{
				in = false;
				break;
			}
		}
		if ( !in )
		{
			continue;
		}
		const CCVector2d uv = m_frame.toUV( CCVector3d( P->x, P->y, P->z ) );
		if ( center && dist( uv, *center ) > reach )
		{
			continue;
		}
		out.push_back( uv );
	}
}

CloudLines::Params DrawTool::cloudParams() const
{
	CloudLines::Params p;
	p.scale = m_scale;
	p.tol = std::max( 0.003, m_clTol ) * m_scale;
	p.gap = std::max( 0.01, m_clGap ) * m_scale;
	p.minLen = std::max( 0.05, m_clMinLen ) * m_scale;
	p.square = m_clSquare;
	return p;
}

void DrawTool::detectRoom( const CCVector2d& click )
{
	m_clBatch.clear();
	m_clRoomClick = click;
	QApplication::setOverrideCursor( Qt::WaitCursor );
	std::vector<CCVector2d> pts;
	slicePoints2D( pts, &click, 25.0 * m_scale );
	double openRays = 1.0;
	double openFlood = 1.0;
	if ( pts.size() >= 20 )
	{
		// obě metody: paprsky z kliknutí (v1.15) a rozlití volné plochy; pravoúhlost do 5°,
		// shodné hrany = fáze 1, hrany jen jedné metody ležící na bodech = fáze 2
		const CloudLines::Params p = cloudParams();
		CloudLinesV15::Params p15;
		p15.tol = p.tol;
		p15.gap = p.gap;
		p15.minLen = p.minLen;
		p15.scale = p.scale;
		p15.square = p.square;
		std::vector<CloudLines::Seg> rays;
		for ( const CloudLinesV15::Seg& sg : CloudLinesV15::room( pts, click, p15, openRays ) )
		{
			CloudLines::Seg s;
			s.a = sg.a;
			s.b = sg.b;
			rays.push_back( s );
		}
		const std::vector<CloudLines::Seg> flood = CloudLines::room( pts, click, p, openFlood );
		m_clBatch = CloudLines::combineRoom( rays, flood, pts, p, ( p.square ? 5.0 : 90.0 ) / RadToDeg );
	}
	QApplication::restoreOverrideCursor();
	if ( m_clBatch.empty() )
	{
		m_flash = QStringLiteral( "Místnost: nenašel jsem obrys – klikněte dál od zdi, doprostřed místnosti" );
	}
	else
	{
		m_flash = roomPrompt();
		if ( openFlood > 0.99 && openRays > 0.25 )
		{
			m_flash += QStringLiteral( " – pozor, místnost není uzavřená" );
		}
	}
}

QString DrawTool::roomPrompt() const
{
	static const char* names[] = { "obě metody se shodují", "jen jedna metoda – zkontrolujte" };
	const int ph = std::max( 0, std::min( 1, batchPhase() ) );
	int n = 0;
	int n2 = 0;
	for ( const CloudLines::Seg& sg : m_clBatch )
	{
		n += sg.cls == ph ? 1 : 0;
		n2 += sg.cls > ph ? 1 : 0;
	}
	return QStringLiteral( "Místnost %1/2 (%2): %3 čar%4 – Enter nebo klik do místnosti = potvrdit, pravé = přeskočit, klik do jiné = další místnost" )
	    .arg( ph + 1 )
	    .arg( QString::fromUtf8( names[ph] ) )
	    .arg( n )
	    .arg( n2 > 0 ? QStringLiteral( " (pak ještě %1)" ).arg( n2 ) : QString() );
}

void DrawTool::detectBatch()
{
	m_clBatch.clear();
	QApplication::setOverrideCursor( Qt::WaitCursor );
	std::vector<CCVector2d> pts;
	slicePoints2D( pts, nullptr, 0.0 );
	const CloudLines::Params p = cloudParams();
	std::vector<CloudLines::Seg> segs = CloudLines::batch( pts, p );
	// vynechat úseky, které už jsou nakreslené (oba konce u rovnoběžné čáry)
	std::vector<Segment> drawn;
	for ( const Segment& sg : collectSegments() )
	{
		if ( !sg.ref && !sg.arc && sg.poly && !isText( sg.poly ) )
		{
			drawn.push_back( sg );
		}
	}
	const double nearTol = 2.0 * p.tol + 0.01 * m_scale;
	for ( const CloudLines::Seg& sg : segs )
	{
		bool dup = false;
		for ( const Segment& d : drawn )
		{
			if ( dist( closestOnSegment( d.a, d.b, sg.a ), sg.a ) < nearTol && dist( closestOnSegment( d.a, d.b, sg.b ), sg.b ) < nearTol )
			{
				dup = true;
				break;
			}
		}
		if ( !dup )
		{
			m_clBatch.push_back( sg );
		}
	}
	QApplication::restoreOverrideCursor();
	m_flash = m_clBatch.empty() ? QStringLiteral( "Hromadně: v plátku nejsou žádné nové přímé úseky" ) : batchPrompt();
}

int DrawTool::batchPhase() const
{
	int ph = 3;
	for ( const CloudLines::Seg& sg : m_clBatch )
	{
		ph = std::min( ph, sg.cls );
	}
	return ph;
}

QString DrawTool::batchPrompt() const
{
	static const char* names[] = { "jisté – uzavřené místnosti", "navazující čáry s volným koncem", "nejisté – zkontrolujte" };
	const int ph = std::max( 0, std::min( 2, batchPhase() ) );
	int n = 0;
	for ( const CloudLines::Seg& sg : m_clBatch )
	{
		n += sg.cls == ph ? 1 : 0;
	}
	return QStringLiteral( "Hromadně %1/3 (%2): %3 čar – Enter nebo klik = potvrdit, pravé = přeskočit, Esc = konec" )
	    .arg( ph + 1 )
	    .arg( QString::fromUtf8( names[ph] ) )
	    .arg( n );
}

void DrawTool::skipBatchPhase()
{
	const int ph = batchPhase();
	m_clBatch.erase( std::remove_if( m_clBatch.begin(), m_clBatch.end(), [ph]( const CloudLines::Seg& sg ) { return sg.cls == ph; } ), m_clBatch.end() );
	const bool room = m_mode == Mode::CloudRoom;
	m_flash = m_clBatch.empty() ? ( room ? QStringLiteral( "Místnost: hotovo" ) : QStringLiteral( "Hromadně: hotovo" ) ) : ( room ? roomPrompt() : batchPrompt() );
}

void DrawTool::acceptBatch()
{
	if ( m_clBatch.empty() )
	{
		return;
	}
	// po fázích podle jistoty: přidat jen aktuální fázi (místnost má vše ve fázi 0), zbytek nechat k dalšímu potvrzení
	const int ph = batchPhase();
	UndoItem u;
	int n = 0;
	for ( const CloudLines::Seg& sg : m_clBatch )
	{
		if ( sg.cls == ph && dist( sg.a, sg.b ) > 1e-9 )
		{
			addLine( sg.a, sg.b, m_category, m_hidden, &u );
			++n;
		}
	}
	pushUndo( std::move( u ) );
	m_clBatch.erase( std::remove_if( m_clBatch.begin(), m_clBatch.end(), [ph]( const CloudLines::Seg& sg ) { return sg.cls == ph; } ), m_clBatch.end() );
	m_flash = m_clBatch.empty() ? QStringLiteral( "Přidáno čar: %1" ).arg( n )
	                            : QStringLiteral( "Přidáno čar: %1. " ).arg( n ) + ( m_mode == Mode::CloudRoom ? roomPrompt() : batchPrompt() );
}

// ---------------------------------------------------------------- režimy

bool DrawTool::previewGeometry( CCVector2d& a, CCVector2d& b ) const
{
	if ( m_mode == Mode::SectionDef )
	{
		if ( m_step != 2 )
		{
			return false;
		}
		a = m_base;
		b = m_secB;
		return true;
	}
	CCVector2d sa;
	CCVector2d sb;
	if ( !m_hasCursor || !lineEnds( m_sel, sa, sb ) )
	{
		return false;
	}

	switch ( m_mode )
	{
	case Mode::Move:
	case Mode::Copy:
	{
		if ( m_step != 2 || isArc( m_sel ) )
		{
			return false;
		}
		const CCVector2d d = m_cursor - m_base;
		a = sa + d;
		b = sb + d;
		return true;
	}
	case Mode::Offset:
	{
		CCVector2d u = sb - sa;
		if ( u.norm() < 1e-9 )
		{
			return false;
		}
		u.normalize();
		const CCVector2d n( -u.y, u.x );
		const double side = ( m_cursor - sa ).dot( n );
		const double d = m_offsetDist > 0.0 ? m_offsetDist * m_scale : std::abs( side );
		const CCVector2d off = n * ( side >= 0 ? d : -d );
		a = sa + off;
		b = sb + off;
		return true;
	}
	case Mode::Break:
	{
		if ( m_step != 1 )
		{
			return false;
		}
		a = m_base;
		b = closestOnSegment( sa, sb, m_cursor );
		return true;
	}
	case Mode::Extend:
	{
		const CCVector2d fixed = ( m_selEnd == 0 ) ? sb : sa;
		const CCVector2d moving = ( m_selEnd == 0 ) ? sa : sb;
		CCVector2d u = moving - fixed;
		if ( u.norm() < 1e-9 )
		{
			return false;
		}
		u.normalize();
		const double L = ( m_cursor - fixed ).dot( u );
		if ( L <= 1e-6 )
		{
			return false;
		}
		const CCVector2d moved = fixed + u * L;
		a = ( m_selEnd == 0 ) ? moved : sa;
		b = ( m_selEnd == 0 ) ? sb : moved;
		return true;
	}
	default:
		return false;
	}
}

void DrawTool::onDataPoint( const QPoint& pos )
{
	SnapType st;
	switch ( m_mode )
	{
	case Mode::Line:
	{
		const CCVector2d P = cursorPoint( pos, st, m_chain.empty() ? nullptr : &m_chain.back() );
		if ( !m_chain.empty() )
		{
			UndoItem u;
			addLine( m_chain.back(), P, m_category, m_hidden, &u );
			pushUndo( std::move( u ) );
		}
		m_chain.push_back( P );
		m_perpLock = false; // kolmice platí jen pro jeden úsek
		break;
	}
	case Mode::Arc:
	{
		const CCVector2d P = cursorPoint( pos, st, m_arcPts.empty() ? nullptr : &m_arcPts.back() );
		if ( m_arcPts.size() < 2 )
		{
			if ( m_arcPts.empty() || dist( P, m_arcPts.back() ) > 1e-6 )
			{
				m_arcPts.push_back( P );
				if ( m_arcPts.size() == 2 && m_arcMethod == 2 )
				{
					// sledování směru oblouku pohybem myši od začátku
					m_arcSweep = 0.0;
					m_arcPrevAng = std::atan2( m_arcPts[0].y - P.y, m_arcPts[0].x - P.x );
				}
			}
		}
		else
		{
			m_cursor = P;
			std::vector<CCVector2d> pts;
			if ( arcPreview( pts ) )
			{
				UndoItem u;
				addPolyline( pts, m_category, m_hidden, true, &u );
				pushUndo( std::move( u ) );
			}
			m_arcPts.clear();
			m_arcSweep = 0.0;
		}
		break;
	}
	case Mode::CloudLine:
	{
		CCVector2d click;
		if ( !screenToLocal( pos, click ) )
		{
			break;
		}
		if ( m_clHas )
		{
			// klik na náhled = jen potvrdit; jinde = potvrdit a hledat další čáru
			const bool onPreview = dist( closestOnSegment( m_clA, m_clB, click ), click ) < 10.0 * pixelSize();
			acceptCloudLine();
			if ( onPreview )
			{
				break;
			}
		}
		detectCloudLine( click );
		break;
	}
	case Mode::CloudRoom:
	{
		CCVector2d click;
		if ( !screenToLocal( pos, click ) )
		{
			break;
		}
		if ( !m_clBatch.empty() )
		{
			// klik ve stejné místnosti = potvrdit aktuální fázi; jinde = potvrdit fázi, zbytek zahodit a další místnost
			const bool same = dist( click, m_clRoomClick ) < 0.5 * m_scale;
			acceptBatch();
			if ( same )
			{
				break;
			}
			m_clBatch.clear();
		}
		detectRoom( click );
		break;
	}
	case Mode::CloudBatch:
	{
		if ( !m_clBatch.empty() )
		{
			acceptBatch();
		}
		else
		{
			detectBatch();
		}
		break;
	}
	case Mode::Delete:
	{
		if ( ccPolyline* p = pickLine( pos ) )
		{
			UndoItem u;
			removeLine( p, &u );
			pushUndo( std::move( u ) );
		}
		break;
	}
	case Mode::RotateView:
	{
		// 2 body: směr 1. → 2. bod bude na obrazovce vodorovně doprava
		if ( m_step == 0 )
		{
			m_base = cursorPoint( pos, st, nullptr );
			m_step = 1;
		}
		else
		{
			const CCVector2d B = cursorPoint( pos, st, nullptr );
			if ( ( B - m_base ).norm() > 1e-6 )
			{
				const CCVector3d A3 = m_frame.to3D( m_base, false );
				const CCVector3d B3 = m_frame.to3D( B, false );
				Q_EMIT rotateViewRequested( std::atan2( B3.y - A3.y, B3.x - A3.x ) );
				const Mode back = m_prevMode;
				m_mode = back;
				Q_EMIT modeChanged( m_mode );
				resetTool();
				return;
			}
		}
		break;
	}
	case Mode::Measure:
	{
		const CCVector2d P = cursorPoint( pos, st, ( m_step == 1 ) ? &m_measA : nullptr );
		if ( m_step == 0 || m_measDone )
		{
			m_measA = P;
			m_measDone = false;
			m_step = 1;
		}
		else
		{
			m_measB = P;
			m_measDone = true;
			m_step = 0;
		}
		break;
	}
	case Mode::SectionDef:
	{
		if ( m_step == 0 )
		{
			m_base = cursorPoint( pos, st, nullptr );
			m_step = 1;
		}
		else if ( m_step == 1 )
		{
			m_secB = cursorPoint( pos, st, &m_base );
			if ( dist( m_secB, m_base ) > 1e-3 )
			{
				m_step = 2;
			}
		}
		else
		{
			CCVector2d C;
			if ( screenToLocal( pos, C ) )
			{
				const CCVector3d A3 = m_frame.to3D( m_base, false );
				const CCVector3d B3 = m_frame.to3D( m_secB, false );
				const CCVector3d C3 = m_frame.to3D( C, false );
				Q_EMIT sectionDefined( m_defKind, A3.x, A3.y, B3.x, B3.y, C3.x, C3.y );
			}
			m_mode = m_prevMode;
			Q_EMIT modeChanged( m_mode );
			resetTool();
			return;
		}
		break;
	}
	case Mode::Move:
	case Mode::Copy:
	{
		if ( m_step == 0 )
		{
			if ( ccPolyline* p = pickLine( pos ) )
			{
				select( p );
				m_step = 1;
			}
		}
		else if ( m_step == 1 )
		{
			m_base = cursorPoint( pos, st, nullptr );
			m_step = 2;
		}
		else if ( !m_selSet.empty() )
		{
			// přesun / kopie celého výběru
			m_cursor = cursorPoint( pos, st, &m_base );
			const CCVector2d d = m_cursor - m_base;
			UndoItem u;
			std::vector<ccPolyline*> created;
			for ( ccPolyline* p : m_selSet )
			{
				translateItem( p, d, m_mode == Mode::Copy, &u );
			}
			pushUndo( std::move( u ) );
			if ( m_mode == Mode::Move )
			{
				m_step = 1; // výběr zůstává, další přesun od nového výchozího bodu
			}
		}
		else
		{
			m_cursor = cursorPoint( pos, st, &m_base );
			if ( lineExists( m_sel ) )
			{
				UndoItem u;
				translateItem( m_sel, m_cursor - m_base, m_mode == Mode::Copy, &u );
				pushUndo( std::move( u ) );
				if ( m_mode == Mode::Move )
				{
					select( nullptr );
					m_step = 0;
				}
			}
		}
		break;
	}
	case Mode::Select:
	case Mode::Modify:
	{
		// úprava prvku / úchop: 1. klik vybere konec, 2. klik ho položí
		if ( m_step == 0 )
		{
			if ( m_mode == Mode::Select )
			{
				break; // výběr obsluhuje eventFilter
			}
			CCVector2d click;
			ccPolyline* p = pickLine( pos, &click );
			if ( p && !rejectArc( p ) )
			{
				CCVector2d a;
				CCVector2d b;
				lineEnds( p, a, b );
				m_selEnd = ( dist( click, a ) <= dist( click, b ) ) ? 0 : 1;
				m_fixed = ( m_selEnd == 0 ) ? b : a;
				select( p );
				m_step = 1;
			}
		}
		else
		{
			const CCVector2d P = cursorPoint( pos, st, &m_fixed );
			CCVector2d a;
			CCVector2d b;
			if ( lineEnds( m_sel, a, b ) )
			{
				UndoItem u;
				if ( isText( m_sel ) )
				{
					setLine( m_sel, P, P, &u );
				}
				else
				{
					setLine( m_sel, m_selEnd == 0 ? P : a, m_selEnd == 0 ? b : P, &u );
				}
				pushUndo( std::move( u ) );
			}
			select( nullptr );
			m_step = 0;
		}
		break;
	}
	case Mode::InsertVertex:
	{
		if ( m_step == 0 )
		{
			CCVector2d click;
			if ( ccPolyline* p = pickLine( pos, &click ) )
			{
				CCVector2d a;
				CCVector2d b;
				if ( !isText( p ) && !rejectArc( p ) && lineEnds( p, a, b ) && dist( a, b ) > 1e-9 )
				{
					select( p );
					m_fixed = a;
					m_far2 = b;
					m_step = 1;
				}
			}
		}
		else
		{
			const CCVector2d P = cursorPoint( pos, st, nullptr );
			if ( lineExists( m_sel ) && dist( P, m_fixed ) > 1e-9 && dist( P, m_far2 ) > 1e-9 )
			{
				int c0;
				bool h0;
				lineAttrs( m_sel, c0, h0 );
				ccPolyline* line = m_sel;
				select( nullptr );
				UndoItem u;
				setLine( line, m_fixed, P, &u );
				addLine( P, m_far2, c0, h0, &u );
				pushUndo( std::move( u ) );
			}
			select( nullptr );
			m_step = 0;
		}
		break;
	}
	case Mode::DeleteVertex:
	{
		// spoj dvou čar u kliknutí → jedna čára
		CCVector2d P;
		if ( !screenToLocal( pos, P ) )
		{
			break;
		}
		const double tol = SnapPixels * pixelSize();
		struct EndRef
		{
			ccPolyline* poly;
			CCVector2d at;
			CCVector2d other;
		};
		std::vector<EndRef> ends;
		for ( const Segment& sg : collectSegments() )
		{
			if ( sg.ref || sg.arc || isText( sg.poly ) )
			{
				continue;
			}
			if ( dist( sg.a, P ) < tol )
				ends.push_back( { sg.poly, sg.a, sg.b } );
			else if ( dist( sg.b, P ) < tol )
				ends.push_back( { sg.poly, sg.b, sg.a } );
		}
		if ( ends.empty() )
		{
			break;
		}
		// nejbližší vrchol a čáry, které v něm končí
		std::sort( ends.begin(), ends.end(), [&P]( const EndRef& x, const EndRef& y ) { return dist( x.at, P ) < dist( y.at, P ); } );
		const CCVector2d V = ends.front().at;
		std::vector<EndRef> atV;
		for ( const EndRef& e : ends )
		{
			if ( dist( e.at, V ) < 1e-3 * m_scale )
			{
				atV.push_back( e );
			}
		}
		if ( atV.size() != 2 )
		{
			Q_EMIT statusChanged( QStringLiteral( "Smazat vrchol: ve vrcholu se musí potkat právě dvě čáry (je jich %1)" ).arg( atV.size() ) );
			return;
		}
		UndoItem u;
		setLine( atV[0].poly, atV[0].other, atV[1].other, &u );
		removeLine( atV[1].poly, &u );
		pushUndo( std::move( u ) );
		break;
	}
	case Mode::BreakLine:
	{
		const CCVector2d P = cursorPoint( pos, st, m_step == 1 ? &m_base : nullptr );
		if ( m_step == 0 )
		{
			m_base = P;
			m_step = 1;
		}
		else if ( dist( P, m_base ) > 1e-9 )
		{
			const auto hits = breakLineHits( m_base, P );
			UndoItem u;
			for ( const auto& h : hits )
			{
				CCVector2d a;
				CCVector2d b;
				if ( !lineEnds( h.first, a, b ) )
				{
					continue;
				}
				const int cat = h.first->getMetaData( Drawing::MetaCategory ).toInt();
				const bool hid = h.first->getMetaData( Drawing::MetaHidden ).toBool();
				setLine( h.first, a, h.second, &u );
				addLine( h.second, b, cat, hid, &u );
			}
			if ( !u.empty() )
			{
				pushUndo( std::move( u ) );
			}
			m_flash = QStringLiteral( "Rozděleno čar: %1" ).arg( hits.size() );
			m_step = 0;
		}
		break;
	}
	case Mode::Pin:
	{
		CCVector2d P = cursorPoint( pos, st, nullptr );
		const CCVector3d P3 = m_frame.to3D( P, false );
		Q_EMIT show3DRequested( P3.x, P3.y, m_frame.plan ? std::numeric_limits<double>::quiet_NaN() : P3.z );
		break;
	}
	case Mode::Kota:
	{
		if ( m_frame.plan )
		{
			m_flash = QStringLiteral( "Kóta jde jen v řezu nebo pohledu" );
			break;
		}
		if ( m_step == 0 )
		{
			m_kotaTip = cursorPoint( pos, st, nullptr );
			m_step = 1;
		}
		else
		{
			const CCVector2d P = cursorPoint( pos, st, nullptr );
			UndoItem u;
			addKota( m_kotaTip, P.x < m_kotaTip.x ? -1 : 1, m_kotaFmt, m_kotaScale, &u );
			pushUndo( std::move( u ) );
			m_step = 0;
		}
		break;
	}
	case Mode::Text:
	{
		const CCVector2d P = cursorPoint( pos, st, nullptr );
		bool ok = false;
		const QString txt = QInputDialog::getText( qobject_cast<QWidget*>( m_winObject ), QStringLiteral( "Vložit text" ), QStringLiteral( "Text:" ), QLineEdit::Normal, m_lastText, &ok );
		if ( ok && !txt.trimmed().isEmpty() )
		{
			m_lastText = txt;
			UndoItem u;
			addText( P, txt, &u );
			pushUndo( std::move( u ) );
		}
		keepCrossCursor();
		break;
	}
	case Mode::Rotate:
	{
		if ( m_step == 0 )
		{
			if ( ccPolyline* p = pickLine( pos ) )
			{
				select( p );
				m_step = 1;
			}
		}
		else if ( m_step == 1 )
		{
			m_base = cursorPoint( pos, st, nullptr ); // střed otáčení
			m_step = 2;
		}
		else if ( m_step == 2 )
		{
			m_rotRef = cursorPoint( pos, st, &m_base ); // výchozí směr
			if ( dist( m_rotRef, m_base ) > 1e-9 )
			{
				m_step = 3;
			}
		}
		else
		{
			m_cursor = cursorPoint( pos, st, &m_base ); // cílový směr
			const double ang = rotAngle();
			const std::vector<ccPolyline*> targets = rotTargets();
			UndoItem u;
			for ( ccPolyline* p : targets )
			{
				rotateItem( p, m_base, ang, &u );
			}
			pushUndo( std::move( u ) );
			if ( !m_selSet.empty() )
			{
				pruneSelection();
				m_step = m_selSet.empty() ? 0 : 1;
			}
			else
			{
				select( nullptr );
				m_step = 0;
			}
		}
		break;
	}
	case Mode::PlaceText:
	{
		const CCVector2d P = cursorPoint( pos, st, nullptr );
		if ( !m_placeText.isEmpty() )
		{
			addTextAt( placeOrigin( P ), m_placeText, m_placeAng, m_placeLevel );
			m_placeText.clear();
		}
		const Mode back = ( m_prevMode == Mode::PlaceText ) ? Mode::Select : m_prevMode;
		QTimer::singleShot( 0, this, [this, back]() { setMode( back ); } );
		break;
	}
	case Mode::EditText:
	{
		ccPolyline* p = pickLine( pos );
		if ( !p || !isText( p ) || isKota( p ) )
		{
			m_flash = QStringLiteral( "Klikněte na text" );
			break;
		}
		select( p );
		redraw2D();
		bool ok = false;
		const QString old = p->getMetaData( "qRezy.text" ).toString();
		const QString txt = QInputDialog::getText( qobject_cast<QWidget*>( m_winObject ), QStringLiteral( "Upravit text" ), QStringLiteral( "Text:" ), QLineEdit::Normal, old, &ok );
		select( nullptr );
		if ( ok && !txt.trimmed().isEmpty() && txt != old )
		{
			const CCVector2d P = m_frame.toUV( CCVector3d::fromArray( p->getPoint( 0 )->u ) );
			UndoItem u;
			ccPolyline* n = addText( P, txt, &u );
			if ( n )
			{
				for ( const char* key : { "qRezy.textLevel", "qRezy.textH", "qRezy.imported", "qRezy.textAng" } )
				{
					if ( p->hasMetaData( key ) )
					{
						n->setMetaData( key, p->getMetaData( key ) );
					}
				}
			}
			removeLine( p, &u );
			pushUndo( std::move( u ) );
		}
		keepCrossCursor();
		break;
	}
	case Mode::Offset:
	{
		if ( m_step == 0 )
		{
			ccPolyline* p = pickLine( pos );
			if ( p && !rejectArc( p ) )
			{
				select( p );
				m_step = 1;
			}
		}
		else
		{
			m_cursor = cursorPoint( pos, st, nullptr );
			CCVector2d a;
			CCVector2d b;
			if ( previewGeometry( a, b ) )
			{
				const int cat = m_sel->getMetaData( Drawing::MetaCategory ).toInt();
				const bool hid = m_sel->getMetaData( Drawing::MetaHidden ).toBool();
				UndoItem u;
				addLine( a, b, cat, hid, &u );
				pushUndo( std::move( u ) );
			}
		}
		break;
	}
	case Mode::Extend:
	{
		if ( m_step == 0 )
		{
			CCVector2d click;
			ccPolyline* p = pickLine( pos, &click );
			if ( p && !rejectArc( p ) )
			{
				CCVector2d a;
				CCVector2d b;
				lineEnds( p, a, b );
				m_selEnd = ( dist( click, a ) < dist( click, b ) ) ? 0 : 1;
				select( p );
				m_step = 1;
			}
		}
		else
		{
			m_cursor = cursorPoint( pos, st, nullptr );
			CCVector2d a;
			CCVector2d b;
			if ( previewGeometry( a, b ) )
			{
				UndoItem u;
				setLine( m_sel, a, b, &u );
				pushUndo( std::move( u ) );
			}
			select( nullptr );
			m_step = 0;
		}
		break;
	}
	case Mode::Corner:
	{
		CCVector2d click;
		ccPolyline* p = pickLine( pos, &click );
		if ( !p || rejectArc( p ) )
		{
			return;
		}
		if ( m_step == 0 )
		{
			select( p );
			m_selClick = click;
			m_step = 1;
		}
		else if ( p != m_sel )
		{
			CCVector2d a1;
			CCVector2d b1;
			CCVector2d a2;
			CCVector2d b2;
			CCVector2d I;
			if ( lineEnds( m_sel, a1, b1 ) && lineEnds( p, a2, b2 ) && lineIntersection( a1, b1, a2, b2, I ) )
			{
				// zachová se ta část čáry, na kterou se kliklo
				auto fix = [&]( const CCVector2d& a, const CCVector2d& b, const CCVector2d& c, CCVector2d& na, CCVector2d& nb )
				{
					const double ti = paramOn( a, b, I );
					const double tc = paramOn( a, b, c );
					if ( tc < ti )
					{
						na = a;
						nb = I;
					}
					else
					{
						na = I;
						nb = b;
					}
				};
				CCVector2d n1a;
				CCVector2d n1b;
				CCVector2d n2a;
				CCVector2d n2b;
				fix( a1, b1, m_selClick, n1a, n1b );
				fix( a2, b2, click, n2a, n2b );
				ccPolyline* first = m_sel;
				select( nullptr );
				UndoItem u;
				setLine( first, n1a, n1b, &u );
				setLine( p, n2a, n2b, &u );
				pushUndo( std::move( u ) );
			}
			else
			{
				select( nullptr );
			}
			m_step = 0;
		}
		break;
	}
	case Mode::Break:
	{
		// rozdělit: 2 body na čáře → část mezi nimi se smaže; dvakrát stejný bod = jen rozdělit
		if ( m_step == 0 )
		{
			ccPolyline* p = pickLine( pos );
			if ( p && !rejectArc( p ) )
			{
				CCVector2d a;
				CCVector2d b;
				lineEnds( p, a, b );
				select( p );
				m_base = closestOnSegment( a, b, cursorPoint( pos, st, nullptr ) );
				m_step = 1;
			}
		}
		else
		{
			CCVector2d a;
			CCVector2d b;
			if ( lineEnds( m_sel, a, b ) )
			{
				const CCVector2d P2 = closestOnSegment( a, b, cursorPoint( pos, st, nullptr ) );
				const int cat = m_sel->getMetaData( Drawing::MetaCategory ).toInt();
				const bool hid = m_sel->getMetaData( Drawing::MetaHidden ).toBool();
				const double minLen = 2.0 * pixelSize();
				ccPolyline* line = m_sel;
				select( nullptr );
				UndoItem u;
				if ( dist( P2, m_base ) < minLen )
				{
					if ( dist( m_base, a ) > minLen && dist( m_base, b ) > minLen )
					{
						setLine( line, a, m_base, &u );
						addLine( m_base, b, cat, hid, &u );
					}
				}
				else
				{
					double t1 = paramOn( a, b, m_base );
					double t2 = paramOn( a, b, P2 );
					if ( t1 > t2 )
					{
						std::swap( t1, t2 );
					}
					const CCVector2d C1 = a + ( b - a ) * t1;
					const CCVector2d C2 = a + ( b - a ) * t2;
					const bool keep1 = dist( a, C1 ) > minLen;
					const bool keep2 = dist( C2, b ) > minLen;
					if ( keep1 )
					{
						setLine( line, a, C1, &u );
						if ( keep2 )
						{
							addLine( C2, b, cat, hid, &u );
						}
					}
					else if ( keep2 )
					{
						setLine( line, C2, b, &u );
					}
					else
					{
						removeLine( line, &u );
					}
				}
				if ( !u.empty() )
				{
					pushUndo( std::move( u ) );
				}
			}
			select( nullptr );
			m_step = 0;
		}
		break;
	}
	case Mode::Trim:
	{
		// oříznout/prodloužit k prvku: 1. čára = hranice, pak libovolně čar k oříznutí
		CCVector2d click;
		ccPolyline* p = pickLine( pos, &click );
		if ( !p || rejectArc( p ) )
		{
			return;
		}
		if ( m_step == 0 )
		{
			select( p );
			m_step = 1;
		}
		else if ( p != m_sel )
		{
			CCVector2d ba;
			CCVector2d bb;
			CCVector2d a;
			CCVector2d b;
			CCVector2d I;
			if ( lineEnds( m_sel, ba, bb ) && lineEnds( p, a, b ) && lineIntersection( a, b, ba, bb, I ) )
			{
				const double ti = paramOn( a, b, I );
				const double tc = paramOn( a, b, click );
				CCVector2d na = a;
				CCVector2d nb = b;
				if ( ti > 0.0 && ti < 1.0 )
				{
					// hranice čáru protíná → zůstane část, na kterou se kliklo
					if ( tc < ti )
						nb = I;
					else
						na = I;
				}
				else if ( ti >= 1.0 )
				{
					nb = I; // prodloužit konec b
				}
				else
				{
					na = I; // prodloužit konec a
				}
				if ( dist( na, nb ) > 1e-9 )
				{
					UndoItem u;
					setLine( p, na, nb, &u );
					pushUndo( std::move( u ) );
				}
			}
		}
		break;
	}
	}

	m_typed.clear();
	redraw3D();
	updateTip();
	updateStatus();
}

void DrawTool::onTypedValue( double v )
{
	if ( v <= 0.0 )
	{
		return;
	}
	const double L = v * m_scale;

	if ( ( m_mode == Mode::Modify || m_mode == Mode::Select ) && m_step == 1 && m_hasCursor && lineExists( m_sel ) && !isText( m_sel ) )
	{
		// nová délka čáry od pevného konce ve směru kurzoru
		CCVector2d dir = m_cursor - m_fixed;
		if ( dir.norm() < 1e-9 )
		{
			return;
		}
		dir.normalize();
		const CCVector2d P = m_fixed + dir * L;
		CCVector2d a;
		CCVector2d b;
		lineEnds( m_sel, a, b );
		UndoItem u;
		setLine( m_sel, m_selEnd == 0 ? P : a, m_selEnd == 0 ? b : P, &u );
		pushUndo( std::move( u ) );
		select( nullptr );
		m_step = 0;
		return;
	}
	switch ( m_mode )
	{
	case Mode::Line:
	{
		if ( !m_hasCursor )
		{
			return;
		}
		if ( m_cursorTracked )
		{
			// vzdálenost od získaného bodu po pomocné čáře
			CCVector2d dir = m_cursor - m_trackFrom;
			if ( dir.norm() < 1e-9 )
			{
				return;
			}
			dir.normalize();
			const CCVector2d P = m_trackFrom + dir * L;
			if ( !m_chain.empty() )
			{
				UndoItem u;
				addLine( m_chain.back(), P, m_category, m_hidden, &u );
				pushUndo( std::move( u ) );
			}
			m_chain.push_back( P );
			m_acquired.clear();
	m_acqDirs.clear();
			break;
		}
		if ( m_chain.empty() )
		{
			return;
		}
		const CCVector2d target = m_ortho ? applyOrtho( m_chain.back(), m_cursor ) : m_cursor;
		CCVector2d dir = target - m_chain.back();
		if ( m_perpLock )
		{
			dir = m_perpDir * ( ( m_cursor - m_chain.back() ).dot( m_perpDir ) >= 0 ? 1.0 : -1.0 );
		}
		if ( dir.norm() < 1e-9 )
		{
			return;
		}
		dir.normalize();
		const CCVector2d P = m_chain.back() + dir * L;
		UndoItem u;
		addLine( m_chain.back(), P, m_category, m_hidden, &u );
		pushUndo( std::move( u ) );
		m_chain.push_back( P );
		m_perpLock = false;
		break;
	}
	case Mode::Move:
	case Mode::Copy:
	{
		if ( m_step != 2 || !m_hasCursor )
		{
			return;
		}
		CCVector2d dir = m_cursor - m_base;
		if ( dir.norm() < 1e-9 )
		{
			return;
		}
		dir.normalize();
		const CCVector2d d = dir * L;
		UndoItem u;
		if ( !m_selSet.empty() )
		{
			for ( ccPolyline* p : m_selSet )
			{
				translateItem( p, d, m_mode == Mode::Copy, &u );
			}
			if ( m_mode == Mode::Move )
			{
				m_step = 1;
			}
		}
		else if ( lineExists( m_sel ) )
		{
			translateItem( m_sel, d, m_mode == Mode::Copy, &u );
			if ( m_mode == Mode::Move )
			{
				select( nullptr );
				m_step = 0;
			}
		}
		pushUndo( std::move( u ) );
		break;
	}
	case Mode::Offset:
	{
		if ( m_step != 1 )
		{
			return;
		}
		const double savedDist = m_offsetDist;
		m_offsetDist = v;
		CCVector2d a;
		CCVector2d b;
		if ( previewGeometry( a, b ) )
		{
			UndoItem u;
			addLine( a, b, m_sel->getMetaData( Drawing::MetaCategory ).toInt(), m_sel->getMetaData( Drawing::MetaHidden ).toBool(), &u );
			pushUndo( std::move( u ) );
		}
		m_offsetDist = savedDist;
		break;
	}
	case Mode::Extend:
	{
		// zadaná hodnota = nová celková délka čáry
		CCVector2d sa;
		CCVector2d sb;
		if ( m_step != 1 || !lineEnds( m_sel, sa, sb ) )
		{
			return;
		}
		const CCVector2d fixed = ( m_selEnd == 0 ) ? sb : sa;
		const CCVector2d moving = ( m_selEnd == 0 ) ? sa : sb;
		CCVector2d u = moving - fixed;
		if ( u.norm() < 1e-9 )
		{
			return;
		}
		u.normalize();
		const CCVector2d moved = fixed + u * L;
		UndoItem und;
		setLine( m_sel, m_selEnd == 0 ? moved : sa, m_selEnd == 0 ? sb : moved, &und );
		pushUndo( std::move( und ) );
		select( nullptr );
		m_step = 0;
		break;
	}
	default:
		return;
	}
	redraw3D();
}

// ---------------------------------------------------------------- zobrazení

void DrawTool::clearOverlayPool()
{
	for ( ccPolyline* p : m_overlayPool )
	{
		if ( m_win )
		{
			m_win->removeFromOwnDB( p );
		}
		delete p;
	}
	m_overlayPool.clear();
}

void DrawTool::setOverlays( const std::vector<Overlay>& ovs )
{
	if ( !m_win )
	{
		return;
	}
	static const unsigned Capacity = 64;
	while ( m_overlayPool.size() < ovs.size() )
	{
		ccPointCloud* verts = nullptr;
		ccPolyline* p = make2DPolyline( verts, Capacity, ccColor::Rgb( 255, 255, 255 ), 1 );
		p->setDisplay( m_win );
		m_win->addToOwnDB( p );
		m_overlayPool.push_back( p );
	}
	for ( size_t i = 0; i < m_overlayPool.size(); ++i )
	{
		ccPolyline* p = m_overlayPool[i];
		if ( i >= ovs.size() || ovs[i].pts.size() < 2 )
		{
			p->setEnabled( false );
			continue;
		}
		const Overlay& o = ovs[i];
		auto* verts = static_cast<ccPointCloud*>( p->getAssociatedCloud() );
		const unsigned n = static_cast<unsigned>( std::min<size_t>( o.pts.size(), Capacity ) );
		for ( unsigned k = 0; k < n; ++k )
		{
			setVertex( verts, k, v3( o.pts[k], 0 ) );
		}
		p->clear();
		p->addPointIndex( 0, n );
		p->setColor( o.color );
		p->setWidth( o.width );
		p->setEnabled( true );
	}
}

void DrawTool::addMarker( std::vector<Overlay>& ovs, const CCVector2d& P, SnapType t, const ccColor::Rgb& col ) const
{
	CCVector2d C;
	if ( !localToScreen( P, C ) )
	{
		return;
	}
	const double r = 6.0;
	Overlay o;
	o.color = col;
	o.width = 2.0f;
	const double x = C.x;
	const double y = C.y;
	switch ( t )
	{
	case SnapType::Mid: // trojúhelník
		o.pts = { { x - r, y - r }, { x + r, y - r }, { x, y + r }, { x - r, y - r } };
		break;
	case SnapType::Intersection: // křížek
		o.pts = { { x - r, y - r }, { x + r, y + r }, { x, y }, { x - r, y + r }, { x + r, y - r } };
		break;
	case SnapType::OnLine: // kosočtverec
		o.pts = { { x, y - r }, { x + r, y }, { x, y + r }, { x - r, y }, { x, y - r } };
		break;
	case SnapType::None: // malé plus = získaný bod trasování
		o.pts = { { x - r, y }, { x + r, y } };
		ovs.push_back( o );
		o.pts = { { x, y - r }, { x, y + r } };
		break;
	default: // čtverec = konec
		o.pts = { { x - r, y - r }, { x + r, y - r }, { x + r, y + r }, { x - r, y + r }, { x - r, y - r } };
		break;
	}
	ovs.push_back( o );
}

void DrawTool::addArrow( std::vector<Overlay>& ovs, const CCVector2d& from, const CCVector2d& dir, double lenPx, const ccColor::Rgb& col ) const
{
	CCVector2d A;
	CCVector2d B;
	CCVector2d d = dir;
	if ( d.norm() < 1e-12 || !localToScreen( from, A ) || !localToScreen( from + d, B ) )
	{
		return;
	}
	// směr v pixelech
	CCVector2d u = B - A;
	if ( u.norm() < 1e-9 )
	{
		return;
	}
	u.normalize();
	const CCVector2d T = A + u * lenPx;
	const CCVector2d n( -u.y, u.x );
	const double h = 10.0;
	Overlay o;
	o.color = col;
	o.width = 2.0f;
	o.pts = { A, T };
	ovs.push_back( o );
	o.pts = { T - u * h + n * ( h * 0.5 ), T, T - u * h - n * ( h * 0.5 ) };
	ovs.push_back( o );
}

void DrawTool::updateTip()
{
	if ( !m_win )
	{
		return;
	}
	std::vector<Overlay> ovs;
	const ccColor::Rgb white( 255, 255, 255 );
	const ccColor::Rgb yellow( 255, 220, 0 );
	const ccColor::Rgb cyan( 0, 230, 230 );
	const ccColor::Rgb track( 120, 255, 120 );

	auto segment = [&]( const CCVector2d& a, const CCVector2d& b, const ccColor::Rgb& col, float w )
	{
		CCVector2d A;
		CCVector2d B;
		if ( localToScreen( a, A ) && localToScreen( b, B ) )
		{
			Overlay o;
			o.pts = { A, B };
			o.color = col;
			o.width = w;
			ovs.push_back( o );
		}
	};

	// pomocné čáry trasování (přes celé okno) a získané body
	if ( m_trackingOn )
	{
		const double farLen = 2000.0 * pixelSize();
		for ( const auto& tl : m_trackLines )
		{
			// čárkovaně: řada krátkých úseků
			const double step = 12.0 * pixelSize();
			for ( double t = -farLen; t < farLen; t += 2 * step )
			{
				segment( tl.first + tl.second * t, tl.first + tl.second * ( t + step ), track, 1.0f );
			}
		}
		for ( const CCVector2d& A : m_acquired )
		{
			addMarker( ovs, A, SnapType::None, track );
		}
	}
	// kolmice: uchopený bod a jeho pata na kolmici
	if ( m_perpRefValid && m_hasCursor && m_mode == Mode::Line && m_perpLock )
	{
		addMarker( ovs, m_perpRef, m_perpRefType, ccColor::Rgb( 255, 220, 0 ) );
		segment( m_perpRef, m_cursor, ccColor::Rgb( 255, 220, 0 ), 1.0f );
	}

	if ( m_hasCursor )
	{
		// gumová úsečka od posledního bodu ke kurzoru
		const CCVector2d* from = nullptr;
		ccColor::Rgb col = white;
		if ( m_mode == Mode::Line && !m_chain.empty() )
		{
			from = &m_chain.back();
			col = toRgb( Drawing::displayColor( m_category, m_hidden ) );
		}
		else if ( ( ( m_mode == Mode::Move || m_mode == Mode::Copy ) && m_step == 2 ) || ( ( m_mode == Mode::SectionDef || m_mode == Mode::RotateView ) && m_step == 1 ) )
		{
			from = &m_base;
			if ( m_mode == Mode::SectionDef )
				col = cyan;
		}
		else if ( m_mode == Mode::Measure && m_step == 1 && !m_measDone )
		{
			from = &m_measA;
			col = yellow;
		}
		if ( from )
		{
			segment( *from, m_cursor, col, m_mode == Mode::Line ? 1.0f : 2.0f );
		}
		// úprava prvku / úchop: čára od pevného konce ke kurzoru
		if ( ( m_mode == Mode::Modify || m_mode == Mode::Select ) && m_step == 1 )
		{
			segment( m_fixed, m_cursor, yellow, 2.0f );
		}
		// vložit vrchol: obě části ke kurzoru
		if ( m_mode == Mode::InsertVertex && m_step == 1 )
		{
			segment( m_fixed, m_cursor, yellow, 2.0f );
			segment( m_cursor, m_far2, yellow, 2.0f );
		}
		// polyline (oblouk) jako řada úseků
		auto polyline = [&]( const std::vector<CCVector2d>& pts, const CCVector2d& d, const ccColor::Rgb& col, float w )
		{
			Overlay o;
			o.color = col;
			o.width = w;
			for ( const CCVector2d& P : pts )
			{
				CCVector2d S;
				if ( localToScreen( P + d, S ) )
				{
					o.pts.push_back( S );
				}
			}
			if ( o.pts.size() >= 2 )
			{
				ovs.push_back( o );
			}
		};
		// oblouk: pomocné čáry a náhled
		if ( m_mode == Mode::Arc && !m_arcPts.empty() )
		{
			const ccColor::Rgb lc = toRgb( Drawing::displayColor( m_category, m_hidden ) );
			if ( m_arcPts.size() == 1 )
			{
				segment( m_arcPts[0], m_cursor, white, 1.0f );
			}
			else
			{
				if ( m_arcMethod == 2 )
				{
					segment( m_arcPts[1], m_arcPts[0], white, 1.0f ); // poloměr
					segment( m_arcPts[1], m_cursor, white, 1.0f );
				}
				else if ( m_arcMethod == 1 )
				{
					segment( m_arcPts[0], m_arcPts[1], white, 1.0f ); // tětiva
				}
				std::vector<CCVector2d> pts;
				if ( arcPreview( pts ) )
				{
					polyline( pts, CCVector2d( 0, 0 ), lc, 2.0f );
				}
			}
		}
		// rozdělit čarou: řezná čára + průsečíky
		if ( m_mode == Mode::BreakLine && m_step == 1 )
		{
			const ccColor::Rgb red( 255, 60, 60 );
			segment( m_base, m_cursor, red, 1.5f );
			const double r = 4.0 * pixelSize();
			for ( const auto& h : breakLineHits( m_base, m_cursor ) )
			{
				const CCVector2d& I = h.second;
				segment( I + CCVector2d( -r, -r ), I + CCVector2d( r, r ), red, 2.0f );
				segment( I + CCVector2d( -r, r ), I + CCVector2d( r, -r ), red, 2.0f );
			}
		}
		// kóta: náhled značky (hrot na kurzoru, nebo pevný hrot a strana podle kurzoru)
		if ( m_mode == Mode::Kota && !m_frame.plan )
		{
			const CCVector2d T = m_step == 1 ? m_kotaTip : m_cursor;
			const int side = ( m_step == 1 && m_cursor.x < m_kotaTip.x ) ? -1 : 1;
			const KotaGeom g = kotaGeom( m_frame.to3D( T, false ), m_frame.U, m_frame.V, side, m_kotaFmt, m_kotaScale );
			for ( size_t k = 0; k + 1 < g.segs.size(); k += 2 )
			{
				segment( m_frame.toUV( g.segs[k] ), m_frame.toUV( g.segs[k + 1] ), cyan, 1.5f );
			}
		}
		// přesun / kopie jednoho oblouku
		if ( ( m_mode == Mode::Move || m_mode == Mode::Copy ) && m_step == 2 && m_selSet.empty() && isArc( m_sel ) )
		{
			polyline( polyPoints( m_sel ), m_cursor - m_base, yellow, 2.0f );
		}
		// přesun / kopie výběru
		if ( ( m_mode == Mode::Move || m_mode == Mode::Copy ) && m_step == 2 && !m_selSet.empty() )
		{
			const CCVector2d d = m_cursor - m_base;
			for ( ccPolyline* p : m_selSet )
			{
				CCVector2d a;
				CCVector2d b;
				if ( isArc( p ) )
				{
					polyline( polyPoints( p ), d, yellow, 2.0f );
					continue;
				}
				if ( lineEnds( p, a, b ) )
				{
					if ( dist( a, b ) < 1e-9 )
					{
						addMarker( ovs, a + d, SnapType::End, yellow );
					}
					else
					{
						segment( a + d, b + d, yellow, 2.0f );
					}
				}
			}
		}
	}

	// výběr: úchopy na koncích vybraných čar
	if ( m_mode == Mode::Select || ( ( m_mode == Mode::Move || m_mode == Mode::Copy ) && m_step < 2 ) )
	{
		for ( ccPolyline* p : m_selSet )
		{
			CCVector2d a;
			CCVector2d b;
			if ( !isArc( p ) && lineEnds( p, a, b ) )
			{
				for ( const CCVector2d& E : { a, b } )
				{
					CCVector2d S;
					if ( localToScreen( E, S ) )
					{
						const double h = 3.5 * m_win->getDevicePixelRatio();
						Overlay o;
						o.pts = { S + CCVector2d( -h, -h ), S + CCVector2d( h, -h ), S + CCVector2d( h, h ), S + CCVector2d( -h, h ), S + CCVector2d( -h, -h ) };
						o.color = ccColor::Rgb( 0, 160, 255 );
						o.width = 2.0f;
						ovs.push_back( o );
					}
				}
			}
		}
	}

	// okno výběru: zleva doprava modré (uvnitř), zprava doleva zelené čárkované (křížem)
	if ( m_mode == Mode::Select && m_winDragging )
	{
		const QPointF g0 = m_win->toCornerGLCoordinates( m_winStart.x(), m_winStart.y() );
		const QPointF g1 = m_win->toCornerGLCoordinates( m_mousePos.x(), m_mousePos.y() );
		ccGLCameraParameters camera;
		m_win->getGLCameraParameters( camera );
		const double cx = camera.viewport[2] / 2.0;
		const double cy = camera.viewport[3] / 2.0;
		const CCVector2d c[4] = { CCVector2d( g0.x() - cx, g0.y() - cy ), CCVector2d( g1.x() - cx, g0.y() - cy ), CCVector2d( g1.x() - cx, g1.y() - cy ), CCVector2d( g0.x() - cx, g1.y() - cy ) };
		const bool inside = m_mousePos.x() >= m_winStart.x();
		const ccColor::Rgb wc = inside ? ccColor::Rgb( 70, 140, 255 ) : ccColor::Rgb( 60, 220, 90 );
		for ( int i = 0; i < 4; ++i )
		{
			const CCVector2d A = c[i];
			const CCVector2d B = c[( i + 1 ) % 4];
			if ( inside )
			{
				Overlay o;
				o.pts = { A, B };
				o.color = wc;
				o.width = 1.5f;
				ovs.push_back( o );
			}
			else
			{
				const double L = dist( A, B );
				const CCVector2d u = L > 0 ? ( B - A ) / L : CCVector2d( 0, 0 );
				for ( double t = 0; t < L; t += 12.0 )
				{
					Overlay o;
					o.pts = { A + u * t, A + u * std::min( L, t + 6.0 ) };
					o.color = wc;
					o.width = 1.5f;
					ovs.push_back( o );
				}
			}
		}
	}

	// náhled upravované čáry
	CCVector2d a;
	CCVector2d b;
	if ( m_mode != Mode::SectionDef && previewGeometry( a, b ) )
	{
		segment( a, b, yellow, 2.0f );
	}

	// definice řezu: čára řezu a šipky na stranu, kam se díváme
	if ( m_mode == Mode::SectionDef && m_step == 2 )
	{
		segment( m_base, m_secB, cyan, 2.0f );
		CCVector2d d = m_secB - m_base;
		if ( d.norm() > 1e-9 && m_hasCursor )
		{
			d.normalize();
			CCVector2d n( -d.y, d.x );
			if ( ( m_cursor - m_base ).dot( n ) < 0 )
			{
				n = n * -1.0;
			}
			addArrow( ovs, m_base, n, 40.0, cyan );
			addArrow( ovs, m_secB, n, 40.0, cyan );
		}
	}

	// čára z mračna: náhled k potvrzení (a posunutý konec předchozí čáry)
	if ( m_mode == Mode::CloudLine && m_clHas )
	{
		const ccColor::Rgb cl( 0, 230, 255 );
		segment( m_clA, m_clB, cl, 3.0f );
		addMarker( ovs, m_clA, SnapType::End, cl );
		addMarker( ovs, m_clB, SnapType::End, cl );
		for ( const ClJoin& j : m_clJoins )
		{
			segment( j.a, j.b, cl, 1.0f );
		}
	}
	// místnost / hromadně: náhled všech nalezených čar
	if ( ( m_mode == Mode::CloudRoom || m_mode == Mode::CloudBatch ) && !m_clBatch.empty() )
	{
		// aktuální fáze výrazně, další (méně jisté) tlumeně
		const int ph = batchPhase();
		const ccColor::Rgb cl( 0, 230, 255 );
		const ccColor::Rgb dim( 120, 120, 150 );
		for ( const CloudLines::Seg& sg : m_clBatch )
		{
			if ( sg.cls != ph )
			{
				segment( sg.a, sg.b, dim, 1.0f );
			}
		}
		for ( const CloudLines::Seg& sg : m_clBatch )
		{
			if ( sg.cls == ph )
			{
				segment( sg.a, sg.b, cl, 2.0f );
			}
		}
	}

	// dočasné položky vrstvy (popisky měření, posouvané texty)
	std::vector<RezLayer::Item> temp;
	CCVector3d upv = m_frame.V;
	if ( m_frame.plan )
	{
		const double a = viewAngle();
		upv = CCVector3d( -std::sin( a ), std::cos( a ), 0.0 );
	}

	// měření (D): kóta s patkami a vzdáleností – během měření i výsledek
	if ( m_mode == Mode::Measure && ( m_measDone || ( m_step == 1 && m_hasCursor ) ) )
	{
		const CCVector2d A = m_measA;
		const CCVector2d B = m_measDone ? m_measB : m_cursor;
		const double L = dist( A, B );
		if ( L > 1e-9 )
		{
			const CCVector2d d = ( B - A ) / L;
			const CCVector2d n( -d.y, d.x );
			const double t = 6.0 * pixelSize();
			segment( A, B, yellow, 2.0f );
			for ( const CCVector2d& E : { A, B } )
			{
				segment( E - n * t, E + n * t, yellow, 2.0f );                                   // patka
				segment( E - ( d + n ) * ( t * 0.7 ), E + ( d + n ) * ( t * 0.7 ), yellow, 2.0f ); // šikmá čárka
			}
			RezLayer::Item it;
			it.P = m_frame.to3D( ( A + B ) / 2.0 );
			it.up = upv;
			it.text = QString::number( L / m_scale, 'f', 3 ).replace( '.', ',' ) + QStringLiteral( " m" );
			it.color = yellow;
			it.background = true;
			temp.push_back( it );
		}
		if ( m_measDone )
		{
			addMarker( ovs, m_measA, SnapType::End, yellow );
			addMarker( ovs, m_measB, SnapType::End, yellow );
		}
	}

	// otočení: paprsky ze středu a náhled otočených prvků
	if ( m_mode == Mode::Rotate && m_hasCursor && m_step >= 2 )
	{
		if ( m_step == 2 )
		{
			segment( m_base, m_cursor, yellow, 1.5f );
		}
		else
		{
			segment( m_base, m_rotRef, cyan, 1.5f );
			segment( m_base, m_cursor, yellow, 1.5f );
			const double ang = rotAngle();
			const double ca = std::cos( ang );
			const double sa = std::sin( ang );
			auto rot = [&]( const CCVector2d& P ) { const CCVector2d d = P - m_base; return m_base + CCVector2d( d.x * ca - d.y * sa, d.x * sa + d.y * ca ); };
			const double th = QSettings().value( QStringLiteral( "qRezy/userTextHeight" ), 0.15 ).toDouble() * m_scale;
			for ( ccPolyline* poly : rotTargets() )
			{
				if ( !poly || !lineExists( poly ) )
				{
					continue;
				}
				if ( isText( poly ) && !isKota( poly ) )
				{
					const CCVector2d P = m_frame.toUV( CCVector3d::fromArray( poly->getPoint( 0 )->u ) );
					const double a = ( poly->hasMetaData( "qRezy.textAng" ) ? poly->getMetaData( "qRezy.textAng" ).toDouble() : 0.0 ) + ang;
					RezLayer::Item it;
					it.P = m_frame.to3D( rot( P ) );
					it.up = upv;
					it.height = th;
					it.text = poly->getMetaData( "qRezy.text" ).toString();
					it.color = yellow;
					it.dir = m_frame.U * std::cos( a ) + m_frame.V * std::sin( a );
					temp.push_back( it );
					continue;
				}
				if ( isText( poly ) )
				{
					continue;
				}
				const std::vector<CCVector2d> pts = polyPoints( poly );
				for ( size_t k = 0; k + 1 < pts.size(); ++k )
				{
					segment( rot( pts[k] ), rot( pts[k + 1] ), yellow, 2.0f );
				}
			}
		}
	}

	// ruční umístění textu z pomocného řezu: text na kurzoru
	if ( m_mode == Mode::PlaceText && m_hasCursor && !m_placeText.isEmpty() )
	{
		RezLayer::Item it;
		it.P = m_frame.to3D( placeOrigin( m_cursor ) );
		it.up = upv;
		it.height = QSettings().value( QStringLiteral( "qRezy/userTextHeight" ), 0.15 ).toDouble() * m_scale;
		it.text = m_placeText;
		it.color = yellow;
		it.dir = m_frame.U * std::cos( m_placeAng ) + m_frame.V * std::sin( m_placeAng );
		temp.push_back( it );
	}

	// přesun / kopie: posouvané texty a kóty u kurzoru
	if ( ( m_mode == Mode::Move || m_mode == Mode::Copy ) && m_step == 2 && m_hasCursor )
	{
		const CCVector3d shift = m_frame.to3D( m_cursor, false ) - m_frame.to3D( m_base, false );
		std::vector<ccPolyline*> moving = m_selSet;
		if ( moving.empty() && m_sel )
		{
			moving.push_back( m_sel );
		}
		const double th = QSettings().value( QStringLiteral( "qRezy/userTextHeight" ), 0.15 ).toDouble() * m_scale;
		for ( ccPolyline* poly : moving )
		{
			if ( !poly || !isText( poly ) || poly->size() == 0 )
			{
				continue;
			}
			if ( isKota( poly ) )
			{
				const KotaGeom g = kotaGeom( poly );
				RezLayer::Item li;
				for ( const CCVector3d& Q : g.segs )
				{
					li.segs.push_back( Q + shift );
				}
				li.color = yellow;
				temp.push_back( li );
				for ( int k = 0; k < g.texts.size(); ++k )
				{
					RezLayer::Item ti;
					ti.P = g.textPos[static_cast<size_t>( k )] + shift;
					ti.up = upv;
					ti.height = g.textH;
					ti.text = g.texts[k];
					ti.color = yellow;
					temp.push_back( ti );
				}
				continue;
			}
			RezLayer::Item it;
			it.P = CCVector3d::fromArray( poly->getPoint( 0 )->u ) + shift;
			it.up = upv;
			it.height = th;
			it.text = poly->getMetaData( "qRezy.text" ).toString();
			it.color = yellow;
			if ( poly->hasMetaData( "qRezy.textAng" ) )
			{
				const double a = poly->getMetaData( "qRezy.textAng" ).toDouble();
				it.dir = m_frame.U * std::cos( a ) + m_frame.V * std::sin( a );
			}
			temp.push_back( it );
		}
	}
	if ( m_layer )
	{
		const bool had = !m_layer->temp.empty();
		m_layer->temp = temp;
		if ( had || !temp.empty() )
		{
			m_layer->prepareDisplayForRefresh();
		}
	}

	// značka uchopení
	if ( m_hasCursor && m_cursorSnap != SnapType::None )
	{
		addMarker( ovs, m_cursor, m_cursorSnap, ccColor::Rgb( 255, 200, 0 ) );
	}

	// nitkový kříž ±45 px (jako AutoCAD), na světlém pozadí černý
	if ( m_hasCursor )
	{
		const ccColor::Rgbub& bg = ccGui::Parameters().backgroundCol;
		const double lum = 0.299 * bg.r + 0.587 * bg.g + 0.114 * bg.b;
		const ccColor::Rgb cc = ( lum > 140.0 ) ? ccColor::Rgb( 0, 0, 0 ) : ccColor::Rgb( 255, 255, 255 );
		const QPointF g = m_win->toCornerGLCoordinates( m_mousePos.x(), m_mousePos.y() );
		ccGLCameraParameters camera;
		m_win->getGLCameraParameters( camera );
		const CCVector2d C( g.x() - camera.viewport[2] / 2.0, g.y() - camera.viewport[3] / 2.0 );
		const double r = CrossPixels * m_win->getDevicePixelRatio();
		Overlay h;
		h.pts = { C + CCVector2d( -r, 0 ), C + CCVector2d( r, 0 ) };
		h.color = cc;
		ovs.push_back( h );
		Overlay v;
		v.pts = { C + CCVector2d( 0, -r ), C + CCVector2d( 0, r ) };
		v.color = cc;
		ovs.push_back( v );
		if ( pickingCursor() )
		{
			const double q = PickBoxPixels * m_win->getDevicePixelRatio();
			Overlay box;
			box.pts = { C + CCVector2d( -q, -q ), C + CCVector2d( q, -q ), C + CCVector2d( q, q ), C + CCVector2d( -q, q ), C + CCVector2d( -q, -q ) };
			box.color = cc;
			ovs.push_back( box );
			// s Ctrl znaménko + / − (přidat / ubrat z výběru)
			if ( m_mode == Mode::Select && ( QApplication::keyboardModifiers() & Qt::ControlModifier ) )
			{
				ccPolyline* under = pickLine( m_mousePos );
				const bool minus = under && isSelected( under );
				const double s0 = 4.0 * m_win->getDevicePixelRatio();
				const CCVector2d M = C + CCVector2d( 3 * s0, 3 * s0 );
				Overlay hz;
				hz.pts = { M + CCVector2d( -s0, 0 ), M + CCVector2d( s0, 0 ) };
				hz.color = cc;
				hz.width = 2.0f;
				ovs.push_back( hz );
				if ( !minus )
				{
					Overlay vt;
					vt.pts = { M + CCVector2d( 0, -s0 ), M + CCVector2d( 0, s0 ) };
					vt.color = cc;
					vt.width = 2.0f;
					ovs.push_back( vt );
				}
			}
		}
	}

	setOverlays( ovs );
}

void DrawTool::redraw2D()
{
	if ( m_win )
	{
		refreshLayer();
		m_win->redraw( true, false );
	}
}

void DrawTool::redraw3D()
{
	if ( m_win )
	{
		refreshLayer();
		m_win->deprecate3DLayer(); // jinak se po Zpět / mazání může ukázat starý obraz
		m_win->redraw( false, false );
	}
}

void DrawTool::keepCrossCursor()
{
	// systémový kurzor skrytý, nitkový kříž kreslíme sami (updateTip)
	if ( m_win )
	{
		m_win->setWindowCursor( Qt::BlankCursor );
	}
}

void DrawTool::showToolMenu( int group )
{
	struct Item
	{
		int key;
		const char* label;
		bool enabled;
		Mode mode;
	};
	std::vector<Item> items;
	QString title;
	if ( group == 3 )
	{
		title = QStringLiteral( "Manipulace" );
		items = {
			{ 1, "Kopie  (Copy)", true, Mode::Copy },
			{ 2, "Přesun  (Move)", true, Mode::Move },
			{ 3, "Měřítko  (Scale)", false, Mode::Line },
			{ 4, "Otočit  (Rotate, 3 body)", true, Mode::Rotate },
			{ 5, "Zrcadlit  (Mirror)", false, Mode::Line },
			{ 6, "Pole  (Array)", false, Mode::Line },
			{ 7, "Zarovnat  (Align By Edge)", false, Mode::Line },
			{ 8, "Protáhnout  (Stretch)", false, Mode::Line },
			{ 9, "Rovnoběžně  (Move Parallel)", true, Mode::Offset },
			{ 0, "Přisunout  (Move To Contact)", false, Mode::Line },
		};
	}
	else if ( group == Qt::Key_A )
	{
		title = QStringLiteral( "Text" );
		items = {
			{ Qt::Key_A, "Nový text  (Place Text)", true, Mode::Text },
			{ Qt::Key_B, "Upravit text  (Edit Text)", true, Mode::EditText },
		};
	}
	else
	{
		title = QStringLiteral( "Úpravy" );
		items = {
			{ 1, "Upravit prvek  (Modify Element)", true, Mode::Modify },
			{ 2, "Rozdělit  (Break Element)", true, Mode::Break },
			{ 3, "Prodloužit/zkrátit  (Extend Line)", true, Mode::Extend },
			{ 4, "Roh  (Trim To Intersection)", true, Mode::Corner },
			{ 5, "Oříznout k prvku  (Trim To Element)", true, Mode::Trim },
			{ 6, "Rozdělit čarou  (řezná čára přes více čar)", true, Mode::BreakLine },
			{ 7, "Vložit vrchol  (Insert Vertex)", true, Mode::InsertVertex },
			{ 8, "Smazat vrchol  (Delete Vertex)", true, Mode::DeleteVertex },
			{ 9, "Zaoblit  (Circular Fillet)", false, Mode::Line },
			{ 0, "Parabolické zaoblení  (Parabolic Fillet)", false, Mode::Line },
			{ Qt::Key_Q, "Zkosit  (Chamfer)", false, Mode::Line },
		};
	}
	KeyMenu menu( qobject_cast<QWidget*>( m_winObject ) );
	menu.setTitle( title );
	QAction* head = menu.addAction( QStringLiteral( "%1 (%2)" ).arg( title, group == Qt::Key_A ? QStringLiteral( "A" ) : QString::number( group ) ) );
	head->setEnabled( false );
	menu.addSeparator();
	for ( const Item& it : items )
	{
		const QString key = ( it.key >= Qt::Key_A && it.key <= Qt::Key_Z ) ? QString( QChar( it.key ) ) : QString::number( it.key );
		QAction* a = menu.addAction( QStringLiteral( "%1\t%2" ).arg( QString::fromUtf8( it.label ), key ) );
		a->setEnabled( it.enabled );
		const Mode m = it.mode;
		connect( a, &QAction::triggered, this, [this, m]() { setMode( m ); } );
		menu.keys.push_back( { it.key, a } );
	}
	menu.exec( QCursor::pos() );
	keepCrossCursor();
}

void DrawTool::showPin( const CCVector3d& P )
{
	m_pin3 = P;
	m_hasPin = true;
	refreshLayer();
	// posunout okno, aby byl špendlík uprostřed
	const QWidget* w = qobject_cast<const QWidget*>( m_winObject );
	if ( m_win && w )
	{
		ccGLCameraParameters cam;
		m_win->getGLCameraParameters( cam );
		CCVector3d S;
		if ( cam.project( P, S ) )
		{
			const double dpr = m_win->getDevicePixelRatio();
			const double sx = S.x / dpr;
			const double sy = ( cam.viewport[3] - S.y ) / dpr;
			panBy( QPoint( static_cast<int>( std::lround( w->width() / 2.0 - sx ) ), static_cast<int>( std::lround( w->height() / 2.0 - sy ) ) ) );
		}
	}
	redraw2D();
}

void DrawTool::clearPin()
{
	if ( m_hasPin )
	{
		m_hasPin = false;
		refreshLayer();
		redraw2D();
	}
}

bool DrawTool::viewCenter( CCVector3d& out ) const
{
	const QWidget* w = qobject_cast<const QWidget*>( m_winObject );
	CCVector2d P;
	if ( !w || !screenToLocal( QPoint( w->width() / 2, w->height() / 2 ), P ) )
	{
		return false;
	}
	out = m_frame.to3D( P, false );
	return true;
}

void DrawTool::panBy( const QPoint& d )
{
	if ( !m_win || d.isNull() )
	{
		return;
	}
	const double px = m_win->computeActualPixelSize() * m_win->getDevicePixelRatio();
	CCVector3d u( d.x() * px, -d.y() * px, 0.0 );
	if ( m_win->getViewportParameters().objectCenteredView )
	{
		u = -u;
	}
	m_win->moveCamera( u );
	m_win->redraw();
}

bool DrawTool::pickingCursor() const
{
	// čtvereček pro výběr čáry
	switch ( m_mode )
	{
	case Mode::Delete:
	case Mode::DeleteVertex:
		return true;
	case Mode::Select:
	case Mode::Modify:
	case Mode::InsertVertex:
		return m_step == 0;
	case Mode::Move:
	case Mode::Copy:
		return m_step == 0 && m_selSet.empty();
	case Mode::Offset:
	case Mode::Extend:
	case Mode::Break:
		return m_step == 0;
	case Mode::Corner:
	case Mode::Trim:
		return m_step <= 1;
	default:
		return false;
	}
}

void DrawTool::updateStatus()
{
	QString prompt;
	switch ( m_mode )
	{
	case Mode::Line:
		prompt = m_chain.empty() ? QStringLiteral( "Čára: první bod" ) : QStringLiteral( "Čára: další bod nebo délka (reset = konec)" );
		break;
	case Mode::Delete:
		prompt = QStringLiteral( "Mazání: klikněte na čáru" );
		break;
	case Mode::RotateView:
		prompt = m_step == 0 ? QStringLiteral( "Natočení pohledu: první bod (směr 1. → 2. bod bude vodorovně doprava)" )
		                     : QStringLiteral( "Natočení pohledu: druhý bod" );
		break;
	case Mode::Measure:
	{
		if ( m_measDone || m_step == 1 )
		{
			const CCVector2d B = m_measDone ? m_measB : m_cursor;
			const CCVector2d d = B - m_measA;
			const bool plan = m_frame.plan;
			const double ang = std::atan2( d.y, d.x ) * RadToDeg;
			prompt = QStringLiteral( "Měření: %1 m   (%2 %3 m, %4 %5 m, úhel %6°)" )
			             .arg( num( d.norm() / m_scale ) )
			             .arg( plan ? QStringLiteral( "ΔX" ) : QStringLiteral( "vodorovně" ) )
			             .arg( num( d.x / m_scale ) )
			             .arg( plan ? QStringLiteral( "ΔY" ) : QStringLiteral( "výškově" ) )
			             .arg( num( d.y / m_scale ) )
			             .arg( QString::number( ang, 'f', 2 ).replace( '.', ',' ) );
			if ( m_measDone )
				prompt += QStringLiteral( "  –  další klik = nové měření" );
		}
		else
		{
			prompt = QStringLiteral( "Měření: první bod" );
		}
		break;
	}
	case Mode::SectionDef:
	{
		const QString n = m_defKind == 1 ? QStringLiteral( "Nový pohled" ) : m_defKind == 2 ? QStringLiteral( "Pomocný řez" ) : QStringLiteral( "Nový řez" );
		prompt = m_step == 0 ? n + QStringLiteral( ": první bod roviny" )
		       : m_step == 1 ? n + QStringLiteral( ": druhý bod roviny" )
		                     : n + QStringLiteral( ": klikněte na stranu, KAM se díváte (šipky)" );
	}
		break;
	case Mode::Move:
	case Mode::Copy:
	{
		const QString n = m_mode == Mode::Move ? QStringLiteral( "Přesun" ) : QStringLiteral( "Kopie" );
		if ( m_step == 0 )
			prompt = n + QStringLiteral( ": vyberte čáru" );
		else if ( m_step == 1 )
			prompt = n + QStringLiteral( ": výchozí bod" );
		else
			prompt = n + QStringLiteral( ": cílový bod nebo vzdálenost" );
		break;
	}
	case Mode::Offset:
		prompt = m_step == 0 ? QStringLiteral( "Rovnoběžně: vyberte čáru" )
		                     : ( m_offsetDist > 0.0 ? QStringLiteral( "Rovnoběžně %1 m: klikněte na stranu (nebo napište jinou vzdálenost)" ).arg( num( m_offsetDist ) )
		                                            : QStringLiteral( "Rovnoběžně: bod, kudy má kopie vést, nebo vzdálenost" ) );
		break;
	case Mode::Extend:
		prompt = m_step == 0 ? QStringLiteral( "Prodloužit/zkrátit: klikněte na čáru u konce, který se má změnit" )
		                     : QStringLiteral( "Prodloužit/zkrátit: nový konec nebo celková délka" );
		break;
	case Mode::Corner:
		prompt = m_step == 0 ? QStringLiteral( "Roh: klikněte na první čáru (na část, která zůstane)" )
		                     : QStringLiteral( "Roh: klikněte na druhou čáru" );
		break;
	case Mode::Break:
		prompt = m_step == 0 ? QStringLiteral( "Rozdělit: první bod na čáře" )
		                     : QStringLiteral( "Rozdělit: druhý bod – část mezi body se smaže (stejný bod = jen rozdělit)" );
		break;
	case Mode::Text:
		prompt = QStringLiteral( "Text: klikněte, kam text vložit (levý dolní roh)" );
		break;
	case Mode::EditText:
		prompt = QStringLiteral( "Upravit text: klikněte na text" );
		break;
	case Mode::Rotate:
		if ( m_step == 0 )
			prompt = QStringLiteral( "Otočit: klikněte na prvek (nebo nejdřív vyberte víc prvků)" );
		else if ( m_step == 1 )
			prompt = QStringLiteral( "Otočit: střed otáčení" );
		else if ( m_step == 2 )
			prompt = QStringLiteral( "Otočit: výchozí směr (bod)" );
		else
			prompt = QStringLiteral( "Otočit: cílový směr (bod) – úhel %1°" ).arg( QString::number( rotAngle() * 57.29577951308232, 'f', 2 ).replace( '.', ',' ) );
		break;
	case Mode::PlaceText:
		prompt = QStringLiteral( "Umístit text „%1“: klikněte do půdorysu (Esc = zrušit)" ).arg( m_placeText );
		break;
	case Mode::BreakLine:
		prompt = m_step == 0 ? QStringLiteral( "Rozdělit čarou: první bod řezné čáry" )
		                     : QStringLiteral( "Rozdělit čarou: druhý bod – protnuté čáry se rozdělí (%1)" ).arg( m_hasCursor ? breakLineHits( m_base, m_cursor ).size() : 0 );
		break;
	case Mode::Pin:
		prompt = QStringLiteral( "Špendlík: klikněte na místo – ukáže se ve 3D náhledu (také prostřední klik kdekoli)" );
		break;
	case Mode::Kota:
	{
		static const char* fm[] = { "Bpv", "místní", "oboje" };
		if ( m_frame.plan )
		{
			prompt = QStringLiteral( "Kóta: jen v řezu nebo pohledu" );
			break;
		}
		const CCVector2d T = m_step == 1 ? m_kotaTip : m_cursor;
		const KotaGeom g = kotaGeom( m_frame.to3D( T ), m_frame.U, m_frame.V, 1, 2, m_kotaScale );
		prompt = ( m_step == 0 ? QStringLiteral( "Kóta (%1, 1:%2): klikněte na bod (hrot šipky)" ) : QStringLiteral( "Kóta (%1, 1:%2): klikněte na stranu poličky (vlevo / vpravo)" ) )
		             .arg( QString::fromUtf8( fm[m_kotaFmt] ) ).arg( m_kotaScale );
		if ( m_hasCursor || m_step == 1 )
		{
			prompt += QStringLiteral( "  |  %1  (%2 Bpv)" ).arg( g.texts.value( 0 ), g.texts.value( 1 ) );
		}
		break;
	}
	case Mode::Select:
		prompt = m_step == 1 ? QStringLiteral( "Úchop: nové místo konce čáry (nebo délka)" )
		                     : QStringLiteral( "Výběr: klik = vybrat, Ctrl+klik = přidat/ubrat, okno tažením (zleva = uvnitř, zprava = křížem); úchop na konci = přesun konce. Vybráno: %1" ).arg( m_selSet.size() );
		break;
	case Mode::Modify:
		prompt = m_step == 0 ? QStringLiteral( "Upravit prvek: klikněte na čáru u konce, který se má přesunout" )
		                     : QStringLiteral( "Upravit prvek: nové místo konce (nebo délka)" );
		break;
	case Mode::InsertVertex:
		prompt = m_step == 0 ? QStringLiteral( "Vložit vrchol: klikněte na čáru" )
		                     : QStringLiteral( "Vložit vrchol: umístěte nový vrchol" );
		break;
	case Mode::DeleteVertex:
		prompt = QStringLiteral( "Smazat vrchol: klikněte na spoj dvou čar" );
		break;
	case Mode::Trim:
		prompt = m_step == 0 ? QStringLiteral( "Oříznout k prvku: vyberte hraniční čáru" )
		                     : QStringLiteral( "Oříznout k prvku: klikněte na čáry (na část, která zůstane); reset = nová hranice" );
		break;
	case Mode::Arc:
	{
		static const char* names[] = { "začátek – střed oblouku – konec", "začátek – konec – střed oblouku", "začátek – střed kružnice – konec" };
		const QString n = QStringLiteral( "Oblouk (%1)" ).arg( QString::fromUtf8( names[m_arcMethod] ) );
		if ( m_arcPts.empty() )
			prompt = n + QStringLiteral( ": začátek" );
		else if ( m_arcPts.size() == 1 )
			prompt = n + ( m_arcMethod == 0 ? QStringLiteral( ": bod na oblouku (střed oblouku)" ) : m_arcMethod == 1 ? QStringLiteral( ": konec" ) : QStringLiteral( ": střed kružnice" ) );
		else
			prompt = n + ( m_arcMethod == 0 ? QStringLiteral( ": konec" ) : m_arcMethod == 1 ? QStringLiteral( ": bod na oblouku (střed oblouku)" ) : QStringLiteral( ": konec (směr podle pohybu myši)" ) );
		std::vector<CCVector2d> pts;
		if ( arcPreview( pts ) && pts.size() >= 3 )
		{
			CCVector2d C;
			if ( circleFrom3( pts.front(), pts[pts.size() / 2], pts.back(), C ) )
			{
				prompt += QStringLiteral( "  |  poloměr %1 m" ).arg( num( dist( C, pts.front() ) / m_scale ) );
			}
		}
		break;
	}
	case Mode::CloudLine:
		prompt = m_clHas ? QStringLiteral( "Čára z mračna: %1 m – klik na čáru nebo Enter = potvrdit, klik jinam = potvrdit a další, pravé = zahodit%2" )
		                       .arg( num( dist( m_clA, m_clB ) / m_scale ), m_clJoin ? QStringLiteral( " (napojí se v rohu)" ) : QString() )
		                 : QStringLiteral( "Čára z mračna: klikněte k bodům řezu (na líc zdi)" );
		break;
	case Mode::CloudRoom:
		prompt = !m_clBatch.empty() ? roomPrompt()
		                            : QStringLiteral( "Místnost: klikněte dovnitř místnosti (plátek nejlépe u stropu – bez oken a dveří)" );
		break;
	case Mode::CloudBatch:
		prompt = !m_clBatch.empty() ? batchPrompt()
		                            : QStringLiteral( "Hromadně: klik = hledat čáry v celém plátku" );
		break;
	}

	if ( !m_flash.isEmpty() )
	{
		prompt = m_flash;
		m_flash.clear();
	}
	QString s = prompt;
	s += QStringLiteral( "\nHladina: %1%2" ).arg( Drawing::categoryName( m_category ), m_hidden ? QStringLiteral( " – zakrytá" ) : QString() );
	if ( m_ortho )
	{
		s += QStringLiteral( "  |  ORTO" );
	}
	if ( m_perpLock )
	{
		s += QStringLiteral( "  |  KOLMO na předchozí úsek" );
	}
	if ( m_hasCursor && m_cursorSnap != SnapType::None )
	{
		s += QStringLiteral( "  |  uchopení: %1" ).arg( snapName( m_cursorSnap ) );
	}
	const CCVector2d* from = nullptr;
	if ( m_mode == Mode::Line && !m_chain.empty() )
	{
		from = &m_chain.back();
	}
	else if ( ( m_mode == Mode::Move || m_mode == Mode::Copy ) && m_step == 2 )
	{
		from = &m_base;
	}
	if ( !from && ( m_mode == Mode::Modify || m_mode == Mode::Select ) && m_step == 1 )
	{
		from = &m_fixed;
	}
	if ( from && m_hasCursor )
	{
		s += QStringLiteral( "  |  délka %1 m" ).arg( num( ( m_cursor - *from ).norm() / m_scale ) );
	}
	CCVector2d a;
	CCVector2d b;
	if ( previewGeometry( a, b ) )
	{
		if ( m_mode == Mode::Extend )
		{
			s += QStringLiteral( "  |  nová délka %1 m" ).arg( num( ( b - a ).norm() / m_scale ) );
		}
		else if ( m_mode == Mode::Offset )
		{
			CCVector2d sa;
			CCVector2d sb;
			if ( lineEnds( m_sel, sa, sb ) )
			{
				s += QStringLiteral( "  |  vzdálenost %1 m" ).arg( num( dist( closestOnSegment( a, b, sa ), sa ) / m_scale ) );
			}
		}
	}
	if ( !m_typed.isEmpty() )
	{
		s += QStringLiteral( "  |  zadáno: %1 (Enter)" ).arg( m_typed );
	}
	Q_EMIT statusChanged( s );
}

// ---------------------------------------------------------------- události

bool DrawTool::eventFilter( QObject* obj, QEvent* event )
{
	if ( obj != m_winObject || !m_win )
	{
		return QObject::eventFilter( obj, event );
	}

	switch ( event->type() )
	{
	case QEvent::MouseButtonPress:
	case QEvent::MouseButtonDblClick: // rychlé druhé kliknutí = další bod, ne dvojklik
	{
		auto* e = static_cast<QMouseEvent*>( event );
		m_mousePos = e->pos();
		if ( m_panning )
		{
			return true;
		}
		if ( e->button() == Qt::LeftButton && !( e->modifiers() & Qt::ShiftModifier ) )
		{
			if ( m_mode == Mode::Select && m_step == 0 )
			{
				ccPolyline* hp = nullptr;
				int he = 0;
				if ( !( e->modifiers() & Qt::ControlModifier ) && pickHandle( e->pos(), hp, he ) )
				{
					// úchop: konec vybrané čáry se přesune dalším klikem
					CCVector2d a;
					CCVector2d b;
					lineEnds( hp, a, b );
					m_selEnd = he;
					m_fixed = ( he == 0 ) ? b : a;
					select( hp );
					m_step = 1;
					updateTip();
					updateStatus();
					redraw2D();
				}
				else
				{
					m_winPressed = true;
					m_winDragging = false;
					m_winStart = e->pos();
				}
			}
			else
			{
				onDataPoint( e->pos() );
			}
		}
		else if ( e->button() == Qt::LeftButton || e->button() == Qt::MiddleButton )
		{
			// Shift + levé tažení nebo tažení kolečkem = posun pohledu
			m_panning = true;
			m_panMoved = false;
			m_panButton = e->button();
			m_panPress = e->pos();
			m_panLast = e->pos();
		}
		else if ( e->button() == Qt::RightButton )
		{
			m_rightPressed = true;
		}
		return true; // CloudCompare tlačítka myši nedostane (žádné otáčení ani posun omylem)
	}
	case QEvent::MouseButtonRelease:
	{
		auto* e = static_cast<QMouseEvent*>( event );
		m_mousePos = e->pos();
		if ( m_panning && e->button() == m_panButton )
		{
			m_panning = false;
			if ( !m_panMoved )
			{
				if ( m_panButton == Qt::MiddleButton )
				{
					// prostřední kliknutí = ukázat místo ve 3D
					CCVector2d P;
					if ( screenToLocal( e->pos(), P ) )
					{
						const CCVector3d P3 = m_frame.to3D( P, false );
						Q_EMIT show3DRequested( P3.x, P3.y, m_frame.plan ? std::numeric_limits<double>::quiet_NaN() : P3.z );
					}
				}
				else
				{
					onDataPoint( e->pos() ); // Shift + kliknutí = obyčejný bod
				}
			}
		}
		else if ( e->button() == Qt::LeftButton && m_winPressed )
		{
			m_winPressed = false;
			const bool ctrl = ( e->modifiers() & Qt::ControlModifier );
			if ( m_winDragging )
			{
				m_winDragging = false;
				selectWindow( m_winStart, e->pos(), ctrl );
			}
			else
			{
				ccPolyline* p = pickLine( e->pos() );
				if ( ctrl )
				{
					if ( p )
						toggleSelected( p );
				}
				else
				{
					setSelection( p ? std::vector<ccPolyline*>{ p } : std::vector<ccPolyline*>{} );
				}
			}
			updateStatus();
		}
		else if ( e->button() == Qt::RightButton && m_rightPressed )
		{
			// pravé tlačítko = vždy jen reset (konec čáry / zpět o krok nástroje)
			m_rightPressed = false;
			if ( m_mode == Mode::Select && m_step == 0 )
			{
				setSelection( {} );
			}
			if ( ( m_mode == Mode::Move || m_mode == Mode::Copy ) && !m_selSet.empty() )
			{
				setSelection( {} ); // reset zruší výběr, další klik vybírá čáru
			}
			m_chain.clear();
			m_arcPts.clear();
			m_arcSweep = 0.0;
			m_perpLock = false;
			m_clHas = false; // čára z mračna: reset = zahodit náhled
			m_clJoins.clear();
			if ( ( m_mode == Mode::CloudBatch || m_mode == Mode::CloudRoom ) && !m_clBatch.empty() )
			{
				skipBatchPhase(); // hromadně / místnost: pravé = přeskočit jen aktuální fázi
			}
			else
			{
				m_clBatch.clear();
			}
			m_step = 0;
			select( nullptr );
			m_typed.clear();
			updateStatus();
		}
		keepCrossCursor();
		updateTip();
		redraw2D();
		return true;
	}
	case QEvent::MouseMove:
	{
		auto* e = static_cast<QMouseEvent*>( event );
		m_mousePos = e->pos();
		if ( m_panning )
		{
			const int tol = ( m_panButton == Qt::MiddleButton ) ? 3 * ClickTolerance : ClickTolerance;
			if ( !m_panMoved && ( e->pos() - m_panPress ).manhattanLength() > tol )
			{
				m_panMoved = true;
			}
			if ( m_panMoved )
			{
				panBy( e->pos() - m_panLast );
				m_panLast = e->pos();
			}
			updateTip();
			redraw2D();
			return true;
		}
		if ( m_winPressed && ( e->buttons() & Qt::LeftButton ) && !m_winDragging && ( e->pos() - m_winStart ).manhattanLength() > ClickTolerance )
		{
			m_winDragging = true;
		}
		SnapType st;
		const CCVector2d* from = nullptr;
		if ( m_mode == Mode::Line && !m_chain.empty() )
		{
			from = &m_chain.back();
		}
		else if ( ( ( m_mode == Mode::Move || m_mode == Mode::Copy ) && m_step == 2 ) || ( ( m_mode == Mode::SectionDef || m_mode == Mode::RotateView ) && m_step == 1 ) )
		{
			from = &m_base;
		}
		else if ( m_mode == Mode::Measure && m_step == 1 && !m_measDone )
		{
			from = &m_measA;
		}
		else if ( ( m_mode == Mode::Modify || m_mode == Mode::Select ) && m_step == 1 )
		{
			from = &m_fixed;
		}
		else if ( m_mode == Mode::InsertVertex && m_step == 1 )
		{
			from = &m_fixed;
		}
		else if ( m_mode == Mode::Arc && !m_arcPts.empty() )
		{
			from = &m_arcPts.back();
		}
		m_trackLines.clear();
		m_cursor = cursorPoint( e->pos(), st, from, &m_trackLines );
		if ( m_mode == Mode::Arc && m_arcMethod == 2 && m_arcPts.size() == 2 && dist( m_cursor, m_arcPts[1] ) > 1e-9 )
		{
			// směr oblouku podle pohybu myši kolem středu
			const double ang = std::atan2( m_cursor.y - m_arcPts[1].y, m_cursor.x - m_arcPts[1].x );
			m_arcSweep += wrapPi( ang - m_arcPrevAng );
			m_arcSweep = std::max( -TwoPi + 1e-3, std::min( TwoPi - 1e-3, m_arcSweep ) );
			m_arcPrevAng = ang;
		}
		m_cursorSnap = st;
		m_hasCursor = true;

		// trasování: vzdálenost se zadává od získaného bodu (ne od posledního bodu čáry)
		m_cursorTracked = false;
		for ( const auto& tl : m_trackLines )
		{
			if ( !from || ( tl.first - *from ).norm() > 1e-9 )
			{
				m_cursorTracked = true;
				m_trackFrom = tl.first;
				break;
			}
		}
		// kandidát na získání bodu (stojí-li kurzor na konci/středu/průsečíku)
		const bool candOk = m_perpRefValid || st == SnapType::End || st == SnapType::Mid || st == SnapType::Intersection;
		m_lastCand = m_perpRefValid ? m_perpRef : m_cursor;
		if ( m_trackingOn && candOk )
		{
			if ( !m_hoverValid || ( m_hoverCand - m_lastCand ).norm() > 1e-9 )
			{
				m_hoverCand = m_lastCand;
				m_hoverValid = true;
				m_hoverTimer->start();
			}
		}
		else
		{
			m_hoverValid = false;
			m_hoverTimer->stop();
		}
		keepCrossCursor();
		updateTip();
		updateStatus();
		redraw2D();
		if ( e->buttons() != Qt::NoButton )
		{
			return true; // tažení tlačítkem nepouštět do CloudCompare
		}
		break;
	}
	case QEvent::Enter:
		if ( QWidget* w = qobject_cast<QWidget*>( m_winObject ) )
		{
			w->setFocus();
		}
		keepCrossCursor();
		break;
	case QEvent::Leave:
		m_hasCursor = false;
		updateTip();
		redraw2D();
		break;
	case QEvent::Wheel:
	{
		auto* e = static_cast<QWheelEvent*>( event );
		if ( e->modifiers() & Qt::ControlModifier )
		{
			// Ctrl + kolečko = tloušťka řezu
			const int d = e->angleDelta().y();
			if ( d != 0 )
			{
				Q_EMIT thicknessWheel( d > 0 ? 1 : -1 );
			}
			return true;
		}
		// zoom k místu pod kurzorem
		const int d = e->angleDelta().y();
		if ( d != 0 && !( e->modifiers() & ( Qt::ShiftModifier | Qt::AltModifier ) ) )
		{
			zoomAtCursor( m_win, e->pos(), d / 8.0f );
			updateTip();
			redraw2D();
			return true;
		}
		QTimer::singleShot( 0, this, [this]() { updateTip(); redraw2D(); } );
		break;
	}
	case QEvent::ShortcutOverride:
	case QEvent::KeyPress:
	{
		auto* e = static_cast<QKeyEvent*>( event );
		const int k = e->key();
		const bool ctrl = ( e->modifiers() & Qt::ControlModifier );
		// horní řada bez Shiftu = menu nástrojů (jako MicroStation), čísla z numerické klávesnice nebo se Shiftem
		const int rowDigit = topRowDigit( e );
		const bool digit = rowDigit < 0 && ( ( k >= Qt::Key_0 && k <= Qt::Key_9 ) || k == Qt::Key_Comma || k == Qt::Key_Period );
		const bool plain = !ctrl && !( e->modifiers() & ( Qt::AltModifier | Qt::ShiftModifier ) );
		const bool wantsNumber = ( m_mode == Mode::Line && ( !m_chain.empty() || m_cursorTracked ) ) || ( ( m_mode == Mode::Move || m_mode == Mode::Copy ) && m_step == 2 )
		                      || ( ( m_mode == Mode::Modify || m_mode == Mode::Select ) && m_step == 1 )
		                      || ( ( m_mode == Mode::Offset || m_mode == Mode::Extend ) && m_step == 1 );

		bool handled = true;
		if ( digit && wantsNumber )
		{
			if ( event->type() == QEvent::KeyPress )
			{
				m_typed += ( k == Qt::Key_Comma || k == Qt::Key_Period ) ? QStringLiteral( "," ) : e->text();
				updateStatus();
			}
		}
		else if ( k == Qt::Key_Backspace && !m_typed.isEmpty() )
		{
			if ( event->type() == QEvent::KeyPress )
			{
				m_typed.chop( 1 );
				updateStatus();
			}
		}
		else if ( ( k == Qt::Key_Return || k == Qt::Key_Enter ) && !m_typed.isEmpty() )
		{
			if ( event->type() == QEvent::KeyPress )
			{
				bool ok = false;
				const double v = QString( m_typed ).replace( ',', '.' ).toDouble( &ok );
				m_typed.clear();
				if ( ok )
				{
					onTypedValue( v );
				}
				updateTip();
				updateStatus();
				redraw2D();
			}
		}
		else if ( ( k == Qt::Key_Return || k == Qt::Key_Enter ) && m_typed.isEmpty() && ( m_mode == Mode::CloudRoom || m_mode == Mode::CloudBatch ) )
		{
			if ( event->type() == QEvent::KeyPress )
			{
				acceptBatch(); // Enter = potvrdit místnost / hromadné čáry
				updateTip();
				updateStatus();
				redraw2D();
			}
		}
		else if ( ( k == Qt::Key_Return || k == Qt::Key_Enter ) && m_typed.isEmpty() && ( m_mode == Mode::Line || m_mode == Mode::CloudLine ) )
		{
			if ( event->type() == QEvent::KeyPress )
			{
				if ( m_mode == Mode::CloudLine )
				{
					acceptCloudLine(); // Enter = potvrdit čáru z mračna
				}
				else if ( m_chain.size() >= 2 )
				{
					// Enter = další úsek kolmo na předchozí (jen jeden úsek; znovu Enter = zrušit)
					CCVector2d d = m_chain.back() - m_chain[m_chain.size() - 2];
					if ( d.norm() > 1e-9 && !m_perpLock )
					{
						d.normalize();
						m_perpDir = CCVector2d( -d.y, d.x );
						m_perpLock = true;
					}
					else
					{
						m_perpLock = false;
					}
				}
				else
				{
					m_flash = QStringLiteral( "Kolmo: nejdřív nakreslete úsek, pak Enter = další úsek kolmo na něj" );
				}
				updateTip();
				updateStatus();
				redraw3D();
			}
		}
		else if ( k == Qt::Key_Escape )
		{
			if ( event->type() == QEvent::KeyPress )
			{
				// Esc = zrušit krok; bez rozdělané práce zpět na Výběr (jako ve Vrstevnicích)
				if ( m_mode == Mode::Select && m_step == 0 )
				{
					setSelection( {} );
					updateStatus();
				}
				else if ( m_step == 0 && m_chain.empty() && m_arcPts.empty() && !m_clHas )
				{
					setMode( Mode::Select );
				}
				else
				{
					resetTool();
				}
			}
		}
		else if ( k == Qt::Key_Z && ctrl )
		{
			if ( event->type() == QEvent::KeyPress )
				undo();
		}
		else if ( k >= Qt::Key_F1 && k <= Qt::Key_F5 && !ctrl )
		{
			if ( event->type() == QEvent::KeyPress )
			{
				const int id = Drawing::layerIdAt( k - Qt::Key_F1 );
				if ( id >= 0 )
					setCategory( id );
			}
		}
		else if ( k == Qt::Key_F6 && !ctrl )
		{
			if ( event->type() == QEvent::KeyPress )
				setHidden( !m_hidden );
		}
		else if ( k == Qt::Key_F8 && !ctrl )
		{
			if ( event->type() == QEvent::KeyPress )
				setOrtho( !m_ortho );
		}
		else if ( k == Qt::Key_F11 && !ctrl )
		{
			if ( event->type() == QEvent::KeyPress )
				setTracking( !m_trackingOn );
		}
		else if ( k == Qt::Key_Delete )
		{
			if ( event->type() == QEvent::KeyPress )
				setMode( Mode::Delete );
		}
		else if ( rowDigit == 3 || rowDigit == 7 )
		{
			if ( event->type() == QEvent::KeyPress )
			{
				const int g = rowDigit;
				QTimer::singleShot( 0, this, [this, g]() { showToolMenu( g ); } );
			}
		}
		else if ( rowDigit == 8 )
		{
			if ( event->type() == QEvent::KeyPress )
				setMode( Mode::Delete );
		}
		else if ( rowDigit == 1 )
		{
			if ( event->type() == QEvent::KeyPress )
				setMode( Mode::Select );
		}
		else if ( k == Qt::Key_A && ctrl )
		{
			if ( event->type() == QEvent::KeyPress )
				selectAll();
		}
		else if ( plain && k == Qt::Key_Q )
		{
			if ( event->type() == QEvent::KeyPress )
				setMode( Mode::Line );
		}
		else if ( plain && k == Qt::Key_V )
		{
			if ( event->type() == QEvent::KeyPress )
				setMode( Mode::CloudLine );
		}
		else if ( plain && k == Qt::Key_C )
		{
			if ( event->type() == QEvent::KeyPress )
				QTimer::singleShot( 0, this, [this]() { Q_EMIT auxSectionRequested(); } );
		}
		else if ( plain && k == Qt::Key_A )
		{
			if ( event->type() == QEvent::KeyPress )
				QTimer::singleShot( 0, this, [this]() { showToolMenu( Qt::Key_A ); } );
		}
		else if ( plain && k == Qt::Key_D )
		{
			if ( event->type() == QEvent::KeyPress )
				setMode( Mode::Measure );
		}
		else if ( plain && k == Qt::Key_E )
		{
			if ( event->type() == QEvent::KeyPress )
				setMode( Mode::Arc );
		}
		else
		{
			handled = false;
		}

		if ( handled )
		{
			e->accept();
			return true;
		}
		break;
	}
	default:
		break;
	}
	return QObject::eventFilter( obj, event );
}


// ---------------------------------------------------------------- výběr

bool DrawTool::lineAttrs( ccPolyline* poly, int& cat, bool& hidden ) const
{
	if ( !poly )
	{
		return false;
	}
	cat = poly->hasMetaData( Drawing::MetaCategory ) ? poly->getMetaData( Drawing::MetaCategory ).toInt() : m_category;
	hidden = poly->hasMetaData( Drawing::MetaHidden ) && poly->getMetaData( Drawing::MetaHidden ).toBool();
	return poly->hasMetaData( Drawing::MetaCategory );
}

bool DrawTool::isSelected( ccPolyline* poly ) const
{
	return poly && std::find( m_selSet.begin(), m_selSet.end(), poly ) != m_selSet.end();
}

void DrawTool::restoreLook( ccPolyline* poly )
{
	if ( !poly )
	{
		return;
	}
	if ( poly == m_sel )
	{
		poly->setColor( ccColor::Rgb( 255, 220, 0 ) );
		poly->setWidth( 4 );
	}
	else if ( isSelected( poly ) )
	{
		poly->setColor( ccColor::Rgb( 60, 180, 255 ) ); // vybraná čára
		poly->setWidth( 3 );
	}
	else if ( isText( poly ) )
	{
		poly->setColor( ccColor::Rgb( 255, 235, 140 ) );
		poly->setWidth( 1 );
	}
	else
	{
		int cat;
		bool hid;
		lineAttrs( poly, cat, hid );
		poly->setColor( toRgb( Drawing::displayColor( cat, hid ) ) );
		poly->setWidth( 2 );
	}
	poly->prepareDisplayForRefresh();
}

void DrawTool::applyLook( ccPolyline* poly, int cat, bool hidden )
{
	if ( !poly || isText( poly ) )
	{
		return;
	}
	poly->setMetaData( Drawing::MetaCategory, cat );
	poly->setMetaData( Drawing::MetaHidden, hidden );
	poly->setName( Drawing::categoryName( cat ) + ( isArc( poly ) ? QStringLiteral( " – oblouk" ) : QString() ) + ( hidden ? QStringLiteral( " (zakrytá)" ) : QString() ) );
	if ( auto* rl = dynamic_cast<RezLine*>( poly ) )
	{
		rl->setDashed( hidden );
	}
	restoreLook( poly );
}

void DrawTool::pruneSelection()
{
	std::vector<ccPolyline*> keep;
	for ( ccPolyline* p : m_selSet )
	{
		if ( lineExists( p ) )
		{
			keep.push_back( p );
		}
	}
	m_selSet.swap( keep );
}

void DrawTool::setSelection( const std::vector<ccPolyline*>& sel )
{
	std::vector<ccPolyline*> old;
	old.swap( m_selSet );
	m_selSet = sel;
	for ( ccPolyline* p : old )
	{
		if ( lineExists( p ) )
		{
			restoreLook( p );
		}
	}
	for ( ccPolyline* p : m_selSet )
	{
		restoreLook( p );
	}
	redraw3D();
	updateTip();
}

void DrawTool::toggleSelected( ccPolyline* poly )
{
	std::vector<ccPolyline*> sel = m_selSet;
	auto it = std::find( sel.begin(), sel.end(), poly );
	if ( it != sel.end() )
	{
		sel.erase( it );
	}
	else
	{
		sel.push_back( poly );
	}
	setSelection( sel );
}

void DrawTool::selectAll()
{
	std::vector<ccPolyline*> sel;
	for ( const Segment& s : collectSegments() )
	{
		if ( !s.ref && std::find( sel.begin(), sel.end(), s.poly ) == sel.end() )
		{
			sel.push_back( s.poly );
		}
	}
	if ( m_mode != Mode::Select && m_mode != Mode::Move && m_mode != Mode::Copy )
	{
		m_mode = Mode::Select;
		resetTool();
		Q_EMIT modeChanged( m_mode );
	}
	setSelection( sel );
	updateStatus();
}

void DrawTool::deleteSelection()
{
	pruneSelection();
	if ( m_selSet.empty() )
	{
		return;
	}
	std::vector<ccPolyline*> sel = m_selSet;
	setSelection( {} );
	UndoItem u;
	for ( ccPolyline* p : sel )
	{
		removeLine( p, &u );
	}
	pushUndo( std::move( u ) );
	redraw3D();
	updateTip();
	updateStatus();
}

void DrawTool::selectWindow( const QPoint& p0, const QPoint& p1, bool add )
{
	CCVector2d a;
	CCVector2d b;
	CCVector2d c;
	CCVector2d d;
	// rohy okna v rovině kresby (pohled může být natočený)
	if ( !screenToLocal( p0, a ) || !screenToLocal( QPoint( p1.x(), p0.y() ), b ) || !screenToLocal( p1, c ) || !screenToLocal( QPoint( p0.x(), p1.y() ), d ) )
	{
		return;
	}
	const bool inside = p1.x() >= p0.x(); // zleva = jen uvnitř, zprava = i křížem
	const CCVector2d poly[4] = { a, b, c, d };
	auto pointIn = [&poly]( const CCVector2d& P ) -> bool
	{
		// konvexní čtyřúhelník: znaménko vektorových součinů
		int pos = 0;
		int neg = 0;
		for ( int i = 0; i < 4; ++i )
		{
			const CCVector2d& A = poly[i];
			const CCVector2d& B = poly[( i + 1 ) % 4];
			const double cr = ( B.x - A.x ) * ( P.y - A.y ) - ( B.y - A.y ) * ( P.x - A.x );
			if ( cr > 0 )
				++pos;
			else if ( cr < 0 )
				++neg;
		}
		return pos == 0 || neg == 0;
	};
	std::vector<ccPolyline*> sel = add ? m_selSet : std::vector<ccPolyline*>();
	// po prvcích: uvnitř = všechny úseky uvnitř, křížem = kterýkoli úsek zasažen
	std::vector<ccPolyline*> order;
	std::vector<std::pair<bool, bool>> state; //!< (všechny uvnitř, něco zasaženo)
	for ( const Segment& s : collectSegments() )
	{
		if ( s.ref || std::find( sel.begin(), sel.end(), s.poly ) != sel.end() )
		{
			continue;
		}
		size_t idx = std::find( order.begin(), order.end(), s.poly ) - order.begin();
		if ( idx == order.size() )
		{
			order.push_back( s.poly );
			state.push_back( { true, false } );
		}
		const bool ia = pointIn( s.a );
		const bool ib = pointIn( s.b );
		bool hit = ia || ib;
		for ( int i = 0; i < 4 && !hit; ++i )
		{
			CCVector2d I;
			hit = segmentIntersection( s.a, s.b, poly[i], poly[( i + 1 ) % 4], I );
		}
		state[idx].first = state[idx].first && ia && ib;
		state[idx].second = state[idx].second || hit;
	}
	for ( size_t i = 0; i < order.size(); ++i )
	{
		if ( inside ? state[i].first : state[i].second )
		{
			sel.push_back( order[i] );
		}
	}
	setSelection( sel );
}

bool DrawTool::pickHandle( const QPoint& pos, ccPolyline*& poly, int& end ) const
{
	CCVector2d P;
	if ( m_selSet.empty() || !screenToLocal( pos, P ) )
	{
		return false;
	}
	const double tol = 6.0 * pixelSize();
	double best = tol;
	bool found = false;
	for ( ccPolyline* p : m_selSet )
	{
		CCVector2d a;
		CCVector2d b;
		if ( isArc( p ) || !lineEnds( p, a, b ) )
		{
			continue;
		}
		const double da = dist( a, P );
		const double db = dist( b, P );
		if ( da < best )
		{
			best = da;
			poly = p;
			end = 0;
			found = true;
		}
		if ( db < best )
		{
			best = db;
			poly = p;
			end = 1;
			found = true;
		}
	}
	return found;
}

// ---------------------------------------------------------------- body a texty

bool DrawTool::isText( const ccHObject* obj )
{
	return obj && obj->hasMetaData( "qRezy.text" );
}

ccPolyline* DrawTool::addText( const CCVector2d& P, const QString& text, UndoItem* undo )
{
	ccHObject* group = drawingGroup( true );
	if ( !group )
	{
		return nullptr;
	}
	// text = „čára“ se dvěma stejnými body (uloží se s projektem, jde přesunout/kopírovat/smazat)
	auto* verts = new ccPointCloud( "vertices" );
	verts->reserve( 2 );
	verts->addPoint( m_frame.to3D( P ).toFloat() );
	verts->addPoint( m_frame.to3D( P ).toFloat() );
	verts->setEnabled( false );
	verts->setGlobalShift( m_shift );
	verts->setGlobalScale( m_scale );
	auto* poly = new ccPolyline( verts );
	poly->addChild( verts );
	poly->addPointIndex( 0, 2 );
	poly->setGlobalShift( m_shift );
	poly->setGlobalScale( m_scale );
	poly->setColor( ccColor::Rgb( 255, 235, 140 ) );
	poly->showColors( true );
	poly->setName( QStringLiteral( "Text: " ) + text );
	poly->setMetaData( "qRezy.text", text );
	poly->setMetaData( Drawing::MetaView, m_frame.viewId );
	poly->setMetaData( "qRezy.fx", m_frame.N.x * m_frame.back );
	poly->setMetaData( "qRezy.fy", m_frame.N.y * m_frame.back );
	poly->setMetaData( "qRezy.fz", m_frame.N.z * m_frame.back );
	group->addChild( poly );
	m_app->addToDB( poly, false, false, false, false );
	if ( undo )
	{
		Action act;
		act.kind = Action::Add;
		act.id = poly->getUniqueID();
		undo->push_back( act );
	}
	return poly;
}

ccPolyline* DrawTool::addTextAt( const CCVector2d& P, const QString& text, double angle, int level )
{
	UndoItem u;
	ccPolyline* poly = addText( P, text, &u );
	if ( poly )
	{
		poly->setMetaData( "qRezy.textAng", angle );
		if ( level > 0 )
		{
			poly->setMetaData( "qRezy.textLevel", level );
		}
		pushUndo( std::move( u ) );
		redraw2D();
	}
	return poly;
}

std::vector<ccPolyline*> DrawTool::rotTargets() const
{
	std::vector<ccPolyline*> t = m_selSet;
	if ( t.empty() && m_sel )
	{
		t.push_back( m_sel );
	}
	return t;
}

double DrawTool::rotAngle() const
{
	const CCVector2d r = m_rotRef - m_base;
	const CCVector2d c = m_cursor - m_base;
	if ( r.norm() < 1e-12 || c.norm() < 1e-12 )
	{
		return 0.0;
	}
	return std::atan2( c.y, c.x ) - std::atan2( r.y, r.x );
}

void DrawTool::rotateItem( ccPolyline* poly, const CCVector2d& C, double ang, UndoItem* undo )
{
	if ( !lineExists( poly ) )
	{
		return;
	}
	const double ca = std::cos( ang );
	const double sa = std::sin( ang );
	auto rot = [&]( const CCVector2d& P ) { const CCVector2d d = P - C; return C + CCVector2d( d.x * ca - d.y * sa, d.x * sa + d.y * ca ); };
	if ( isText( poly ) )
	{
		const CCVector2d P = m_frame.toUV( CCVector3d::fromArray( poly->getPoint( 0 )->u ) );
		if ( isKota( poly ) )
		{
			translateItem( poly, rot( P ) - P, false, undo ); // kóta zůstává svislá, jen se posune hrot
			return;
		}
		// text: nová poloha a natočení (přidá se k dosavadnímu)
		const double a0 = poly->hasMetaData( "qRezy.textAng" ) ? poly->getMetaData( "qRezy.textAng" ).toDouble() : 0.0;
		if ( ccPolyline* n = addText( rot( P ), poly->getMetaData( "qRezy.text" ).toString(), undo ) )
		{
			for ( const char* key : { "qRezy.textLevel", "qRezy.textH", "qRezy.imported" } )
			{
				if ( poly->hasMetaData( key ) )
				{
					n->setMetaData( key, poly->getMetaData( key ) );
				}
			}
			n->setMetaData( "qRezy.textAng", a0 + ang );
			removeLine( poly, undo );
		}
		return;
	}
	std::vector<CCVector2d> pts = polyPoints( poly );
	for ( CCVector2d& P : pts )
	{
		P = rot( P );
	}
	setPolyPoints( poly, pts, undo );
}

void DrawTool::startPlaceText( const QString& text, double angle, int level )
{
	m_placeText = text;
	m_placeAng = angle;
	m_placeLevel = level;
	setMode( Mode::PlaceText );
}

CCVector2d DrawTool::placeOrigin( const CCVector2d& c ) const
{
	// kurzor = střed spodní hrany textu
	const double h = QSettings().value( QStringLiteral( "qRezy/userTextHeight" ), 0.15 ).toDouble() * m_scale;
	const double w = 0.8 * h * m_placeText.size();
	return c - CCVector2d( std::cos( m_placeAng ), std::sin( m_placeAng ) ) * ( w / 2.0 );
}

// ---------------------------------------------------------------- rozdělit řeznou čarou

std::vector<std::pair<ccPolyline*, CCVector2d>> DrawTool::breakLineHits( const CCVector2d& A, const CCVector2d& B ) const
{
	std::vector<std::pair<ccPolyline*, CCVector2d>> hits;
	const double L = dist( A, B );
	if ( L < 1e-9 )
	{
		return hits;
	}
	const double minLen = 2.0 * pixelSize();
	for ( const Segment& sg : collectSegments() )
	{
		if ( sg.ref || sg.arc || !sg.poly || isText( sg.poly ) || sg.poly->size() != 2 )
		{
			continue;
		}
		CCVector2d I;
		if ( !lineIntersection( A, B, sg.a, sg.b, I ) )
		{
			continue;
		}
		const double u = paramOn( A, B, I );
		const double t = paramOn( sg.a, sg.b, I );
		const double len = dist( sg.a, sg.b );
		if ( u < 0.0 || u > 1.0 || len < 1e-9 )
		{
			continue;
		}
		if ( t * len <= minLen || ( 1.0 - t ) * len <= minLen )
		{
			continue; // na konci čáry se nic nedělí
		}
		hits.emplace_back( sg.poly, I );
	}
	return hits;
}

// ---------------------------------------------------------------- výšková kóta („kačena“)

bool DrawTool::isKota( const ccHObject* obj )
{
	return obj && obj->hasMetaData( "qRezy.kota" );
}

void DrawTool::setKotaFormat( int f )
{
	m_kotaFmt = std::max( 0, std::min( 2, f ) );
	QSettings().setValue( QStringLiteral( "qRezy/kotaFormat" ), m_kotaFmt );
	updateTip();
	updateStatus();
}

void DrawTool::setKotaScale( int n )
{
	m_kotaScale = std::max( 1, n );
	QSettings().setValue( QStringLiteral( "qRezy/kotaScale" ), m_kotaScale );
	updateTip();
}

double DrawTool::heightOf( const CCVector3d& local ) const
{
	return local.z / m_scale - m_shift.z;
}

DrawTool::KotaGeom DrawTool::kotaGeom( const CCVector3d& tip, const CCVector3d& U, const CCVector3d& V, int side, int fmt, int scale ) const
{
	// rozměry v mm na papíře (podle výkresu uživatele v 1:70: čára 0,70 m, text 0,18 m)
	const double f = scale / 1000.0 * m_scale; // mm → lokální jednotky
	const double s = side < 0 ? -1.0 : 1.0;
	auto P = [&]( double x, double y ) { return tip + U * ( x * f ) + V * ( y * f ); };
	KotaGeom g;
	const double halfBase = 5.05;
	const double arm = 2.65;
	const double stem = 5.9;
	const double shelf = 8.05;
	const double th = 2.5;
	const double gap = 0.7;
	g.segs = { P( -halfBase, 0 ), P( halfBase, 0 ),
	           P( -arm, arm ), tip, tip, P( arm, arm ),
	           tip, P( 0, stem ), P( 0, stem ), P( s * shelf, stem ) };
	g.textH = th * f;
	const double x0 = s > 0 ? 0.25 : -shelf;
	const double h = heightOf( tip );
	const double zero = m_zeroProvider ? m_zeroProvider() : 0.0;
	const QString bpv = QString::number( h, 'f', 3 );
	const double r = h - zero;
	const QString loc = std::abs( r ) < 0.0005 ? QStringLiteral( "±0.000" ) : ( ( r > 0 ? QStringLiteral( "+" ) : QStringLiteral( "-" ) ) + QString::number( std::abs( r ), 'f', 3 ) );
	if ( fmt == 0 )
	{
		g.texts << bpv;
		g.textPos.push_back( P( x0, stem + gap ) );
	}
	else if ( fmt == 1 )
	{
		g.texts << loc;
		g.textPos.push_back( P( x0, stem + gap ) );
	}
	else
	{
		g.texts << loc << bpv; // místní nad poličkou, Bpv pod ní
		g.textPos.push_back( P( x0, stem + gap ) );
		g.textPos.push_back( P( x0, stem - gap - th ) );
	}
	return g;
}

DrawTool::KotaGeom DrawTool::kotaGeom( const ccPolyline* poly ) const
{
	const CCVector3d tip = CCVector3d::fromArray( poly->getPoint( 0 )->u );
	const CCVector3d U( poly->getMetaData( "qRezy.kux" ).toDouble(), poly->getMetaData( "qRezy.kuy" ).toDouble(), poly->getMetaData( "qRezy.kuz" ).toDouble() );
	const CCVector3d V( poly->getMetaData( "qRezy.kvx" ).toDouble(), poly->getMetaData( "qRezy.kvy" ).toDouble(), poly->getMetaData( "qRezy.kvz" ).toDouble() );
	return kotaGeom( tip, U, V, poly->getMetaData( "qRezy.kotaSide" ).toInt(), poly->getMetaData( "qRezy.kotaFmt" ).toInt(), std::max( 1, poly->getMetaData( "qRezy.kotaScale" ).toInt() ) );
}

ccPolyline* DrawTool::addKota( const CCVector2d& P, int side, int fmt, int scale, UndoItem* undo, const ccPolyline* like )
{
	ccPolyline* poly = addText( P, QString(), undo );
	if ( !poly )
	{
		return nullptr;
	}
	poly->setName( QStringLiteral( "Kóta" ) );
	poly->setMetaData( "qRezy.kota", true );
	poly->setMetaData( "qRezy.kotaSide", side < 0 ? -1 : 1 );
	poly->setMetaData( "qRezy.kotaFmt", fmt );
	poly->setMetaData( "qRezy.kotaScale", scale );
	CCVector3d U = m_frame.U;
	CCVector3d V = m_frame.V;
	if ( like && isKota( like ) )
	{
		U = CCVector3d( like->getMetaData( "qRezy.kux" ).toDouble(), like->getMetaData( "qRezy.kuy" ).toDouble(), like->getMetaData( "qRezy.kuz" ).toDouble() );
		V = CCVector3d( like->getMetaData( "qRezy.kvx" ).toDouble(), like->getMetaData( "qRezy.kvy" ).toDouble(), like->getMetaData( "qRezy.kvz" ).toDouble() );
	}
	poly->setMetaData( "qRezy.kux", U.x );
	poly->setMetaData( "qRezy.kuy", U.y );
	poly->setMetaData( "qRezy.kuz", U.z );
	poly->setMetaData( "qRezy.kvx", V.x );
	poly->setMetaData( "qRezy.kvy", V.y );
	poly->setMetaData( "qRezy.kvz", V.z );
	return poly;
}

void DrawTool::setReference( int viewId, bool snap )
{
	m_refView = viewId;
	m_refSnap = snap;
	updateLineVisibility();
	redraw3D();
}

void zoomAtCursor( ccGLWindowInterface* win, const QPoint& pos, float wheelDeltaDeg )
{
	if ( !win )
	{
		return;
	}
	ccGLCameraParameters cam;
	win->getGLCameraParameters( cam );
	const QPointF g = win->toCornerGLCoordinates( pos.x(), pos.y() );
	// hloubka středu otáčení (v pravoúhlém pohledu na ní nezáleží)
	double depth = 0.5;
	CCVector3d Sp;
	if ( cam.project( win->getViewportParameters().getPivotPoint(), Sp ) && Sp.z > 0.0 && Sp.z < 1.0 )
	{
		depth = Sp.z;
	}
	CCVector3d P;
	const bool ok = cam.unproject( CCVector3d( g.x(), g.y(), depth ), P );
	// rychlost: násobek přiblížení na jeden zub kolečka (15°), nastavitelné
	const double step = std::max( 1.02, std::min( 3.0, QSettings().value( QStringLiteral( "qRezy/zoomStep" ), 1.25 ).toDouble() ) );
	const ccViewportParameters& vp = win->getViewportParameters();
	if ( !vp.perspectiveView )
	{
		const double fd = vp.getFocalDistance();
		const double f = std::pow( step, wheelDeltaDeg / 15.0 );
		CCVector3d v( 0.0, 0.0, -( fd - fd / f ) );
		win->moveCamera( v );
	}
	else
	{
		// perspektiva: CC krok × poměr k jeho výchozím ~5 %
		win->onWheelEvent( static_cast<float>( wheelDeltaDeg * ( step - 1.0 ) / 0.05 ) );
	}
	if ( !ok )
	{
		return;
	}
	win->getGLCameraParameters( cam );
	CCVector3d S;
	if ( !cam.project( P, S ) )
	{
		return;
	}
	// posun, aby bod P byl zase pod kurzorem
	const double dpr = win->getDevicePixelRatio();
	const double dx = ( g.x() - S.x ) / dpr;
	const double dy = -( g.y() - S.y ) / dpr;
	const double px = win->computeActualPixelSize() * dpr;
	CCVector3d u( dx * px, -dy * px, 0.0 );
	if ( win->getViewportParameters().objectCenteredView )
	{
		u = -u;
	}
	win->moveCamera( u );
	win->redraw();
}

void DrawTool::setShowPoints( bool on )
{
	m_showPoints = on;
	redraw2D();
}

void DrawTool::setShowHeights( bool on, int fmt )
{
	m_showHeights = on;
	m_heightFmt = fmt;
	QSettings st;
	st.setValue( QStringLiteral( "qRezy/ptHeights" ), on );
	st.setValue( QStringLiteral( "qRezy/ptHeightFmt" ), fmt );
	redraw2D();
}

void DrawTool::setPinVisible( bool on )
{
	m_pinVisible = on;
	refreshLayer();
	redraw2D();
}

void DrawTool::setShowCodes( bool on )
{
	m_showCodes = on;
	redraw2D();
}

std::vector<DrawTool::VisPoint> DrawTool::visiblePoints() const
{
	if ( !m_showPoints )
	{
		return {};
	}
	return framePoints( m_frame );
}

std::vector<std::pair<CCVector3d, QString>> DrawTool::pointsInFrame( const DrawFrame& f ) const
{
	std::vector<std::pair<CCVector3d, QString>> out;
	if ( f.plan )
	{
		return out;
	}
	std::vector<CCVector3d> local;
	const std::vector<VisPoint> vps = framePoints( f, &local );
	for ( size_t i = 0; i < vps.size(); ++i )
	{
		out.push_back( { local[i], vps[i].name } );
	}
	return out;
}

std::vector<DrawTool::VisPoint> DrawTool::framePoints( const DrawFrame& frame, std::vector<CCVector3d>* local ) const
{
	std::vector<VisPoint> out;
	ccHObject* group = const_cast<DrawTool*>( this )->drawingGroup( false );
	if ( !group )
	{
		return out;
	}
	for ( unsigned i = 0; i < group->getChildrenNumber(); ++i )
	{
		ccHObject* ch = group->getChild( i );
		if ( !ch->isA( CC_TYPES::POINT_CLOUD ) || !ch->hasMetaData( "qRezy.points" ) || !ch->isEnabled() )
		{
			continue;
		}
		auto* pc = static_cast<ccPointCloud*>( ch );
		const QStringList names = pc->getMetaData( "qRezy.names" ).toStringList();
		const QStringList codes = pc->getMetaData( "qRezy.codes" ).toStringList();
		const QString hasZ = pc->getMetaData( "qRezy.hasZ" ).toString();
		for ( unsigned k = 0; k < pc->size(); ++k )
		{
			const CCVector3d P = CCVector3d::fromArray( pc->getPoint( k )->u );
			const bool z = ( static_cast<int>( k ) < hasZ.size() ) && hasZ[static_cast<int>( k )] == QChar( '1' );
			if ( !frame.plan )
			{
				// řez / pohled: jen body s výškou v dosahu roviny (sklopí se do ní)
				if ( !z )
				{
					continue;
				}
				const double sN = ( P - frame.O ).dot( frame.N );
				if ( sN < frame.ptMin || sN > frame.ptMax )
				{
					continue;
				}
			}
			if ( local )
			{
				local->push_back( P );
			}
			VisPoint vp;
			vp.uv = frame.toUV( P );
			vp.name = ( static_cast<int>( k ) < names.size() ) ? names[static_cast<int>( k )] : QString();
			vp.code = ( static_cast<int>( k ) < codes.size() ) ? codes[static_cast<int>( k )] : QString();
			vp.hasZ = z;
			vp.zLocal = P.z;
			out.push_back( vp );
		}
	}
	return out;
}

void DrawTool::refreshLayer()
{
	if ( !m_layer )
	{
		return;
	}
	m_layer->items.clear();
	if ( m_hasPin && m_pinVisible )
	{
		RezLayer::Item pi;
		pi.P = m_pin3;
		pi.pin = true;
		m_layer->items.push_back( pi );
	}
	// směr „nahoru“ na obrazovce (půdorys může být natočený)
	CCVector3d up = m_frame.V;
	if ( m_frame.plan )
	{
		const double a = viewAngle();
		up = CCVector3d( -std::sin( a ), std::cos( a ), 0.0 );
	}
	QSettings st;
	const QColor cm = st.value( QStringLiteral( "qRezy/ptColMarker" ), QColor( 255, 90, 255 ) ).value<QColor>();
	const QColor cn = st.value( QStringLiteral( "qRezy/ptColName" ), QColor( 255, 235, 0 ) ).value<QColor>();
	const QColor cc = st.value( QStringLiteral( "qRezy/ptColCode" ), QColor( 0, 230, 255 ) ).value<QColor>();
	const bool bkg = st.value( QStringLiteral( "qRezy/ptBkg" ), true ).toBool();
	const QColor ch3 = st.value( QStringLiteral( "qRezy/ptColHeight" ), QColor( 150, 255, 150 ) ).value<QColor>();
	for ( const VisPoint& vp : visiblePoints() )
	{
		RezLayer::Item it;
		it.P = m_frame.to3D( vp.uv );
		it.up = up;
		it.marker = true;
		it.markerColor = toRgb( cm );
		it.color = toRgb( cn );
		it.text = vp.name;
		if ( m_showCodes && !vp.code.isEmpty() )
		{
			it.text2 = vp.code;
			it.color2 = toRgb( cc );
		}
		if ( m_showHeights && vp.hasZ )
		{
			const double h = heightOf( CCVector3d( 0, 0, vp.zLocal ) );
			if ( m_heightFmt == 1 )
			{
				const double r = h - ( m_zeroProvider ? m_zeroProvider() : 0.0 );
				it.text3 = std::abs( r ) < 0.0005 ? QStringLiteral( "±0.000" ) : ( ( r > 0 ? QStringLiteral( "+" ) : QStringLiteral( "-" ) ) + QString::number( std::abs( r ), 'f', 3 ) );
			}
			else
			{
				it.text3 = QString::number( h, 'f', 3 );
			}
			it.color3 = toRgb( ch3 );
		}
		it.background = bkg;
		m_layer->items.push_back( it );
	}
	// texty aktuálního pohledu
	ccHObject* group = drawingGroup( false );
	if ( group )
	{
		const double h = QSettings().value( QStringLiteral( "qRezy/userTextHeight" ), 0.15 ).toDouble() * m_scale;
		for ( unsigned i = 0; i < group->getChildrenNumber(); ++i )
		{
			ccHObject* ch = group->getChild( i );
			if ( !ch->isA( CC_TYPES::POLY_LINE ) || !isText( ch ) )
			{
				continue;
			}
			const int view = ch->getMetaData( Drawing::MetaView ).toInt();
			auto* poly = static_cast<ccPolyline*>( ch );
			if ( view != m_frame.viewId || poly->size() == 0 )
			{
				continue;
			}
			if ( isKota( poly ) )
			{
				const KotaGeom g = kotaGeom( poly );
				const ccColor::Rgb kc = ( ch == m_sel || isSelected( poly ) ) ? ccColor::Rgb( 255, 220, 0 ) : ccColor::Rgb( 0, 200, 200 );
				RezLayer::Item li;
				li.segs = g.segs;
				li.color = kc;
				m_layer->items.push_back( li );
				for ( int k = 0; k < g.texts.size(); ++k )
				{
					RezLayer::Item ti;
					ti.P = g.textPos[static_cast<size_t>( k )]; // levý dolní roh (RezLayer posune o výšku písma)
					ti.up = up;
					ti.height = g.textH;
					ti.text = g.texts[k];
					ti.color = kc;
					m_layer->items.push_back( ti );
				}
				continue;
			}
			RezLayer::Item it;
			it.P = CCVector3d::fromArray( poly->getPoint( 0 )->u ); // levý dolní roh jako v DGN
			if ( poly->hasMetaData( "qRezy.textAng" ) )
			{
				const double a = poly->getMetaData( "qRezy.textAng" ).toDouble();
				it.dir = m_frame.U * std::cos( a ) + m_frame.V * std::sin( a );
			}
			it.up = up;
			it.height = h;
			it.text = ch->getMetaData( "qRezy.text" ).toString();
			it.color = ( ch == m_sel || isSelected( poly ) ) ? ccColor::Rgb( 255, 220, 0 ) : ccColor::Rgb( 255, 235, 140 );
			m_layer->items.push_back( it );
		}
	}
}

bool DrawTool::importPoints( const QString& path, QString& message )
{
	ccHObject* root = m_app ? m_app->dbRootObject() : nullptr;
	auto* cloud = root ? dynamic_cast<ccGenericPointCloud*>( root->find( m_cloudID ) ) : nullptr;
	if ( !cloud )
	{
		message = QStringLiteral( "Nejdřív vyberte mračno." );
		return false;
	}
	QFile f( path );
	if ( !f.open( QIODevice::ReadOnly | QIODevice::Text ) )
	{
		message = QStringLiteral( "Soubor nelze otevřít." );
		return false;
	}
	struct Rec
	{
		QString name;
		double a = 0;
		double b = 0;
		double z = 0;
		bool hasZ = false;
		QString code;
	};
	std::vector<Rec> recs;
	QTextStream ts( &f );
	ts.setCodec( "Windows-1250" );
	while ( !ts.atEnd() )
	{
		const QString line = ts.readLine().trimmed();
		if ( line.isEmpty() || line.startsWith( '#' ) )
		{
			continue;
		}
		QStringList tok = line.split( QRegExp( QStringLiteral( "[\\s;\\t]+" ) ), QString::SkipEmptyParts );
		if ( tok.size() < 3 )
		{
			continue;
		}
		Rec r;
		r.name = tok[0];
		std::vector<double> nums;
		QStringList rest;
		for ( int i = 1; i < tok.size(); ++i )
		{
			bool ok = false;
			const double v = QString( tok[i] ).replace( ',', '.' ).toDouble( &ok );
			if ( ok && rest.isEmpty() && nums.size() < 3 )
			{
				nums.push_back( v );
			}
			else
			{
				rest << tok[i];
			}
		}
		if ( nums.size() < 2 )
		{
			continue;
		}
		r.a = nums[0];
		r.b = nums[1];
		if ( nums.size() >= 3 && std::abs( nums[2] ) > 1e-6 )
		{
			r.z = nums[2];
			r.hasZ = true;
		}
		r.code = rest.join( ' ' );
		recs.push_back( r );
	}
	if ( recs.empty() )
	{
		message = QStringLiteral( "V souboru nejsou body (očekává se: číslo Y X [Z] [kód])." );
		return false;
	}

	// orientace souřadnic S-JTSK v mračnu: (−Y, −X), (Y, X), …  – vybere se ta, kde body padnou do mračna
	const ccBBox bb = cloud->getOwnBB();
	const CCVector3d gmin = cloud->toGlobal3d( bb.minCorner() );
	const CCVector3d gmax = cloud->toGlobal3d( bb.maxCorner() );
	const double margin = 50.0;
	auto mapXY = [&]( int variant, const Rec& r ) -> CCVector2d
	{
		switch ( variant )
		{
		case 0:
			return CCVector2d( -r.a, -r.b );
		case 1:
			return CCVector2d( r.a, r.b );
		case 2:
			return CCVector2d( -r.b, -r.a );
		default:
			return CCVector2d( r.b, r.a );
		}
	};
	int best = 0;
	int bestCount = -1;
	for ( int v = 0; v < 4; ++v )
	{
		int count = 0;
		for ( const Rec& r : recs )
		{
			const CCVector2d P = mapXY( v, r );
			if ( P.x > gmin.x - margin && P.x < gmax.x + margin && P.y > gmin.y - margin && P.y < gmax.y + margin )
			{
				++count;
			}
		}
		if ( count > bestCount )
		{
			bestCount = count;
			best = v;
		}
	}

	ccHObject* group = drawingGroup( true );
	auto* pc = new ccPointCloud( QStringLiteral( "Body: " ) + QFileInfo( path ).fileName() );
	pc->reserve( static_cast<unsigned>( recs.size() ) );
	QStringList names;
	QStringList codes;
	QString hasZ;
	const double zMid = ( gmin.z + gmax.z ) / 2.0;
	for ( const Rec& r : recs )
	{
		const CCVector2d P = mapXY( best, r );
		const CCVector3d L = cloud->toLocal3d( CCVector3d( P.x, P.y, r.hasZ ? r.z : zMid ) );
		pc->addPoint( L.toFloat() );
		names << r.name;
		codes << r.code;
		hasZ += r.hasZ ? QChar( '1' ) : QChar( '0' );
	}
	pc->setGlobalShift( cloud->getGlobalShift() );
	pc->setGlobalScale( cloud->getGlobalScale() );
	pc->setMetaData( "qRezy.points", true );
	pc->setMetaData( "qRezy.names", names );
	pc->setMetaData( "qRezy.codes", codes );
	pc->setMetaData( "qRezy.hasZ", hasZ );
	pc->setVisible( false ); // kreslí je vrstva popisků
	pc->setEnabled( true );
	group->addChild( pc );
	m_app->addToDB( pc, false, false, false, false );

	const int noZ = static_cast<int>( hasZ.count( QChar( '0' ) ) );
	message = QStringLiteral( "Načteno %1 bodů" ).arg( recs.size() );
	if ( noZ > 0 )
	{
		message += QStringLiteral( " (%1 bez výšky – jen v půdorysu)" ).arg( noZ );
	}
	if ( bestCount < static_cast<int>( recs.size() ) / 2 )
	{
		message += QStringLiteral( ". POZOR: většina bodů leží mimo mračno – je mračno v S-JTSK?" );
	}
	m_showPoints = true;
	redraw2D();
	return true;
}

std::vector<DgnV7::Element> DrawTool::exportElements( const Drawing::SymbologyTable& table, const ExportMap& map, int textLevel, double textHeight ) const
{
	QTextCodec* codec = QTextCodec::codecForName( "Windows-1250" );
	std::vector<DgnV7::Element> out;
	// barva textů podle hladiny (jako v zakládacím seedu: KOTY 16 = 135, VYSKY 9 = 162); qRezy/textLevelColors „hladina=barva;…“
	QMap<int, int> textColors;
	for ( const QString& kv : QSettings().value( QStringLiteral( "qRezy/textLevelColors" ), QStringLiteral( "16=135;9=162" ) ).toString().split( ';', Qt::SkipEmptyParts ) )
	{
		const QStringList p = kv.split( '=' );
		if ( p.size() == 2 )
		{
			textColors.insert( p[0].trimmed().toInt(), p[1].trimmed().toInt() );
		}
	}
	ccHObject* group = const_cast<DrawTool*>( this )->drawingGroup( false );
	if ( !group )
	{
		return out;
	}
	for ( unsigned i = 0; i < group->getChildrenNumber(); ++i )
	{
		ccHObject* ch = group->getChild( i );
		if ( !ch->isA( CC_TYPES::POLY_LINE ) )
		{
			continue;
		}
		auto* poly = static_cast<ccPolyline*>( ch );
		if ( m_exportSkipImported && poly->hasMetaData( "qRezy.imported" ) )
		{
			continue; // jen doplněné prvky
		}
		const int view = poly->hasMetaData( Drawing::MetaView ) ? poly->getMetaData( Drawing::MetaView ).toInt() : 0;
		if ( view >= 0 && isKota( poly ) && poly->size() > 0 )
		{
			const KotaGeom g = kotaGeom( poly );
			DgnV7::Symbology ks;
			ks.level = m_kotaLevel;
			ks.color = m_kotaColor;
			bool ok = true;
			std::vector<DgnV7::Point2> P2( g.segs.size() );
			for ( size_t k = 0; k < g.segs.size() && ok; ++k )
			{
				ok = map( view, g.segs[k], P2[k] );
			}
			if ( !ok )
			{
				continue;
			}
			// základní čára, šipka (lomená), svislice s poličkou (lomená)
			DgnV7::Element base;
			base.sym = ks;
			base.pts = { P2[0], P2[1] };
			out.push_back( base );
			DgnV7::Element arrow;
			arrow.sym = ks;
			arrow.pts = { P2[2], P2[3], P2[5] };
			out.push_back( arrow );
			DgnV7::Element stem;
			stem.sym = ks;
			stem.pts = { P2[6], P2[7], P2[9] };
			out.push_back( stem );
			// výška textu v metrech výkresu: z mapování dvou bodů svislice
			const double stemLen = std::hypot( P2[7].x - P2[6].x, P2[7].y - P2[6].y );
			const double stemLocal = ( g.segs[7] - g.segs[6] ).norm();
			const double th = stemLocal > 0 ? g.textH * stemLen / stemLocal : 0.0;
			for ( int k = 0; k < g.texts.size(); ++k )
			{
				DgnV7::Element t;
				t.isText = true;
				t.sym = ks;
				t.textHeight = th;
				DgnV7::Point2 TP;
				if ( !map( view, g.textPos[static_cast<size_t>( k )], TP ) )
				{
					continue;
				}
				t.pts.push_back( TP );
				const QByteArray bytes = codec ? codec->fromUnicode( g.texts[k] ) : g.texts[k].toLatin1();
				t.text = std::string( bytes.constData(), static_cast<size_t>( bytes.size() ) );
				out.push_back( t );
			}
			continue;
		}
		if ( view >= 0 && isText( poly ) && poly->size() > 0 )
		{
			DgnV7::Element t;
			t.isText = true;
			t.sym.level = poly->hasMetaData( "qRezy.textLevel" ) ? poly->getMetaData( "qRezy.textLevel" ).toInt() : textLevel;
			t.sym.color = textColors.value( t.sym.level, 0 );
			t.textHeight = poly->hasMetaData( "qRezy.textH" ) ? poly->getMetaData( "qRezy.textH" ).toDouble() : textHeight;
			DgnV7::Point2 P;
			if ( map( view, CCVector3d::fromArray( poly->getPoint( 0 )->u ), P ) )
			{
				t.pts.push_back( P );
				const QString str = poly->getMetaData( "qRezy.text" ).toString();
				const QByteArray bytes = codec ? codec->fromUnicode( str ) : str.toLatin1();
				t.text = std::string( bytes.constData(), static_cast<size_t>( bytes.size() ) );
				if ( poly->hasMetaData( "qRezy.textAng" ) )
				{
					t.textAngleDeg = poly->getMetaData( "qRezy.textAng" ).toDouble() * 57.29577951308232;
				}
				out.push_back( t );
			}
			continue;
		}
		if ( view < 0 || !poly->hasMetaData( Drawing::MetaCategory ) )
		{
			continue; // definice řezu apod.
		}
		const int cat = poly->getMetaData( Drawing::MetaCategory ).toInt();
		const bool hid = poly->getMetaData( Drawing::MetaHidden ).toBool();

		DgnV7::Element e;
		e.sym = table.get( cat, hid );
		bool ok = true;
		for ( unsigned k = 0; k < poly->size() && ok; ++k )
		{
			DgnV7::Point2 P;
			ok = map( view, CCVector3d::fromArray( poly->getPoint( k )->u ), P );
			e.pts.push_back( P );
		}
		if ( ok && isArc( poly ) && e.pts.size() >= 3 )
		{
			// skutečný oblouk (typ 16) ze začátku, středu a konce
			const DgnV7::Point2& S = e.pts.front();
			const DgnV7::Point2& M = e.pts[e.pts.size() / 2];
			const DgnV7::Point2& E = e.pts.back();
			CCVector2d C;
			double r = 0.0;
			double a0 = 0.0;
			double sweep = 0.0;
			if ( arcFrom3( CCVector2d( S.x, S.y ), CCVector2d( M.x, M.y ), CCVector2d( E.x, E.y ), C, r, a0, sweep ) )
			{
				if ( sweep < 0 )
				{
					a0 += sweep;
					sweep = -sweep;
				}
				e.isArc = true;
				e.center = { C.x, C.y };
				e.radius = r;
				e.startDeg = a0 * RadToDeg;
				e.sweepDeg = sweep * RadToDeg;
			}
		}
		if ( ok )
		{
			out.push_back( e );
		}
	}
	return out;
}

int DrawTool::countCategory( int id ) const
{
	int n = 0;
	ccHObject* group = const_cast<DrawTool*>( this )->drawingGroup( false );
	if ( !group )
	{
		return 0;
	}
	for ( unsigned i = 0; i < group->getChildrenNumber(); ++i )
	{
		ccHObject* ch = group->getChild( i );
		if ( ch->isA( CC_TYPES::POLY_LINE ) && ch->hasMetaData( Drawing::MetaCategory ) && ch->getMetaData( Drawing::MetaCategory ).toInt() == id )
		{
			++n;
		}
	}
	return n;
}

int DrawTool::moveCategory( int fromId, int toId )
{
	int n = 0;
	ccHObject* group = drawingGroup( false );
	if ( !group )
	{
		return 0;
	}
	for ( unsigned i = 0; i < group->getChildrenNumber(); ++i )
	{
		ccHObject* ch = group->getChild( i );
		if ( ch->isA( CC_TYPES::POLY_LINE ) && ch->hasMetaData( Drawing::MetaCategory ) && ch->getMetaData( Drawing::MetaCategory ).toInt() == fromId )
		{
			auto* p = static_cast<ccPolyline*>( ch );
			applyLook( p, toId, ch->getMetaData( Drawing::MetaHidden ).toBool() );
			++n;
		}
	}
	// zpět by vracelo neexistující hladinu
	m_undo.clear();
	return n;
}

void DrawTool::refreshLooks()
{
	ccHObject* group = drawingGroup( false );
	if ( group )
	{
		for ( unsigned i = 0; i < group->getChildrenNumber(); ++i )
		{
			ccHObject* ch = group->getChild( i );
			if ( ch->isA( CC_TYPES::POLY_LINE ) && ch->hasMetaData( Drawing::MetaCategory ) )
			{
				auto* p = static_cast<ccPolyline*>( ch );
				applyLook( p, ch->getMetaData( Drawing::MetaCategory ).toInt(), ch->getMetaData( Drawing::MetaHidden ).toBool() );
			}
		}
	}
	if ( !Drawing::layerById( m_category ) )
	{
		m_category = Drawing::layerIdAt( 0 );
		Q_EMIT categoryChanged( m_category );
	}
	updateLineVisibility();
	redraw3D();
	updateStatus();
}

int DrawTool::importPrims( const std::vector<ImportPrim>& prims )
{
	UndoItem u;
	int n = 0;
	auto mark = [&n]( ccPolyline* p )
	{
		if ( p )
		{
			p->setMetaData( "qRezy.imported", true );
			++n;
		}
	};
	for ( const ImportPrim& ip : prims )
	{
		if ( ip.kind == 0 )
		{
			for ( size_t k = 0; k + 1 < ip.pts.size(); ++k )
			{
				mark( addLine( ip.pts[k], ip.pts[k + 1], ip.cat, ip.hidden, &u ) );
			}
		}
		else if ( ip.kind == 1 )
		{
			if ( ip.radius <= 0.0 )
			{
				continue;
			}
			if ( std::abs( ip.sweep ) >= TwoPi - 1e-6 )
			{
				// kružnice = dva půloblouky
				mark( addPolyline( arcPoints( ip.center, ip.radius, ip.a0, TwoPi / 2 ), ip.cat, ip.hidden, true, &u ) );
				mark( addPolyline( arcPoints( ip.center, ip.radius, ip.a0 + TwoPi / 2, TwoPi / 2 ), ip.cat, ip.hidden, true, &u ) );
			}
			else
			{
				mark( addPolyline( arcPoints( ip.center, ip.radius, ip.a0, ip.sweep ), ip.cat, ip.hidden, true, &u ) );
			}
		}
		else if ( ip.kind == 2 && !ip.pts.empty() && !ip.text.isEmpty() )
		{
			ccPolyline* t = addText( ip.pts.front(), ip.text, &u );
			if ( t )
			{
				t->setMetaData( "qRezy.textLevel", ip.textLevel );
				t->setMetaData( "qRezy.textH", ip.textHeight );
			}
			mark( t );
		}
	}
	pushUndo( std::move( u ) );
	updateLineVisibility();
	redraw3D();
	return n;
}

int DrawTool::countImported() const
{
	int n = 0;
	ccHObject* group = const_cast<DrawTool*>( this )->drawingGroup( false );
	if ( group )
	{
		for ( unsigned i = 0; i < group->getChildrenNumber(); ++i )
		{
			if ( group->getChild( i )->hasMetaData( "qRezy.imported" ) )
			{
				++n;
			}
		}
	}
	return n;
}
