#pragma once

// Druhé okno: 3D náhled okolí místa, na které se klikne prostředním tlačítkem.
// Zobrazuje zředěnou kopii mračna (originál může být jen v jednom 3D okně)
// a kopie nakreslených čar. Ořez okolí posuvníky od–do ve třech směrech
// (jako Cross Section v CloudCompare): šířka a hloubka podle natočení pohledu, výška.

#include <QDockWidget>

#include <CCGeom.h>
#include <ccColorTypes.h>
#include <ccDrawableObject.h>

#include <ccBBox.h>

#include "Extract.h"

#include <QVector>
#include <vector>

class QTimer;

class ccMainAppInterface;
class ccGLWindowInterface;
class ccPointCloud;
class ccHObject;
class QLabel;
class QCheckBox;
class RangeSlider;

class View3D : public QDockWidget
{
	Q_OBJECT

public:
	struct Line
	{
		std::vector<CCVector3d> pts; //!< lokální souřadnice mračna
		ccColor::Rgb color;
		bool dashed = false;
		CCVector3d shiftDir; //!< směr posunu kresby posuvníkem (lokálně, na 1 m)
	};

	View3D( ccMainAppInterface* app, QWidget* parent = nullptr, bool auxOnly = false );
	~View3D() override;

	//! připraví kopii mračna (pokud ještě není pro toto mračno)
	bool setSource( ccPointCloud* cloud, unsigned maxPoints );

	//! zaměří pohled na okolí bodu (lokální souřadnice mračna), natočení podle pohledu
	void focusOn( const CCVector3d& localP, double viewAngle );

	//! špendlík: posune a přiblíží náhled na místo (natočení, perspektiva a ořez zůstanou), ukáže špendlík;
	//! plan = klik v půdorysu (jehla přes celou výšku ořezu)
	void pinAt( const CCVector3d& localP, double viewAngle, bool plan );
	//! špendlík v pomocném řezu (jen když místo leží v řezu; pohled se nemění)
	void auxPin( const CCVector3d& localP, bool plan );

	//! změřené body ze souboru (lokální 3D, číslo) a jejich barvy
	struct SurveyPt
	{
		CCVector3d P;
		QString name;
	};
	void setSurveyPoints( const std::vector<SurveyPt>& pts, const ccColor::Rgb& marker, const ccColor::Rgb& name );

	//! výškové kóty mezi po sobě jdoucími body (svislá kótovací čára, patky, hodnota); front = posun k pozorovateli
	void setMeasures( const std::vector<CCVector3d>& pts, const CCVector3d& N, double front );
	void clearMeasure();

	//! nakreslené čáry (kopie pro toto okno)
	void setLines( const std::vector<Line>& lines );

	//! pomocný řez: pravoúhlý pohled na rovinu A–B (lokálně), N = k pozorovateli; jen pro měření
	void auxSection( const CCVector3d& A, const CCVector3d& B, const CCVector3d& N, double planZ, double zeroGlobal, double thickness );
	bool isAux() const { return m_aux; }
	//! pomocný řez: přepnout režim zápisu (0 okno, 1 překlad, 2 výška, 3 strop, 4 podlaha) a ukázat nápovědu
	void setWriteMode( int id, const QString& hint = QString() );
	double auxFloor() const { return m_auxFloor; } //!< pomocný řez: odhad podlahy (místní z)
	double auxCeil() const { return m_auxCeil; }   //!< pomocný řez: odhad stropu (místní z)

	unsigned sourceID() const { return m_sourceID; }

Q_SIGNALS:
	//! prostřední klik do mračna ve 3D náhledu → místo (lokální souřadnice) pro špendlík ve výkresu
	void pinPicked( double x, double y, double z );
	//! pomocný řez: změřeny 2 body (lokální 3D) → kóta v 3D náhledu
	void measureDone( const QVector<double>& xyz, double nx, double ny );
	//! pomocný řez: zapsat výšku do půdorysu (kind 0 = okno, 1 = překlad, 2 = výška)
	void writeHeight( const QString& text, int kind, double x, double y ); //!< x, y = místo odečtu (místní)

protected:
	bool eventFilter( QObject* obj, QEvent* event ) override;
	double scale() const { return m_scale; }

private:
	void release();
	void clearLines();
	void applyClip();
	void buildLines();
	void updateRanges();   //!< rozsah posuvníků podle mračna kolem bodu
	void updateValueLabels();
	void saveSettings() const;
	void leaveAux( bool refocus );
	bool auxPoint( const QPoint& pos, CCVector3d& P ) const;
	void updateMeasure( bool live );
	QString heightText( const CCVector3d& P ) const;

	ccMainAppInterface* m_app = nullptr;
	ccGLWindowInterface* m_win = nullptr;
	QWidget* m_widget = nullptr;
	ccPointCloud* m_clone = nullptr;
	ccHObject* m_linesGroup = nullptr;
	unsigned m_sourceID = 0;
	//! výřez mračna v plné hustotě: roviny, ze kterých je m_clone vybraný (s rezervou)
	ccBBox m_srcBB;
	std::vector<Extract::Plane> m_regPlanes;
	QTimer* m_regTimer = nullptr;
	bool m_extracting = false;
	std::vector<Extract::Plane> wantedPlanes( double margin ) const;
	void updateRegion( bool now );
	void extractRegion();
	double m_scale = 1.0;
	QLabel* m_info = nullptr;

	// ořez
	RangeSlider* m_rs[3] = { nullptr, nullptr, nullptr }; //!< šířka (U), hloubka (V), výška (Z) – metry od bodu
	QLabel* m_rsLabel[3] = { nullptr, nullptr, nullptr };
	QCheckBox* m_showLines = nullptr;
	QCheckBox* m_showPoints = nullptr;
	QCheckBox* m_showSurvey = nullptr;
	QCheckBox* m_showPin = nullptr;
	void rebuildSurvey();
	std::vector<SurveyPt> m_survey;
	ccColor::Rgb m_surveyMarker = ccColor::Rgb( 255, 90, 255 );
	ccColor::Rgb m_surveyName = ccColor::Rgb( 255, 235, 0 );
	class RezLayer* m_surveyLayer = nullptr;
	void clearPin();
	void buildPin();
	ccHObject* m_pinGroup = nullptr;
	CCVector3d m_pinP;
	bool m_pinPlan = false;
	bool m_hasPin = false;
	QPoint m_midPress;
	bool m_midDown = false;
	QPoint m_rightPress;
	bool m_rightDown = false;
	bool pickPoint( const QPoint& pos, CCVector3d& P ) const;
	std::vector<Line> m_lines;            //!< čáry bez posunu
	class QSlider* m_shift = nullptr;     //!< posun kresby (cm)
	QLabel* m_shiftLabel = nullptr;
	CCVector3d m_focus;
	double m_angle = 0.0;
	bool m_hasFocus = false;
	ccClipPlaneSet m_clipSet;

	// průhlednost mračna
	class BlendGroup* m_blend = nullptr;
	class QSlider* m_alpha = nullptr;

	// nitkový kříž (pomocný řez)
	void updateCross( const QPoint& pos, bool show );
	class ccPolyline* m_cross[2] = { nullptr, nullptr };

	// pomocný řez
	bool m_auxOnly = false; //!< samostatné okno jen pro pomocný řez
	bool m_aux = false;
	CCVector3d m_auxA;
	CCVector3d m_auxU;
	CCVector3d m_auxN;
	double m_auxLen = 0.0;
	double m_auxFloor = 0.0; //!< odhad podlahy (místní z)
	double m_auxCeil = 0.0;  //!< odhad stropu
	double m_zeroG = 0.0;
	class QPushButton* m_backBtn = nullptr;
	class ccPolyline* m_measPoly = nullptr;
	// zápis výšky do půdorysu (jen pomocný řez)
	class QWidget* m_writeRow = nullptr;
	class QButtonGroup* m_writeMode = nullptr; //!< 0 okno, 1 překlad, 2 výška, 3 strop
	int m_parapetMM = -1;
	void writeMeasured();
	void auxZoom( double len );
	// kóta měření v 3D náhledu
	ccHObject* m_measGroup = nullptr;
	class RezLayer* m_measLayer = nullptr;
	int m_measStep = 0;
	std::vector<CCVector3d> m_mp; //!< kliknuté body měření (okno 3, jinak 2)
	int measNeeded() const; //!< počet kliků (bez definované podlahy)
	int measTotal() const;  //!< počet bodů měření včetně podlahy (okno 3, podlaha 1, jinak 2)
	bool floorMode() const; //!< režim zápisu Podlaha (výška podlahy)
	QString floorText( const CCVector3d& P ) const;
	std::vector<CCVector3d> measPts( const std::vector<CCVector3d>& clicks ) const; //!< kliky + podlaha
	void updateFloor();
	class QToolButton* m_floorBtn = nullptr;
	class QToolButton* m_rotBtn = nullptr; //!< pomocný řez: Text 90° (klávesa B)
	QWidget* m_modeCol = nullptr; //!< sloupec režimů zápisu (pomocný řez)
	bool m_floorPick = false;
	bool m_hasFloor = false;
	double m_floorZ = 0.0;
	ccHObject* m_floorGroup = nullptr;
	CCVector3d m_mCursor;
};
