#pragma once

// Druhé okno: 3D náhled okolí místa, na které se v půdorysu klikne prostředním tlačítkem.
// Zobrazuje zředěnou kopii mračna (originál může být jen v jednom 3D okně).

#include <QDockWidget>

#include <CCGeom.h>

class ccMainAppInterface;
class ccGLWindowInterface;
class ccPointCloud;
class QLabel;

class View3D : public QDockWidget
{
	Q_OBJECT

public:
	View3D( ccMainAppInterface* app, QWidget* parent = nullptr );
	~View3D() override;

	//! připraví kopii mračna (pokud ještě není pro toto mračno)
	bool setSource( ccPointCloud* cloud, unsigned maxPoints );

	//! zaměří pohled na okolí bodu (lokální souřadnice mračna), natočení podle půdorysu
	void focusOn( const CCVector3d& localP, double radius, double viewAngle );

	unsigned sourceID() const { return m_sourceID; }

private:
	void release();

	ccMainAppInterface* m_app = nullptr;
	ccGLWindowInterface* m_win = nullptr;
	QWidget* m_widget = nullptr;
	ccPointCloud* m_clone = nullptr;
	unsigned m_sourceID = 0;
	QLabel* m_info = nullptr;
};
