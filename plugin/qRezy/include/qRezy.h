#pragma once

// qRezy - řezy mračnem bodů a kreslení 2D výkresu nad nimi

#include "ccStdPluginInterface.h"

class RezDock;

class qRezy : public QObject, public ccStdPluginInterface
{
	Q_OBJECT
	Q_INTERFACES( ccPluginInterface ccStdPluginInterface )
	Q_PLUGIN_METADATA( IID "cz.geoprim.cloudcompare.plugin.qRezy" FILE "../info.json" )

public:
	explicit qRezy( QObject* parent = nullptr );
	~qRezy() override = default;

	void onNewSelection( const ccHObject::Container& selectedEntities ) override;
	QList<QAction*> getActions() override;

private:
	void showDock();

	QAction* m_action = nullptr;
	RezDock* m_dock = nullptr;
};
