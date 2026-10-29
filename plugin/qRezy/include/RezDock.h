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
};
