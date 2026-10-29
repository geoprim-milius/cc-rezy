#include "DrawTool.h"

#include "ccGLWindowInterface.h"
#include "ccMainAppInterface.h"

#include <ccGenericPointCloud.h>
#include <ccHObjectCaster.h>
#include <ccPointCloud.h>
#include <ccPolyline.h>

#include <QApplication>
#include <QKeyEvent>
#include <QMouseEvent>

#include <cmath>
#include <limits>

namespace
{
	constexpr double SnapPixels = 10.0;   //!< dosah uchopení v pixelech
	constexpr double PickPixels = 8.0;    //!< dosah výběru čáry pro mazání
	constexpr int ClickTolerance = 4;     //!< max. posun myši, aby šlo o kliknutí

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
}

DrawTool::DrawTool( ccMainAppInterface* app, QObject* parent )
	: QObject( parent )
	, m_app( app )
{
}

DrawTool::~DrawTool()
{
	stop();
	for ( UndoItem& u : m_undo )
	{
		if ( !u.added && u.poly && !u.poly->getParent() )
		{
			delete u.poly;
		}
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
	m_cloudID = cloud->getUniqueID();
	m_shift = cloud->getGlobalShift();
	m_scale = cloud->getGlobalScale();
	// čáry kreslíme nad mračnem, aby je v pohledu shora nic nezakrylo
	m_drawZ = cloud->getOwnBB().maxCorner().z + 1.0;

	m_savedInteraction = static_cast<int>( win->getInteractionMode() );
	win->setPickingMode( ccGLWindowInterface::NO_PICKING );
	win->setInteractionMode( ccGLWindowInterface::INTERACT_PAN
	                       | ccGLWindowInterface::INTERACT_ZOOM_CAMERA
	                       | ccGLWindowInterface::INTERACT_2D_ITEMS
	                       | ccGLWindowInterface::INTERACT_SIG_MOUSE_MOVED );
	win->setWindowCursor( Qt::CrossCursor );

	// náhled rozkreslené úsečky a značka uchopení (2D, v popředí)
	m_tipVerts = new ccPointCloud( "tip vertices" );
	m_tipVerts->reserve( 2 );
	m_tipVerts->addPoint( CCVector3( 0, 0, 0 ) );
	m_tipVerts->addPoint( CCVector3( 0, 0, 0 ) );
	m_tip = new ccPolyline( m_tipVerts );
	m_tip->addChild( m_tipVerts );
	m_tip->addPointIndex( 0, 2 );
	m_tip->set2DMode( true );
	m_tip->setForeground( true );
	m_tip->setWidth( 1 );
	m_tip->showColors( true );
	m_tip->setEnabled( false );
	m_tip->setDisplay( win );
	win->addToOwnDB( m_tip );

	m_markerVerts = new ccPointCloud( "marker vertices" );
	m_markerVerts->reserve( 5 );
	for ( int i = 0; i < 5; ++i )
	{
		m_markerVerts->addPoint( CCVector3( 0, 0, 0 ) );
	}
	m_marker = new ccPolyline( m_markerVerts );
	m_marker->addChild( m_markerVerts );
	m_marker->addPointIndex( 0, 5 );
	m_marker->set2DMode( true );
	m_marker->setForeground( true );
	m_marker->setWidth( 2 );
	m_marker->showColors( true );
	m_marker->setColor( ccColor::Rgb( 255, 200, 0 ) );
	m_marker->setEnabled( false );
	m_marker->setDisplay( win );
	win->addToOwnDB( m_marker );

	m_winObject->installEventFilter( this );
	if ( QWidget* w = qobject_cast<QWidget*>( m_winObject ) )
	{
		w->setFocus();
	}

	drawingGroup( true );
	resetChain();
	updateStatus();
	return true;
}

void DrawTool::stop()
{
	if ( !m_win )
	{
		return;
	}
	if ( m_winObject )
	{
		m_winObject->removeEventFilter( this );
	}
	if ( m_tip )
	{
		m_win->removeFromOwnDB( m_tip );
		delete m_tip;
		m_tip = nullptr;
		m_tipVerts = nullptr;
	}
	if ( m_marker )
	{
		m_win->removeFromOwnDB( m_marker );
		delete m_marker;
		m_marker = nullptr;
		m_markerVerts = nullptr;
	}
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
	m_category = std::max( 0, std::min( Drawing::CategoryCount - 1, cat ) );
	updateTip();
	updateStatus();
	Q_EMIT categoryChanged( m_category );
}

void DrawTool::setHidden( bool hidden )
{
	m_hidden = hidden;
	updateTip();
	updateStatus();
	Q_EMIT hiddenChanged( m_hidden );
}

void DrawTool::setMode( Mode m )
{
	m_mode = m;
	resetChain();
}

void DrawTool::setOrtho( bool on )
{
	m_ortho = on;
	updateTip();
	updateStatus();
	Q_EMIT orthoChanged( on );
}

void DrawTool::resetChain()
{
	m_chain.clear();
	m_typed.clear();
	updateTip();
	updateStatus();
}

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
	out = CCVector2d( P3.x, P3.y );
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
	if ( !camera.project( CCVector3d( P.x, P.y, m_drawZ ), S ) )
	{
		return false;
	}
	centered = CCVector2d( S.x - camera.viewport[2] / 2.0, S.y - camera.viewport[3] / 2.0 );
	return true;
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
		auto* poly = static_cast<ccPolyline*>( ch );
		const unsigned n = poly->size();
		for ( unsigned k = 0; k + 1 < n; ++k )
		{
			const CCVector3* A = poly->getPoint( k );
			const CCVector3* B = poly->getPoint( k + 1 );
			segs.push_back( { CCVector2d( A->x, A->y ), CCVector2d( B->x, B->y ), poly } );
		}
	}
	return segs;
}

CCVector2d DrawTool::snapPoint( const QPoint& pos, SnapType& type ) const
{
	type = SnapType::None;
	CCVector2d P;
	if ( !screenToLocal( pos, P ) )
	{
		return P;
	}
	if ( !m_snapEnabled )
	{
		return P;
	}

	const double tol = SnapPixels * pixelSize();
	const std::vector<Segment> segs = collectSegments();

	// body rozkreslené čáry jsou také uchopitelné
	std::vector<CCVector2d> ends;
	for ( const Segment& s : segs )
	{
		ends.push_back( s.a );
		ends.push_back( s.b );
	}
	ends.insert( ends.end(), m_chain.begin(), m_chain.end() );

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

	// jen úsečky v dosahu
	std::vector<const Segment*> near;
	for ( const Segment& s : segs )
	{
		if ( dist( closestOnSegment( s.a, s.b, P ), P ) < tol )
		{
			near.push_back( &s );
		}
	}

	// 2) průsečíky
	for ( size_t i = 0; i < near.size(); ++i )
	{
		for ( size_t j = i + 1; j < near.size(); ++j )
		{
			CCVector2d I;
			if ( segmentIntersection( near[i]->a, near[i]->b, near[j]->a, near[j]->b, I ) )
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

	// 3) středy
	for ( const Segment* s : near )
	{
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
	for ( const Segment* s : near )
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
	const CCVector2d v = to - from;
	const CCVector2d ax( std::cos( m_axisAngle ), std::sin( m_axisAngle ) );
	const CCVector2d ay( -ax.y, ax.x );
	const double px = v.dot( ax );
	const double py = v.dot( ay );
	return ( std::abs( px ) >= std::abs( py ) ) ? from + ax * px : from + ay * py;
}

ccPolyline* DrawTool::addLine( const CCVector2d& a, const CCVector2d& b )
{
	ccHObject* group = drawingGroup( true );
	if ( !group || dist( a, b ) < 1e-6 )
	{
		return nullptr;
	}

	auto* verts = new ccPointCloud( "vertices" );
	verts->reserve( 2 );
	verts->addPoint( CCVector3( static_cast<PointCoordinateType>( a.x ), static_cast<PointCoordinateType>( a.y ), static_cast<PointCoordinateType>( m_drawZ ) ) );
	verts->addPoint( CCVector3( static_cast<PointCoordinateType>( b.x ), static_cast<PointCoordinateType>( b.y ), static_cast<PointCoordinateType>( m_drawZ ) ) );
	verts->setEnabled( false );
	verts->setGlobalShift( m_shift );
	verts->setGlobalScale( m_scale );

	auto* poly = new ccPolyline( verts );
	poly->addChild( verts );
	poly->addPointIndex( 0, 2 );
	poly->setClosed( false );
	poly->setGlobalShift( m_shift );
	poly->setGlobalScale( m_scale );
	poly->setColor( toRgb( Drawing::displayColor( m_category, m_hidden ) ) );
	poly->showColors( true );
	poly->setWidth( 2 );
	poly->setName( Drawing::categoryName( m_category ) + ( m_hidden ? QStringLiteral( " (zakrytá)" ) : QString() ) );
	poly->setMetaData( Drawing::MetaCategory, m_category );
	poly->setMetaData( Drawing::MetaHidden, m_hidden );
	poly->setMetaData( Drawing::MetaView, 0 );

	group->addChild( poly );
	m_app->addToDB( poly, false, false, false, false );
	m_undo.push_back( { true, nullptr, poly->getUniqueID() } );
	return poly;
}

void DrawTool::addPoint( const CCVector2d& P )
{
	if ( !m_chain.empty() )
	{
		addLine( m_chain.back(), P );
	}
	m_chain.push_back( P );
	m_typed.clear();
	if ( m_win )
	{
		m_win->redraw();
	}
	updateStatus();
}

void DrawTool::deleteAt( const QPoint& pos )
{
	CCVector2d P;
	if ( !screenToLocal( pos, P ) )
	{
		return;
	}
	const double tol = PickPixels * pixelSize();
	ccPolyline* best = nullptr;
	double bestD = tol;
	for ( const Segment& s : collectSegments() )
	{
		const double d = dist( closestOnSegment( s.a, s.b, P ), P );
		if ( d < bestD )
		{
			bestD = d;
			best = s.poly;
		}
	}
	if ( !best )
	{
		return;
	}

	if ( m_pickAxis )
	{
		const CCVector3* A = best->getPoint( 0 );
		const CCVector3* B = best->getPoint( best->size() - 1 );
		m_axisAngle = std::atan2( B->y - A->y, B->x - A->x );
		m_pickAxis = false;
		m_ortho = true;
		Q_EMIT orthoChanged( true );
		updateStatus();
		return;
	}

	m_app->removeFromDB( best, false ); // necháme si objekt kvůli Zpět
	m_undo.push_back( { false, best, best->getUniqueID() } );
	if ( m_win )
	{
		m_win->redraw();
	}
	updateStatus();
}

void DrawTool::undo()
{
	if ( m_undo.empty() )
	{
		return;
	}
	UndoItem u = m_undo.back();
	m_undo.pop_back();

	if ( u.added )
	{
		ccHObject* obj = m_app->dbRootObject()->find( u.id );
		if ( obj )
		{
			// pokud se ruší poslední úsečka rozkreslené čáry, vrátíme i bod
			if ( m_chain.size() >= 2 )
			{
				m_chain.pop_back();
			}
			m_app->removeFromDB( obj, true );
		}
	}
	else if ( u.poly )
	{
		if ( ccHObject* group = drawingGroup( true ) )
		{
			group->addChild( u.poly );
			m_app->addToDB( u.poly, false, false, false, false );
		}
	}
	if ( m_win )
	{
		m_win->redraw();
	}
	updateTip();
	updateStatus();
}

void DrawTool::placeTypedDistance()
{
	if ( m_chain.empty() || !m_hasCursor )
	{
		return;
	}
	bool ok = false;
	const double d = QString( m_typed ).replace( ',', '.' ).toDouble( &ok );
	m_typed.clear();
	if ( !ok || d <= 0.0 )
	{
		updateStatus();
		return;
	}
	CCVector2d target = m_ortho ? applyOrtho( m_chain.back(), m_cursor ) : m_cursor;
	CCVector2d dir = target - m_chain.back();
	if ( dir.norm() < 1e-9 )
	{
		updateStatus();
		return;
	}
	dir.normalize();
	addPoint( m_chain.back() + dir * ( d * m_scale ) );
	updateTip();
}

void DrawTool::updateTip()
{
	if ( !m_tip || !m_marker || !m_win )
	{
		return;
	}

	// náhled úsečky od posledního bodu ke kurzoru
	bool tipOn = false;
	if ( m_mode == Mode::Line && !m_chain.empty() && m_hasCursor )
	{
		CCVector2d A2;
		CCVector2d B2;
		if ( localToScreen( m_chain.back(), A2 ) && localToScreen( m_cursor, B2 ) )
		{
			*const_cast<CCVector3*>( m_tipVerts->getPointPersistentPtr( 0 ) ) = CCVector3( static_cast<PointCoordinateType>( A2.x ), static_cast<PointCoordinateType>( A2.y ), 0 );
			*const_cast<CCVector3*>( m_tipVerts->getPointPersistentPtr( 1 ) ) = CCVector3( static_cast<PointCoordinateType>( B2.x ), static_cast<PointCoordinateType>( B2.y ), 0 );
			m_tip->setColor( toRgb( Drawing::displayColor( m_category, m_hidden ) ) );
			tipOn = true;
		}
	}
	m_tip->setEnabled( tipOn );

	// značka uchopení
	bool markerOn = false;
	if ( m_hasCursor && m_cursorSnap != SnapType::None )
	{
		CCVector2d C;
		if ( localToScreen( m_cursor, C ) )
		{
			const float r = 6.0f;
			const float x = static_cast<float>( C.x );
			const float y = static_cast<float>( C.y );
			CCVector3 pts[5];
			switch ( m_cursorSnap )
			{
			case SnapType::Mid: // trojúhelník
				pts[0] = CCVector3( x - r, y - r, 0 );
				pts[1] = CCVector3( x + r, y - r, 0 );
				pts[2] = CCVector3( x, y + r, 0 );
				pts[3] = CCVector3( x - r, y - r, 0 );
				pts[4] = CCVector3( x - r, y - r, 0 );
				break;
			case SnapType::Intersection: // křížek
				pts[0] = CCVector3( x - r, y - r, 0 );
				pts[1] = CCVector3( x + r, y + r, 0 );
				pts[2] = CCVector3( x, y, 0 );
				pts[3] = CCVector3( x - r, y + r, 0 );
				pts[4] = CCVector3( x + r, y - r, 0 );
				break;
			case SnapType::OnLine: // kosočtverec
				pts[0] = CCVector3( x, y - r, 0 );
				pts[1] = CCVector3( x + r, y, 0 );
				pts[2] = CCVector3( x, y + r, 0 );
				pts[3] = CCVector3( x - r, y, 0 );
				pts[4] = CCVector3( x, y - r, 0 );
				break;
			default: // čtverec = konec
				pts[0] = CCVector3( x - r, y - r, 0 );
				pts[1] = CCVector3( x + r, y - r, 0 );
				pts[2] = CCVector3( x + r, y + r, 0 );
				pts[3] = CCVector3( x - r, y + r, 0 );
				pts[4] = CCVector3( x - r, y - r, 0 );
				break;
			}
			for ( unsigned i = 0; i < 5; ++i )
			{
				*const_cast<CCVector3*>( m_markerVerts->getPointPersistentPtr( i ) ) = pts[i];
			}
			markerOn = true;
		}
	}
	m_marker->setEnabled( markerOn );
}

void DrawTool::redraw2D()
{
	if ( m_win )
	{
		m_win->redraw( true, false );
	}
}

void DrawTool::updateStatus()
{
	QString s = QStringLiteral( "Hladina: %1%2" ).arg( Drawing::categoryName( m_category ), m_hidden ? QStringLiteral( " – zakrytá" ) : QString() );
	if ( m_mode == Mode::Delete )
	{
		s += QStringLiteral( "   |   MAZÁNÍ: klikněte na čáru" );
	}
	if ( m_pickAxis )
	{
		s += QStringLiteral( "   |   klikněte na čáru pro natočení os" );
	}
	if ( m_ortho )
	{
		s += QStringLiteral( "   |   ORTO %1°" ).arg( m_axisAngle * 57.29577951308232, 0, 'f', 1 );
	}
	if ( m_hasCursor && m_cursorSnap != SnapType::None )
	{
		s += QStringLiteral( "   |   uchopení: %1" ).arg( snapName( m_cursorSnap ) );
	}
	if ( !m_chain.empty() && m_hasCursor )
	{
		const CCVector2d d = m_cursor - m_chain.back();
		s += QStringLiteral( "   |   délka %1 m" ).arg( QString::number( d.norm() / m_scale, 'f', 3 ).replace( '.', ',' ) );
	}
	if ( !m_typed.isEmpty() )
	{
		s += QStringLiteral( "   |   zadaná délka: %1 (Enter)" ).arg( m_typed );
	}
	Q_EMIT statusChanged( s );
}

bool DrawTool::eventFilter( QObject* obj, QEvent* event )
{
	if ( obj != m_winObject || !m_win )
	{
		return QObject::eventFilter( obj, event );
	}

	switch ( event->type() )
	{
	case QEvent::MouseButtonPress:
	{
		auto* e = static_cast<QMouseEvent*>( event );
		if ( e->button() == Qt::LeftButton )
		{
			if ( m_mode == Mode::Delete || m_pickAxis )
			{
				deleteAt( e->pos() );
				return true;
			}
			SnapType st;
			CCVector2d P = snapPoint( e->pos(), st );
			if ( m_ortho && !m_chain.empty() && st == SnapType::None )
			{
				P = applyOrtho( m_chain.back(), P );
			}
			addPoint( P );
			return true; // levé tlačítko neotáčí pohledem
		}
		if ( e->button() == Qt::RightButton )
		{
			m_rightPressed = true;
			m_rightPressPos = e->pos();
		}
		break; // pravé tlačítko necháme CloudCompare (posun pohledu)
	}
	case QEvent::MouseButtonDblClick:
	{
		auto* e = static_cast<QMouseEvent*>( event );
		if ( e->button() == Qt::LeftButton )
		{
			return true;
		}
		break;
	}
	case QEvent::MouseButtonRelease:
	{
		auto* e = static_cast<QMouseEvent*>( event );
		if ( e->button() == Qt::RightButton && m_rightPressed )
		{
			m_rightPressed = false;
			if ( ( e->pos() - m_rightPressPos ).manhattanLength() <= ClickTolerance )
			{
				// reset = konec rozkreslené čáry
				resetChain();
				m_pickAxis = false;
				redraw2D();
			}
		}
		if ( e->button() == Qt::LeftButton )
		{
			return true;
		}
		break;
	}
	case QEvent::MouseMove:
	{
		auto* e = static_cast<QMouseEvent*>( event );
		SnapType st;
		CCVector2d P = snapPoint( e->pos(), st );
		if ( m_ortho && !m_chain.empty() && st == SnapType::None )
		{
			P = applyOrtho( m_chain.back(), P );
		}
		m_cursor = P;
		m_cursorSnap = st;
		m_hasCursor = true;
		updateTip();
		updateStatus();
		redraw2D();
		break; // necháme projít (posun pohledu pravým tlačítkem)
	}
	case QEvent::Enter:
		// klávesy (F1–F8, čísla, Del) mají jít do kreslení
		if ( QWidget* w = qobject_cast<QWidget*>( m_winObject ) )
		{
			w->setFocus();
		}
		break;
	case QEvent::ShortcutOverride:
	{
		// naše klávesy mají přednost před zkratkami CloudCompare (F1–F8, Del…)
		auto* e = static_cast<QKeyEvent*>( event );
		const int k = e->key();
		const bool digit = ( k >= Qt::Key_0 && k <= Qt::Key_9 ) || k == Qt::Key_Comma || k == Qt::Key_Period;
		if ( ( k >= Qt::Key_F1 && k <= Qt::Key_F5 ) || k == Qt::Key_F8 || k == Qt::Key_Delete || k == Qt::Key_Escape
		     || ( digit && !m_chain.empty() ) || ( k == Qt::Key_Backspace && !m_typed.isEmpty() )
		     || ( ( k == Qt::Key_Return || k == Qt::Key_Enter ) && !m_typed.isEmpty() )
		     || ( k == Qt::Key_Z && ( e->modifiers() & Qt::ControlModifier ) ) )
		{
			e->accept();
			return true;
		}
		break;
	}
	case QEvent::Leave:
		m_hasCursor = false;
		updateTip();
		redraw2D();
		break;
	case QEvent::Wheel:
		// po zoomu přepočítat náhled
		QMetaObject::invokeMethod( this, [this]() { updateTip(); redraw2D(); }, Qt::QueuedConnection );
		break;
	case QEvent::KeyPress:
	{
		auto* e = static_cast<QKeyEvent*>( event );
		const int k = e->key();
		if ( ( k >= Qt::Key_0 && k <= Qt::Key_9 ) || k == Qt::Key_Comma || k == Qt::Key_Period )
		{
			if ( !m_chain.empty() )
			{
				m_typed += ( k == Qt::Key_Comma || k == Qt::Key_Period ) ? QStringLiteral( "," ) : e->text();
				updateStatus();
				return true;
			}
		}
		else if ( k == Qt::Key_Backspace && !m_typed.isEmpty() )
		{
			m_typed.chop( 1 );
			updateStatus();
			return true;
		}
		else if ( k == Qt::Key_Return || k == Qt::Key_Enter )
		{
			if ( !m_typed.isEmpty() )
			{
				placeTypedDistance();
				redraw2D();
				return true;
			}
		}
		else if ( k == Qt::Key_Escape )
		{
			resetChain();
			m_pickAxis = false;
			redraw2D();
			return true;
		}
		else if ( k == Qt::Key_Z && ( e->modifiers() & Qt::ControlModifier ) )
		{
			undo();
			return true;
		}
		else if ( k >= Qt::Key_F1 && k <= Qt::Key_F4 )
		{
			setCategory( k - Qt::Key_F1 );
			return true;
		}
		else if ( k == Qt::Key_F5 )
		{
			setHidden( !m_hidden );
			return true;
		}
		else if ( k == Qt::Key_F8 )
		{
			setOrtho( !m_ortho );
			return true;
		}
		else if ( k == Qt::Key_Delete )
		{
			setMode( m_mode == Mode::Delete ? Mode::Line : Mode::Delete );
			updateStatus();
			return true;
		}
		break;
	}
	default:
		break;
	}
	return QObject::eventFilter( obj, event );
}

std::vector<DgnV7::Element> DrawTool::exportElements( const Drawing::SymbologyTable& table ) const
{
	std::vector<DgnV7::Element> out;
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
		const int cat = poly->getMetaData( Drawing::MetaCategory ).toInt();
		const bool hid = poly->getMetaData( Drawing::MetaHidden ).toBool();

		DgnV7::Element e;
		e.sym = table.get( std::max( 0, std::min( Drawing::CategoryCount - 1, cat ) ), hid );
		for ( unsigned k = 0; k < poly->size(); ++k )
		{
			const CCVector3d G = poly->toGlobal3d( *poly->getPoint( k ) );
			e.pts.push_back( { G.x, G.y } );
		}
		out.push_back( e );
	}
	return out;
}
