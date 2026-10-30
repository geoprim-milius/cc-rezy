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

	View3D( ccMainAppInterface* app, QWidget* parent = nullptr );
	~View3D() override;

	//! připraví kopii mračna (pokud ještě není pro toto mračno)
	bool setSource( ccPointCloud* cloud, unsigned maxPoints );

	//! zaměří pohled na okolí bodu (lokální souřadnice mračna), natočení podle pohledu
	void focusOn( const CCVector3d& localP, double viewAngle );

	//! nakreslené čáry (kopie pro toto okno)
	void setLines( const std::vector<Line>& lines );

	unsigned sourceID() const { return m_sourceID; }
	double scale() const { return m_scale; }

private:
	void release();
	void clearLines();
	void applyClip();
	void updateRanges();   //!< rozsah posuvníků podle mračna kolem bodu
	void updateValueLabels();
	void saveSettings() const;

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
};
