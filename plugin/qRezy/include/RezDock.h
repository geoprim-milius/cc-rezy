#pragma once

#include <QDockWidget>

#include "DrawTool.h"
#include "ccHObject.h"

#include <QByteArray>

#include <array>

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
class Ribbon;

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
		double face = -1.0;   //!< pohled: líc objektu za rovinou (m), -1 = neznámo
		ccPolyline* poly = nullptr;
	};

	// patra (půdorysy): id 0 = 1. patro, další id >= FloorBase
	struct FloorInfo
	{
		int id = 0;
		QString name;
		double cut = 0.0;   //!< výška řezu (globálně)
		double level = 0.0; //!< podlaha patra (globálně) – rovina kresby ve 3D
	};
	static constexpr int FloorBase = 1000;
	static bool isPlanView( int id ) { return id == 0 || id >= FloorBase; }
	std::vector<FloorInfo> loadFloors() const;
	void saveFloors( const std::vector<FloorInfo>& floors );
	bool floorById( int id, FloorInfo& f ) const;
	void storeActiveFloor();
	double currentLevel() const;
	void newFloor();
	void storeState( bool withCamera ); //!< nastavení panelu (+ pohled kamery) do projektu
	void saveProject();                 //!< uložit rozdělanou práci (projekt CloudCompare .bin)
	bool m_restoring = false;
	void refreshRefCombo();
	double estimateFloorRange( ccGenericPointCloud* cloud, double zMin, double zMax, bool& ok ) const;
	double estimateFace( const SectionInfo& si ) const;

	void buildPanel();
	void buildToolbar();
	QAction* mkAct( const char* icon, const QString& label, const QString& tip, bool checkable = false );
	void refreshIcons();
	void refreshCatCombo();
	void fitPanelWidth(); //!< šířka panelu podle obsahu
	void importDgn();     //!< načíst výkres V7 do aktivního půdorysu / řezu
	//! rastr mračna (TIFF + .tfw) pro půdorys patra nebo řez/pohled v souřadnicích výkresu
	bool writeCloudRaster( const QString& path, int viewId, double px, int dot, QString& info );
	std::vector<std::array<double, 4>> slicePlanes() const; //!< roviny aktuálního řezu (pro čáru z mračna)
	void renameView();

public:
	void setTheme( const QString& theme );
	void setWorkMode( bool on );

private:

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
	void exportV7( const std::vector<int>& floorIds, bool sections );
	void openSaveDialog();
	bool chooseSeed( QWidget* parent );
	void editSymbology();
	void focusView();
	bool ensure3D( bool aux = false ); //!< aux = samostatné okno pomocného řezu
	void show3DAt( double x, double y, double z, bool pin = true );
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
	QWidget* m_floorRow = nullptr;
	QDoubleSpinBox* m_floorLevel = nullptr;
	QComboBox* m_refCombo = nullptr;
	QCheckBox* m_refSnap = nullptr;
	QWidget* m_refRow = nullptr;
	int m_lastPlan = 0;
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
	Ribbon* m_ribbon = nullptr;
	std::vector<std::pair<QAction*, QString>> m_iconActs;
	QAction* m_bgDarkAct = nullptr;
	QAction* m_bgLightAct = nullptr;
	QAction* m_workAct = nullptr;
	QByteArray m_savedLayout;

	DrawTool* m_draw = nullptr;
	View3D* m_view3D = nullptr;
	View3D* m_viewAux = nullptr; //!< pomocný řez (vlastní okno, neruší 3D náhled)
	double m_viewAngle = 0.0; //!< natočení půdorysného pohledu (rad)
	int m_activeView = 0;     //!< 0 = půdorys, jinak id řezu/pohledu
	QString m_lastPrompt;
};
