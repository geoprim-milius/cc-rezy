#pragma once

#include <QDockWidget>

#include "ccHObject.h"

class ccMainAppInterface;
class ccGenericPointCloud;
class QLabel;
class QDoubleSpinBox;
class QCheckBox;
class QPushButton;
class QShortcut;
class QComboBox;
class DrawTool;
class View3D;
class QButtonGroup;

//! Panel „Řezy“: vodorovný řez mračnem s posunem nahoru/dolů
class RezDock : public QDockWidget
{
	Q_OBJECT

public:
	RezDock( ccMainAppInterface* app, QWidget* parent = nullptr );
	~RezDock() override;

	void onSelectionChanged( const ccHObject::Container& selected );

protected:
	void closeEvent( QCloseEvent* event ) override;

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
	void toggleDrawing( bool on );
	void exportV7();
	void chooseSeed();
	void editSymbology();
	void focusView();
	bool ensure3D();
	void show3DAt( double localX, double localY );

	ccMainAppInterface* m_app = nullptr;
	unsigned m_cloudID = 0;
	ccHObject::Container m_lastSelection;

	QLabel* m_cloudLabel = nullptr;
	QPushButton* m_useSelectedBtn = nullptr;
	QCheckBox* m_enabled = nullptr;
	QDoubleSpinBox* m_height = nullptr;
	QDoubleSpinBox* m_thickness = nullptr;
	QDoubleSpinBox* m_step = nullptr;
	QDoubleSpinBox* m_zero = nullptr;
	QLabel* m_relLabel = nullptr;
	QShortcut* m_up = nullptr;
	QShortcut* m_down = nullptr;

	// kreslení
	DrawTool* m_draw = nullptr;
	QPushButton* m_drawBtn = nullptr;
	QComboBox* m_catCombo = nullptr;
	QCheckBox* m_hiddenChk = nullptr;
	QCheckBox* m_orthoChk = nullptr;
	QButtonGroup* m_toolGroup = nullptr;
	QDoubleSpinBox* m_offsetSpin = nullptr;
	QDoubleSpinBox* m_radius3D = nullptr;
	double m_viewAngle = 0.0; //!< natočení půdorysného pohledu (rad)
	View3D* m_view3D = nullptr;
	QLabel* m_status = nullptr;
	QLabel* m_seedLabel = nullptr;
};
