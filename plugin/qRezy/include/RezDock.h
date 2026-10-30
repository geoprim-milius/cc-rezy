#pragma once

#include <QDockWidget>

#include "DrawTool.h"
#include "ccHObject.h"

#include <vector>

class ccMainAppInterface;
class ccGenericPointCloud;
class QLabel;
class QDoubleSpinBox;
class QCheckBox;
class QPushButton;
class QToolButton;
class QShortcut;
class QComboBox;
class QSlider;
class QToolBar;
class QAction;
class QActionGroup;
class View3D;

//! Panel „Řezy“: pohled (půdorys / řezy / pohledy), výška a tloušťka řezu.
//! Kreslicí nástroje jsou v liště s ikonami, ukládání v samostatném okně.
class RezDock : public QDockWidget
{
	Q_OBJECT

public:
	RezDock( ccMainAppInterface* app, QWidget* parent = nullptr );
	~RezDock() override;

	void onSelectionChanged( const ccHObject::Container& selected );
	QToolBar* toolBar() const { return m_toolbar; }

protected:
	void closeEvent( QCloseEvent* event ) override;
	void showEvent( QShowEvent* event ) override;
	bool eventFilter( QObject* obj, QEvent* event ) override;

private:
	// svislé řezy a pohledy
	struct SectionInfo
	{
		int id = 0;
		int kind = 0;         //!< 0 = řez, 1 = pohled (fasáda)
		QString name;
		CCVector2d A, B;      //!< konce čáry (lokálně, půdorys)
		CCVector2d N;         //!< normála k pozorovateli (půdorys)
		double offset = 0.0;  //!< posun roviny ve směru pohledu (m)
		double depth = 15.0;  //!< pohled: hloubka za rovinou (m)
		double insX = 0.0;    //!< umístění počátku ve výkresu (globálně)
		double insY = 0.0;
		ccPolyline* poly = nullptr;
	};

	void buildPanel();
	void buildToolbar();

	ccGenericPointCloud* cloud() const;
	void setCloud( ccGenericPointCloud* cloud );
	void useSelected();
	void step( int direction );
	void applyClip();
	void clearClip();
	void updateLabels();
	void topView();
	void zeroFromFloor();
	double estimateFloor( ccGenericPointCloud* cloud ) const;
	void setDrawing( bool on );
	void exportV7();
	void openSaveDialog();
	bool chooseSeed( QWidget* parent );
	void editSymbology();
	void focusView();
	bool ensure3D( bool aux = false ); //!< aux = samostatné okno pomocného řezu
	void show3DAt( double x, double y, double z );
	void refresh3DLines();
	double planZLocal() const;

	// tloušťka
	void setThickness( double t );
	void thicknessStep( int direction );
	void syncThicknessSlider();
	void syncPosSlider();
	void onPosSlider( int cm );

	std::vector<SectionInfo> loadSections() const;
	bool sectionById( int id, SectionInfo& s ) const;
	void saveSection( const SectionInfo& s );
	void createSection( int kind, double ax, double ay, double bx, double by, double cx, double cy );
	void deleteActiveSection();
	DrawFrame frameFor( int viewId ) const;
	void activateView( int viewId );
	void refreshViewCombo();
	void sectionCamera( const SectionInfo& s );
	void onStatus( const QString& text );

	ccMainAppInterface* m_app = nullptr;
	unsigned m_cloudID = 0;
	ccHObject::Container m_lastSelection;

	// panel
	QLabel* m_cloudLabel = nullptr;
	QComboBox* m_viewCombo = nullptr;
	QCheckBox* m_enabled = nullptr;
	QWidget* m_heightRow = nullptr;
	QDoubleSpinBox* m_height = nullptr;
	QDoubleSpinBox* m_thickness = nullptr;
	QSlider* m_thickSlider = nullptr;
	QSlider* m_posSlider = nullptr; //!< poloha řezu (výška / posun) po 1 cm
	QLabel* m_posMin = nullptr;
	QLabel* m_posMax = nullptr;
	QDoubleSpinBox* m_step = nullptr;
	QDoubleSpinBox* m_zero = nullptr;
	QWidget* m_depthRow = nullptr;
	QDoubleSpinBox* m_depth = nullptr;
	QLabel* m_relLabel = nullptr;
	QToolButton* m_upBtn = nullptr;
	QToolButton* m_downBtn = nullptr;
	QLabel* m_status = nullptr;
	QShortcut* m_up = nullptr;
	QShortcut* m_down = nullptr;

	// lišta nástrojů
	QToolBar* m_toolbar = nullptr;
	QAction* m_drawAct = nullptr;
	QActionGroup* m_toolGroup = nullptr;
	QAction* m_orthoAct = nullptr;
	QAction* m_trackAct = nullptr;
	QAction* m_hiddenAct = nullptr;
	QComboBox* m_catCombo = nullptr;
	QDoubleSpinBox* m_offsetSpin = nullptr;

	DrawTool* m_draw = nullptr;
	View3D* m_view3D = nullptr;
	View3D* m_viewAux = nullptr; //!< pomocný řez (vlastní okno, neruší 3D náhled)
	double m_viewAngle = 0.0; //!< natočení půdorysného pohledu (rad)
	int m_activeView = 0;     //!< 0 = půdorys, jinak id řezu/pohledu
	QString m_lastPrompt;
};
