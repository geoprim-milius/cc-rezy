#include "Theme.h"

#include "ccGuiParameters.h"
#include "ccMainAppInterface.h"

#include <QApplication>
#include <QFile>
#include <QFontDatabase>
#include <QSettings>

namespace Theme
{
	namespace
	{
		struct Pal
		{
			const char* tabRow;
			const char* ribbon;
			const char* text;
			const char* dim;
			const char* line;
			const char* hover;
			const char* acc;
			const char* accSoft;
			const char* field;
		};

		Pal palette( const QString& t )
		{
			if ( t == QLatin1String( "acad" ) )
			{
				return { "#252B34", "#323A46", "#E6EAF0", "#9AA4B2", "#4A5566", "#3E4857", "#3D8EE6", "rgba(61,142,230,60)", "#1E232B" };
			}
			if ( t == QLatin1String( "geoprim" ) )
			{
				return { "#1A1918", "#232120", "#ECE7DF", "#9C958B", "#4A4643", "#2E2B29", "#C9A46A", "rgba(201,164,106,55)", "#141312" };
			}
			return { "#E4E7EB", "#F7F8FA", "#1E2329", "#6B7380", "#CDD2D9", "#E3EAF3", "#2F7BD8", "rgba(47,123,216,40)", "#FFFFFF" };
		}

		QString ribbonQss( const QString& t )
		{
			const Pal p = palette( t );
			const QString radius = t == QLatin1String( "geoprim" ) ? QStringLiteral( "0px" ) : QStringLiteral( "3px" );
			QString s = QStringLiteral(
				"QToolBar#qRezyToolbar { background: %2; border: none; border-bottom: 1px solid %5; padding: 0px; spacing: 0px; }\n"
				"#rezyRibbon, #rezyRibbon QWidget { background: %2; color: %3; }\n"
				"#rezyRibbon QWidget#rezyTabRow, #rezyRibbon QWidget#rezyTabRow QWidget { background: %1; }\n"
				"#rezyRibbon QTabBar { qproperty-drawBase: 0; }\n"
				"#rezyRibbon QTabBar::tab { background: %1; color: %4; border: none; padding: 6px 16px; margin: 0px; }\n"
				"#rezyRibbon QTabBar::tab:hover { color: %3; }\n"
				"#rezyRibbon QTabBar::tab:selected { background: %2; color: %3; border-top: 2px solid %7; }\n"
				"#rezyRibbon QToolButton#rezyApp { background: transparent; color: %3; border: 1px solid %7; border-radius: %10; padding: 3px 12px; margin: 3px 8px 3px 4px; font-weight: 600; }\n"
				"#rezyRibbon QToolButton#rezyApp:checked { background: %7; color: #FFFFFF; }\n"
				"#rezyRibbon QToolButton#rezyBig, #rezyRibbon QToolButton#rezySmall { background: transparent; color: %3; border: 1px solid transparent; border-radius: %10; }\n"
				"#rezyRibbon QToolButton#rezyBig { padding: 4px 2px 2px 2px; }\n"
				"#rezyRibbon QToolButton#rezySmall { padding: 1px 6px 1px 2px; text-align: left; }\n"
				"#rezyRibbon QToolButton#rezyBig:hover, #rezyRibbon QToolButton#rezySmall:hover { background: %6; border-color: %5; }\n"
				"#rezyRibbon QToolButton#rezyBig:checked, #rezyRibbon QToolButton#rezySmall:checked { background: %8; border-color: %7; }\n"
				"#rezyRibbon QToolButton:disabled { color: %4; }\n"
				"#rezyRibbon QLabel#rezyKey { color: %4; font-size: 7.5pt; font-weight: 600; background: transparent; }\n"
				"#rezyRibbon QLabel#rezyGroupTitle { color: %4; font-size: 8pt; }\n"
				"#rezyRibbon QFrame#rezySep { background: %5; border: none; }\n"
				"#rezyRibbon QComboBox, #rezyRibbon QDoubleSpinBox { background: %9; color: %3; border: 1px solid %5; border-radius: %10; padding: 2px 6px; min-height: 18px; }\n"
				"#rezyRibbon QComboBox QAbstractItemView { background: %9; color: %3; selection-background-color: %7; }\n" )
				.arg( QString::fromLatin1( p.tabRow ), QString::fromLatin1( p.ribbon ), QString::fromLatin1( p.text ), QString::fromLatin1( p.dim ), QString::fromLatin1( p.line ),
				      QString::fromLatin1( p.hover ), QString::fromLatin1( p.acc ), QString::fromLatin1( p.accSoft ), QString::fromLatin1( p.field ) )
				.arg( radius );
			return s;
		}

		ccColor::Rgbub darkColor( const QString& t )
		{
			if ( t == QLatin1String( "geoprim" ) )
			{
				return ccColor::Rgbub( 30, 28, 27 );
			}
			return ccColor::Rgbub( 33, 40, 48 );
		}
	}

	QString current()
	{
		return QSettings().value( QStringLiteral( "qRezy/theme" ) ).toString();
	}

	bool darkBackground()
	{
		return QSettings().value( QStringLiteral( "qRezy/darkBackground" ), !current().isEmpty() ).toBool();
	}

	void apply( const QString& theme )
	{
		QSettings().setValue( QStringLiteral( "qRezy/theme" ), theme );
		QString qss;
		if ( theme == QLatin1String( "acad" ) || theme == QLatin1String( "geoprim" ) )
		{
			if ( theme == QLatin1String( "geoprim" ) )
			{
				static bool fontsLoaded = false;
				if ( !fontsLoaded )
				{
					QFontDatabase::addApplicationFont( QStringLiteral( ":/CC/plugin/qRezy/fonts/Jost_400Regular.ttf" ) );
					QFontDatabase::addApplicationFont( QStringLiteral( ":/CC/plugin/qRezy/fonts/Jost_500Medium.ttf" ) );
					fontsLoaded = true;
				}
			}
			QFile f( QStringLiteral( ":/CC/plugin/qRezy/themes/%1.qss" ).arg( theme ) );
			if ( f.open( QIODevice::ReadOnly ) )
			{
				qss = QString::fromUtf8( f.readAll() );
			}
		}
		qss += ribbonQss( theme );
		qApp->setStyleSheet( qss );
	}

	void applyBackground( bool dark, ccMainAppInterface* app )
	{
		QSettings().setValue( QStringLiteral( "qRezy/darkBackground" ), dark );
		ccGui::ParamStruct p = ccGui::Parameters();
		p.drawBackgroundGradient = false;
		if ( dark )
		{
			p.backgroundCol = darkColor( current() );
			p.textDefaultCol = ccColor::Rgba( 255, 255, 255, 255 );
		}
		else
		{
			p.backgroundCol = ccColor::Rgbub( 250, 250, 250 );
			p.textDefaultCol = ccColor::Rgba( 20, 20, 20, 255 );
		}
		ccGui::Set( p );
		p.toPersistentSettings();
		if ( app )
		{
			app->redrawAll( false );
		}
	}
}
