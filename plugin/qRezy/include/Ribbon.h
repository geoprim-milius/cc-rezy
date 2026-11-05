#pragma once

// Pás karet (jako v programu Vrstevnice): karty se skupinami velkých a malých tlačítek.

#include <QWidget>

#include <vector>

class QAction;
class QHBoxLayout;
class QStackedWidget;
class QTabBar;
class QToolButton;
class QVBoxLayout;

class RibbonGroup;

//! sloupec malých tlačítek (max. 3 pod sebou)
class RibbonColumn
{
public:
	explicit RibbonColumn( QVBoxLayout* l )
		: m_layout( l )
	{
	}
	QToolButton* addSmall( QAction* a, const QString& key = QString() );
	void widget( QWidget* w, const QString& label = QString() );

private:
	QVBoxLayout* m_layout;
};

class RibbonGroup
{
public:
	explicit RibbonGroup( QHBoxLayout* l )
		: m_layout( l )
	{
	}
	QToolButton* addBig( QAction* a, const QString& key = QString() );
	RibbonColumn column();

private:
	QHBoxLayout* m_layout;
};

class Ribbon : public QWidget
{
	Q_OBJECT

public:
	explicit Ribbon( QWidget* parent = nullptr );

	//! tlačítko vlevo v řádku karet (zapnout/vypnout kreslení)
	void setAppAction( QAction* a );
	int addPage( const QString& title );
	RibbonGroup addGroup( int page, const QString& title );
	void finishPages(); //!< roztažení na konci karet
	void setCurrentPage( int page );
	int currentPage() const;

private:
	QTabBar* m_tabs = nullptr;
	QStackedWidget* m_stack = nullptr;
	QToolButton* m_app = nullptr;
	std::vector<QHBoxLayout*> m_pages;
};
