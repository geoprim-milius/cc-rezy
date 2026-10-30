#pragma once

// Druhé okno: 3D náhled okolí místa, na které se klikne prostředním tlačítkem.
// Zobrazuje zředěnou kopii mračna (originál může být jen v jednom 3D okně)
// a kopie nakreslených čar. Ořez okolí posuvníky od–do ve třech směrech
// (jako Cross Section v CloudCompare): šířka a hloubka podle natočení pohledu, výška.

#include <QDockWidget>

#include <CCGeom.h>
#include <ccColorTypes.h>
#include <ccDrawableObject.h>

#include <vector>

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
	};

	View3D( ccMainAppInterface* app, QWidget* parent = nullptr, bool auxOnly = false );
	~View3D() override;

	//! připraví kopii mračna (pokud ještě není pro toto mračno)
	bool setSource( ccPointCloud* cloud, unsigned maxPoints );

	//! zaměří pohled na okolí bodu (lokální souřadnice mračna), natočení podle pohledu
	void focusOn( const CCVector3d& localP, double viewAngle );

	//! nakreslené čáry (kopie pro toto okno)
	void setLines( const std::vector<Line>& lines );

	//! pomocný řez: pravoúhlý pohled na rovinu A–B (lokálně), N = k pozorovateli; jen pro měření
	void auxSection( const CCVector3d& A, const CCVector3d& B, const CCVector3d& N, double planZ, double zeroGlobal, double thickness );
	bool isAux() const { return m_aux; }

	unsigned sourceID() const { return m_sourceID; }

protected:
	bool eventFilter( QObject* obj, QEvent* event ) override;
	double scale() const { return m_scale; }

private:
	void release();
	void clearLines();
	void applyClip();
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
	double m_scale = 1.0;
	QLabel* m_info = nullptr;

	// ořez
	RangeSlider* m_rs[3] = { nullptr, nullptr, nullptr }; //!< šířka (U), hloubka (V), výška (Z) – metry od bodu
	QLabel* m_rsLabel[3] = { nullptr, nullptr, nullptr };
	QCheckBox* m_showLines = nullptr;
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
	double m_zeroG = 0.0;
	class QPushButton* m_backBtn = nullptr;
	class ccPolyline* m_measPoly = nullptr;
	int m_measStep = 0;
	CCVector3d m_m1;
	CCVector3d m_m2;
	CCVector3d m_mCursor;
};
