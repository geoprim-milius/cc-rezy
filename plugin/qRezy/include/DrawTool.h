#pragma once

#include "CloudLines.h"

// Kreslení a úpravy 2D čar nad řezem s ovládáním jako v MicroStationu:
// levé tlačítko = datový bod, pravé = reset, Shift + levé tažení nebo tažení kolečkem = posun,
// kolečko = zoom, prostřední kliknutí = ukázat místo ve 3D.
// Uchopení na konce, středy, průsečíky a na čáru.

#include "Drawing.h"

#include <QObject>
#include <QPoint>
#include <QString>
#include <QStringList>

#include <CCGeom.h>

#include <ccColorTypes.h>

#include <array>
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
	bool plan = true;                      //!< půdorys (patro) – jinak svislý řez / pohled
	CCVector3d O = CCVector3d( 0, 0, 0 );
	CCVector3d U = CCVector3d( 1, 0, 0 );
	CCVector3d V = CCVector3d( 0, 1, 0 );
	CCVector3d N = CCVector3d( 0, 0, 1 ); //!< k pozorovateli
	double front = 0.0;                    //!< posun čar k pozorovateli (aby je body nezakryly)
	double back = 0.0;                     //!< skutečná poloha čáry = vrchol − N·back (pro 3D náhled)
	double ptMin = -1e30;                  //!< změřené body: rozsah (P − O)·N, ve kterém se v řezu zobrazí
	double ptMax = 1e30;

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

//! zoom kolečkem k místu pod kurzorem (bod pod myší zůstane na místě)
void zoomAtCursor( ccGLWindowInterface* win, const QPoint& pos, float wheelDeltaDeg );

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
		RotateView, //!< natočit pohled: 2 body (směr 1. → 2. bod bude vodorovně doprava)
		SectionDef, //!< nový svislý řez / pohled: 2 body roviny + strana pohledu
		Measure,    //!< měření vzdálenosti
		Break,      //!< rozdělit čáru / smazat část mezi 2 body
		Trim,       //!< oříznout / prodloužit k hraniční čáře
		Text,       //!< vložit text
		Select,     //!< výběr (klik, Ctrl+klik, okno), úchopy na koncích
		Modify,     //!< upravit prvek: posun bližšího konce čáry
		InsertVertex, //!< vložit vrchol (rozdělí čáru, vrchol se umístí)
		DeleteVertex, //!< smazat vrchol (sloučí dvě navazující čáry)
		Arc,          //!< oblouk (3 body, metoda podle setArcMethod)
		CloudLine,    //!< čára z mračna: klik u bodů řezu → proložená čára, potvrdit / zahodit
		Kota,         //!< výšková kóta („kačena“) v řezu / pohledu: hrot, strana poličky
		BreakLine,    //!< rozdělit řeznou čarou: 2 body → všechny protnuté čáry se rozdělí v průsečíku
		Pin,          //!< špendlík: klik → ukázat místo ve 3D náhledu
		EditText,     //!< upravit text: klik na text → nový obsah
		PlaceText,    //!< umístit připravený text (z pomocného řezu) ručně – text visí na kurzoru
		Rotate,       //!< otočit prvek / výběr třemi body: střed, výchozí směr, cílový směr
		CloudRoom,    //!< čára z mračna – místnost: klik dovnitř → obrys místnosti s rohy, Enter potvrdit
		CloudBatch,   //!< čára z mračna – hromadně: celý plátek → všechny přímé úseky s rohy, Enter potvrdit
		Door          //!< dveře v půdorysu: závěs, druhý konec otvoru, strana → křídlo + oblouk + kóta šířky (výška z pomocného řezu)
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
	void setTracking( bool on );
	bool tracking() const { return m_trackingOn; }
	//! druh nové roviny pro SectionDef: 0 = řez, 1 = pohled (fasáda)
	void startSectionDef( int kind );
	//! oblouk: 0 = začátek–střed–konec, 1 = začátek–konec–střed, 2 = tři body (začátek–bod na oblouku–konec)
	void setArcMethod( int m );
	int arcMethod() const { return m_arcMethod; }
	//! čára z mračna: roviny aktuálního řezu (a·x + b·y + c·z + d ≥ 0, lokálně)
	using SliceProvider = std::function<std::vector<std::array<double, 4>>()>;
	void setSliceProvider( SliceProvider p ) { m_sliceProvider = std::move( p ); }
	void setCloudLineParams( double gap, double tol, bool square, double reach );
	void setCloudMinLen( double m ) { m_clMinLen = m; }
	//! výšková kóta: ±0,000 v Bpv (z panelu), zápis 0 = Bpv, 1 = místní, 2 = oboje; měřítko výkresu 1:N
	void setZeroProvider( std::function<double()> p ) { m_zeroProvider = std::move( p ); }
	void setKotaFormat( int f );
	int kotaFormat() const { return m_kotaFmt; }
	void setKotaScale( int n );
	int kotaScale() const { return m_kotaScale; }
	void setKotaSymbology( int level, int color ) { m_kotaLevel = level; m_kotaColor = color; }
	static bool isKota( const ccHObject* obj );
	//! změřené body s výškou v dosahu dané roviny (lokální 3D poloha, číslo) – pro výstup
	std::vector<std::pair<CCVector3d, QString>> pointsInFrame( const DrawFrame& f ) const;
	//! přesune čáry z hladiny na jinou (smazání hladiny); vrací počet
	int moveCategory( int fromId, int toId );
	int countCategory( int id ) const;
	//! import z DGN: prvky v souřadnicích aktuálního pohledu (UV)
	struct ImportPrim
	{
		int kind = 0;                 //!< 0 úsečky (lomená), 1 oblouk, 2 text
		std::vector<CCVector2d> pts;  //!< úsečky: vrcholy; text: poloha
		CCVector2d center;            //!< oblouk
		double radius = 0.0;
		double a0 = 0.0;              //!< oblouk: počátek (rad)
		double sweep = 0.0;           //!< oblouk: rozsah (rad, + proti směru)
		int cat = 0;
		bool hidden = false;
		QString text;
		int textLevel = 0;
		double textHeight = 0.0;
	};
	int importPrims( const std::vector<ImportPrim>& prims );
	void setExportSkipImported( bool on ) { m_exportSkipImported = on; }
	int countImported() const;
	//! obnoví barvy a názvy čar (po změně hladin)
	void refreshLooks();

	void resetTool();
	void undo();
	void selectAll(); //!< Ctrl+A: vše v aktuálním pohledu
	bool hasSelection() const { return !m_selSet.empty(); }

	//! skupina s nakreslenými čarami (vytvoří ji, pokud neexistuje)
	ccHObject* drawingGroup( bool create );

	//! rovina kreslení (půdorys / řez)
	void setFrame( const DrawFrame& f );
	//! výška půdorysného řezu (lokálně) – kvůli umístění čar ve 3D, nic neresetuje
	void setPlanHeight( double zLocal );
	const DrawFrame& frame() const { return m_frame; }
	//! zobrazí jen čáry aktuálního pohledu
	void updateLineVisibility();

	//! převod 3D bodu čáry na 2D bod výkresu (globálně), vrací false = vynechat
	using ExportMap = std::function<bool( int viewId, const CCVector3d& local3D, DgnV7::Point2& out )>;
	//! všechny nakreslené prvky jako prvky V7
	std::vector<DgnV7::Element> exportElements( const Drawing::SymbologyTable& table, const ExportMap& map, int textLevel, double textHeight ) const;

	//! načte změřené body (číslo Y X [Z] [kód], S-JTSK); vrací false a zprávu při chybě
	bool importPoints( const QString& path, QString& message );
	void setShowPoints( bool on );
	bool showPoints() const { return m_showPoints; }
	void setShowCodes( bool on );
	//! referenční výkres (jiné patro) – šedě, volitelně s uchopením; -1 = žádný
	void setReference( int viewId, bool snap );
	//! překreslí popisky (po změně barev apod.)
	void refreshDisplay() { redraw2D(); }
	bool showCodes() const { return m_showCodes; }
	//! výška bodu pod číslem: zapnuto, zápis 0 = Bpv, 1 = místní (k ±0)
	void setShowHeights( bool on, int fmt );
	bool showHeights() const { return m_showHeights; }
	int heightFormat() const { return m_heightFmt; }
	//! špendlík ve výkresu zobrazit / skrýt (poloha zůstane)
	void setPinVisible( bool on );
	//! text natočený podél směru (úhel v rovině pohledu, rad) na zadané hladině DGN (0 = výchozí hladina textů)
	ccPolyline* addTextAt( const CCVector2d& P, const QString& text, double angle, int level );
	//! výška otvoru posledních nakreslených dveří (text v mm) pod kótu šířky; false = žádné dveře nečekají
	bool writeDoorHeight( const QString& text );
	bool doorPending() const { return m_doorPend; }
	void clearDoorPending() { m_doorPend = false; }
	//! připravený text k ručnímu umístění (text visí na kurzoru, střed dole; klik = vložit, Esc = zrušit)
	void startPlaceText( const QString& text, double angle, int level );


	//! střed okna v rovině kreslení (lokálně 3D)
	bool viewCenter( CCVector3d& out ) const;
	//! špendlík z 3D náhledu: značka v kresbě (lokální 3D) a posun okna, aby byl uprostřed
	void showPin( const CCVector3d& P );
	void clearPin();

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
	void sectionDefined( int kind, double ax, double ay, double bx, double by, double cx, double cy );
	void trackingChanged( bool on );
	//! Ctrl + kolečko: změna tloušťky řezu (+1 / -1)
	void thicknessWheel( int direction );
	//! výkres se změnil (přidání, úprava, smazání)
	void drawingChanged();
	//! klávesa C: pomocný řez
	void auxSectionRequested();
	//! dveře nakresleny – čeká se na výšku otvoru z pomocného řezu
	void doorHeightWanted();

protected:
	bool eventFilter( QObject* obj, QEvent* event ) override;

private:
	struct Segment
	{
		CCVector2d a;
		CCVector2d b;
		ccPolyline* poly;
		bool ref = false; //!< čára referenčního výkresu (jen uchopení)
		bool arc = false; //!< úsek oblouku
	};

	struct Action
	{
		enum Kind
		{
			Add,
			Remove,
			Modify,
			Attr //!< změna hladiny / zakrytí
		} kind;
		int cat = 0;       //!< původní hladina (Attr)
		bool hidden = false;
		unsigned id = 0;
		ccPolyline* poly = nullptr; //!< u smazaných držíme objekt
		CCVector2d a;               //!< původní body (Modify)
		CCVector2d b;
		std::vector<CCVector2d> pts; //!< původní vrcholy oblouku (Modify)
	};
	using UndoItem = std::vector<Action>;

	// převody souřadnic
	bool screenToLocal( const QPoint& pos, CCVector2d& out ) const;
	bool localToScreen( const CCVector2d& P, CCVector2d& centered ) const;
	double pixelSize() const;

	// geometrie výkresu
	std::vector<Segment> collectSegments() const;
	ccPolyline* pickLine( const QPoint& pos, CCVector2d* clicked = nullptr ) const;
	std::array<CCVector2d, 4> textCorners( ccPolyline* poly, const CCVector2d& origin ) const; //!< obrys písmen textu
	bool lineEnds( ccPolyline* poly, CCVector2d& a, CCVector2d& b ) const;
	bool lineExists( ccPolyline* poly ) const;
	CCVector2d snapPoint( const QPoint& pos, SnapType& type ) const;
	CCVector2d applyOrtho( const CCVector2d& from, const CCVector2d& to ) const;
	using TrackLines = std::vector<std::pair<CCVector2d, CCVector2d>>; //!< (bod, směr) pomocných čar
	CCVector2d cursorPoint( const QPoint& pos, SnapType& type, const CCVector2d* orthoFrom, TrackLines* lines = nullptr ) const;

	// úpravy
	ccPolyline* addLine( const CCVector2d& a, const CCVector2d& b, int cat, bool hidden, UndoItem* undo );
	ccPolyline* addPolyline( const std::vector<CCVector2d>& pts, int cat, bool hidden, bool arc, UndoItem* undo );
	static bool isArc( const ccHObject* obj );
	std::vector<CCVector2d> polyPoints( ccPolyline* poly ) const;
	void setPolyPoints( ccPolyline* poly, const std::vector<CCVector2d>& pts, UndoItem* undo );
	//! posun prvku (čára, oblouk, text) o d; copy = vytvořit kopii
	void translateItem( ccPolyline* poly, const CCVector2d& d, bool copy, UndoItem* undo );
	bool rejectArc( ccPolyline* poly );
	// oblouk
	std::vector<CCVector2d> arcPoints( const CCVector2d& C, double r, double a0, double sweep ) const;
	bool arcPreview( std::vector<CCVector2d>& pts ) const;
	// čára z mračna
	bool detectCloudLine( const CCVector2d& click );
	void acceptCloudLine();
	//! body plátku v rovině kreslení (center = jen v dosahu reach)
	void slicePoints2D( std::vector<CCVector2d>& out, const CCVector2d* center, double reach ) const;
	CloudLines::Params cloudParams() const;
	void detectRoom( const CCVector2d& click );
	void detectBatch();
	void acceptBatch();
	int batchPhase() const;       //!< nejjistější fáze, která v náhledu zbývá (0–2)
	QString batchPrompt() const;
	QString roomPrompt() const;
	void skipBatchPhase();
	void setLine( ccPolyline* poly, const CCVector2d& a, const CCVector2d& b, UndoItem* undo );
	void removeLine( ccPolyline* poly, UndoItem* undo );
	void pushUndo( UndoItem&& u );
	void select( ccPolyline* poly );
	// výběrová sada (nástroj Výběr)
	void setSelection( const std::vector<ccPolyline*>& sel );
	void toggleSelected( ccPolyline* poly );
	bool isSelected( ccPolyline* poly ) const;
	void restoreLook( ccPolyline* poly );
	void applyLook( ccPolyline* poly, int cat, bool hidden );
	void pruneSelection();
	void deleteSelection();
	void selectWindow( const QPoint& p0, const QPoint& p1, bool add );
	bool pickHandle( const QPoint& pos, ccPolyline*& poly, int& end ) const;
	bool lineAttrs( ccPolyline* poly, int& cat, bool& hidden ) const;

	// obsluha režimů
	void onDataPoint( const QPoint& pos );
	void onTypedValue( double v );
	bool previewGeometry( CCVector2d& a, CCVector2d& b ) const; //!< náhled upravované čáry

	// náhledy v okně (2D čáry v pixelech, v popředí)
	struct Overlay
	{
		std::vector<CCVector2d> pts; //!< lokální souřadnice v rovině kreslení
		ccColor::Rgb color;
		float width = 1.0f;
	};
	void setOverlays( const std::vector<Overlay>& ovs );
	void clearOverlayPool();
	void addMarker( std::vector<Overlay>& ovs, const CCVector2d& P, SnapType t, const ccColor::Rgb& col ) const;
	void addArrow( std::vector<Overlay>& ovs, const CCVector2d& from, const CCVector2d& dir, double lenPx, const ccColor::Rgb& col ) const;

	// trasování (jako AutoCAD): získané body a pomocné čáry
	TrackLines trackRays( const CCVector2d* from ) const;
	bool trackingPoint( const CCVector2d& raw, const TrackLines& rays, CCVector2d& out, TrackLines* lines ) const;
	void onHoverTimeout();

	void updateTip();
	void updateStatus();
	void redraw2D();
	void redraw3D();
	void keepCrossCursor();
	void panBy( const QPoint& d );
	void showToolMenu( int group ); //!< 3 = Manipulace, 7 = Úpravy

	// změřené body a texty
	struct VisPoint
	{
		CCVector2d uv;
		QString name;
		QString code;
		bool hasZ = false;
		double zLocal = 0.0;
	};
	std::vector<VisPoint> visiblePoints() const;
	std::vector<VisPoint> framePoints( const DrawFrame& f, std::vector<CCVector3d>* local = nullptr ) const;
	void refreshLayer();
	ccPolyline* addText( const CCVector2d& P, const QString& text, UndoItem* undo );
	static bool isText( const ccHObject* obj );
	//! kóta: geometrie v lokálních 3D souřadnicích (úsečky po dvojicích, počátky a texty)
	struct KotaGeom
	{
		std::vector<CCVector3d> segs;
		std::vector<CCVector3d> textPos;
		QStringList texts;
		double textH = 0.0; //!< lokální jednotky
	};
	KotaGeom kotaGeom( const CCVector3d& tip, const CCVector3d& U, const CCVector3d& V, int side, int fmt, int scale ) const;
	KotaGeom kotaGeom( const ccPolyline* poly ) const;
	double heightOf( const CCVector3d& local ) const;
	//! průsečíky řezné čáry A–B s čarami pohledu (čára, bod)
	std::vector<std::pair<ccPolyline*, CCVector2d>> breakLineHits( const CCVector2d& A, const CCVector2d& B ) const;
	ccPolyline* addKota( const CCVector2d& P, int side, int fmt, int scale, UndoItem* undo, const ccPolyline* like = nullptr );
	bool pickingCursor() const;

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
	int m_defKind = 0;               //!< SectionDef: 0 = řez, 1 = pohled
	CCVector2d m_measA;              //!< měření
	CCVector2d m_measB;
	bool m_measDone = false;
	Mode m_prevMode = Mode::Line;    //!< návrat po natočení pohledu / definici řezu

	// trasování
	bool m_trackingOn = true;
	std::vector<CCVector2d> m_acquired;   //!< získané body
	std::vector<std::vector<CCVector2d>> m_acqDirs; //!< směry čar v získaném bodě (prodloužení)
	CCVector2d m_lastCand;                //!< poslední kandidát na získání bodu
	// kolmice: uchopený bod, jehož pata na kolmici je kurzor
	mutable bool m_perpRefValid = false;
	mutable CCVector2d m_perpRef;
	mutable SnapType m_perpRefType = SnapType::None;
	CCVector2d m_hoverCand;               //!< bod pod kurzorem (kandidát na získání)
	bool m_hoverValid = false;
	class QTimer* m_hoverTimer = nullptr;
	bool m_cursorTracked = false;         //!< kurzor je na pomocné čáře
	CCVector2d m_trackFrom;               //!< bod, od kterého vede pomocná čára
	std::vector<std::pair<CCVector2d, CCVector2d>> m_trackLines; //!< (bod, směr) zobrazené pomocné čáry

	CCVector2d m_cursor;
	SnapType m_cursorSnap = SnapType::None;
	bool m_hasCursor = false;
	QString m_typed; //!< hodnota zadávaná z klávesnice

	bool m_rightPressed = false;
	QPoint m_mousePos;           //!< poloha myši (kreslený kříž)
	bool m_panning = false;      //!< posun pohledu (Shift + levé / kolečko tažením)
	bool m_panMoved = false;
	Qt::MouseButton m_panButton = Qt::NoButton;
	QPoint m_panPress;
	QPoint m_panLast;

	class RezLayer* m_layer = nullptr; //!< popisky bodů a texty
	bool m_showPoints = true;
	bool m_showCodes = true;
	bool m_showHeights = true;
	int m_heightFmt = 0;
	bool m_pinVisible = true;
	int m_refView = -1;
	std::vector<ccPolyline*> m_selSet; //!< výběr
	bool m_winPressed = false;         //!< výběr: levé stisknuto
	bool m_winDragging = false;        //!< výběr oknem
	QPoint m_winStart;
	CCVector2d m_fixed;                //!< pevný konec (úprava prvku / úchop)
	CCVector2d m_far2;                 //!< vložit vrchol: druhý konec
	bool m_refSnap = false;
	QString m_lastText;
	QString m_flash; //!< jednorázová zpráva ve stavovém řádku
	bool m_exportSkipImported = false;

	// náhledy (2D prvky v okně) – zásobník znovu používaných čar
	std::vector<ccPolyline*> m_overlayPool;

	std::vector<UndoItem> m_undo;

	// kolmo na předchozí úsek (Enter při kreslení čáry) – platí pro jeden úsek
	bool m_perpLock = false;
	CCVector2d m_perpDir;

	// oblouk
	int m_arcMethod = 0;
	std::vector<CCVector2d> m_arcPts; //!< zadané body
	double m_arcSweep = 0.0;          //!< začátek–střed–konec: úhel sledovaný pohybem myši
	double m_arcPrevAng = 0.0;

	// čára z mračna
	SliceProvider m_sliceProvider;
	std::function<double()> m_zeroProvider;
	int m_kotaFmt = 0;
	int m_kotaScale = 70;
	int m_kotaLevel = 16;
	QString m_placeText;
	double m_placeAng = 0.0;
	int m_placeLevel = 0;
	CCVector2d placeOrigin( const CCVector2d& c ) const;
	CCVector2d m_rotRef;
	void rotateItem( ccPolyline* poly, const CCVector2d& C, double ang, UndoItem* undo );
	double rotAngle() const;
	std::vector<ccPolyline*> rotTargets() const;
	CCVector3d m_pin3;
	bool m_hasPin = false;
	int m_kotaColor = 135;
	CCVector2d m_kotaTip;
	double m_clGap = 0.10;
	double m_clTol = 0.02;
	// dveře: kóta čeká na výšku (3D lokálně: střed kóty, směr textu, „nahoru“ textu)
	bool m_doorPend = false;
	CCVector3d m_doorC;
	CCVector3d m_doorT;
	CCVector3d m_doorUp;
	void buildDoor( const CCVector2d& A, const CCVector2d& B, const CCVector2d& side );
	bool m_auxKeep = false; //!< stopa pomocného řezu v půdorysu (3D lokálně)
	CCVector3d m_auxKA;
	CCVector3d m_auxKB;
	CCVector3d m_auxKN;
	double m_clReach = 0.05; //!< čára z mračna: konec se napojí na průsečík jen do této vzdálenosti [m]
	bool m_clSquare = true;
	bool m_clHas = false;           //!< náhled k potvrzení
	CCVector2d m_clA;
	CCVector2d m_clB;
	ccPolyline* m_clPrev = nullptr; //!< naposledy vytvořená čára (napojení v rohu)
	bool m_clJoin = false;
	int m_clPrevEnd = 0;            //!< který konec předchozí čáry se posune
	CCVector2d m_clPrevA;           //!< předchozí čára po napojení
	CCVector2d m_clPrevB;
	struct ClJoin
	{
		ccPolyline* poly;
		CCVector2d a;
		CCVector2d b;
	};
	std::vector<ClJoin> m_clJoins;          //!< okolní čáry upravené do rohu s novou čárou z mračna
	std::vector<CloudLines::Seg> m_clBatch; //!< náhled místnosti / hromadného hledání
	CCVector2d m_clRoomClick;
	double m_clMinLen = 0.30;
};
