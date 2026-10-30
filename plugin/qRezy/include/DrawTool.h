#pragma once

// Kreslení a úpravy 2D čar nad řezem s ovládáním jako v MicroStationu:
// levé tlačítko = datový bod, pravé kliknutí = reset, pravé tažení = posun pohledu,
// kolečko = zoom, prostřední kliknutí = ukázat místo ve 3D.
// Uchopení na konce, středy, průsečíky a na čáru.

#include "Drawing.h"

#include <QObject>
#include <QPoint>
#include <QString>

#include <CCGeom.h>

#include <functional>
#include <vector>

class ccMainAppInterface;
class ccGLWindowInterface;
class ccHObject;
class ccPolyline;
class ccPointCloud;
class ccGenericPointCloud;

//! Rovina kreslení: půdorys (id 0) nebo svislý řez (id ≥ 1).
//! 2D souřadnice (u, v) v rovině; 3D bod = O + U·u + V·v (+ N·front pro zobrazení)
struct DrawFrame
{
	int viewId = 0;
	CCVector3d O = CCVector3d( 0, 0, 0 );
	CCVector3d U = CCVector3d( 1, 0, 0 );
	CCVector3d V = CCVector3d( 0, 1, 0 );
	CCVector3d N = CCVector3d( 0, 0, 1 ); //!< k pozorovateli
	double front = 0.0;                    //!< posun čar k pozorovateli (aby je body nezakryly)

	CCVector3d to3D( const CCVector2d& uv, bool withFront = true ) const
	{
		return O + U * uv.x + V * uv.y + ( withFront ? N * front : CCVector3d( 0, 0, 0 ) );
	}
	CCVector2d toUV( const CCVector3d& P ) const
	{
		const CCVector3d d = P - O;
		return CCVector2d( d.dot( U ), d.dot( V ) );
	}
};

class DrawTool : public QObject
{
	Q_OBJECT

public:
	enum class Mode
	{
		Line,       //!< kreslení navazujících úseček
		Delete,     //!< mazání
		Move,       //!< přesun
		Copy,       //!< kopie
		Offset,     //!< rovnoběžná kopie
		Extend,     //!< prodloužit / zkrátit
		Corner,     //!< dvě čáry do průsečíku (roh)
		RotateView, //!< natočit pohled podle čáry
		SectionDef  //!< nový svislý řez: 2 body čáry řezu + strana pohledu
	};

	enum class SnapType
	{
		None,
		End,
		Mid,
		Intersection,
		OnLine
	};

	DrawTool( ccMainAppInterface* app, QObject* parent = nullptr );
	~DrawTool() override;

	//! mračno, ke kterému výkres patří (posun souřadnic, skupina výkresu)
	void setCloud( ccGenericPointCloud* cloud );
	//! zapne kreslení v aktivním 3D okně nad daným mračnem
	bool start( ccGenericPointCloud* cloud );
	void stop();
	bool isActive() const { return m_win != nullptr; }

	void setCategory( int cat );
	int category() const { return m_category; }
	void setHidden( bool hidden );
	bool hidden() const { return m_hidden; }
	void setMode( Mode m );
	Mode mode() const { return m_mode; }
	void setOrtho( bool on );
	bool ortho() const { return m_ortho; }
	void setOffsetDistance( double d ) { m_offsetDist = d; }

	void resetTool();
	void undo();

	//! skupina s nakreslenými čarami (vytvoří ji, pokud neexistuje)
	ccHObject* drawingGroup( bool create );

	//! rovina kreslení (půdorys / řez)
	void setFrame( const DrawFrame& f );
	const DrawFrame& frame() const { return m_frame; }
	//! zobrazí jen čáry aktuálního pohledu
	void updateLineVisibility();

	//! převod 3D bodu čáry na 2D bod výkresu (globálně), vrací false = vynechat
	using ExportMap = std::function<bool( int viewId, const CCVector3d& local3D, DgnV7::Point2& out )>;
	//! všechny nakreslené prvky jako prvky V7
	std::vector<DgnV7::Element> exportElements( const Drawing::SymbologyTable& table, const ExportMap& map ) const;

	//! natočení pohledu (rad) – směr osy X obrazovky ve světě
	double viewAngle() const;

	//! převod lokálních (posunutých) souřadnic na globální
	CCVector3d toGlobal( const CCVector2d& local ) const;

Q_SIGNALS:
	void statusChanged( const QString& text );
	void categoryChanged( int cat );
	void hiddenChanged( bool hidden );
	void orthoChanged( bool on );
	void modeChanged( Mode m );
	void rotateViewRequested( double angleRad );
	//! prostřední klik: místo v lokálních 3D souřadnicích (v půdorysu je z = NaN)
	void show3DRequested( double x, double y, double z );
	//! nový řez: body A, B (lokálně v půdorysu) a bod na straně, kam se díváme
	void sectionDefined( double ax, double ay, double bx, double by, double cx, double cy );

protected:
	bool eventFilter( QObject* obj, QEvent* event ) override;

private:
	struct Segment
	{
		CCVector2d a;
		CCVector2d b;
		ccPolyline* poly;
	};

	struct Action
	{
		enum Kind
		{
			Add,
			Remove,
			Modify
		} kind;
		unsigned id = 0;
		ccPolyline* poly = nullptr; //!< u smazaných držíme objekt
		CCVector2d a;               //!< původní body (Modify)
		CCVector2d b;
	};
	using UndoItem = std::vector<Action>;

	// převody souřadnic
	bool screenToLocal( const QPoint& pos, CCVector2d& out ) const;
	bool localToScreen( const CCVector2d& P, CCVector2d& centered ) const;
	double pixelSize() const;

	// geometrie výkresu
	std::vector<Segment> collectSegments() const;
	ccPolyline* pickLine( const QPoint& pos, CCVector2d* clicked = nullptr ) const;
	bool lineEnds( ccPolyline* poly, CCVector2d& a, CCVector2d& b ) const;
	bool lineExists( ccPolyline* poly ) const;
	CCVector2d snapPoint( const QPoint& pos, SnapType& type ) const;
	CCVector2d applyOrtho( const CCVector2d& from, const CCVector2d& to ) const;
	CCVector2d cursorPoint( const QPoint& pos, SnapType& type, const CCVector2d* orthoFrom ) const;

	// úpravy
	ccPolyline* addLine( const CCVector2d& a, const CCVector2d& b, int cat, bool hidden, UndoItem* undo );
	void setLine( ccPolyline* poly, const CCVector2d& a, const CCVector2d& b, UndoItem* undo );
	void removeLine( ccPolyline* poly, UndoItem* undo );
	void pushUndo( UndoItem&& u );
	void select( ccPolyline* poly );

	// obsluha režimů
	void onDataPoint( const QPoint& pos );
	void onTypedValue( double v );
	bool previewGeometry( CCVector2d& a, CCVector2d& b ) const; //!< náhled upravované čáry

	void updateTip();
	void updateStatus();
	void redraw2D();
	void redraw3D();
	void keepCrossCursor();

	ccMainAppInterface* m_app = nullptr;
	ccGLWindowInterface* m_win = nullptr;
	QObject* m_winObject = nullptr;
	int m_savedInteraction = 0;

	unsigned m_cloudID = 0;
	CCVector3d m_shift;
	double m_scale = 1.0;
	double m_planFront = 0.0; //!< výška čar půdorysu (nad mračnem)
	DrawFrame m_frame;

	int m_category = Drawing::Walls;
	bool m_hidden = false;
	Mode m_mode = Mode::Line;
	bool m_ortho = false;
	double m_offsetDist = 0.0;

	// stav nástroje
	int m_step = 0;
	std::vector<CCVector2d> m_chain; //!< body rozkreslené čáry
	ccPolyline* m_sel = nullptr;     //!< vybraná čára (úpravy)
	ccPolyline* m_sel2 = nullptr;
	CCVector2d m_selClick;           //!< kde byla čára vybrána
	CCVector2d m_base;               //!< výchozí bod přesunu/kopie
	int m_selEnd = 0;                //!< upravovaný konec (prodloužení)
	CCVector2d m_secB;               //!< druhý bod čáry řezu

	CCVector2d m_cursor;
	SnapType m_cursorSnap = SnapType::None;
	bool m_hasCursor = false;
	QString m_typed; //!< hodnota zadávaná z klávesnice

	QPoint m_rightPressPos;
	bool m_rightPressed = false;
	QPoint m_midPressPos;
	bool m_midPressed = false;

	// náhled (2D prvky v okně)
	ccPolyline* m_tip = nullptr;
	ccPointCloud* m_tipVerts = nullptr;
	ccPolyline* m_ghost = nullptr;
	ccPointCloud* m_ghostVerts = nullptr;
	ccPolyline* m_marker = nullptr;
	ccPointCloud* m_markerVerts = nullptr;

	std::vector<UndoItem> m_undo;
};
