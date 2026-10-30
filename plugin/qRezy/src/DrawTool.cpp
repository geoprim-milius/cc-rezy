#include "DrawTool.h"
#include "RezLine.h"

#include "ccGLWindowInterface.h"
#include "ccMainAppInterface.h"

#include <ccGenericPointCloud.h>
#include <ccHObjectCaster.h>
#include <ccPointCloud.h>
#include <ccPolyline.h>

#include <QApplication>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QTimer>

#include <algorithm>
#include <cmath>
#include <limits>

namespace
{
	constexpr double SnapPixels = 10.0; //!< dosah uchopení v pixelech
	constexpr double PickPixels = 8.0;  //!< dosah výběru čáry
	constexpr int ClickTolerance = 4;   //!< max. posun myši, aby šlo o kliknutí
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
	if ( m_frame.viewId == 0 )
	{
		m_frame = DrawFrame();
		m_frame.front = m_planFront;
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

	// náhledy: gumová úsečka, upravovaná čára a značka uchopení (2D, v popředí)
	m_tip = make2DPolyline( m_tipVerts, 2, ccColor::Rgb( 255, 255, 255 ), 1 );
	m_ghost = make2DPolyline( m_ghostVerts, 2, ccColor::Rgb( 255, 220, 0 ), 2 );
	m_marker = make2DPolyline( m_markerVerts, 5, ccColor::Rgb( 255, 200, 0 ), 2 );
	for ( ccPolyline* p : { m_tip, m_ghost, m_marker } )
	{
		p->setDisplay( win );
		win->addToOwnDB( p );
	}

	m_winObject->installEventFilter( this );
	if ( QWidget* w = qobject_cast<QWidget*>( m_winObject ) )
	{
		w->setFocus();
	}

	drawingGroup( true );
	updateLineVisibility();
	resetTool();
	return true;
}

void DrawTool::setFrame( const DrawFrame& f )
{
	m_frame = f;
	if ( m_frame.viewId == 0 )
	{
		m_frame.front = m_planFront;
	}
	if ( m_mode == Mode::RotateView && m_frame.viewId != 0 )
	{
		m_mode = Mode::Line;
		Q_EMIT modeChanged( m_mode );
	}
	updateLineVisibility();
	resetTool();
	redraw3D();
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
		const bool on = ( view == m_frame.viewId ) || ( view == -1 && m_frame.viewId == 0 );
		ch->setEnabled( on );
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
	for ( ccPolyline** p : { &m_tip, &m_ghost, &m_marker } )
	{
		if ( *p )
		{
			m_win->removeFromOwnDB( *p );
			delete *p;
			*p = nullptr;
		}
	}
	m_tipVerts = m_ghostVerts = m_markerVerts = nullptr;
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
	resetTool();
	Q_EMIT modeChanged( m );
}

void DrawTool::setOrtho( bool on )
{
	m_ortho = on;
	updateTip();
	updateStatus();
	Q_EMIT orthoChanged( on );
}

void DrawTool::resetTool()
{
	m_chain.clear();
	m_typed.clear();
	m_step = 0;
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
		if ( view != m_frame.viewId )
		{
			continue;
		}
		auto* poly = static_cast<ccPolyline*>( ch );
		const unsigned n = poly->size();
		for ( unsigned k = 0; k + 1 < n; ++k )
		{
			const CCVector3* A = poly->getPoint( k );
			const CCVector3* B = poly->getPoint( k + 1 );
			segs.push_back( { m_frame.toUV( CCVector3d::fromArray( A->u ) ), m_frame.toUV( CCVector3d::fromArray( B->u ) ), poly } );
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
		const double d = dist( closestOnSegment( s.a, s.b, P ), P );
		if ( d < bestD )
		{
			bestD = d;
			best = s.poly;
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

	// 3) středy
	for ( const Segment* s : nearSegs )
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

CCVector2d DrawTool::cursorPoint( const QPoint& pos, SnapType& type, const CCVector2d* orthoFrom ) const
{
	CCVector2d P = snapPoint( pos, type );
	if ( m_ortho && orthoFrom && type == SnapType::None )
	{
		P = applyOrtho( *orthoFrom, P );
	}
	return P;
}

// ---------------------------------------------------------------- úpravy

ccPolyline* DrawTool::addLine( const CCVector2d& a, const CCVector2d& b, int cat, bool hidden, UndoItem* undo )
{
	ccHObject* group = drawingGroup( true );
	if ( !group || dist( a, b ) < 1e-6 )
	{
		return nullptr;
	}

	auto* verts = new ccPointCloud( "vertices" );
	verts->reserve( 2 );
	verts->addPoint( m_frame.to3D( a ).toFloat() );
	verts->addPoint( m_frame.to3D( b ).toFloat() );
	verts->setEnabled( false );
	verts->setGlobalShift( m_shift );
	verts->setGlobalScale( m_scale );

	auto* poly = new RezLine( verts );
	poly->addChild( verts );
	poly->addPointIndex( 0, 2 );
	poly->setClosed( false );
	poly->setGlobalShift( m_shift );
	poly->setGlobalScale( m_scale );
	poly->setDashed( hidden );
	poly->setColor( toRgb( Drawing::displayColor( cat, hidden ) ) );
	poly->showColors( true );
	poly->setWidth( 2 );
	poly->setName( Drawing::categoryName( cat ) + ( hidden ? QStringLiteral( " (zakrytá)" ) : QString() ) );
	poly->setMetaData( Drawing::MetaCategory, cat );
	poly->setMetaData( Drawing::MetaHidden, hidden );
	poly->setMetaData( Drawing::MetaView, m_frame.viewId );

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
	}
}

void DrawTool::select( ccPolyline* poly )
{
	if ( m_sel && lineExists( m_sel ) )
	{
		const int cat = m_sel->getMetaData( Drawing::MetaCategory ).toInt();
		const bool hid = m_sel->getMetaData( Drawing::MetaHidden ).toBool();
		m_sel->setColor( toRgb( Drawing::displayColor( cat, hid ) ) );
		m_sel->setWidth( 2 );
	}
	m_sel = poly;
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
					setLine( static_cast<ccPolyline*>( obj ), a.a, a.b, nullptr );
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
	redraw3D();
	updateTip();
	updateStatus();
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
		if ( m_step != 2 )
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
		CCVector2d a;
		CCVector2d b;
		if ( ccPolyline* p = pickLine( pos ) )
		{
			if ( lineEnds( p, a, b ) )
			{
				double ang = std::atan2( b.y - a.y, b.x - a.x );
				// čára vodorovně, „sever“ co nejvíc nahoru
				while ( ang > 1.5707963267948966 + 1e-9 )
				{
					ang -= 3.141592653589793;
				}
				while ( ang <= -1.5707963267948966 + 1e-9 )
				{
					ang += 3.141592653589793;
				}
				Q_EMIT rotateViewRequested( ang );
				m_mode = Mode::Line;
				Q_EMIT modeChanged( m_mode );
				resetTool();
				return;
			}
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
				Q_EMIT sectionDefined( A3.x, A3.y, B3.x, B3.y, C3.x, C3.y );
			}
			m_mode = Mode::Line;
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
		else
		{
			m_cursor = cursorPoint( pos, st, &m_base );
			CCVector2d a;
			CCVector2d b;
			if ( previewGeometry( a, b ) )
			{
				UndoItem u;
				if ( m_mode == Mode::Move )
				{
					setLine( m_sel, a, b, &u );
					pushUndo( std::move( u ) );
					select( nullptr );
					m_step = 0;
				}
				else
				{
					const int cat = m_sel->getMetaData( Drawing::MetaCategory ).toInt();
					const bool hid = m_sel->getMetaData( Drawing::MetaHidden ).toBool();
					addLine( a, b, cat, hid, &u );
					pushUndo( std::move( u ) );
				}
			}
		}
		break;
	}
	case Mode::Offset:
	{
		if ( m_step == 0 )
		{
			if ( ccPolyline* p = pickLine( pos ) )
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
			if ( ccPolyline* p = pickLine( pos, &click ) )
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
		if ( !p )
		{
			break;
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

	switch ( m_mode )
	{
	case Mode::Line:
	{
		if ( m_chain.empty() || !m_hasCursor )
		{
			return;
		}
		const CCVector2d target = m_ortho ? applyOrtho( m_chain.back(), m_cursor ) : m_cursor;
		CCVector2d dir = target - m_chain.back();
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
		const CCVector2d saved = m_cursor;
		m_cursor = m_base + dir * L;
		CCVector2d a;
		CCVector2d b;
		if ( previewGeometry( a, b ) )
		{
			UndoItem u;
			if ( m_mode == Mode::Move )
			{
				setLine( m_sel, a, b, &u );
				select( nullptr );
				m_step = 0;
			}
			else
			{
				addLine( a, b, m_sel->getMetaData( Drawing::MetaCategory ).toInt(), m_sel->getMetaData( Drawing::MetaHidden ).toBool(), &u );
			}
			pushUndo( std::move( u ) );
		}
		m_cursor = saved;
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

void DrawTool::updateTip()
{
	if ( !m_tip || !m_marker || !m_ghost || !m_win )
	{
		return;
	}

	// gumová úsečka od posledního bodu ke kurzoru
	bool tipOn = false;
	const CCVector2d* from = nullptr;
	if ( m_mode == Mode::Line && !m_chain.empty() )
	{
		from = &m_chain.back();
	}
	else if ( ( m_mode == Mode::Move || m_mode == Mode::Copy ) && m_step == 2 )
	{
		from = &m_base;
	}
	else if ( m_mode == Mode::SectionDef && m_step == 1 )
	{
		from = &m_base;
	}
	if ( from && m_hasCursor )
	{
		CCVector2d A2;
		CCVector2d B2;
		if ( localToScreen( *from, A2 ) && localToScreen( m_cursor, B2 ) )
		{
			setVertex( m_tipVerts, 0, v3( A2, 0 ) );
			setVertex( m_tipVerts, 1, v3( B2, 0 ) );
			m_tip->setColor( m_mode == Mode::Line ? toRgb( Drawing::displayColor( m_category, m_hidden ) ) : ccColor::Rgb( 255, 255, 255 ) );
			tipOn = true;
		}
	}
	m_tip->setEnabled( tipOn );

	// náhled upravované čáry
	bool ghostOn = false;
	CCVector2d a;
	CCVector2d b;
	if ( previewGeometry( a, b ) )
	{
		CCVector2d A2;
		CCVector2d B2;
		if ( localToScreen( a, A2 ) && localToScreen( b, B2 ) )
		{
			setVertex( m_ghostVerts, 0, v3( A2, 0 ) );
			setVertex( m_ghostVerts, 1, v3( B2, 0 ) );
			ghostOn = true;
		}
	}
	m_ghost->setEnabled( ghostOn );

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
				setVertex( m_markerVerts, i, pts[i] );
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

void DrawTool::redraw3D()
{
	if ( m_win )
	{
		m_win->redraw( false, false );
	}
}

void DrawTool::keepCrossCursor()
{
	if ( m_win )
	{
		m_win->setWindowCursor( Qt::CrossCursor );
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
		prompt = QStringLiteral( "Natočení pohledu: klikněte na čáru, která má být vodorovně" );
		break;
	case Mode::SectionDef:
		prompt = m_step == 0 ? QStringLiteral( "Nový řez: první bod čáry řezu" )
		       : m_step == 1 ? QStringLiteral( "Nový řez: druhý bod čáry řezu" )
		                     : QStringLiteral( "Nový řez: klikněte na stranu, KAM se díváte" );
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
	}

	QString s = prompt;
	s += QStringLiteral( "\nHladina: %1%2" ).arg( Drawing::categoryName( m_category ), m_hidden ? QStringLiteral( " – zakrytá" ) : QString() );
	if ( m_ortho )
	{
		s += QStringLiteral( "  |  ORTO" );
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
	{
		auto* e = static_cast<QMouseEvent*>( event );
		if ( e->button() == Qt::LeftButton )
		{
			onDataPoint( e->pos() );
			return true; // levé tlačítko neotáčí pohledem
		}
		if ( e->button() == Qt::RightButton )
		{
			m_rightPressed = true;
			m_rightPressPos = e->pos();
		}
		if ( e->button() == Qt::MiddleButton )
		{
			m_midPressed = true;
			m_midPressPos = e->pos();
		}
		break; // pravé a prostřední tlačítko necháme CloudCompare (posun, zoom)
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
				// reset: konec čáry / zpět o krok nástroje
				if ( m_mode == Mode::Line || m_step == 0 )
				{
					m_chain.clear();
					m_step = 0;
					select( nullptr );
				}
				else
				{
					m_step = 0;
					select( nullptr );
				}
				m_typed.clear();
				updateTip();
				updateStatus();
				redraw2D();
			}
		}
		if ( e->button() == Qt::MiddleButton && m_midPressed )
		{
			m_midPressed = false;
			if ( ( e->pos() - m_midPressPos ).manhattanLength() <= ClickTolerance )
			{
				CCVector2d P;
				if ( screenToLocal( e->pos(), P ) )
				{
					const CCVector3d P3 = m_frame.to3D( P, false );
					Q_EMIT show3DRequested( P3.x, P3.y, m_frame.viewId == 0 ? std::numeric_limits<double>::quiet_NaN() : P3.z );
				}
			}
		}
		// CloudCompare po posunu nastaví šipku – vrátíme kříž
		QTimer::singleShot( 0, this, [this]() { keepCrossCursor(); } );
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
		const CCVector2d* from = nullptr;
		if ( m_mode == Mode::Line && !m_chain.empty() )
		{
			from = &m_chain.back();
		}
		else if ( ( ( m_mode == Mode::Move || m_mode == Mode::Copy ) && m_step == 2 ) || ( m_mode == Mode::SectionDef && m_step == 1 ) )
		{
			from = &m_base;
		}
		m_cursor = cursorPoint( e->pos(), st, from );
		m_cursorSnap = st;
		m_hasCursor = true;
		if ( e->buttons() == Qt::NoButton )
		{
			keepCrossCursor();
		}
		updateTip();
		updateStatus();
		redraw2D();
		break; // necháme projít (posun pohledu pravým tlačítkem)
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
		// po zoomu přepočítat náhled
		QTimer::singleShot( 0, this, [this]() { updateTip(); redraw2D(); } );
		break;
	case QEvent::ShortcutOverride:
	case QEvent::KeyPress:
	{
		auto* e = static_cast<QKeyEvent*>( event );
		const int k = e->key();
		const bool ctrl = ( e->modifiers() & Qt::ControlModifier );
		const bool digit = ( k >= Qt::Key_0 && k <= Qt::Key_9 ) || k == Qt::Key_Comma || k == Qt::Key_Period;
		const bool wantsNumber = ( m_mode == Mode::Line && !m_chain.empty() ) || ( ( m_mode == Mode::Move || m_mode == Mode::Copy ) && m_step == 2 )
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
		else if ( k == Qt::Key_Escape )
		{
			if ( event->type() == QEvent::KeyPress )
			{
				if ( m_mode != Mode::Line && m_step == 0 )
				{
					setMode( Mode::Line );
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
		else if ( k >= Qt::Key_F1 && k <= Qt::Key_F4 && !ctrl )
		{
			if ( event->type() == QEvent::KeyPress )
				setCategory( k - Qt::Key_F1 );
		}
		else if ( k == Qt::Key_F5 && !ctrl )
		{
			if ( event->type() == QEvent::KeyPress )
				setHidden( !m_hidden );
		}
		else if ( k == Qt::Key_F8 && !ctrl )
		{
			if ( event->type() == QEvent::KeyPress )
				setOrtho( !m_ortho );
		}
		else if ( k == Qt::Key_Delete )
		{
			if ( event->type() == QEvent::KeyPress )
				setMode( Mode::Delete );
		}
		else if ( !ctrl && !( e->modifiers() & Qt::AltModifier ) && ( k == Qt::Key_L || k == Qt::Key_M || k == Qt::Key_C || k == Qt::Key_O || k == Qt::Key_E || k == Qt::Key_R || ( k == Qt::Key_S && m_frame.viewId == 0 ) ) )
		{
			if ( event->type() == QEvent::KeyPress )
			{
				switch ( k )
				{
				case Qt::Key_L:
					setMode( Mode::Line );
					break;
				case Qt::Key_M:
					setMode( Mode::Move );
					break;
				case Qt::Key_C:
					setMode( Mode::Copy );
					break;
				case Qt::Key_O:
					setMode( Mode::Offset );
					break;
				case Qt::Key_E:
					setMode( Mode::Extend );
					break;
				case Qt::Key_R:
					setMode( Mode::Corner );
					break;
				case Qt::Key_S:
					setMode( Mode::SectionDef );
					break;
				default:
					break;
				}
			}
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

std::vector<DgnV7::Element> DrawTool::exportElements( const Drawing::SymbologyTable& table, const ExportMap& map ) const
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
		const int view = poly->hasMetaData( Drawing::MetaView ) ? poly->getMetaData( Drawing::MetaView ).toInt() : 0;
		if ( view < 0 || !poly->hasMetaData( Drawing::MetaCategory ) )
		{
			continue; // definice řezu apod.
		}
		const int cat = poly->getMetaData( Drawing::MetaCategory ).toInt();
		const bool hid = poly->getMetaData( Drawing::MetaHidden ).toBool();

		DgnV7::Element e;
		e.sym = table.get( std::max( 0, std::min( Drawing::CategoryCount - 1, cat ) ), hid );
		bool ok = true;
		for ( unsigned k = 0; k < poly->size() && ok; ++k )
		{
			DgnV7::Point2 P;
			ok = map( view, CCVector3d::fromArray( poly->getPoint( k )->u ), P );
			e.pts.push_back( P );
		}
		if ( ok )
		{
			out.push_back( e );
		}
	}
	return out;
}
