#pragma once

#include "CloudLines.h"

// Kreslení a úpravy 2D čar nad řezem s ovládáním jako v MicroStationu:
// levé tlačítko = datový bod, pravé = reset, Shift + levé tažení nebo tažení kolečkem = posun,
// kolečko = zoom, prostřední kliknutí = ukázat místo ve 3D.
// Uchopení na konce, středy, průsečíky a na čáru.

#include "Drawing.h"

#include <QObject>
#include <QPointer>
#include <QDialog>
#include <QPoint>
#include <QString>
#include <QStringList>

#include <CCGeom.h>

#include <ccColorTypes.h>

#include <array>
#include <functional>
#include <set>
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
		Door,         //!< dveře v půdorysu: závěs, druhý konec otvoru, strana → křídlo + oblouk + kóta šířky (výška z pomocného řezu)
		Opening,      //!< okno v půdorysu: 2 body otvoru, strana kóty → kóta délky (výška(parapet) z pomocného řezu, funkce Okno)
		RoomLabel,    //!< popis místnosti: klik do místnosti → pomocný řez (podlaha, strop) → okno s údaji → umístění (případně s odkazovou čárou)
		RefCopy,      //!< převzít z reference pohledu: obdélník 2 kliky → čáry reference uvnitř se zkopírují do výkresu
		Info,         //!< informace o prvku: klik → okno s hladinou, typem, délkou, body…
		Dim           //!< délková kóta: 2 body, poloha kótovací čáry, další body = řetěz; pravé = konec
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
	//! délkové kóty: směr (0 zarovnaná, 1 vodorovně, 2 svisle – podle os obrazovky), zaokrouhlení v mm, text ručně
	void setDimOrient( int o );
	int dimOrient() const { return m_dimOrient; }
	void setDimRound( int mm );
	int dimRound() const { return m_dimRound; }
	void setDimTextManual( bool on );
	bool dimTextManual() const { return m_dimTextManual; }
	static bool isDim( const ccHObject* obj );
	//! hladiny, ze kterých nevedou vodicí čáry (z půdorysu ani vodorovné z Ref. pohledu)
	void setGuideLayersOff( const std::set<int>& cats );
	void benchFill();
	//! kóta výšková nebo délková (značka kreslená z metadat, ne text ani čára)
	static bool isSymbol( const ccHObject* obj ) { return isKota( obj ) || isDim( obj ); }
	int kotaScale() const { return m_kotaScale; }
	void setKotaSymbology( int level, int color ) { m_kotaLevel = level; m_kotaColor = color; }
	static bool isKota( const ccHObject* obj );
	//! okno s informacemi o prvku (hladina, typ, styl, délka, body, text…)
	void showInfo( ccPolyline* poly );
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
	//! jen rozsah změřených bodů v řezu/pohledu (po změně hloubky / posunu), bez resetu nástroje
	void updatePointRange( const DrawFrame& f );
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
	//! půdorys jako reference v pohledu/řezu: posun líce (m od ±0,000), barevně podle hladin, svislé vodicí čáry z líce
	//! guideMode: 0 bez, 1 viditelné ve směru pohledu (b), 2 průsečíky roviny řezu (a), 3 obojí
	void setViewRefOptions( double shiftM, bool color, int guideMode );
	//! reference jiného pohledu / řezu (viewId <= 0 = žádná): vedle (pohled←cokoli, řez←řez) nebo promítnutá v dosahu (řez←pohled)
	void setViewReference2( int viewId, const DrawFrame& refFrame, int refKind, int curKind, double sideShiftM, bool hGuides, double bandM );
	void copyFromReference( const CCVector2d& c0, const CCVector2d& c1 );
	//! výška bodu mračna pod kurzorem: funkce (pozice v okně → bod v lokálních souř. a text) a zapnutí
	using ProbeFunc = std::function<bool( const QPoint&, CCVector3d&, QString& )>;
	void setProbeFunc( ProbeFunc f ) { m_probeFunc = std::move( f ); }
	void setProbe( bool on );
	//! zobrazení hladin: vypnuté hladiny (id) se nekreslí, nejsou v referencích, vodicích čarách ani uchopení (export beze změny)
	void setHiddenLayers( const std::set<int>& cats );
	bool layerHidden( const ccHObject* obj ) const;
	//! reference jiného patra v půdorysu barevně (jinak šedě)
	void setPlanRefColor( bool on );
	//! viditelné vodicí čáry: jak daleko za rovinou řezu brát vrcholy (m)
	void setGuideDepth( double m );
	double viewRefTop() const { return m_viewRefShift; }
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
	//! údaje popisu místnosti (texty tak, jak se zapíšou)
	struct RoomLabelData
	{
		QString number;
		QString floor;  //!< podlaha Bpv, např. 386.330
		QString height; //!< světlá výška, např. 3.320
		QString area;   //!< výměra bez jednotky, např. 24.33
		QString name;
		bool leader = false; //!< umístit mimo místnost s odkazovou čárou (kroužek, lom, popis)
	};
	//! výměra místnosti kolem posledního kliku v Popisu místnosti [m²], < 0 = obrys se nenašel
	double roomArea() const { return m_roomArea; }
	//! začít umisťovat popis místnosti (za myší / s odkazovou čárou)
	void startRoomPlace( const RoomLabelData& d );
	int doorPendingKind() const { return m_doorKind; } //!< 0 dveře, 1 okno
	void clearDoorPending() { m_doorPend = false; m_roomPoly.clear(); m_roomPolyDone = false; }
	//! připravený text k ručnímu umístění (text visí na kurzoru, střed dole; klik = vložit, Esc = zrušit)
	void startPlaceText( const QString& text, double angle, int level );
	//! trám z pomocného řezu: 1. klik = začátek odkazové čáry, 2. klik = lom, text nad vodorovnou čarou (hladina PREKLADY)
	void startPlaceBeam( const QString& text, int level );
	//! severka (pohled / řez): značka na kurzoru natočená na skutečný sever, klik = umístit (hladina 14)
	void startPlaceNorth();
	//! značky řezů a pohledů do půdorysů (A–A', B–B' …): úsečky a texty v souřadnicích půdorysu (lokálně, x y)
	struct SectionMarks
	{
		std::vector<std::pair<CCVector2d, CCVector2d>> segs;
		struct Text
		{
			CCVector2d origin; //!< vlevo dole, natočení 0
			QString text;
		};
		std::vector<Text> texts;
		double textHeight = 0.0; //!< lokální jednotky
	};
	SectionMarks sectionMarks() const;


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
	//! klávesa vodicích čar v řezu / pohledu: 0 = N (svislé z půdorysu), 1 = M (vodorovné z reference)
	void guideKey( int which );
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
	void doorHeightWanted( int kind ); //!< 0 dveře (Výška), 1 okno (Okno)

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
		bool guide = false; //!< vodicí čára reference (bod na ní jde získat pro trasování)
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
	std::vector<ccPolyline*> pickAll( const QPoint& pos ) const; //!< všechny prvky v toleranci kliku, nejbližší první
	std::vector<ccPolyline*> m_cycle; //!< výběr: prvky v místě posledního kliku (pravé = další)
	size_t m_cycleIdx = 0;
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
	//! bod leží na vodicí čáře reference (dir = její směr)
	bool onGuide( const CCVector2d& P, CCVector2d* dir = nullptr ) const;
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
		CCVector3d textDir = CCVector3d( 0, 0, 0 ); //!< směr textu (nula = osa U výkresu)
		CCVector3d textUp = CCVector3d( 0, 0, 0 );  //!< směr „nahoru“ textu (nula = osa V)
	};
	KotaGeom kotaGeom( const CCVector3d& tip, const CCVector3d& U, const CCVector3d& V, int side, int fmt, int scale ) const;
	KotaGeom kotaGeom( const ccPolyline* poly ) const;
	//! délková kóta: A, B = kótované body, L = bod na kótovací čáře, D = směr kótovací čáry (3D lokální)
	KotaGeom dimGeom( const CCVector3d& A, const CCVector3d& B, const CCVector3d& L, const CCVector3d& D, const CCVector3d& U, const CCVector3d& V, double textT, int roundMm ) const;
	KotaGeom dimGeom( const ccPolyline* poly ) const;
	//! geometrie značky (výšková nebo délková kóta)
	KotaGeom symGeom( const ccPolyline* poly ) const;
	ccPolyline* addDim( const CCVector3d& A, const CCVector3d& B, const CCVector3d& L, const CCVector3d& D, double textT, int roundMm, UndoItem* undo, const ccPolyline* like = nullptr );
	//! délková kóta z bodů ve výkresu (u, v)
	ccPolyline* addDimUV( const CCVector2d& A, const CCVector2d& B, const CCVector2d& L, const CCVector2d& D, double textT, UndoItem* undo );
	//! kopie / posun / otočení délkové kóty: nové body a směr (u, v)
	ccPolyline* remakeDim( const ccPolyline* poly, const CCVector2d& shift, const CCVector2d* rotC, double ang, UndoItem* undo );
	//! směr kótovací čáry pro body A, B podle volby směru
	CCVector2d dimDirFor( const CCVector2d& A, const CCVector2d& B ) const;
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
	QPointer<QDialog> m_infoDlg;     //!< okno Informace o prvku (nemodální, znovu použité)
	class QTextBrowser* m_infoText = nullptr;

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
	// půdorys v pohledu (reference): úsečky v souřadnicích pohledu, barvy, vodicí čáry
	ProbeFunc m_probeFunc;
	bool m_probeOn = false;
	bool m_probeHas = false;
	CCVector3d m_probeP;
	QString m_probeText;
	class QTimer* m_probeTimer = nullptr;
	double m_viewRefShift = -1.0; //!< líc půdorysu (strana k pozorovateli) v m od ±0,000
	bool m_viewRefColor = false;
	int m_viewRefGuideMode = 1;
	std::set<int> m_hiddenCats;
	std::set<int> m_hiddenLevels; //!< DGN hladiny vypnutých hladin (texty)
	bool m_planRefColor = false;
	double m_guideDepth = 20.0;
	std::vector<ccColor::Rgb> m_viewRefGuideCol; //!< barva svislých vodicích čar (podle hladiny, když „barva“)
	int m_ref2View = -1;
	DrawFrame m_ref2Frame;
	bool m_hasRef2Frame = false;
	int m_ref2Kind = 1;
	int m_curKind = 1;
	double m_ref2Shift = 0.0;
	struct ViewRef2Seg
	{
		CCVector2d a, b;
		ccColor::Rgb color;
		int cat;
		bool hidden;
		ccPolyline* src;
	};
	std::vector<ViewRef2Seg> m_viewRef2;
	std::vector<ccPolyline*> m_refSel;      //!< Převzít z reference: vybrané čáry reference
	ccPolyline* refPick( const QPoint& pos ) const;
	void refWindow( const QPoint& a, const QPoint& b, bool ctrl );
	void copyRefSelection();    //!< reference jiného pohledu (u, v)
	struct HGuide
	{
		double v, u0, u1;
	};
	std::vector<HGuide> m_viewRefHGuides;   //!< vodorovné vodicí čáry z reference pohledu (ze společné strany)
	bool m_ref2HGuides = true;
	double m_ref2Band = 2.0;                //!< pás od společného kraje reference, ze kterého se vedou vodorovné čáry (m)
	CCVector2d m_refCopyA;                  //!< převzít z reference: první roh
	struct ViewRefSeg
	{
		CCVector2d a, b;
		ccColor::Rgb color;
	};
	std::vector<ViewRefSeg> m_viewRef;     //!< promítnutý půdorys (u, v)
	std::vector<CCVector2d> m_viewRefGuideU; //!< svislé vodicí čáry: (u, v začátku v promítnutém půdorysu)
	double m_viewRefGuideTop = 0.0;        //!< horní konec vodicích čar (v)
	void rebuildViewRef();
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
	bool m_placeBeam = false; //!< umisťuje se popis trámu s odkazovou čarou
	bool m_placeNorth = false; //!< umisťuje se severka
	double northAngle() const; //!< natočení severky ve výkresu (sever = nahoru při 0)
	void buildNorth( const CCVector2d& C, bool preview, std::vector<std::vector<CCVector2d>>* out );
	CCVector2d m_beamA;       //!< začátek odkazové čáry trámu
	struct BeamGeom
	{
		std::vector<CCVector2d> line; //!< (začátek) – lom – konec podtržení
		CCVector2d textP;             //!< počátek textu
		double ang = 0.0;
	};
	BeamGeom beamGeom( const CCVector2d* A, const CCVector2d& bend ) const;
	void buildBeam( const CCVector2d& bend );
	CCVector2d placeOrigin( const CCVector2d& c ) const;
	CCVector2d m_rotRef;
	void rotateItem( ccPolyline* poly, const CCVector2d& C, double ang, UndoItem* undo );
	double rotAngle() const;
	std::vector<ccPolyline*> rotTargets() const;
	CCVector3d m_pin3;
	bool m_hasPin = false;
	int m_kotaColor = 135;
	CCVector2d m_kotaTip;
	// délková kóta (rozpracovaná)
	std::set<int> m_guideOff;
	mutable std::vector<Segment> m_segCache; //!< collectSegments během jednoho pohybu myši
	mutable bool m_segCacheOn = false;
	mutable bool m_segCacheValid = false;
	int m_dimOrient = 0;
	int m_dimRound = 10;
	bool m_dimTextManual = false;
	CCVector2d m_dimA;          //!< 1. bod (u řetězu předchozí bod)
	CCVector2d m_dimB;          //!< 2. bod (čeká se na polohu čáry nebo na text)
	CCVector2d m_dimDir;        //!< směr kótovací čáry řetězu
	CCVector2d m_dimL;          //!< bod na kótovací čáře řetězu
	bool m_dimChain = false;    //!< kótovací čára už je dána (další body = řetěz)
	bool m_dimWaitText = false; //!< ruční text: čeká se na klik polohy textu
	CCVector2d m_dimPrevA;      //!< poslední vytvořená kóta (pro ruční text)
	CCVector2d m_dimPrevB;
	double m_clGap = 0.10;
	double m_clTol = 0.02;
	// dveře: kóta čeká na výšku (3D lokálně: střed kóty, směr textu, „nahoru“ textu)
	bool m_doorPend = false;
	// popis místnosti
	bool m_roomPlacing = false;
	RoomLabelData m_roomLab;
	double m_roomArea = -1.0;
	std::vector<CCVector2d> m_roomPoly; //!< obrys místnosti klikaný po bodech (výměra)
	bool m_roomPolyDone = false;        //!< obrys uzavřen, čeká se na pomocný řez
	void finishRoomPoly();
	double roomPolyArea( const std::vector<CCVector2d>& p ) const; //!< výměra v m²
	CCVector2d m_lead0;
	CCVector2d m_lead1;
	struct RoomGeom
	{
		std::vector<std::pair<CCVector2d, CCVector2d>> lines; //!< rámeček (POP_MISTN)
		struct Txt
		{
			CCVector2d P; //!< levý dolní roh
			QString s;
			double factor; //!< násobek výšky písma
			int level;
		};
		std::vector<Txt> texts;
		CCVector2d leftMid; //!< levá hrana rámečku u dělicí čáry (konec odkazové čáry)
		double h = 0.0;     //!< výška písma polí (místní)
		CCVector2d R;       //!< směr vpravo na obrazovce
		CCVector2d U;       //!< směr nahoru na obrazovce
	};
	RoomGeom roomGeom( const CCVector2d& cursor ) const;
	std::vector<CCVector2d> circlePts( const CCVector2d& c, double r ) const;
	void buildRoomLabel( const CCVector2d& cursor );
	double computeRoomArea( const CCVector2d& P ) const;
	int m_doorKind = 0;
	CCVector3d m_doorC;
	CCVector3d m_doorT;
	CCVector3d m_doorUp;
	void buildDoor( const CCVector2d& A, const CCVector2d& B, const CCVector2d& side, bool window );
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
