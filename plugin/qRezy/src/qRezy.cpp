#include "qRezy.h"
#include "RezDock.h"
#include "Theme.h"

#include <QApplication>
#include <QMainWindow>
#include <QSettings>
#include <QToolBar>

qRezy::qRezy( QObject* parent )
	: QObject( parent )
	, ccStdPluginInterface( ":/CC/plugin/qRezy/info.json" )
{
}

void qRezy::onNewSelection( const ccHObject::Container& selectedEntities )
{
	if ( m_dock )
	{
		m_dock->onSelectionChanged( selectedEntities );
	}
}

QList<QAction*> qRezy::getActions()
{
	if ( !m_action )
	{
		m_action = new QAction( QStringLiteral( "Řezy" ), this );
		m_action->setToolTip( QStringLiteral( "Řezy mračnem bodů – panel pro řez a kreslení" ) );
		m_action->setIcon( getIcon() );
		connect( m_action, &QAction::triggered, this, &qRezy::showDock );
	}
	return { m_action };
}

void qRezy::showDock()
{
	if ( !m_app )
	{
		return;
	}

	if ( !m_dock )
	{
		// vzhled (qRezy/theme) – stylesheet celé aplikace + pás karet
		Theme::apply( Theme::current() );
		if ( !Theme::current().isEmpty() && !QSettings().contains( QStringLiteral( "qRezy/darkBackground" ) ) )
		{
			Theme::applyBackground( true, m_app );
		}
		m_dock = new RezDock( m_app, m_app->getMainWindow() );
		m_app->getMainWindow()->addDockWidget( Qt::RightDockWidgetArea, m_dock );
		m_app->getMainWindow()->addToolBarBreak( Qt::TopToolBarArea );
		m_app->getMainWindow()->addToolBar( Qt::TopToolBarArea, m_dock->toolBar() );
		if ( QSettings().value( QStringLiteral( "qRezy/workMode" ), false ).toBool() )
		{
			m_dock->setWorkMode( true );
		}
	}

	m_dock->show();
	m_dock->raise();
	m_dock->toolBar()->show();
	m_dock->onSelectionChanged( m_app->getSelectedEntities() );
}
