#pragma once

// Kreslení 2D čar nad řezem s ovládáním jako v MicroStationu:
// levé tlačítko = datový bod, pravé kliknutí = reset (konec čáry), pravé tažení = posun pohledu,
// kolečko = zoom. Uchopení na konce, středy, průsečíky a na čáru.

#include "Drawing.h"

#include <QObject>
#include <QPoint>
#include <QString>

#include <CCGeom.h>

#include <vector>

class ccMainAppInterface;
class ccGLWindowInterface;
class ccHObject;
class ccPolyline;
class ccPointCloud;
class ccGenericPointCloud;

class DrawTool : public QObject
{
	Q_OBJECT

public:
	enum class Mode
	{
		Line,  //!< kreslení úseček navazujících na sebe
		Delete //!< mazání čar kliknutím
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
	void setSnap( bool on ) { m_snapEnabled = on; }

	//! natočení os pro ortogonální kreslení (rad)
	void setAxisAngle( double rad ) { m_axisAngle = rad; }
	double axisAngle() const { return m_axisAngle; }
	//! další klik na čáru nastaví natočení os podle ní
	void pickAxisFromLine() { m_pickAxis = true; }

	void resetChain();
	void undo();

	//! skupina s nakreslenými čarami (vytvoří ji, pokud neexistuje)
	ccHObject* drawingGroup( bool create );

	//! všechny nakreslené prvky jako prvky V7 v globálních souřadnicích
	std::vector<DgnV7::Element> exportElements( const Drawing::SymbologyTable& table ) const;

Q_SIGNALS:
	void statusChanged( const QString& text );
	void categoryChanged( int cat );
	void hiddenChanged( bool hidden );
	void orthoChanged( bool on );

protected:
	bool eventFilter( QObject* obj, QEvent* event ) override;

private:
	struct Segment
	{
		CCVector2d a;
		CCVector2d b;
		ccPolyline* poly;
	};

	bool screenToLocal( const QPoint& pos, CCVector2d& out ) const;
	bool localToScreen( const CCVector2d& P, CCVector2d& centered ) const;
	double pixelSize() const;
	std::vector<Segment> collectSegments() const;
	CCVector2d snapPoint( const QPoint& pos, SnapType& type ) const;
	CCVector2d applyOrtho( const CCVector2d& from, const CCVector2d& to ) const;
	void addPoint( const CCVector2d& P );
	ccPolyline* addLine( const CCVector2d& a, const CCVector2d& b );
	void deleteAt( const QPoint& pos );
	void updateTip();
	void updateStatus();
	void redraw2D();
	void placeTypedDistance();

	ccMainAppInterface* m_app = nullptr;
	ccGLWindowInterface* m_win = nullptr;
	QObject* m_winObject = nullptr;
	int m_savedInteraction = 0;

	unsigned m_cloudID = 0;
	CCVector3d m_shift;
	double m_scale = 1.0;
	double m_drawZ = 0.0; //!< výška (lokální), ve které se čáry zobrazují

	int m_category = Drawing::Walls;
	bool m_hidden = false;
	Mode m_mode = Mode::Line;
	bool m_ortho = false;
	bool m_snapEnabled = true;
	double m_axisAngle = 0.0;
	bool m_pickAxis = false;

	std::vector<CCVector2d> m_chain; //!< body rozkreslené čáry (lokální souřadnice)
	CCVector2d m_cursor;
	SnapType m_cursorSnap = SnapType::None;
	bool m_hasCursor = false;
	QString m_typed; //!< zadávaná délka z klávesnice

	QPoint m_rightPressPos;
	bool m_rightPressed = false;

	// náhled (2D prvky v okně)
	ccPolyline* m_tip = nullptr;
	ccPointCloud* m_tipVerts = nullptr;
	ccPolyline* m_marker = nullptr;
	ccPointCloud* m_markerVerts = nullptr;

	struct UndoItem
	{
		bool added;            //!< true = čára byla přidána (undo ji smaže)
		ccPolyline* poly;      //!< pro smazané čáry držíme objekt
		unsigned id;
	};
	std::vector<UndoItem> m_undo;
};
