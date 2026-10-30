#pragma once

// Ikony nástrojů kreslené v kódu (bez externích souborů)

#include <QIcon>
#include <QString>

namespace Icons
{
	QIcon get( const QString& name );
	QIcon colorSwatch( const QColor& c, bool dashed );
}
