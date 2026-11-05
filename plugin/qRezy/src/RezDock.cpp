#include "RezDock.h"
#include "DgnV7.h"
#include "DrawTool.h"
#include "Drawing.h"
#include "Icons.h"
#include "RezLine.h"
#include "Ribbon.h"
#include "Theme.h"
#include "View3D.h"

#include "ccGLUtils.h"
#include "ccGLWindowInterface.h"
#include "ccMainAppInterface.h"

#include <cc2DViewportObject.h>
#include <ccGenericPointCloud.h>

#include <limits>
#include <ccHObjectCaster.h>
#include <ccPointCloud.h>
#include <ccPolyline.h>

#include <QAbstractSpinBox>
#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QCheckBox>
#include <QCloseEvent>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFile>
#include <QFileDialog>
#include <QColorDialog>
#include <QInputDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QMainWindow>
#include <QMessageBox>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QSettings>
#include <QShortcut>
#include <QSlider>
#include <QSpinBox>
#include <QTableWidget>
#include <QTextCodec>
#include <QToolBar>
#include <QToolButton>
#include <QMenu>
#include <QVBoxLayout>
#include <QLineEdit>
#include <map>

#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>

namespace
{
	QDoubleSpinBox* makeSpin( double min, double max, double value, double step, int decimals, const QString& suffix )
	{
		auto* s = new QDoubleSpinBox;
		s->setRange( min, max );
		s->setDecimals( decimals );
		s->setSingleStep( step );
		s->setValue( value );
		s->setSuffix( suffix );
		s->setKeyboardTracking( false );
		return s;
	}

	QToolButton* iconButton( const QString& icon, const QString& tip )
	{
		auto* b = new QToolButton;
		b->setIcon( Icons::get( icon ) );
		b->setIconSize( QSize( 22, 22 ) );
		b->setToolTip( tip );
		b->setAutoRaise( true );
		return b;
	}

	QString fmt( double v )
	{
		// český formát výšky: +1,200 / -0,350 / ±0,000
		if ( std::abs( v ) < 0.0005 )
		{
			return QStringLiteral( "±0,000" );
		}
		QString s = QString::number( std::abs( v ), 'f', 3 ).replace( '.', ',' );
		return ( v > 0 ? QStringLiteral( "+" ) : QStringLiteral( "-" ) ) + s;
	}

	// řada tlouštěk pro −/+ a Ctrl+kolečko (m)
	const double ThickLadder[] = { 0.005, 0.01, 0.02, 0.03, 0.05, 0.075, 0.10, 0.15, 0.20, 0.30, 0.50, 0.75, 1.0, 1.5, 2.0, 3.0, 5.0 };

	// posuvník: logaritmicky 0,5 cm … 5 m
	int thickToSlider( double t )
	{
		return static_cast<int>( std::round( 1000.0 * std::log( std::max( 0.005, std::min( 5.0, t ) ) / 0.005 ) / std::log( 1000.0 ) ) );
	}
	double sliderToThick( int v )
	{
		const double t = 0.005 * std::pow( 1000.0, v / 1000.0 );
		// zaokrouhlit na rozumné hodnoty
		if ( t < 0.1 )
			return std::round( t * 1000.0 ) / 1000.0;
		if ( t < 1.0 )
			return std::round( t * 100.0 ) / 100.0;
		return std::round( t * 10.0 ) / 10.0;
	}

	const char* KSecId = "qRezy.secId";

	QString sectionName( int kind, int no )
	{
		if ( kind == 1 )
		{
			return QStringLiteral( "Pohled %1" ).arg( no );
		}
		if ( no >= 1 && no <= 26 )
		{
			const QChar ch( 'A' + no - 1 );
			return QStringLiteral( "Řez %1-%1'" ).arg( ch );
		}
		return QStringLiteral( "Řez %1-%1'" ).arg( no );
	}
}

// ================================================================ konstrukce

RezDock::RezDock( ccMainAppInterface* app, QWidget* parent )
	: QDockWidget( QStringLiteral( "Řezy" ), parent )
	, m_app( app )
{
	setObjectName( QStringLiteral( "qRezyDock" ) );
	m_draw = new DrawTool( m_app, this );

	buildPanel();
	buildToolbar();

	// klávesy fungují v celém okně CloudCompare
	m_up = new QShortcut( QKeySequence( Qt::Key_PageUp ), parent );
	m_down = new QShortcut( QKeySequence( Qt::Key_PageDown ), parent );
	m_up->setContext( Qt::ApplicationShortcut );
	m_down->setContext( Qt::ApplicationShortcut );
	connect( m_up, &QShortcut::activated, this, [this]() { step( +1 ); } );
	connect( m_down, &QShortcut::activated, this, [this]() { step( -1 ); } );

	// signály kreslení
	connect( m_draw, &DrawTool::statusChanged, this, &RezDock::onStatus );
	connect( m_draw, &DrawTool::categoryChanged, this, [this]( int c ) { const QSignalBlocker b( m_catCombo ); m_catCombo->setCurrentIndex( c ); } );
	connect( m_draw, &DrawTool::hiddenChanged, this, [this]( bool h ) { const QSignalBlocker b( m_hiddenAct ); m_hiddenAct->setChecked( h ); } );
	connect( m_draw, &DrawTool::orthoChanged, this, [this]( bool o ) { const QSignalBlocker b( m_orthoAct ); m_orthoAct->setChecked( o ); } );
	connect( m_draw, &DrawTool::trackingChanged, this, [this]( bool o ) { const QSignalBlocker b( m_trackAct ); m_trackAct->setChecked( o ); } );
	connect( m_draw, &DrawTool::modeChanged, this, [this]( DrawTool::Mode m )
	{
		for ( QAction* a : m_toolGroup->actions() )
		{
			if ( a->data().toInt() == static_cast<int>( m ) )
			{
				a->setChecked( true );
				return;
			}
		}
		// dočasný režim (natočení, nový řez) – žádný nástroj nezvýrazněn
		if ( QAction* c = m_toolGroup->checkedAction() )
		{
			m_toolGroup->setExclusive( false );
			c->setChecked( false );
			m_toolGroup->setExclusive( true );
		}
	} );
	connect( m_draw, &DrawTool::rotateViewRequested, this, [this]( double a ) { m_viewAngle = a; topView(); focusView(); } );
	connect( m_draw, &DrawTool::show3DRequested, this, &RezDock::show3DAt );
	connect( m_draw, &DrawTool::sectionDefined, this, &RezDock::createSection );
	connect( m_draw, &DrawTool::thicknessWheel, this, &RezDock::thicknessStep );
	connect( m_draw, &DrawTool::drawingChanged, this, &RezDock::refresh3DLines );

	updateLabels();
}

RezDock::~RezDock()
{
	if ( m_draw )
	{
		m_draw->stop();
	}
	clearClip();
}

void RezDock::buildPanel()
{
	auto* w = new QWidget;
	auto* lay = new QVBoxLayout( w );
	lay->setSpacing( 6 );

	// mračno
	auto* cloudRow = new QHBoxLayout;
	m_cloudLabel = new QLabel;
	m_cloudLabel->setWordWrap( true );
	auto* useBtn = iconButton( QStringLiteral( "cloud" ), QStringLiteral( "Použít mračno vybrané v seznamu objektů" ) );
	cloudRow->addWidget( m_cloudLabel, 1 );
	cloudRow->addWidget( useBtn );
	lay->addLayout( cloudRow );

	// pohled
	auto* viewRow = new QHBoxLayout;
	m_viewCombo = new QComboBox;
	m_viewCombo->setToolTip( QStringLiteral( "Půdorys / svislé řezy / pohledy" ) );
	auto* delBtn = iconButton( QStringLiteral( "trash" ), QStringLiteral( "Smazat vybraný řez / pohled i s jeho kresbou" ) );
	viewRow->addWidget( m_viewCombo, 1 );
	viewRow->addWidget( delBtn );
	lay->addLayout( viewRow );

	// velký údaj o poloze řezu + posun
	m_relLabel = new QLabel;
	QFont f = m_relLabel->font();
	f.setPointSizeF( f.pointSizeF() * 1.5 );
	f.setBold( true );
	m_relLabel->setFont( f );
	m_relLabel->setAlignment( Qt::AlignCenter );
	auto* moveRow = new QHBoxLayout;
	m_downBtn = iconButton( QStringLiteral( "down" ), QStringLiteral( "Dolů / blíž (PgDn)" ) );
	m_upBtn = iconButton( QStringLiteral( "up" ), QStringLiteral( "Nahoru / dál (PgUp)" ) );
	moveRow->addWidget( m_downBtn );
	moveRow->addWidget( m_relLabel, 1 );
	moveRow->addWidget( m_upBtn );
	lay->addLayout( moveRow );

	// posuvník polohy řezu (celé mračno, po 1 cm)
	m_posSlider = new QSlider( Qt::Horizontal );
	m_posSlider->setToolTip( QStringLiteral( "Poloha řezu – táhněte (půdorys: výška, řez: posun roviny)" ) );
	m_posSlider->setSingleStep( 1 );
	m_posSlider->setPageStep( 10 );
	m_posMin = new QLabel;
	m_posMax = new QLabel;
	QFont sf = m_posMin->font();
	sf.setPointSizeF( sf.pointSizeF() * 0.85 );
	m_posMin->setFont( sf );
	m_posMax->setFont( sf );
	auto* posRow = new QHBoxLayout;
	posRow->addWidget( m_posMin );
	posRow->addWidget( m_posSlider, 1 );
	posRow->addWidget( m_posMax );
	lay->addLayout( posRow );

	auto* form = new QFormLayout;
	form->setLabelAlignment( Qt::AlignRight );
	m_enabled = new QCheckBox( QStringLiteral( "ořezat mračno" ) );
	m_enabled->setChecked( true );
	form->addRow( QString(), m_enabled );

	m_height = makeSpin( -1.0e6, 1.0e6, 0.0, 0.05, 3, QStringLiteral( " m" ) );
	m_heightRow = m_height;
	form->addRow( QStringLiteral( "Výška:" ), m_height );

	// tloušťka: posuvník, −/+, pole
	auto* thickRow = new QWidget;
	auto* tl = new QHBoxLayout( thickRow );
	tl->setContentsMargins( 0, 0, 0, 0 );
	auto* minus = iconButton( QStringLiteral( "minus" ), QStringLiteral( "Tenčí řez (Ctrl + kolečko)" ) );
	auto* plus = iconButton( QStringLiteral( "plus" ), QStringLiteral( "Tlustší řez (Ctrl + kolečko)" ) );
	m_thickSlider = new QSlider( Qt::Horizontal );
	m_thickSlider->setRange( 0, 1000 );
	m_thickSlider->setToolTip( QStringLiteral( "Tloušťka řezu (0,5 cm – 5 m)" ) );
	m_thickness = makeSpin( 0.005, 100.0, 0.10, 0.01, 3, QStringLiteral( " m" ) );
	tl->addWidget( minus );
	tl->addWidget( m_thickSlider, 1 );
	tl->addWidget( plus );
	form->addRow( QStringLiteral( "Tloušťka:" ), thickRow );
	auto* presetRow = new QWidget;
	auto* pl = new QHBoxLayout( presetRow );
	pl->setContentsMargins( 0, 0, 0, 0 );
	pl->addWidget( m_thickness );
	for ( int cm : { 2, 5, 10, 20, 50 } )
	{
		auto* b = new QToolButton;
		b->setText( QString::number( cm ) );
		b->setToolTip( QStringLiteral( "%1 cm" ).arg( cm ) );
		connect( b, &QToolButton::clicked, this, [this, cm]() { setThickness( cm / 100.0 ); focusView(); } );
		pl->addWidget( b );
	}
	form->addRow( QString(), presetRow );

	m_depth = makeSpin( 0.1, 1000.0, 15.0, 1.0, 1, QStringLiteral( " m" ) );
	m_depth->setToolTip( QStringLiteral( "Pohled: jak daleko za rovinou je mračno vidět" ) );
	m_depthRow = m_depth;
	form->addRow( QStringLiteral( "Hloubka:" ), m_depth );

	m_step = makeSpin( 0.001, 100.0, 0.05, 0.01, 3, QStringLiteral( " m" ) );
	form->addRow( QStringLiteral( "Krok PgUp/PgDn:" ), m_step );
	auto* zoomSpin = makeSpin( 1.05, 3.0, QSettings().value( QStringLiteral( "qRezy/zoomStep" ), 1.25 ).toDouble(), 0.05, 2, QStringLiteral( " ×" ) );
	zoomSpin->setToolTip( QStringLiteral( "Zoom kolečkem: kolikrát přiblíží jeden zub kolečka (1,25 ≈ MicroStation)" ) );
	form->addRow( QStringLiteral( "Zoom kolečkem:" ), zoomSpin );
	connect( zoomSpin, qOverload<double>( &QDoubleSpinBox::valueChanged ), this, []( double v ) { QSettings().setValue( QStringLiteral( "qRezy/zoomStep" ), v ); } );

	auto* zeroRow = new QWidget;
	auto* zl = new QHBoxLayout( zeroRow );
	zl->setContentsMargins( 0, 0, 0, 0 );
	m_zero = makeSpin( -1.0e6, 1.0e6, 0.0, 0.01, 3, QStringLiteral( " m" ) );
	auto* floorBtn = iconButton( QStringLiteral( "floor" ), QStringLiteral( "±0,000 = podlaha (odhad nejhustší vodorovné plochy)" ) );
	zl->addWidget( m_zero, 1 );
	zl->addWidget( floorBtn );
	form->addRow( QStringLiteral( "±0,000 =" ), zeroRow );

	// patro: podlaha (rovina kresby ve 3D) a reference
	m_floorLevel = makeSpin( -1.0e6, 1.0e6, 0.0, 0.01, 3, QStringLiteral( " m" ) );
	m_floorLevel->setToolTip( QStringLiteral( "Podlaha patra (Bpv) – výšky v patře se počítají od ní, ve 3D na ní leží kresba" ) );
	m_floorRow = m_floorLevel;
	form->addRow( QStringLiteral( "Podlaha patra:" ), m_floorLevel );
	m_refRow = new QWidget;
	auto* rl = new QHBoxLayout( m_refRow );
	rl->setContentsMargins( 0, 0, 0, 0 );
	m_refCombo = new QComboBox;
	m_refCombo->setToolTip( QStringLiteral( "Jiné patro šedě pod kresbou – kontrola, zda jsou zdi nad sebou" ) );
	m_refSnap = new QCheckBox( QStringLiteral( "uchopit" ) );
	m_refSnap->setToolTip( QStringLiteral( "Uchopovat i na čáry referenčního patra" ) );
	rl->addWidget( m_refCombo, 1 );
	rl->addWidget( m_refSnap );
	form->addRow( QStringLiteral( "Reference:" ), m_refRow );
	lay->addLayout( form );

	m_status = new QLabel;
	m_status->setWordWrap( true );
	m_status->setObjectName( QStringLiteral( "rezyStatus" ) );
	if ( QSettings().value( QStringLiteral( "qRezy/theme" ) ).toString().isEmpty() )
	{
		m_status->setStyleSheet( QStringLiteral( "color: #333; background: #f3f3f3; padding: 4px; border-radius: 3px;" ) );
	}
	m_status->setMinimumHeight( 48 );
	m_status->setAlignment( Qt::AlignTop | Qt::AlignLeft );
	lay->addWidget( m_status );

	auto* help = new QLabel( QStringLiteral(
		"<small>Levé = bod · pravé = reset · <b>Shift + tažení</b> nebo tažení kolečkem = posun · kolečko = zoom · "
		"<b>prostřední klik = 3D náhled</b> · Ctrl+kolečko = tloušťka · Esc = zrušit<br>"
		"Klávesy jako MicroStation: 1 výběr · Q čára · 3 manipulace · 7 úpravy · 8 mazat · D měřit · Esc = výběr. "
		"Délka: čísla na numerické klávesnici (nebo Shift + horní řada) + Enter.<br>"
		"Trasování (F11): podržte kurzor na konci čáry → zelená pomocná čára.</small>" ) );
	help->setWordWrap( true );
	lay->addWidget( help );
	lay->addStretch( 1 );

	auto* scroll = new QScrollArea;
	scroll->setWidgetResizable( true );
	scroll->setFrameShape( QFrame::NoFrame );
	scroll->setHorizontalScrollBarPolicy( Qt::ScrollBarAlwaysOff );
	scroll->setWidget( w );
	setWidget( scroll );

	// kolečko nad poli nemá měnit hodnoty, když se jen posouvá panel
	for ( QWidget* ch : w->findChildren<QWidget*>() )
	{
		if ( qobject_cast<QAbstractSpinBox*>( ch ) || qobject_cast<QComboBox*>( ch ) || qobject_cast<QSlider*>( ch ) )
		{
			ch->setFocusPolicy( Qt::StrongFocus );
			ch->installEventFilter( this );
		}
	}

	connect( useBtn, &QToolButton::clicked, this, &RezDock::useSelected );
	connect( delBtn, &QToolButton::clicked, this, &RezDock::deleteActiveSection );
	connect( m_viewCombo, qOverload<int>( &QComboBox::activated ), this, [this]( int i ) { activateView( m_viewCombo->itemData( i ).toInt() ); focusView(); } );
	connect( m_upBtn, &QToolButton::clicked, this, [this]() { step( +1 ); } );
	connect( m_downBtn, &QToolButton::clicked, this, [this]() { step( -1 ); } );
	connect( floorBtn, &QToolButton::clicked, this, &RezDock::zeroFromFloor );
	connect( minus, &QToolButton::clicked, this, [this]() { thicknessStep( -1 ); } );
	connect( plus, &QToolButton::clicked, this, [this]() { thicknessStep( +1 ); } );
	connect( m_thickSlider, &QSlider::valueChanged, this, [this]( int v )
	{
		const QSignalBlocker b( m_thickSlider );
		m_thickness->setValue( sliderToThick( v ) );
	} );
	connect( m_posSlider, &QSlider::valueChanged, this, &RezDock::onPosSlider );
	connect( m_enabled, &QCheckBox::toggled, this, [this]() { applyClip(); } );
	connect( m_height, qOverload<double>( &QDoubleSpinBox::valueChanged ), this, [this]()
	{
		m_draw->setPlanHeight( planZLocal() );
		applyClip();
	} );
	connect( m_thickness, qOverload<double>( &QDoubleSpinBox::valueChanged ), this, [this]()
	{
		syncThicknessSlider();
		applyClip();
	} );
	connect( m_depth, qOverload<double>( &QDoubleSpinBox::valueChanged ), this, [this]( double v )
	{
		SectionInfo si;
		if ( sectionById( m_activeView, si ) && si.kind == 1 )
		{
			si.depth = v;
			saveSection( si );
			applyClip();
		}
	} );
	connect( m_floorLevel, qOverload<double>( &QDoubleSpinBox::valueChanged ), this, [this]()
	{
		storeActiveFloor();
		updateLabels();
		refresh3DLines();
	} );
	auto refChanged = [this]()
	{
		m_draw->setReference( m_refCombo->currentData().toInt(), m_refSnap->isChecked() );
		focusView();
	};
	connect( m_refCombo, qOverload<int>( &QComboBox::activated ), this, refChanged );
	connect( m_refSnap, &QCheckBox::toggled, this, refChanged );
	connect( m_zero, qOverload<double>( &QDoubleSpinBox::valueChanged ), this, [this]()
	{
		updateLabels();
		if ( !isPlanView( m_activeView ) )
		{
			m_draw->setFrame( frameFor( m_activeView ) );
		}
		refresh3DLines();
	} );
	syncThicknessSlider();
}

QAction* RezDock::mkAct( const char* icon, const QString& label, const QString& tip, bool checkable )
{
	auto* a = new QAction( Icons::get( QString::fromLatin1( icon ) ), label, this );
	a->setToolTip( tip.isEmpty() ? label : QStringLiteral( "<b>%1</b><br>%2" ).arg( label, tip ) );
	a->setCheckable( checkable );
	m_iconActs.push_back( { a, QString::fromLatin1( icon ) } );
	return a;
}

void RezDock::refreshIcons()
{
	for ( const auto& ia : m_iconActs )
	{
		ia.first->setIcon( Icons::get( ia.second ) );
	}
	refreshViewCombo();
	if ( m_upBtn )
	{
		m_upBtn->setIcon( Icons::get( QStringLiteral( "up" ) ) );
		m_downBtn->setIcon( Icons::get( QStringLiteral( "down" ) ) );
	}
}

void RezDock::setTheme( const QString& theme )
{
	const bool wasDark = !Theme::current().isEmpty();
	Theme::apply( theme );
	const bool dark = !theme.isEmpty();
	if ( dark != wasDark || dark )
	{
		// s tmavým vzhledem tmavé pozadí kresby (lze přepnout na světlé)
		Theme::applyBackground( dark ? ( wasDark ? Theme::darkBackground() : true ) : false, m_app );
	}
	if ( m_status )
	{
		m_status->setStyleSheet( theme.isEmpty() ? QStringLiteral( "color: #333; background: #f3f3f3; padding: 4px; border-radius: 3px;" ) : QString() );
	}
	refreshIcons();
	if ( m_bgDarkAct )
	{
		const QSignalBlocker b1( m_bgDarkAct );
		const QSignalBlocker b2( m_bgLightAct );
		m_bgDarkAct->setChecked( Theme::darkBackground() );
		m_bgLightAct->setChecked( !Theme::darkBackground() );
	}
}

void RezDock::setWorkMode( bool on )
{
	auto* mw = qobject_cast<QMainWindow*>( parentWidget() );
	if ( !mw )
	{
		return;
	}
	QSettings().setValue( QStringLiteral( "qRezy/workMode" ), on );
	if ( on )
	{
		if ( m_savedLayout.isEmpty() )
		{
			m_savedLayout = mw->saveState();
		}
		for ( QToolBar* tb : mw->findChildren<QToolBar*>( QString(), Qt::FindDirectChildrenOnly ) )
		{
			if ( tb != m_toolbar )
			{
				tb->hide();
			}
		}
		for ( QDockWidget* dw : mw->findChildren<QDockWidget*>( QString(), Qt::FindDirectChildrenOnly ) )
		{
			if ( dw != this && dw->windowTitle() != QStringLiteral( "Řezy – 3D" ) )
			{
				dw->hide();
			}
		}
	}
	else
	{
		if ( !m_savedLayout.isEmpty() )
		{
			mw->restoreState( m_savedLayout );
			m_savedLayout.clear();
		}
		else
		{
			for ( QToolBar* tb : mw->findChildren<QToolBar*>( QString(), Qt::FindDirectChildrenOnly ) )
			{
				tb->show();
			}
		}
		show();
		m_toolbar->show();
	}
	if ( m_workAct )
	{
		const QSignalBlocker b( m_workAct );
		m_workAct->setChecked( on );
	}
}

void RezDock::renameView()
{
	if ( !cloud() )
	{
		return;
	}
	FloorInfo fi;
	SectionInfo si;
	const bool plan = isPlanView( m_activeView );
	if ( plan ? !floorById( m_activeView, fi ) : !sectionById( m_activeView, si ) )
	{
		return;
	}
	bool ok = false;
	const QString old = plan ? fi.name : si.name;
	const QString name = QInputDialog::getText( this, QStringLiteral( "Přejmenovat" ), plan ? QStringLiteral( "Název patra (použije se i v názvu souboru):" ) : QStringLiteral( "Název řezu / pohledu:" ), QLineEdit::Normal, old, &ok ).trimmed();
	if ( !ok || name.isEmpty() || name == old )
	{
		return;
	}
	if ( plan )
	{
		std::vector<FloorInfo> floors = loadFloors();
		for ( FloorInfo& f : floors )
		{
			if ( f.id == m_activeView )
			{
				f.name = name;
			}
		}
		saveFloors( floors );
	}
	else if ( si.poly )
	{
		si.poly->setMetaData( "qRezy.secName", name );
		si.poly->setName( name );
	}
	refreshViewCombo();
	refreshRefCombo();
	updateLabels();
	m_draw->refreshDisplay();
}

void RezDock::buildToolbar()
{
	m_toolbar = new QToolBar( QStringLiteral( "Řezy – pás karet" ), parentWidget() );
	m_toolbar->setObjectName( QStringLiteral( "qRezyToolbar" ) );
	m_toolbar->setMovable( false );
	m_toolbar->setFloatable( false );
	m_ribbon = new Ribbon;
	m_toolbar->addWidget( m_ribbon );

	m_drawAct = mkAct( "draw", QStringLiteral( "Kreslit" ), QStringLiteral( "Zapnout / vypnout kreslení (zapne se i výběrem nástroje)" ), true );
	connect( m_drawAct, &QAction::toggled, this, &RezDock::setDrawing );
	m_ribbon->setAppAction( m_drawAct );

	// --- nástroje kreslení
	m_toolGroup = new QActionGroup( this );
	m_toolGroup->setExclusive( true );
	std::map<int, QAction*> tool;
	struct ToolDef
	{
		DrawTool::Mode mode;
		const char* icon;
		const char* label;
		const char* tip;
	};
	const ToolDef tools[] = {
		{ DrawTool::Mode::Select, "select", "Výběr", "Klik, Ctrl+klik, okno tažením (zleva = uvnitř, zprava = křížem); úchopy na koncích. S výběrem: F1–F4/F5 změní hladinu/zakrytí, Del smaže, 3→1 / 3→2 kopie / přesun." },
		{ DrawTool::Mode::Line, "line", "Čára", "Body levým tlačítkem, pravé ukončí. Délka: čísla + Enter." },
		{ DrawTool::Mode::Move, "move", "Přesun", "Čára (nebo výběr), výchozí bod, cíl nebo vzdálenost." },
		{ DrawTool::Mode::Copy, "copy", "Kopie", "Čára (nebo výběr), výchozí bod, cíle nebo vzdálenost." },
		{ DrawTool::Mode::Offset, "offset", "Rovnoběžka", "Čára, strana; vzdálenost z pole vedle nebo napsat + Enter." },
		{ DrawTool::Mode::Modify, "modify", "Upravit prvek", "Klik u konce čáry, pak nové místo konce." },
		{ DrawTool::Mode::Break, "break", "Rozdělit", "2 body na čáře, část mezi nimi se smaže; stejný bod 2× = jen rozdělit." },
		{ DrawTool::Mode::Extend, "extend", "Prodloužit", "Klik u konce čáry, nový konec nebo celková délka." },
		{ DrawTool::Mode::Corner, "corner", "Roh", "Dvě čáry do průsečíku; klikejte na části, které zůstanou." },
		{ DrawTool::Mode::Trim, "trim", "Oříznout", "Hraniční čára, pak čáry k oříznutí / prodloužení." },
		{ DrawTool::Mode::InsertVertex, "insvertex", "Vložit vrchol", "Klik na čáru, pak umístit nový vrchol." },
		{ DrawTool::Mode::DeleteVertex, "delvertex", "Smazat vrchol", "Klik na spoj dvou čar → jedna čára." },
		{ DrawTool::Mode::Delete, "delete", "Smazat", "Klik na čáru (s výběrem smaže výběr)." },
		{ DrawTool::Mode::Measure, "measure", "Měřit", "Vzdálenost mezi dvěma body." },
		{ DrawTool::Mode::Text, "text", "Text", "Klik a napsat (např. naměřenou výšku); přesun, kopie a mazání jako u čar." },
	};
	for ( const ToolDef& t : tools )
	{
		QAction* a = mkAct( t.icon, QString::fromUtf8( t.label ), QString::fromUtf8( t.tip ), true );
		a->setData( static_cast<int>( t.mode ) );
		m_toolGroup->addAction( a );
		tool[static_cast<int>( t.mode )] = a;
	}
	tool[static_cast<int>( DrawTool::Mode::Line )]->setChecked( true );
	connect( m_toolGroup, &QActionGroup::triggered, this, [this]( QAction* a )
	{
		if ( !m_draw->isActive() )
		{
			m_drawAct->setChecked( true );
		}
		m_draw->setMode( static_cast<DrawTool::Mode>( a->data().toInt() ) );
		focusView();
	} );
	auto T = [&tool]( DrawTool::Mode m ) { return tool[static_cast<int>( m )]; };

	m_offsetSpin = makeSpin( 0.0, 1000.0, 0.0, 0.05, 3, QStringLiteral( " m" ) );
	m_offsetSpin->setSpecialValueText( QStringLiteral( "myší" ) );
	m_offsetSpin->setToolTip( QStringLiteral( "Vzdálenost rovnoběžky (0 = určit myší)" ) );
	m_offsetSpin->setFixedWidth( 86 );
	connect( m_offsetSpin, qOverload<double>( &QDoubleSpinBox::valueChanged ), this, [this]( double v ) { m_draw->setOffsetDistance( v ); } );

	QAction* undo = mkAct( "undo", QStringLiteral( "Zpět" ), QStringLiteral( "Vrátit poslední krok (Ctrl+Z)" ) );
	connect( undo, &QAction::triggered, this, [this]() { m_draw->undo(); focusView(); } );

	m_orthoAct = mkAct( "ortho", QStringLiteral( "Ortho" ), QStringLiteral( "Ortogonálně podle pohledu (F8)" ), true );
	connect( m_orthoAct, &QAction::toggled, this, [this]( bool b ) { m_draw->setOrtho( b ); focusView(); } );
	m_trackAct = mkAct( "tracking", QStringLiteral( "Trasování" ), QStringLiteral( "Podržte kurzor na konci / středu čáry, pak se ukážou pomocné čáry (F11)" ), true );
	m_trackAct->setChecked( true );
	connect( m_trackAct, &QAction::toggled, this, [this]( bool b ) { m_draw->setTracking( b ); focusView(); } );
	m_hiddenAct = mkAct( "hidden", QStringLiteral( "Zakrytá čára" ), QStringLiteral( "Zakrytá čára / nad řezem (F5); s výběrem změní vybrané" ), true );
	connect( m_hiddenAct, &QAction::toggled, this, [this]( bool b ) { m_draw->setHidden( b ); focusView(); } );

	m_catCombo = new QComboBox;
	m_catCombo->setToolTip( QStringLiteral( "Hladina (F1–F4); s výběrem změní vybrané" ) );
	m_catCombo->setMinimumWidth( 150 );
	for ( int c = 0; c < Drawing::CategoryCount; ++c )
	{
		m_catCombo->addItem( Icons::colorSwatch( Drawing::displayColor( c, false ), false ), QStringLiteral( "F%1  %2" ).arg( c + 1 ).arg( Drawing::categoryName( c ) ) );
	}
	connect( m_catCombo, qOverload<int>( &QComboBox::currentIndexChanged ), this, [this]( int i ) { m_draw->setCategory( i ); focusView(); } );
	QAction* symb = mkAct( "layers", QStringLiteral( "Hladiny a barvy…" ), QStringLiteral( "Hladina, barva, tloušťka a styl čar ve výstupním DGN" ) );
	connect( symb, &QAction::triggered, this, &RezDock::editSymbology );

	// --- pohledy a řezy
	auto startDef = [this]( int kind )
	{
		if ( !isPlanView( m_activeView ) )
		{
			activateView( m_lastPlan );
		}
		if ( !m_draw->isActive() )
		{
			m_drawAct->setChecked( true );
		}
		m_draw->startSectionDef( kind );
		focusView();
	};
	QAction* sec = mkAct( "section", QStringLiteral( "Řez" ), QStringLiteral( "Nový svislý řez (S) – 2 body v půdorysu, pak strana pohledu" ) );
	connect( sec, &QAction::triggered, this, [startDef]() { startDef( 0 ); } );
	QAction* fac = mkAct( "facade", QStringLiteral( "Pohled" ), QStringLiteral( "Nový pohled (fasáda) – rovina před objektem: 2 body, pak strana pohledu" ) );
	connect( fac, &QAction::triggered, this, [startDef]() { startDef( 1 ); } );
	QAction* aux = mkAct( "auxsec", QStringLiteral( "Pomocný\nřez" ), QStringLiteral( "2 body v půdorysu a strana; otevře okno pro měření výšek (nic se neukládá)" ) );
	connect( aux, &QAction::triggered, this, [startDef]() { startDef( 2 ); } );
	QAction* rot = mkAct( "rotate", QStringLiteral( "Otočit pohled" ), QStringLiteral( "2 body – směr 1. → 2. bod bude vodorovně doprava" ) );
	connect( rot, &QAction::triggered, this, [this]()
	{
		if ( !isPlanView( m_activeView ) )
		{
			activateView( m_lastPlan );
		}
		if ( !m_draw->isActive() )
		{
			m_drawAct->setChecked( true );
		}
		m_draw->setMode( DrawTool::Mode::RotateView );
		focusView();
	} );
	QAction* north = mkAct( "north", QStringLiteral( "Sever nahoru" ), QStringLiteral( "Zrušit natočení půdorysu" ) );
	connect( north, &QAction::triggered, this, [this]()
	{
		m_viewAngle = 0.0;
		if ( isPlanView( m_activeView ) )
		{
			topView();
		}
		focusView();
	} );
	QAction* rename = mkAct( "rename", QStringLiteral( "Přejmenovat" ), QStringLiteral( "Přejmenovat aktivní patro / řez / pohled" ) );
	connect( rename, &QAction::triggered, this, &RezDock::renameView );
	QAction* upA = mkAct( "up", QStringLiteral( "Řez výš / dál" ), QStringLiteral( "Posunout řez o krok (PgUp)" ) );
	connect( upA, &QAction::triggered, this, [this]() { step( +1 ); focusView(); } );
	QAction* downA = mkAct( "down", QStringLiteral( "Řez níž / blíž" ), QStringLiteral( "Posunout řez o krok (PgDn)" ) );
	connect( downA, &QAction::triggered, this, [this]() { step( -1 ); focusView(); } );
	QAction* thPlus = mkAct( "plus", QStringLiteral( "Silnější řez" ), QStringLiteral( "Tloušťka řezu + (Ctrl + kolečko)" ) );
	connect( thPlus, &QAction::triggered, this, [this]() { thicknessStep( +1 ); focusView(); } );
	QAction* thMinus = mkAct( "minus", QStringLiteral( "Tenčí řez" ), QStringLiteral( "Tloušťka řezu − (Ctrl + kolečko)" ) );
	connect( thMinus, &QAction::triggered, this, [this]() { thicknessStep( -1 ); focusView(); } );

	QAction* v3 = mkAct( "3d", QStringLiteral( "3D náhled" ), QStringLiteral( "3D náhled kolem středu okna (klik kolečkem do výkresu ukáže místo)" ) );
	connect( v3, &QAction::triggered, this, [this]()
	{
		CCVector3d P;
		if ( m_draw->isActive() && m_draw->viewCenter( P ) )
		{
			show3DAt( P.x, P.y, isPlanView( m_activeView ) ? std::numeric_limits<double>::quiet_NaN() : P.z );
		}
		else if ( ccGenericPointCloud* c = cloud() )
		{
			const CCVector3 C = c->getOwnBB().getCenter();
			show3DAt( C.x, C.y, std::numeric_limits<double>::quiet_NaN() );
		}
	} );

	// --- patra
	QAction* nf = mkAct( "newfloor", QStringLiteral( "Nové patro" ), QStringLiteral( "Samostatný půdorys (předchozí patro se nabídne jako reference)" ) );
	connect( nf, &QAction::triggered, this, [this]() { newFloor(); focusView(); } );
	QAction* floorEst = mkAct( "floor", QStringLiteral( "±0 z mračna" ), QStringLiteral( "Odhadnout výšku podlahy 1. patra (±0,000) z mračna" ) );
	connect( floorEst, &QAction::triggered, this, &RezDock::zeroFromFloor );
	QAction* topA = mkAct( "north", QStringLiteral( "Půdorys" ), QStringLiteral( "Přepnout na půdorys aktuálního patra" ) );
	connect( topA, &QAction::triggered, this, [this]() { activateView( m_lastPlan ); focusView(); } );

	// --- změřené body
	QAction* ptLoad = mkAct( "points", QStringLiteral( "Načíst\nbody" ), QStringLiteral( "Seznam souřadnic S-JTSK: číslo Y X [Z] [kód]" ) );
	QAction* ptShow = mkAct( "points", QStringLiteral( "Zobrazit body" ), QStringLiteral( "Zobrazit body a uchopovat na ně" ), true );
	ptShow->setChecked( true );
	QAction* ptCodes = mkAct( "text", QStringLiteral( "Kódy bodů" ), QStringLiteral( "Zobrazit kódy bodů" ), true );
	ptCodes->setChecked( true );
	QAction* ptColors = mkAct( "theme", QStringLiteral( "Barvy bodů…" ), QStringLiteral( "Barva značky, čísla a kódu" ) );
	connect( ptColors, &QAction::triggered, this, [this]()
	{
		QSettings st;
		QDialog dlg( this );
		dlg.setWindowTitle( QStringLiteral( "Barvy bodů" ) );
		auto* fl = new QFormLayout( &dlg );
		struct Entry
		{
			const char* key;
			const char* label;
			QColor def;
		};
		const Entry entries[] = { { "qRezy/ptColMarker", "Značka (křížek):", QColor( 255, 90, 255 ) },
		                          { "qRezy/ptColName", "Číslo bodu:", QColor( 255, 235, 0 ) },
		                          { "qRezy/ptColCode", "Kód:", QColor( 0, 230, 255 ) } };
		std::vector<std::pair<QString, QColor>> values;
		for ( const Entry& e : entries )
		{
			values.push_back( { QString::fromLatin1( e.key ), st.value( QString::fromLatin1( e.key ), e.def ).value<QColor>() } );
		}
		for ( size_t i = 0; i < values.size(); ++i )
		{
			auto* b = new QPushButton;
			b->setIcon( Icons::colorSwatch( values[i].second, false ) );
			b->setText( values[i].second.name() );
			fl->addRow( QString::fromUtf8( entries[i].label ), b );
			connect( b, &QPushButton::clicked, &dlg, [&values, i, b, &dlg]()
			{
				const QColor c = QColorDialog::getColor( values[i].second, &dlg );
				if ( c.isValid() )
				{
					values[i].second = c;
					b->setIcon( Icons::colorSwatch( c, false ) );
					b->setText( c.name() );
				}
			} );
		}
		auto* bkg = new QCheckBox( QStringLiteral( "tmavý podklad pod textem" ) );
		bkg->setChecked( st.value( QStringLiteral( "qRezy/ptBkg" ), true ).toBool() );
		fl->addRow( QString(), bkg );
		auto* bb = new QDialogButtonBox( QDialogButtonBox::Ok | QDialogButtonBox::Cancel );
		fl->addRow( bb );
		connect( bb, &QDialogButtonBox::accepted, &dlg, &QDialog::accept );
		connect( bb, &QDialogButtonBox::rejected, &dlg, &QDialog::reject );
		if ( dlg.exec() == QDialog::Accepted )
		{
			for ( const auto& v : values )
			{
				st.setValue( v.first, v.second );
			}
			st.setValue( QStringLiteral( "qRezy/ptBkg" ), bkg->isChecked() );
			m_draw->refreshDisplay();
		}
		focusView();
	} );
	connect( ptLoad, &QAction::triggered, this, [this, ptShow]()
	{
		if ( !cloud() )
		{
			QMessageBox::information( this, QStringLiteral( "Řezy" ), QStringLiteral( "Nejdřív vyberte mračno." ) );
			return;
		}
		if ( !m_draw->isActive() )
		{
			m_drawAct->setChecked( true );
		}
		QSettings st;
		const QString path = QFileDialog::getOpenFileName( this, QStringLiteral( "Načíst změřené body (S-JTSK)" ), st.value( QStringLiteral( "qRezy/pointsDir" ) ).toString(),
		                                                   QStringLiteral( "Seznam souřadnic (*.txt *.csv *.ss *.xyz);;Všechny soubory (*)" ) );
		if ( path.isEmpty() )
		{
			return;
		}
		st.setValue( QStringLiteral( "qRezy/pointsDir" ), QFileInfo( path ).absolutePath() );
		QString msg;
		const bool ok = m_draw->importPoints( path, msg );
		m_app->dispToConsole( QStringLiteral( "[Řezy] %1: %2" ).arg( QFileInfo( path ).fileName(), msg ), ok ? ccMainAppInterface::STD_CONSOLE_MESSAGE : ccMainAppInterface::WRN_CONSOLE_MESSAGE );
		m_status->setText( msg );
		if ( ok )
		{
			const QSignalBlocker b( ptShow );
			ptShow->setChecked( true );
		}
		focusView();
	} );
	connect( ptShow, &QAction::toggled, this, [this]( bool on ) { m_draw->setShowPoints( on ); focusView(); } );
	connect( ptCodes, &QAction::toggled, this, [this]( bool on ) { m_draw->setShowCodes( on ); focusView(); } );

	// --- výstup
	QAction* proj = mkAct( "project", QStringLiteral( "Uložit\npráci" ), QStringLiteral( "Projekt CloudCompare (.bin) s mračnem, kresbou, řezy, patry, body, nastavením i pohledem" ) );
	connect( proj, &QAction::triggered, this, &RezDock::saveProject );
	QAction* openA = mkAct( "open", QStringLiteral( "Otevřít" ), QStringLiteral( "Otevřít mračno nebo uloženou práci (.bin)" ) );
	connect( openA, &QAction::triggered, this, [this]()
	{
		if ( QAction* a = parentWidget() ? parentWidget()->findChild<QAction*>( QStringLiteral( "actionOpen" ) ) : nullptr )
		{
			a->trigger();
		}
	} );
	QAction* save = mkAct( "save", QStringLiteral( "Uložit V7" ), QStringLiteral( "Uložit výkres MicroStation V7 (patra, řezy a pohledy) a nastavení výstupu" ) );
	connect( save, &QAction::triggered, this, &RezDock::openSaveDialog );

	// --- zobrazení
	auto* themeCombo = new QComboBox;
	themeCombo->addItem( QStringLiteral( "Světlý (původní)" ), QString() );
	themeCombo->addItem( QStringLiteral( "AutoCAD tmavý" ), QStringLiteral( "acad" ) );
	themeCombo->addItem( QStringLiteral( "Geoprim" ), QStringLiteral( "geoprim" ) );
	themeCombo->setCurrentIndex( std::max( 0, themeCombo->findData( Theme::current() ) ) );
	themeCombo->setMinimumWidth( 150 );
	connect( themeCombo, qOverload<int>( &QComboBox::currentIndexChanged ), this, [this, themeCombo]( int i ) { setTheme( themeCombo->itemData( i ).toString() ); } );
	auto* bgGroup = new QActionGroup( this );
	m_bgDarkAct = mkAct( "bgdark", QStringLiteral( "Tmavé" ), QStringLiteral( "Tmavé pozadí okna s mračnem" ), true );
	m_bgLightAct = mkAct( "bglight", QStringLiteral( "Světlé" ), QStringLiteral( "Světlé pozadí okna s mračnem (když mračno na tmavém není dobře vidět)" ), true );
	bgGroup->addAction( m_bgDarkAct );
	bgGroup->addAction( m_bgLightAct );
	m_bgDarkAct->setChecked( Theme::darkBackground() );
	m_bgLightAct->setChecked( !Theme::darkBackground() );
	connect( m_bgDarkAct, &QAction::triggered, this, [this]() { Theme::applyBackground( true, m_app ); m_draw->refreshDisplay(); } );
	connect( m_bgLightAct, &QAction::triggered, this, [this]() { Theme::applyBackground( false, m_app ); m_draw->refreshDisplay(); } );
	m_workAct = mkAct( "workmode", QStringLiteral( "Pracovní\nrežim" ), QStringLiteral( "Schovat lišty a panely CloudCompare (DB Tree, vlastnosti, konzole); znovu kliknout = vrátit" ), true );
	connect( m_workAct, &QAction::toggled, this, &RezDock::setWorkMode );
	QAction* panelA = mkAct( "draw", QStringLiteral( "Panel Řezy" ), QStringLiteral( "Zobrazit panel Řezy (pohled, výška a tloušťka řezu)" ) );
	connect( panelA, &QAction::triggered, this, [this]() { show(); raise(); } );

	// ================= karty
	Ribbon* r = m_ribbon;
	const int pDraw = r->addPage( QStringLiteral( "Kreslení" ) );
	{
		r->addGroup( pDraw, QStringLiteral( "Výběr" ) ).addBig( T( DrawTool::Mode::Select ), QStringLiteral( "1" ) );
		RibbonGroup g = r->addGroup( pDraw, QStringLiteral( "Kreslení" ) );
		g.addBig( T( DrawTool::Mode::Line ), QStringLiteral( "Q" ) );
		RibbonColumn c = g.column();
		c.addSmall( T( DrawTool::Mode::Text ) );
		c.addSmall( T( DrawTool::Mode::Measure ), QStringLiteral( "D" ) );
		RibbonGroup m = r->addGroup( pDraw, QStringLiteral( "Manipulace · 3" ) );
		RibbonColumn mc = m.column();
		mc.addSmall( T( DrawTool::Mode::Copy ), QStringLiteral( "3·1" ) );
		mc.addSmall( T( DrawTool::Mode::Move ), QStringLiteral( "3·2" ) );
		mc.addSmall( T( DrawTool::Mode::Offset ), QStringLiteral( "3·9" ) );
		RibbonColumn mc2 = m.column();
		for ( int i = 0; i < 2; ++i )
		{
			auto* gap = new QWidget;
			gap->setFixedHeight( 24 );
			mc2.widget( gap );
		}
		mc2.widget( m_offsetSpin );
		RibbonGroup u = r->addGroup( pDraw, QStringLiteral( "Úpravy · 7" ) );
		RibbonColumn u1 = u.column();
		u1.addSmall( T( DrawTool::Mode::Modify ), QStringLiteral( "7·1" ) );
		u1.addSmall( T( DrawTool::Mode::Break ), QStringLiteral( "7·2" ) );
		u1.addSmall( T( DrawTool::Mode::Extend ), QStringLiteral( "7·3" ) );
		RibbonColumn u2 = u.column();
		u2.addSmall( T( DrawTool::Mode::Corner ), QStringLiteral( "7·4" ) );
		u2.addSmall( T( DrawTool::Mode::Trim ), QStringLiteral( "7·5" ) );
		u2.addSmall( T( DrawTool::Mode::InsertVertex ), QStringLiteral( "7·7" ) );
		RibbonColumn u3 = u.column();
		u3.addSmall( T( DrawTool::Mode::DeleteVertex ), QStringLiteral( "7·8" ) );
		r->addGroup( pDraw, QStringLiteral( "Smazat" ) ).addBig( T( DrawTool::Mode::Delete ), QStringLiteral( "8" ) );
		RibbonGroup h = r->addGroup( pDraw, QStringLiteral( "Hladina" ) );
		RibbonColumn hc = h.column();
		hc.widget( m_catCombo );
		hc.addSmall( m_hiddenAct, QStringLiteral( "F5" ) );
		hc.addSmall( symb );
		RibbonGroup p = r->addGroup( pDraw, QStringLiteral( "Pomůcky" ) );
		RibbonColumn pc = p.column();
		pc.addSmall( m_orthoAct, QStringLiteral( "F8" ) );
		pc.addSmall( m_trackAct, QStringLiteral( "F11" ) );
		pc.addSmall( undo, QStringLiteral( "Ctrl+Z" ) );
	}
	const int pSec = r->addPage( QStringLiteral( "Řezy a pohledy" ) );
	{
		RibbonGroup g = r->addGroup( pSec, QStringLiteral( "Nový" ) );
		g.addBig( sec, QStringLiteral( "S" ) );
		g.addBig( fac );
		g.addBig( aux );
		RibbonGroup n = r->addGroup( pSec, QStringLiteral( "Pohled" ) );
		RibbonColumn nc = n.column();
		nc.addSmall( rot );
		nc.addSmall( north );
		nc.addSmall( rename );
		RibbonGroup s = r->addGroup( pSec, QStringLiteral( "Poloha a tloušťka řezu" ) );
		RibbonColumn s1 = s.column();
		s1.addSmall( upA, QStringLiteral( "PgUp" ) );
		s1.addSmall( downA, QStringLiteral( "PgDn" ) );
		RibbonColumn s2 = s.column();
		s2.addSmall( thPlus );
		s2.addSmall( thMinus );
		r->addGroup( pSec, QStringLiteral( "3D" ) ).addBig( v3 );
	}
	const int pFloor = r->addPage( QStringLiteral( "Patra" ) );
	{
		RibbonGroup g = r->addGroup( pFloor, QStringLiteral( "Patro" ) );
		g.addBig( nf );
		RibbonColumn c = g.column();
		c.addSmall( topA );
		c.addSmall( rename );
		c.addSmall( floorEst );
	}
	const int pPts = r->addPage( QStringLiteral( "Body" ) );
	{
		RibbonGroup g = r->addGroup( pPts, QStringLiteral( "Změřené body" ) );
		g.addBig( ptLoad );
		RibbonColumn c = g.column();
		c.addSmall( ptShow );
		c.addSmall( ptCodes );
		c.addSmall( ptColors );
	}
	const int pOut = r->addPage( QStringLiteral( "Výstup" ) );
	{
		RibbonGroup g = r->addGroup( pOut, QStringLiteral( "Rozdělaná práce" ) );
		g.addBig( proj );
		g.addBig( openA );
		RibbonGroup d = r->addGroup( pOut, QStringLiteral( "MicroStation V7" ) );
		d.addBig( save );
		d.column().addSmall( symb );
	}
	const int pView = r->addPage( QStringLiteral( "Zobrazení" ) );
	{
		RibbonGroup g = r->addGroup( pView, QStringLiteral( "Vzhled" ) );
		RibbonColumn c = g.column();
		c.widget( themeCombo );
		RibbonGroup b = r->addGroup( pView, QStringLiteral( "Pozadí kresby" ) );
		RibbonColumn bc = b.column();
		bc.addSmall( m_bgDarkAct );
		bc.addSmall( m_bgLightAct );
		RibbonGroup w = r->addGroup( pView, QStringLiteral( "Okno" ) );
		w.addBig( m_workAct );
		w.column().addSmall( panelA );
	}
	r->finishPages();
}

// ================================================================ patra

std::vector<RezDock::FloorInfo> RezDock::loadFloors() const
{
	std::vector<FloorInfo> out;
	ccHObject* group = m_draw ? m_draw->drawingGroup( false ) : nullptr;
	if ( group && group->hasMetaData( "qRezy.floors" ) )
	{
		for ( const QString& rec : group->getMetaData( "qRezy.floors" ).toStringList() )
		{
			const QStringList p = rec.split( '|' );
			if ( p.size() < 4 )
			{
				continue;
			}
			FloorInfo f;
			f.id = p[0].toInt();
			f.name = p[1];
			f.cut = p[2].toDouble();
			f.level = p[3].toDouble();
			out.push_back( f );
		}
	}
	if ( std::none_of( out.begin(), out.end(), []( const FloorInfo& f ) { return f.id == 0; } ) )
	{
		FloorInfo f0;
		f0.id = 0;
		f0.name = QStringLiteral( "1NP" );
		f0.cut = m_height->value();
		out.insert( out.begin(), f0 );
	}
	for ( FloorInfo& f : out )
	{
		if ( f.id == 0 )
		{
			f.level = m_zero->value(); // 1. patro = ±0,000
		}
		if ( f.id == m_activeView )
		{
			f.cut = m_height->value();
			if ( f.id != 0 )
			{
				f.level = m_floorLevel->value();
			}
		}
	}
	return out;
}

void RezDock::saveFloors( const std::vector<FloorInfo>& floors )
{
	ccHObject* group = m_draw ? m_draw->drawingGroup( true ) : nullptr;
	if ( !group )
	{
		return;
	}
	QStringList list;
	for ( const FloorInfo& f : floors )
	{
		list << QStringLiteral( "%1|%2|%3|%4" ).arg( f.id ).arg( QString( f.name ).remove( '|' ) ).arg( f.cut, 0, 'f', 4 ).arg( f.level, 0, 'f', 4 );
	}
	group->setMetaData( "qRezy.floors", list );
}

bool RezDock::floorById( int id, FloorInfo& f ) const
{
	if ( !isPlanView( id ) )
	{
		return false;
	}
	for ( const FloorInfo& x : loadFloors() )
	{
		if ( x.id == id )
		{
			f = x;
			return true;
		}
	}
	return false;
}

void RezDock::storeActiveFloor()
{
	if ( isPlanView( m_activeView ) && cloud() )
	{
		saveFloors( loadFloors() );
	}
}

double RezDock::currentLevel() const
{
	FloorInfo f;
	if ( isPlanView( m_activeView ) && m_activeView != 0 && floorById( m_activeView, f ) )
	{
		return f.level;
	}
	return m_zero->value();
}

double RezDock::estimateFloorRange( ccGenericPointCloud* c, double zMin, double zMax, bool& ok ) const
{
	// nejhustší vodorovná vrstva bodů ve výškách zMin–zMax (globálně)
	ok = false;
	const unsigned n = c->size();
	const double sc = c->getGlobalScale();
	const double zl0 = c->toLocal3d( CCVector3d( 0, 0, zMin ) ).z;
	const double zl1 = c->toLocal3d( CCVector3d( 0, 0, zMax ) ).z;
	const double bin = 0.02 * sc;
	const size_t bins = static_cast<size_t>( std::max( 1.0, std::ceil( ( zl1 - zl0 ) / bin ) ) );
	if ( n == 0 || bins > 100000 )
	{
		return 0.0;
	}
	std::vector<unsigned> h( bins, 0 );
	const unsigned stride = std::max( 1u, n / 3000000u );
	for ( unsigned i = 0; i < n; i += stride )
	{
		const double z = c->getPoint( i )->z;
		if ( z >= zl0 && z < zl1 )
		{
			++h[static_cast<size_t>( ( z - zl0 ) / bin )];
		}
	}
	const size_t best = std::max_element( h.begin(), h.end() ) - h.begin();
	if ( h[best] < 200 / stride + 20 )
	{
		return 0.0;
	}
	ok = true;
	return c->toGlobal3d( CCVector3d( 0, 0, zl0 + ( best + 0.5 ) * bin ) ).z;
}

double RezDock::estimateFace( const SectionInfo& si ) const
{
	// pohled: líc objektu = nejhustší vrstva bodů za rovinou pohledu (v délce čáry)
	ccGenericPointCloud* c = cloud();
	if ( !c )
	{
		return -1.0;
	}
	const double sc = c->getGlobalScale();
	CCVector3d N( si.N.x, si.N.y, 0.0 );
	const CCVector3d V( 0, 0, 1 );
	const CCVector3d U = V.cross( N );
	const CCVector3d A( si.A.x, si.A.y, 0 );
	const CCVector3d B( si.B.x, si.B.y, 0 );
	const double u0 = std::min( A.dot( U ), B.dot( U ) );
	const double u1 = std::max( A.dot( U ), B.dot( U ) );
	const double d0 = A.dot( N );
	const double depth = si.depth * sc;
	const double bin = 0.02 * sc;
	const size_t bins = static_cast<size_t>( std::ceil( depth / bin ) ) + 1;
	std::vector<unsigned> h( bins, 0 );
	const unsigned n = c->size();
	const unsigned stride = std::max( 1u, n / 3000000u );
	for ( unsigned i = 0; i < n; i += stride )
	{
		const CCVector3* p = c->getPoint( i );
		const CCVector3d P( p->x, p->y, 0 );
		const double u = P.dot( U );
		const double behind = d0 - P.dot( N ); // kladně = za rovinou
		if ( u >= u0 && u <= u1 && behind >= 0.0 && behind < depth )
		{
			++h[static_cast<size_t>( behind / bin )];
		}
	}
	const size_t best = std::max_element( h.begin(), h.end() ) - h.begin();
	if ( h[best] < 50 )
	{
		return -1.0;
	}
	return ( best + 0.5 ) * bin / sc;
}

void RezDock::newFloor()
{
	ccGenericPointCloud* c = cloud();
	if ( !c )
	{
		QMessageBox::information( this, QStringLiteral( "Řezy" ), QStringLiteral( "Nejdřív vyberte mračno." ) );
		return;
	}
	storeActiveFloor();
	std::vector<FloorInfo> floors = loadFloors();
	FloorInfo cur;
	if ( !floorById( m_lastPlan, cur ) )
	{
		cur = floors.front();
	}
	bool ok = false;
	double level = estimateFloorRange( c, cur.level + 2.2, cur.level + 6.0, ok );
	if ( !ok )
	{
		level = cur.level + 3.0;
	}
	FloorInfo nf;
	nf.id = FloorBase;
	for ( const FloorInfo& f : floors )
	{
		nf.id = std::max( nf.id, f.id );
	}
	++nf.id;
	bool okName = false;
	nf.name = QInputDialog::getText( this, QStringLiteral( "Nové patro" ),
	                                 QStringLiteral( "Název patra (podlaha odhadnuta na %1 m, upravíte v panelu):" ).arg( QString::number( level, 'f', 2 ).replace( '.', ',' ) ),
	                                 QLineEdit::Normal, QStringLiteral( "%1NP" ).arg( floors.size() + 1 ), &okName ).trimmed();
	if ( !okName || nf.name.isEmpty() )
	{
		return;
	}
	nf.level = level;
	nf.cut = level + 1.0;
	floors.push_back( nf );
	saveFloors( floors );
	const int prev = cur.id;
	activateView( nf.id );
	// předchozí patro rovnou jako reference
	const int idx = m_refCombo->findData( prev );
	if ( idx >= 0 )
	{
		m_refCombo->setCurrentIndex( idx );
		m_draw->setReference( prev, m_refSnap->isChecked() );
	}
	m_status->setText( QStringLiteral( "Patro %1: podlaha %2 m, řez %3 m. Reference: předchozí patro (šedě)." )
	                       .arg( nf.name, QString::number( nf.level, 'f', 3 ).replace( '.', ',' ), QString::number( nf.cut, 'f', 3 ).replace( '.', ',' ) ) );
}

void RezDock::refreshRefCombo()
{
	if ( !m_refCombo )
	{
		return;
	}
	const int prev = m_refCombo->currentData().isValid() ? m_refCombo->currentData().toInt() : -1;
	const QSignalBlocker b( m_refCombo );
	m_refCombo->clear();
	m_refCombo->addItem( QStringLiteral( "žádná" ), -1 );
	for ( const FloorInfo& f : loadFloors() )
	{
		if ( f.id != m_activeView )
		{
			m_refCombo->addItem( f.name, f.id );
		}
	}
	int idx = m_refCombo->findData( prev );
	if ( idx < 0 )
	{
		idx = 0;
	}
	m_refCombo->setCurrentIndex( idx );
	m_draw->setReference( isPlanView( m_activeView ) ? m_refCombo->currentData().toInt() : -1, m_refSnap->isChecked() );
}

// ================================================================ panel

void RezDock::showEvent( QShowEvent* event )
{
	if ( m_toolbar )
	{
		m_toolbar->show();
	}
	QDockWidget::showEvent( event );
}

void RezDock::closeEvent( QCloseEvent* event )
{
	if ( m_drawAct )
	{
		m_drawAct->setChecked( false );
	}
	clearClip();
	m_cloudID = 0;
	if ( m_toolbar )
	{
		m_toolbar->hide();
	}
	updateLabels();
	QDockWidget::closeEvent( event );
}

bool RezDock::eventFilter( QObject* obj, QEvent* event )
{
	if ( event->type() == QEvent::Wheel )
	{
		QWidget* wdg = qobject_cast<QWidget*>( obj );
		if ( wdg && !wdg->hasFocus() )
		{
			// předat posuvníku panelu
			if ( auto* sa = qobject_cast<QScrollArea*>( widget() ) )
			{
				QCoreApplication::sendEvent( sa->verticalScrollBar(), event );
			}
			return true;
		}
	}
	return QDockWidget::eventFilter( obj, event );
}

void RezDock::onSelectionChanged( const ccHObject::Container& selected )
{
	m_lastSelection = selected;
	if ( !cloud() )
	{
		useSelected();
	}
}

ccGenericPointCloud* RezDock::cloud() const
{
	if ( m_cloudID == 0 || !m_app || !m_app->dbRootObject() )
	{
		return nullptr;
	}
	// mračno mohlo být mezitím smazáno – hledáme ho v databázi
	ccHObject* obj = m_app->dbRootObject()->find( m_cloudID );
	if ( !obj || !obj->isKindOf( CC_TYPES::POINT_CLOUD ) )
	{
		return nullptr;
	}
	return ccHObjectCaster::ToGenericPointCloud( obj );
}

void RezDock::useSelected()
{
	for ( ccHObject* obj : m_lastSelection )
	{
		if ( obj && obj->isKindOf( CC_TYPES::POINT_CLOUD ) )
		{
			setCloud( ccHObjectCaster::ToGenericPointCloud( obj ) );
			return;
		}
	}
	if ( !cloud() && m_app )
	{
		m_app->dispToConsole( QStringLiteral( "[Řezy] Vyberte v seznamu objektů mračno bodů." ), ccMainAppInterface::WRN_CONSOLE_MESSAGE );
	}
}

void RezDock::setCloud( ccGenericPointCloud* c )
{
	if ( !c || c->getUniqueID() == m_cloudID )
	{
		return;
	}

	clearClip();
	m_cloudID = c->getUniqueID();
	m_draw->setCloud( c );

	const double floorZ = estimateFloor( c );
	{
		const QSignalBlocker b1( m_height );
		const QSignalBlocker b2( m_zero );
		m_zero->setValue( floorZ );
		m_height->setValue( floorZ + 1.0 );
	}
	m_app->dispToConsole( QStringLiteral( "[Řezy] Mračno %1, odhad podlahy %2 m" ).arg( c->getName() ).arg( floorZ, 0, 'f', 3 ) );

	// rozdělaná práce uložená v projektu (.bin): nastavení panelu a pohled
	ccHObject* group = m_draw->drawingGroup( false );
	const QVariantMap st = ( group && group->hasMetaData( "qRezy.state" ) ) ? group->getMetaData( "qRezy.state" ).toMap() : QVariantMap();
	int view = 0;
	m_restoring = true;
	if ( !st.isEmpty() )
	{
		const QSignalBlocker b1( m_height );
		const QSignalBlocker b2( m_zero );
		const QSignalBlocker b3( m_thickness );
		const QSignalBlocker b4( m_step );
		const QSignalBlocker b5( m_enabled );
		m_zero->setValue( st.value( QStringLiteral( "zero" ), floorZ ).toDouble() );
		m_height->setValue( st.value( QStringLiteral( "height" ), floorZ + 1.0 ).toDouble() );
		m_thickness->setValue( st.value( QStringLiteral( "thickness" ), m_thickness->value() ).toDouble() );
		m_step->setValue( st.value( QStringLiteral( "step" ), m_step->value() ).toDouble() );
		m_enabled->setChecked( st.value( QStringLiteral( "clip" ), true ).toBool() );
		m_viewAngle = st.value( QStringLiteral( "angle" ), 0.0 ).toDouble();
		view = st.value( QStringLiteral( "view" ), 0 ).toInt();
		syncThicknessSlider();
		m_app->dispToConsole( QStringLiteral( "[Řezy] Obnovena rozdělaná práce z projektu." ) );
	}

	m_activeView = st.isEmpty() ? 0 : -999; // při obnově neukládat výšku řezu do žádného patra
	refreshViewCombo();
	activateView( view );

	if ( m_draw->isActive() )
	{
		m_draw->start( c );
		m_draw->setFrame( frameFor( m_activeView ) );
	}
	if ( !st.isEmpty() )
	{
		const int ref = st.value( QStringLiteral( "ref" ), -1 ).toInt();
		const int idx = m_refCombo->findData( ref );
		if ( idx >= 0 )
		{
			const QSignalBlocker b( m_refSnap );
			m_refSnap->setChecked( st.value( QStringLiteral( "refSnap" ), false ).toBool() );
			m_refCombo->setCurrentIndex( idx );
			m_draw->setReference( ref, m_refSnap->isChecked() );
		}
		// pohled kamery
		if ( group )
		{
			for ( unsigned i = 0; i < group->getChildrenNumber(); ++i )
			{
				if ( auto* vo = dynamic_cast<cc2DViewportObject*>( group->getChild( i ) ) )
				{
					if ( ccGLWindowInterface* win = m_app->getActiveGLWindow() )
					{
						win->setViewportParameters( vo->getParameters() );
						win->redraw();
					}
					break;
				}
			}
		}
	}
	m_restoring = false;
}

void RezDock::storeState( bool withCamera )
{
	if ( m_restoring || !cloud() || !m_draw )
	{
		return;
	}
	ccHObject* group = m_draw->drawingGroup( withCamera );
	if ( !group )
	{
		return;
	}
	storeActiveFloor();
	QVariantMap st;
	st.insert( QStringLiteral( "zero" ), m_zero->value() );
	st.insert( QStringLiteral( "height" ), m_height->value() );
	st.insert( QStringLiteral( "thickness" ), m_thickness->value() );
	st.insert( QStringLiteral( "step" ), m_step->value() );
	st.insert( QStringLiteral( "clip" ), m_enabled->isChecked() );
	st.insert( QStringLiteral( "angle" ), m_viewAngle );
	st.insert( QStringLiteral( "view" ), m_activeView );
	st.insert( QStringLiteral( "ref" ), m_refCombo->currentData().toInt() );
	st.insert( QStringLiteral( "refSnap" ), m_refSnap->isChecked() );
	group->setMetaData( "qRezy.state", st );
	if ( !withCamera )
	{
		return;
	}
	ccGLWindowInterface* win = m_app->getActiveGLWindow();
	if ( !win )
	{
		return;
	}
	cc2DViewportObject* vo = nullptr;
	for ( unsigned i = 0; i < group->getChildrenNumber() && !vo; ++i )
	{
		vo = dynamic_cast<cc2DViewportObject*>( group->getChild( i ) );
	}
	if ( !vo )
	{
		vo = new cc2DViewportObject( QStringLiteral( "Pohled (Řezy)" ) );
		vo->setVisible( false );
		group->addChild( vo );
		m_app->addToDB( vo, false, false, false, false );
	}
	vo->setParameters( win->getViewportParameters() );
}

void RezDock::saveProject()
{
	if ( !cloud() )
	{
		QMessageBox::information( this, QStringLiteral( "Řezy" ), QStringLiteral( "Nejdřív vyberte mračno." ) );
		return;
	}
	storeState( true );
	// projekt CloudCompare (.bin) obsahuje mračno, kresbu, body, řezy, patra i nastavení
	QAction* a = m_app->getMainWindow()->findChild<QAction*>( QStringLiteral( "actionSaveProject" ) );
	if ( a )
	{
		a->trigger();
	}
	else
	{
		QMessageBox::information( this, QStringLiteral( "Řezy" ), QStringLiteral( "Uložte projekt přes File → Save project (.bin)." ) );
	}
}

double RezDock::planZLocal() const
{
	ccGenericPointCloud* c = cloud();
	return c ? c->toLocal3d( CCVector3d( 0, 0, m_height->value() ) ).z : m_height->value();
}

double RezDock::estimateFloor( ccGenericPointCloud* c ) const
{
	// histogram výšek po 2 cm, bereme nejhustší vodorovnou vrstvu
	const unsigned n = c->size();
	if ( n == 0 )
	{
		return 0.0;
	}
	ccBBox box = c->getOwnBB();
	const double zMinL = box.minCorner().z;
	const double zMaxL = box.maxCorner().z;
	const double bin = 0.02 * c->getGlobalScale();
	const size_t bins = static_cast<size_t>( std::max( 1.0, std::ceil( ( zMaxL - zMinL ) / bin ) ) ) + 1;
	if ( bins > 10000000 )
	{
		return c->toGlobal3d( box.getCenter() ).z;
	}
	std::vector<unsigned> h( bins, 0 );
	const unsigned stride = std::max( 1u, n / 2000000u ); // stačí vzorek
	for ( unsigned i = 0; i < n; i += stride )
	{
		const CCVector3* P = c->getPoint( i );
		const size_t k = static_cast<size_t>( ( P->z - zMinL ) / bin );
		if ( k < bins )
		{
			++h[k];
		}
	}
	const size_t best = std::max_element( h.begin(), h.end() ) - h.begin();
	return c->toGlobal3d( CCVector3d( 0.0, 0.0, zMinL + ( best + 0.5 ) * bin ) ).z;
}

void RezDock::step( int direction )
{
	if ( !cloud() || !isVisible() )
	{
		return;
	}
	if ( isPlanView( m_activeView ) )
	{
		m_height->setValue( m_height->value() + direction * m_step->value() );
		return;
	}
	// svislý řez / pohled: PgUp = dál ve směru pohledu, PgDn = blíž
	SectionInfo si;
	if ( sectionById( m_activeView, si ) )
	{
		si.offset += direction * m_step->value();
		saveSection( si );
		applyClip();
	}
}

void RezDock::setThickness( double t )
{
	m_thickness->setValue( t );
}

void RezDock::thicknessStep( int direction )
{
	const double t = m_thickness->value();
	const size_t n = sizeof( ThickLadder ) / sizeof( ThickLadder[0] );
	double next = t;
	if ( direction > 0 )
	{
		for ( size_t i = 0; i < n; ++i )
		{
			if ( ThickLadder[i] > t + 1e-9 )
			{
				next = ThickLadder[i];
				break;
			}
		}
	}
	else
	{
		for ( size_t i = n; i-- > 0; )
		{
			if ( ThickLadder[i] < t - 1e-9 )
			{
				next = ThickLadder[i];
				break;
			}
		}
	}
	setThickness( next );
	m_status->setText( QStringLiteral( "Tloušťka řezu %1 cm" ).arg( QString::number( next * 100.0, 'f', next < 0.1 ? 1 : 0 ).replace( '.', ',' ) ) );
}

void RezDock::onPosSlider( int cm )
{
	const double v = cm / 100.0;
	if ( isPlanView( m_activeView ) )
	{
		const double lv = currentLevel();
		if ( std::abs( m_height->value() - ( v + lv ) ) > 0.004 )
		{
			m_height->setValue( v + lv );
		}
		return;
	}
	SectionInfo si;
	if ( sectionById( m_activeView, si ) && std::abs( si.offset - v ) > 0.004 )
	{
		si.offset = v;
		saveSection( si );
		applyClip();
	}
}

void RezDock::syncPosSlider()
{
	if ( !m_posSlider )
	{
		return;
	}
	ccGenericPointCloud* c = cloud();
	if ( !c )
	{
		m_posSlider->setEnabled( false );
		return;
	}
	const ccBBox box = c->getOwnBB();
	const CCVector3 mn = box.minCorner();
	const CCVector3 mx = box.maxCorner();
	double lo = 0.0;
	double hi = 0.0;
	double cur = 0.0;
	SectionInfo si;
	if ( m_activeView != 0 && sectionById( m_activeView, si ) )
	{
		// posun roviny: offset = (O·N − P·N) / měřítko
		const DrawFrame f = frameFor( m_activeView );
		const double sc = c->getGlobalScale();
		lo = 1e30;
		hi = -1e30;
		for ( int k = 0; k < 8; ++k )
		{
			const CCVector3d P( ( k & 1 ) ? mx.x : mn.x, ( k & 2 ) ? mx.y : mn.y, ( k & 4 ) ? mx.z : mn.z );
			const double o = ( f.O.dot( f.N ) - P.dot( f.N ) ) / sc;
			lo = std::min( lo, o );
			hi = std::max( hi, o );
		}
		cur = si.offset;
	}
	else
	{
		// výška nad ±0
		const double lv = currentLevel();
		lo = c->toGlobal3d( CCVector3d( 0, 0, mn.z ) ).z - lv;
		hi = c->toGlobal3d( CCVector3d( 0, 0, mx.z ) ).z - lv;
		cur = m_height->value() - lv;
	}
	lo = std::min( lo, cur );
	hi = std::max( hi, cur );
	const QSignalBlocker b( m_posSlider );
	m_posSlider->setRange( static_cast<int>( std::floor( lo * 100.0 ) ), static_cast<int>( std::ceil( hi * 100.0 ) ) );
	m_posSlider->setValue( static_cast<int>( std::lround( cur * 100.0 ) ) );
	m_posSlider->setEnabled( true );
	m_posMin->setText( fmt( lo ) );
	m_posMax->setText( fmt( hi ) );
}

void RezDock::syncThicknessSlider()
{
	const QSignalBlocker b( m_thickSlider );
	m_thickSlider->setValue( thickToSlider( m_thickness->value() ) );
}

void RezDock::applyClip()
{
	ccGenericPointCloud* c = cloud();
	updateLabels();
	if ( !c )
	{
		return;
	}

	c->removeAllClipPlanes();

	SectionInfo si;
	if ( m_activeView != 0 && sectionById( m_activeView, si ) )
	{
		if ( m_enabled->isChecked() )
		{
			const double sc = c->getGlobalScale();
			const DrawFrame f = frameFor( m_activeView );
			// rovina: O − N·offset (posun ve směru pohledu)
			const double d = f.O.dot( f.N ) - si.offset * sc;
			if ( si.kind == 0 )
			{
				// řez: tenký plát
				const double half = m_thickness->value() * sc / 2.0;
				ccClipPlane p1;
				p1.equation = Tuple4Tpl<double>( f.N.x, f.N.y, f.N.z, -( d - half ) );
				ccClipPlane p2;
				p2.equation = Tuple4Tpl<double>( -f.N.x, -f.N.y, -f.N.z, d + half );
				c->addClipPlanes( p1 );
				c->addClipPlanes( p2 );
			}
			else
			{
				// pohled: vše za rovinou do zadané hloubky
				const double depth = si.depth * sc;
				ccClipPlane p1;
				p1.equation = Tuple4Tpl<double>( -f.N.x, -f.N.y, -f.N.z, d );
				ccClipPlane p2;
				p2.equation = Tuple4Tpl<double>( f.N.x, f.N.y, f.N.z, -( d - depth ) );
				c->addClipPlanes( p1 );
				c->addClipPlanes( p2 );
			}
			// do stran jen v délce čáry (+ 20 cm)
			const double u0 = f.O.dot( f.U );
			const double len = ( si.B - si.A ).norm();
			const double m = 0.2 * sc;
			ccClipPlane p3;
			p3.equation = Tuple4Tpl<double>( f.U.x, f.U.y, f.U.z, -( u0 - m ) );
			ccClipPlane p4;
			p4.equation = Tuple4Tpl<double>( -f.U.x, -f.U.y, -f.U.z, u0 + len + m );
			c->addClipPlanes( p3 );
			c->addClipPlanes( p4 );
		}
	}
	else if ( m_enabled->isChecked() )
	{
		const double half = m_thickness->value() / 2.0;
		// hranice řezu v souřadnicích uložených v mračnu (s globálním posunem)
		const double zLo = c->toLocal3d( CCVector3d( 0, 0, m_height->value() - half ) ).z;
		const double zHi = c->toLocal3d( CCVector3d( 0, 0, m_height->value() + half ) ).z;
		ccClipPlane lower;
		lower.equation = Tuple4Tpl<double>( 0.0, 0.0, 1.0, -zLo );
		ccClipPlane upper;
		upper.equation = Tuple4Tpl<double>( 0.0, 0.0, -1.0, zHi );
		c->addClipPlanes( lower );
		c->addClipPlanes( upper );
	}

	c->prepareDisplayForRefresh();
	m_app->refreshAll();
	storeState( false );
}

void RezDock::clearClip()
{
	if ( ccGenericPointCloud* c = cloud() )
	{
		c->removeAllClipPlanes();
		c->prepareDisplayForRefresh();
		if ( m_app )
		{
			m_app->refreshAll();
		}
	}
}

void RezDock::updateLabels()
{
	ccGenericPointCloud* c = cloud();
	m_cloudLabel->setText( c ? QStringLiteral( "<b>%1</b><br><small>%2 bodů</small>" ).arg( c->getName() ).arg( c->size() )
	                         : QStringLiteral( "<i>Vyberte mračno v seznamu objektů</i>" ) );

	SectionInfo si;
	const bool sec = m_activeView != 0 && sectionById( m_activeView, si );
	if ( sec )
	{
		m_relLabel->setText( QStringLiteral( "posun %1" ).arg( fmt( si.offset ) ) );
		m_upBtn->setToolTip( QStringLiteral( "Dál ve směru pohledu (PgUp)" ) );
		m_downBtn->setToolTip( QStringLiteral( "Blíž (PgDn)" ) );
	}
	else
	{
		m_relLabel->setText( fmt( m_height->value() - currentLevel() ) );
		m_upBtn->setToolTip( QStringLiteral( "Nahoru (PgUp)" ) );
		m_downBtn->setToolTip( QStringLiteral( "Dolů (PgDn)" ) );
	}
	m_height->setEnabled( !sec );
	m_floorLevel->setEnabled( isPlanView( m_activeView ) && m_activeView != 0 );
	m_refRow->setEnabled( isPlanView( m_activeView ) );
	syncPosSlider();
	m_thickness->setEnabled( !sec || si.kind == 0 );
	m_thickSlider->setEnabled( !sec || si.kind == 0 );
	m_depth->setEnabled( sec && si.kind == 1 );
	if ( sec && si.kind == 1 )
	{
		const QSignalBlocker b( m_depth );
		m_depth->setValue( si.depth );
	}
}

void RezDock::onStatus( const QString& text )
{
	m_status->setText( text );
	// výzva i přímo v okně (jen když se změní)
	const QString prompt = text.section( '\n', 0, 0 );
	if ( prompt != m_lastPrompt && m_draw->isActive() && m_app && m_app->getActiveGLWindow() )
	{
		m_lastPrompt = prompt;
		m_app->getActiveGLWindow()->displayNewMessage( prompt, ccGLWindowInterface::UPPER_CENTER_MESSAGE, false, 3600, ccGLWindowInterface::MANUAL_SEGMENTATION_MESSAGE );
	}
}

void RezDock::topView()
{
	ccGLWindowInterface* win = m_app ? m_app->getActiveGLWindow() : nullptr;
	if ( !win )
	{
		return;
	}
	SectionInfo si;
	if ( m_activeView != 0 && sectionById( m_activeView, si ) )
	{
		sectionCamera( si );
		return;
	}
	win->setPerspectiveState( false, true ); // pravoúhlé zobrazení
	// pohled shora natočený podle objektu – souřadnice se nemění
	ccGLMatrixd rotZ;
	rotZ.initFromParameters( -m_viewAngle, CCVector3d( 0, 0, 1 ), CCVector3d( 0, 0, 0 ) );
	ccGLMatrixd view = ccGLUtils::GenerateViewMat( CC_TOP_VIEW ) * rotZ;
	win->setBaseViewMat( view );
	win->redraw();
}

void RezDock::zeroFromFloor()
{
	if ( ccGenericPointCloud* c = cloud() )
	{
		m_zero->setValue( estimateFloor( c ) );
	}
}

void RezDock::focusView()
{
	if ( m_draw && m_draw->isActive() && m_app && m_app->getActiveGLWindow() )
	{
		if ( QWidget* w = qobject_cast<QWidget*>( m_app->getActiveGLWindow()->asQObject() ) )
		{
			w->setFocus();
		}
	}
}

void RezDock::setDrawing( bool on )
{
	if ( !on )
	{
		m_draw->stop();
		m_lastPrompt.clear();
		if ( m_app && m_app->getActiveGLWindow() )
		{
			m_app->getActiveGLWindow()->displayNewMessage( QString(), ccGLWindowInterface::UPPER_CENTER_MESSAGE, false, 0, ccGLWindowInterface::MANUAL_SEGMENTATION_MESSAGE );
		}
		return;
	}
	ccGenericPointCloud* c = cloud();
	if ( !c )
	{
		m_app->dispToConsole( QStringLiteral( "[Řezy] Nejdřív vyberte mračno (ikona mračna v panelu Řezy)." ), ccMainAppInterface::WRN_CONSOLE_MESSAGE );
		const QSignalBlocker b( m_drawAct );
		m_drawAct->setChecked( false );
		return;
	}
	topView();
	if ( !m_draw->start( c ) )
	{
		const QSignalBlocker b( m_drawAct );
		m_drawAct->setChecked( false );
		return;
	}
	m_draw->setFrame( frameFor( m_activeView ) );
	m_draw->setCategory( m_catCombo->currentIndex() );
	m_draw->setHidden( m_hiddenAct->isChecked() );
	m_draw->setOrtho( m_orthoAct->isChecked() );
	m_draw->setTracking( m_trackAct->isChecked() );
	m_draw->setOffsetDistance( m_offsetSpin->value() );
	if ( QAction* a = m_toolGroup->checkedAction() )
	{
		m_draw->setMode( static_cast<DrawTool::Mode>( a->data().toInt() ) );
	}
	focusView();
}

// ================================================================ 3D náhled

bool RezDock::ensure3D( bool aux )
{
	ccGenericPointCloud* g = cloud();
	ccPointCloud* pc = g ? ccHObjectCaster::ToPointCloud( g ) : nullptr;
	if ( !pc )
	{
		m_app->dispToConsole( QStringLiteral( "[Řezy] Nejdřív vyberte mračno." ), ccMainAppInterface::WRN_CONSOLE_MESSAGE );
		return false;
	}
	View3D*& v = aux ? m_viewAux : m_view3D;
	if ( !v )
	{
		v = new View3D( m_app, m_app->getMainWindow(), aux );
		m_app->getMainWindow()->addDockWidget( Qt::RightDockWidgetArea, v );
		v->setFloating( true );
		v->resize( 760, 600 );
		if ( aux )
		{
			v->move( v->pos() + QPoint( 60, 60 ) );
		}
	}
	v->show();
	v->raise();
	QApplication::setOverrideCursor( Qt::WaitCursor );
	const bool ok = v->setSource( pc, 20000000 );
	QApplication::restoreOverrideCursor();
	if ( ok )
	{
		refresh3DLines();
	}
	return ok;
}

void RezDock::refresh3DLines()
{
	const bool v3 = m_view3D && m_view3D->isVisible();
	const bool va = m_viewAux && m_viewAux->isVisible();
	if ( !v3 && !va )
	{
		return;
	}
	std::vector<View3D::Line> lines;
	ccGenericPointCloud* c = cloud();
	const double sc = c ? c->getGlobalScale() : 1.0;
	ccHObject* group = m_draw->drawingGroup( false );
	if ( group )
	{
		for ( unsigned i = 0; i < group->getChildrenNumber(); ++i )
		{
			ccHObject* ch = group->getChild( i );
			if ( !ch->isA( CC_TYPES::POLY_LINE ) || !ch->hasMetaData( Drawing::MetaCategory ) )
			{
				continue;
			}
			auto* poly = static_cast<ccPolyline*>( ch );
			const int view = ch->hasMetaData( Drawing::MetaView ) ? ch->getMetaData( Drawing::MetaView ).toInt() : 0;
			const int cat = ch->getMetaData( Drawing::MetaCategory ).toInt();
			const bool hid = ch->getMetaData( Drawing::MetaHidden ).toBool();
			View3D::Line l;
			const QColor qc = Drawing::displayColor( cat, hid );
			l.color = ccColor::Rgb( static_cast<ColorCompType>( qc.red() ), static_cast<ColorCompType>( qc.green() ), static_cast<ColorCompType>( qc.blue() ) );
			l.dashed = hid;
			if ( isPlanView( view ) )
			{
				// celé patro v jedné rovině: na podlaze patra (posuvníkem nahoru/dolů)
				FloorInfo fi;
				if ( !floorById( view, fi ) )
				{
					continue;
				}
				const double zl = c ? c->toLocal3d( CCVector3d( 0, 0, fi.level ) ).z : fi.level;
				for ( unsigned k = 0; k < poly->size(); ++k )
				{
					const CCVector3* P = poly->getPoint( k );
					l.pts.push_back( CCVector3d( P->x, P->y, zl ) );
				}
				l.shiftDir = CCVector3d( 0, 0, sc );
			}
			else
			{
				// řez / pohled: v rovině řezu, u pohledu na líci objektu (posuvníkem do hloubky)
				SectionInfo si;
				if ( !sectionById( view, si ) )
				{
					continue;
				}
				const DrawFrame f = frameFor( view );
				const double sN = ( si.kind == 1 && si.face >= 0.0 ) ? -si.face * sc : -si.offset * sc;
				for ( unsigned k = 0; k < poly->size(); ++k )
				{
					const CCVector2d uv = f.toUV( CCVector3d::fromArray( poly->getPoint( k )->u ) );
					l.pts.push_back( f.O + f.U * uv.x + f.V * uv.y + f.N * sN );
				}
				l.shiftDir = f.N * -sc;
			}
			lines.push_back( l );
		}
	}
	if ( v3 )
	{
		m_view3D->setLines( lines );
	}
	if ( va )
	{
		m_viewAux->setLines( lines );
	}
}

void RezDock::show3DAt( double x, double y, double z )
{
	if ( !ensure3D() )
	{
		return;
	}
	const double zLocal = std::isnan( z ) ? planZLocal() : z;
	double ang = m_viewAngle;
	SectionInfo si;
	if ( m_activeView != 0 && sectionById( m_activeView, si ) )
	{
		// pohled z místa pozorovatele řezu
		ang = std::atan2( -si.N.x, si.N.y );
	}
	m_view3D->focusOn( CCVector3d( x, y, zLocal ), ang );
}

// ================================================================ výstup

bool RezDock::chooseSeed( QWidget* parent )
{
	QSettings st;
	const QString old = st.value( QStringLiteral( "qRezy/seed" ) ).toString();
	const QString f = QFileDialog::getOpenFileName( parent, QStringLiteral( "Vyberte SEED – existující výkres V7 (šablona, nemění se)" ), old, QStringLiteral( "MicroStation V7 (*.dgn);;Všechny soubory (*)" ) );
	if ( f.isEmpty() )
	{
		return false;
	}
	QFile file( f );
	if ( !file.open( QIODevice::ReadOnly ) )
	{
		QMessageBox::warning( parent, QStringLiteral( "Řezy" ), QStringLiteral( "Soubor nelze otevřít." ) );
		return false;
	}
	const QByteArray data = file.readAll();
	std::vector<uint8_t> bytes( data.begin(), data.end() );
	DgnV7::Writer w;
	std::string err;
	if ( !w.loadSeedBytes( bytes, err ) )
	{
		QMessageBox::warning( parent, QStringLiteral( "Řezy" ), QString::fromStdString( err ) );
		return false;
	}
	st.setValue( QStringLiteral( "qRezy/seed" ), f );
	return true;
}

void RezDock::openSaveDialog()
{
	if ( !cloud() )
	{
		QMessageBox::information( this, QStringLiteral( "Řezy" ), QStringLiteral( "Nejdřív vyberte mračno." ) );
		return;
	}

	QDialog dlg( this );
	dlg.setWindowTitle( QStringLiteral( "Uložit výkres V7" ) );
	dlg.setMinimumWidth( 480 );
	auto* l = new QVBoxLayout( &dlg );

	// seed
	auto* seedBox = new QGroupBox( QStringLiteral( "Seed (šablona výkresu – nemění se)" ) );
	auto* sl = new QHBoxLayout( seedBox );
	auto* seedLabel = new QLabel;
	auto updateSeed = [seedLabel]()
	{
		const QString s = QSettings().value( QStringLiteral( "qRezy/seed" ) ).toString();
		seedLabel->setText( s.isEmpty() ? QStringLiteral( "<i>nevybrán</i>" ) : QFileInfo( s ).fileName() );
		seedLabel->setToolTip( s );
	};
	updateSeed();
	auto* seedBtn = new QPushButton( QStringLiteral( "Vybrat…" ) );
	auto* symBtn = new QPushButton( QStringLiteral( "Hladiny a barvy…" ) );
	sl->addWidget( seedLabel, 1 );
	sl->addWidget( seedBtn );
	sl->addWidget( symBtn );
	l->addWidget( seedBox );
	connect( seedBtn, &QPushButton::clicked, &dlg, [this, &dlg, updateSeed]() { chooseSeed( &dlg ); updateSeed(); } );
	connect( symBtn, &QPushButton::clicked, &dlg, [this]() { editSymbology(); } );

	// soubory: každé patro zvlášť, řezy a pohledy společně
	storeActiveFloor();
	const std::vector<FloorInfo> floorsAll = loadFloors();
	auto* fileBox = new QGroupBox( QStringLiteral( "Soubory (každé patro zvlášť, řezy a pohledy do společného souboru)" ) );
	auto* fl = new QVBoxLayout( fileBox );
	std::vector<std::pair<int, QCheckBox*>> floorChecks;
	for ( const FloorInfo& f : floorsAll )
	{
		auto* cb = new QCheckBox( QStringLiteral( "Půdorys %1" ).arg( f.name ) );
		cb->setChecked( true );
		fl->addWidget( cb );
		floorChecks.push_back( { f.id, cb } );
	}
	auto* secCheck = new QCheckBox( QStringLiteral( "Řezy a pohledy" ) );
	secCheck->setChecked( true );
	secCheck->setVisible( !loadSections().empty() );
	fl->addWidget( secCheck );
	l->addWidget( fileBox );

	// řezy a pohledy
	QSettings st;
	auto* secBox = new QGroupBox( QStringLiteral( "Řezy a pohledy – umístění ve výkresu (levý konec, ±0,000)" ) );
	auto* secL = new QVBoxLayout( secBox );
	const std::vector<SectionInfo> sections = loadSections();
	auto* table = new QTableWidget( static_cast<int>( sections.size() ), 3 );
	table->setHorizontalHeaderLabels( { QStringLiteral( "Název" ), QStringLiteral( "X" ), QStringLiteral( "Y" ) } );
	table->verticalHeader()->setVisible( false );
	table->horizontalHeader()->setSectionResizeMode( 0, QHeaderView::Stretch );
	for ( int i = 0; i < static_cast<int>( sections.size() ); ++i )
	{
		auto* name = new QTableWidgetItem( sections[i].name );
		name->setFlags( Qt::ItemIsEnabled );
		table->setItem( i, 0, name );
		table->setCellWidget( i, 1, makeSpin( -1.0e8, 1.0e8, sections[i].insX, 1.0, 3, QString() ) );
		table->setCellWidget( i, 2, makeSpin( -1.0e8, 1.0e8, sections[i].insY, 1.0, 3, QString() ) );
	}
	table->setMinimumHeight( sections.empty() ? 40 : 120 );
	secL->addWidget( table );
	auto* absH = new QCheckBox( QStringLiteral( "Výšky v řezech absolutně (Bpv) – jinak relativně k ±0,000" ) );
	absH->setChecked( st.value( QStringLiteral( "qRezy/absHeights" ), false ).toBool() );
	secL->addWidget( absH );
	auto* tf = new QFormLayout;
	auto* textH = makeSpin( 0.01, 10.0, st.value( QStringLiteral( "qRezy/textHeight" ), 0.25 ).toDouble(), 0.05, 2, QStringLiteral( " m" ) );
	tf->addRow( QStringLiteral( "Výška popisů (název, ±0,000):" ), textH );
	auto* uTextH = makeSpin( 0.01, 10.0, st.value( QStringLiteral( "qRezy/userTextHeight" ), 0.15 ).toDouble(), 0.05, 2, QStringLiteral( " m" ) );
	tf->addRow( QStringLiteral( "Vložené texty – výška:" ), uTextH );
	auto* uTextL = new QSpinBox;
	uTextL->setRange( 1, 63 );
	uTextL->setValue( st.value( QStringLiteral( "qRezy/userTextLevel" ), 15 ).toInt() );
	tf->addRow( QStringLiteral( "Vložené texty – hladina:" ), uTextL );
	secL->addLayout( tf );
	l->addWidget( secBox );

	auto* bb = new QDialogButtonBox;
	auto* saveBtn = bb->addButton( QStringLiteral( "Uložit výkres…" ), QDialogButtonBox::AcceptRole );
	saveBtn->setIcon( Icons::get( QStringLiteral( "save" ) ) );
	bb->addButton( QStringLiteral( "Zavřít" ), QDialogButtonBox::RejectRole );
	l->addWidget( bb );
	connect( bb, &QDialogButtonBox::accepted, &dlg, &QDialog::accept );
	connect( bb, &QDialogButtonBox::rejected, &dlg, &QDialog::reject );

	const int res = dlg.exec();

	// nastavení se ukládá vždy
	st.setValue( QStringLiteral( "qRezy/absHeights" ), absH->isChecked() );
	st.setValue( QStringLiteral( "qRezy/textHeight" ), textH->value() );
	st.setValue( QStringLiteral( "qRezy/userTextHeight" ), uTextH->value() );
	st.setValue( QStringLiteral( "qRezy/userTextLevel" ), uTextL->value() );
	for ( int i = 0; i < static_cast<int>( sections.size() ); ++i )
	{
		SectionInfo si = sections[i];
		si.insX = static_cast<QDoubleSpinBox*>( table->cellWidget( i, 1 ) )->value();
		si.insY = static_cast<QDoubleSpinBox*>( table->cellWidget( i, 2 ) )->value();
		saveSection( si );
	}
	if ( res == QDialog::Accepted )
	{
		std::vector<int> ids;
		for ( const auto& fc : floorChecks )
		{
			if ( fc.second->isChecked() )
			{
				ids.push_back( fc.first );
			}
		}
		exportV7( ids, !secCheck->isHidden() && secCheck->isChecked() ); // isVisible() je po zavření okna vždy false
	}
}

void RezDock::exportV7( const std::vector<int>& floorIds, bool withSections )
{
	ccGenericPointCloud* c = cloud();
	if ( !c )
	{
		return;
	}
	QSettings st;
	const Drawing::SymbologyTable table = Drawing::SymbologyTable::load();
	const CCVector3d shift = c->getGlobalShift();
	const double sc = c->getGlobalScale();
	const bool absH = st.value( QStringLiteral( "qRezy/absHeights" ), false ).toBool();
	const double textH = st.value( QStringLiteral( "qRezy/textHeight" ), 0.25 ).toDouble();
	const int userTextLevel = st.value( QStringLiteral( "qRezy/userTextLevel" ), 15 ).toInt();
	const double userTextH = st.value( QStringLiteral( "qRezy/userTextHeight" ), 0.15 ).toDouble();
	const double zeroG = m_zero->value();
	const std::vector<SectionInfo> sections = loadSections();
	const std::vector<FloorInfo> floors = loadFloors();

	// cílové soubory
	struct Target
	{
		QString suffix;
		std::vector<int> views;
		bool sections = false;
	};
	std::vector<Target> targets;
	for ( int id : floorIds )
	{
		FloorInfo f;
		if ( floorById( id, f ) )
		{
			Target t;
			t.suffix = f.name;
			t.views.push_back( id );
			targets.push_back( t );
		}
	}
	if ( withSections && !sections.empty() )
	{
		Target t;
		t.suffix = QStringLiteral( "rezy" );
		t.sections = true;
		for ( const SectionInfo& si : sections )
		{
			t.views.push_back( si.id );
		}
		targets.push_back( t );
	}
	if ( targets.empty() )
	{
		QMessageBox::information( this, QStringLiteral( "Řezy" ), QStringLiteral( "Není zaškrtnutý žádný soubor." ) );
		return;
	}
	// jeden půdorys bez pater a řezů = jeden soubor bez přípony (jako dřív)
	const bool multi = targets.size() > 1 || floors.size() > 1;

	QString seed = st.value( QStringLiteral( "qRezy/seed" ) ).toString();
	if ( seed.isEmpty() || !QFileInfo::exists( seed ) )
	{
		QMessageBox::information( this, QStringLiteral( "Řezy" ),
			QStringLiteral( "Nejdřív vyberte <b>seed</b> – existující výkres V7, ze kterého se převezmou jednotky a nastavení "
			                "(např. váš vzorový výkres). Seed se nezmění a zapamatuje se.<br><br>"
			                "Potom se otevře okno pro uložení nového výkresu." ) );
		if ( !chooseSeed( this ) )
		{
			return;
		}
		seed = st.value( QStringLiteral( "qRezy/seed" ) ).toString();
	}
	QFile in( seed );
	if ( !in.open( QIODevice::ReadOnly ) )
	{
		QMessageBox::warning( this, QStringLiteral( "Řezy" ), QStringLiteral( "Seed nelze otevřít: %1" ).arg( seed ) );
		return;
	}
	const QByteArray seedData = in.readAll();
	std::vector<uint8_t> seedBytes( seedData.begin(), seedData.end() );
	DgnV7::Writer w;
	std::string err;
	if ( !w.loadSeedBytes( seedBytes, err ) )
	{
		QMessageBox::warning( this, QStringLiteral( "Řezy" ), QString::fromStdString( err ) );
		return;
	}

	const QString lastOut = st.value( QStringLiteral( "qRezy/lastOut" ), QFileInfo( seed ).absolutePath() ).toString();
	QString out = QFileDialog::getSaveFileName( this, multi ? QStringLiteral( "Uložit výkresy V7 – zadejte základ názvu (přidá se _1NP, _rezy …)" ) : QStringLiteral( "Uložit NOVÝ výkres V7 jako…" ),
	                                            lastOut, QStringLiteral( "MicroStation V7 (*.dgn)" ) );
	if ( out.isEmpty() )
	{
		return;
	}
	if ( out.endsWith( QStringLiteral( ".dgn" ), Qt::CaseInsensitive ) )
	{
		out.chop( 4 );
	}

	QStringList report;
	QStringList skipped;
	for ( const Target& t : targets )
	{
		std::vector<int> usedViews;
		auto map = [&]( int view, const CCVector3d& P, DgnV7::Point2& o ) -> bool
		{
			if ( std::find( t.views.begin(), t.views.end(), view ) == t.views.end() )
			{
				return false; // patří do jiného souboru
			}
			if ( isPlanView( view ) )
			{
				o.x = P.x / sc - shift.x;
				o.y = P.y / sc - shift.y;
				return true;
			}
			for ( const SectionInfo& si : sections )
			{
				if ( si.id == view )
				{
					const DrawFrame f = frameFor( view );
					const CCVector2d uv = f.toUV( P );
					o.x = si.insX + uv.x / sc;
					o.y = absH ? ( uv.y / sc + zeroG ) : ( si.insY + uv.y / sc );
					if ( std::find( usedViews.begin(), usedViews.end(), view ) == usedViews.end() )
					{
						usedViews.push_back( view );
					}
					return true;
				}
			}
			return false;
		};
		std::vector<DgnV7::Element> elements = m_draw->exportElements( table, map, userTextLevel, userTextH );
		if ( t.sections )
		{
			// popisy řezů a pohledů
			QTextCodec* codec = QTextCodec::codecForName( "Windows-1250" );
			for ( const SectionInfo& si : sections )
			{
				if ( std::find( usedViews.begin(), usedViews.end(), si.id ) == usedViews.end() )
				{
					continue;
				}
				const double y0 = absH ? zeroG : si.insY;
				QStringList texts;
				texts << si.name;
				if ( !absH )
				{
					texts << QStringLiteral( "±0,000 = %1 m n. m. Bpv" ).arg( QString::number( zeroG, 'f', 3 ).replace( '.', ',' ) );
				}
				for ( int i = 0; i < texts.size(); ++i )
				{
					DgnV7::Element tx;
					tx.isText = true;
					tx.sym.level = 14;
					tx.sym.color = 0;
					tx.textHeight = textH;
					tx.pts.push_back( { si.insX, y0 - ( i + 2 ) * textH * 1.6 } );
					const QByteArray bytes = codec ? codec->fromUnicode( texts[i] ) : texts[i].toLatin1();
					tx.text = std::string( bytes.constData(), static_cast<size_t>( bytes.size() ) );
					elements.push_back( tx );
				}
			}
		}
		const QString path = multi ? QStringLiteral( "%1_%2.dgn" ).arg( out, t.suffix ) : out + QStringLiteral( ".dgn" );
		if ( elements.empty() )
		{
			skipped << QFileInfo( path ).fileName();
			continue;
		}
		if ( QFileInfo( path ).absoluteFilePath() == QFileInfo( seed ).absoluteFilePath() )
		{
			QMessageBox::warning( this, QStringLiteral( "Řezy" ), QStringLiteral( "Výstup nesmí přepsat seed výkres." ) );
			return;
		}
		std::vector<uint8_t> bytes;
		if ( !w.writeBytes( bytes, elements, err ) )
		{
			QMessageBox::warning( this, QStringLiteral( "Řezy" ), QString::fromStdString( err ) );
			return;
		}
		QFile f( path );
		if ( !f.open( QIODevice::WriteOnly | QIODevice::Truncate ) || f.write( reinterpret_cast<const char*>( bytes.data() ), static_cast<qint64>( bytes.size() ) ) != static_cast<qint64>( bytes.size() ) )
		{
			QMessageBox::warning( this, QStringLiteral( "Řezy" ), QStringLiteral( "Soubor nelze zapsat: %1" ).arg( path ) );
			return;
		}
		report << QStringLiteral( "%1 – %2 prvků" ).arg( QFileInfo( path ).fileName() ).arg( elements.size() );
		m_app->dispToConsole( QStringLiteral( "[Řezy] Uloženo %1 prvků do %2" ).arg( elements.size() ).arg( path ) );
		st.setValue( QStringLiteral( "qRezy/lastOut" ), path );
	}
	QString msg = report.isEmpty() ? QStringLiteral( "Nic se neuložilo – zatím není nic nakreslené." ) : QStringLiteral( "Uloženo:\n" ) + report.join( '\n' );
	if ( !skipped.isEmpty() )
	{
		msg += QStringLiteral( "\n\nPrázdné (neuloženo): " ) + skipped.join( QStringLiteral( ", " ) );
	}
	QMessageBox::information( this, QStringLiteral( "Řezy" ), msg );
}

std::vector<RezDock::SectionInfo> RezDock::loadSections() const
{
	std::vector<SectionInfo> out;
	ccHObject* group = m_draw ? m_draw->drawingGroup( false ) : nullptr;
	if ( !group )
	{
		return out;
	}
	for ( unsigned i = 0; i < group->getChildrenNumber(); ++i )
	{
		ccHObject* ch = group->getChild( i );
		if ( !ch->isA( CC_TYPES::POLY_LINE ) || !ch->hasMetaData( KSecId ) )
		{
			continue;
		}
		SectionInfo si;
		si.id = ch->getMetaData( KSecId ).toInt();
		si.kind = ch->getMetaData( "qRezy.secKind" ).toInt();
		si.name = sectionName( si.kind, ch->hasMetaData( "qRezy.secNo" ) ? ch->getMetaData( "qRezy.secNo" ).toInt() : si.id );
		if ( ch->hasMetaData( "qRezy.secName" ) )
		{
			si.name = ch->getMetaData( "qRezy.secName" ).toString();
		}
		si.A = CCVector2d( ch->getMetaData( "qRezy.secAx" ).toDouble(), ch->getMetaData( "qRezy.secAy" ).toDouble() );
		si.B = CCVector2d( ch->getMetaData( "qRezy.secBx" ).toDouble(), ch->getMetaData( "qRezy.secBy" ).toDouble() );
		si.N = CCVector2d( ch->getMetaData( "qRezy.secNx" ).toDouble(), ch->getMetaData( "qRezy.secNy" ).toDouble() );
		si.offset = ch->getMetaData( "qRezy.secOffset" ).toDouble();
		si.depth = ch->hasMetaData( "qRezy.secDepth" ) ? ch->getMetaData( "qRezy.secDepth" ).toDouble() : 15.0;
		si.insX = ch->getMetaData( "qRezy.secInsX" ).toDouble();
		si.insY = ch->getMetaData( "qRezy.secInsY" ).toDouble();
		si.face = ch->hasMetaData( "qRezy.secFace" ) ? ch->getMetaData( "qRezy.secFace" ).toDouble() : -1.0;
		si.poly = static_cast<ccPolyline*>( ch );
		out.push_back( si );
	}
	std::sort( out.begin(), out.end(), []( const SectionInfo& a, const SectionInfo& b ) { return a.id < b.id; } );
	return out;
}

bool RezDock::sectionById( int id, SectionInfo& s ) const
{
	if ( id <= 0 )
	{
		return false;
	}
	for ( const SectionInfo& si : loadSections() )
	{
		if ( si.id == id )
		{
			s = si;
			return true;
		}
	}
	return false;
}

void RezDock::saveSection( const SectionInfo& s )
{
	if ( !s.poly )
	{
		return;
	}
	s.poly->setMetaData( KSecId, s.id );
	s.poly->setMetaData( "qRezy.secKind", s.kind );
	s.poly->setMetaData( "qRezy.secAx", s.A.x );
	s.poly->setMetaData( "qRezy.secAy", s.A.y );
	s.poly->setMetaData( "qRezy.secBx", s.B.x );
	s.poly->setMetaData( "qRezy.secBy", s.B.y );
	s.poly->setMetaData( "qRezy.secNx", s.N.x );
	s.poly->setMetaData( "qRezy.secNy", s.N.y );
	s.poly->setMetaData( "qRezy.secOffset", s.offset );
	s.poly->setMetaData( "qRezy.secDepth", s.depth );
	s.poly->setMetaData( "qRezy.secInsX", s.insX );
	s.poly->setMetaData( "qRezy.secInsY", s.insY );
	s.poly->setMetaData( "qRezy.secFace", s.face );
	updateLabels();
}

void RezDock::createSection( int kind, double ax, double ay, double bx, double by, double cx, double cy )
{
	ccGenericPointCloud* c = cloud();
	ccHObject* group = m_draw->drawingGroup( true );
	if ( !c || !group )
	{
		return;
	}
	const CCVector2d A( ax, ay );
	const CCVector2d B( bx, by );
	CCVector2d d = B - A;
	if ( d.norm() < 1e-6 )
	{
		return;
	}
	d.normalize();
	const CCVector2d p( -d.y, d.x );
	const double side = ( CCVector2d( cx, cy ) - A ).dot( p );
	// pozorovatel stojí na opačné straně, než kam se dívá
	const CCVector2d N = ( side >= 0 ) ? p * -1.0 : p;

	if ( kind == 2 )
	{
		// pomocný řez: jen okno pro měření, nic se neukládá
		if ( ensure3D( true ) )
		{
			m_viewAux->auxSection( CCVector3d( ax, ay, 0 ), CCVector3d( bx, by, 0 ), CCVector3d( N.x, N.y, 0 ), planZLocal(), m_zero->value(), m_thickness->value() );
		}
		return;
	}

	// id = jedinečné; číslo = pořadí v rámci řezů (A, B, …) nebo pohledů (1, 2, …)
	int id = 1;
	int no = 1;
	const std::vector<SectionInfo> existing = loadSections();
	for ( const SectionInfo& si : existing )
	{
		id = std::max( id, si.id + 1 );
		if ( si.kind == kind && si.poly )
		{
			no = std::max( no, si.poly->getMetaData( "qRezy.secNo" ).toInt() + 1 );
		}
	}

	// čára řezu / pohledu v půdorysu
	const ccBBox bb = c->getOwnBB();
	const double z = bb.maxCorner().z + 1.0;
	auto* verts = new ccPointCloud( "vertices" );
	verts->reserve( 2 );
	verts->addPoint( CCVector3( static_cast<PointCoordinateType>( ax ), static_cast<PointCoordinateType>( ay ), static_cast<PointCoordinateType>( z ) ) );
	verts->addPoint( CCVector3( static_cast<PointCoordinateType>( bx ), static_cast<PointCoordinateType>( by ), static_cast<PointCoordinateType>( z ) ) );
	verts->setEnabled( false );
	verts->setGlobalShift( c->getGlobalShift() );
	verts->setGlobalScale( c->getGlobalScale() );
	auto* poly = new RezLine( verts );
	poly->addChild( verts );
	poly->addPointIndex( 0, 2 );
	poly->setGlobalShift( c->getGlobalShift() );
	poly->setGlobalScale( c->getGlobalScale() );
	poly->setDashed( true );
	poly->setColor( kind == 1 ? ccColor::Rgb( 60, 140, 255 ) : ccColor::Rgb( 0, 230, 230 ) );
	poly->showColors( true );
	poly->setWidth( 2 );
	poly->setMetaData( Drawing::MetaView, -1 );
	poly->setMetaData( "qRezy.secNo", no );
	group->addChild( poly );
	m_app->addToDB( poly, false, false, false, false );

	SectionInfo si;
	si.id = id;
	si.kind = kind;
	si.name = sectionName( kind, no );
	si.A = A;
	si.B = B;
	si.N = N;
	si.poly = poly;
	poly->setName( si.name );
	// výchozí umístění v souboru řezů: řezy v řadě, pohledy v řadě pod nimi
	const CCVector3d gmin = c->toGlobal3d( bb.minCorner() );
	const CCVector3d gmax = c->toGlobal3d( bb.maxCorner() );
	const double zRange = std::min( 30.0, gmax.z - gmin.z );
	double x = gmin.x;
	for ( const SectionInfo& e : existing )
	{
		if ( e.kind == kind )
		{
			x = std::max( x, e.insX + ( e.B - e.A ).norm() / c->getGlobalScale() + 5.0 );
		}
	}
	si.insX = x;
	si.insY = gmin.y - 5.0 - ( kind == 1 ? ( zRange + 10.0 ) : 0.0 );
	if ( kind == 1 )
	{
		si.face = estimateFace( si );
	}
	saveSection( si );

	refreshViewCombo();
	activateView( id );
}

void RezDock::deleteActiveSection()
{
	if ( isPlanView( m_activeView ) )
	{
		FloorInfo fi;
		if ( m_activeView == 0 || !floorById( m_activeView, fi ) )
		{
			QMessageBox::information( this, QStringLiteral( "Řezy" ), QStringLiteral( "První patro nejde smazat. Vyberte v seznamu řez, pohled nebo jiné patro." ) );
			return;
		}
		if ( QMessageBox::question( this, QStringLiteral( "Řezy" ), QStringLiteral( "Smazat patro %1 i se vším, co je v něm nakreslené?" ).arg( fi.name ) ) != QMessageBox::Yes )
		{
			return;
		}
		ccHObject* group = m_draw->drawingGroup( false );
		const int id = fi.id;
		activateView( 0 );
		std::vector<FloorInfo> floors = loadFloors();
		floors.erase( std::remove_if( floors.begin(), floors.end(), [id]( const FloorInfo& f ) { return f.id == id; } ), floors.end() );
		saveFloors( floors );
		if ( group )
		{
			std::vector<ccHObject*> toRemove;
			for ( unsigned i = 0; i < group->getChildrenNumber(); ++i )
			{
				ccHObject* ch = group->getChild( i );
				if ( ch->isA( CC_TYPES::POLY_LINE ) && ch->hasMetaData( Drawing::MetaView ) && ch->getMetaData( Drawing::MetaView ).toInt() == id )
				{
					toRemove.push_back( ch );
				}
			}
			for ( ccHObject* o : toRemove )
			{
				m_app->removeFromDB( o, true );
			}
		}
		refreshViewCombo();
		refreshRefCombo();
		refresh3DLines();
		m_app->refreshAll();
		return;
	}
	SectionInfo si;
	if ( !sectionById( m_activeView, si ) )
	{
		QMessageBox::information( this, QStringLiteral( "Řezy" ), QStringLiteral( "Vyberte nahoře v seznamu řez nebo pohled, který chcete smazat." ) );
		return;
	}
	if ( QMessageBox::question( this, QStringLiteral( "Řezy" ), QStringLiteral( "Smazat %1 i se vším, co je v něm nakreslené?" ).arg( si.name ) ) != QMessageBox::Yes )
	{
		return;
	}
	ccHObject* group = m_draw->drawingGroup( false );
	activateView( 0 );
	if ( group )
	{
		std::vector<ccHObject*> toRemove;
		for ( unsigned i = 0; i < group->getChildrenNumber(); ++i )
		{
			ccHObject* ch = group->getChild( i );
			if ( ch->isA( CC_TYPES::POLY_LINE ) && ch->hasMetaData( Drawing::MetaView ) && ch->getMetaData( Drawing::MetaView ).toInt() == si.id )
			{
				toRemove.push_back( ch );
			}
		}
		toRemove.push_back( si.poly );
		for ( ccHObject* o : toRemove )
		{
			m_app->removeFromDB( o, true );
		}
	}
	refreshViewCombo();
	refresh3DLines();
	m_app->refreshAll();
}

DrawFrame RezDock::frameFor( int viewId ) const
{
	DrawFrame f;
	SectionInfo si;
	ccGenericPointCloud* c = cloud();
	if ( isPlanView( viewId ) || !c || !sectionById( viewId, si ) )
	{
		// půdorys (patro) ve výšce řezu
		f.viewId = isPlanView( viewId ) ? viewId : 0;
		f.plan = true;
		f.O = CCVector3d( 0, 0, planZLocal() );
		return f;
	}
	f.plan = false;
	const double sc = c->getGlobalScale();
	f.viewId = viewId;
	f.N = CCVector3d( si.N.x, si.N.y, 0.0 );
	f.V = CCVector3d( 0.0, 0.0, 1.0 );
	f.U = f.V.cross( f.N ); // vpravo při pohledu proti N
	// počátek: levý konec čáry ve výšce ±0,000
	const double uA = CCVector3d( si.A.x, si.A.y, 0 ).dot( f.U );
	const double uB = CCVector3d( si.B.x, si.B.y, 0 ).dot( f.U );
	const CCVector2d left = ( uA <= uB ) ? si.A : si.B;
	const double zeroLocal = c->toLocal3d( CCVector3d( 0, 0, m_zero->value() ) ).z;
	f.O = CCVector3d( left.x, left.y, zeroLocal );
	// čáry před rovinou (k pozorovateli), aby je body nezakryly; skutečná poloha = rovina s posunem
	f.back = 20.0 * sc;
	f.front = f.back - si.offset * sc;
	// změřené body: řez ±1 m od roviny, pohled za rovinou do hloubky (+0,5 m před ní)
	const double s0 = -si.offset * sc;
	if ( si.kind == 1 )
	{
		f.ptMin = s0 - si.depth * sc;
		f.ptMax = s0 + 0.5 * sc;
	}
	else
	{
		f.ptMin = s0 - 1.0 * sc;
		f.ptMax = s0 + 1.0 * sc;
	}
	return f;
}

void RezDock::refreshViewCombo()
{
	const QSignalBlocker b( m_viewCombo );
	m_viewCombo->clear();
	const std::vector<FloorInfo> floors = loadFloors();
	for ( const FloorInfo& f : floors )
	{
		m_viewCombo->addItem( Icons::get( QStringLiteral( "newfloor" ) ), floors.size() > 1 ? QStringLiteral( "Půdorys %1" ).arg( f.name ) : QStringLiteral( "Půdorys" ), f.id );
	}
	for ( const SectionInfo& si : loadSections() )
	{
		m_viewCombo->addItem( Icons::get( si.kind == 1 ? QStringLiteral( "facade" ) : QStringLiteral( "section" ) ), si.name, si.id );
	}
	const int idx = m_viewCombo->findData( m_activeView );
	m_viewCombo->setCurrentIndex( idx >= 0 ? idx : 0 );
}

void RezDock::sectionCamera( const SectionInfo& si )
{
	ccGLWindowInterface* win = m_app ? m_app->getActiveGLWindow() : nullptr;
	ccGenericPointCloud* c = cloud();
	if ( !win || !c )
	{
		return;
	}
	const DrawFrame f = frameFor( si.id );
	// matice pohledu: řádky = osy kamery (U vpravo, V nahoru, N k pozorovateli)
	ccGLMatrixd view;
	view.toIdentity();
	double* m = view.data();
	m[0] = f.U.x;
	m[4] = f.U.y;
	m[8] = f.U.z;
	m[1] = f.V.x;
	m[5] = f.V.y;
	m[9] = f.V.z;
	m[2] = f.N.x;
	m[6] = f.N.y;
	m[10] = f.N.z;
	win->setPerspectiveState( false, true );
	win->setBaseViewMat( view );

	// přiblížit na oblast řezu
	const ccBBox bb = c->getOwnBB();
	const double len = ( si.B - si.A ).norm();
	const CCVector3d P0 = f.O;
	const CCVector3d P1 = f.O + f.U * len;
	CCVector3 mn( static_cast<PointCoordinateType>( std::min( P0.x, P1.x ) ), static_cast<PointCoordinateType>( std::min( P0.y, P1.y ) ), bb.minCorner().z );
	CCVector3 mx( static_cast<PointCoordinateType>( std::max( P0.x, P1.x ) ), static_cast<PointCoordinateType>( std::max( P0.y, P1.y ) ), bb.maxCorner().z );
	ccBBox box( mn, mx, true );
	win->updateConstellationCenterAndZoom( &box );
	win->redraw();
}

void RezDock::activateView( int viewId )
{
	SectionInfo si;
	FloorInfo fi;
	if ( isPlanView( viewId ) ? !floorById( viewId, fi ) : !sectionById( viewId, si ) )
	{
		viewId = 0;
		floorById( 0, fi );
	}
	storeActiveFloor(); // výška řezu opouštěného patra
	m_activeView = viewId;
	if ( isPlanView( viewId ) )
	{
		m_lastPlan = viewId;
		const QSignalBlocker b1( m_height );
		const QSignalBlocker b2( m_floorLevel );
		m_height->setValue( fi.cut );
		m_floorLevel->setValue( fi.level );
	}
	m_draw->setFrame( frameFor( viewId ) );
	refreshViewCombo();
	refreshRefCombo();
	applyClip();
	if ( isPlanView( viewId ) )
	{
		topView();
	}
	else
	{
		sectionCamera( si );
	}
}

void RezDock::editSymbology()
{
	Drawing::SymbologyTable table = Drawing::SymbologyTable::load();

	QDialog dlg( this );
	dlg.setWindowTitle( QStringLiteral( "Hladiny a barvy pro výstup V7" ) );
	auto* l = new QVBoxLayout( &dlg );
	auto* tw = new QTableWidget( Drawing::CategoryCount * 2, 5 );
	tw->setHorizontalHeaderLabels( { QStringLiteral( "Prvek" ), QStringLiteral( "Hladina" ), QStringLiteral( "Barva" ), QStringLiteral( "Tloušťka" ), QStringLiteral( "Styl čáry (id)" ) } );
	tw->verticalHeader()->setVisible( false );

	for ( int c = 0; c < Drawing::CategoryCount; ++c )
	{
		for ( int h = 0; h < 2; ++h )
		{
			const int row = c * 2 + h;
			const DgnV7::Symbology& y = table.get( c, h == 1 );
			auto* name = new QTableWidgetItem( Drawing::categoryName( c ) + ( h ? QStringLiteral( " – zakrytá" ) : QStringLiteral( " – plná" ) ) );
			name->setFlags( Qt::ItemIsEnabled );
			tw->setItem( row, 0, name );
			auto spin = [&]( int col, int min, int max, int v )
			{
				auto* sb = new QSpinBox;
				sb->setRange( min, max );
				sb->setValue( v );
				tw->setCellWidget( row, col, sb );
			};
			spin( 1, 1, 63, y.level );
			spin( 2, 0, 255, y.color );
			spin( 3, 0, 31, y.weight );
			spin( 4, -1000, 1000, y.hasLineStyle ? y.lineStyleId : 0 );
		}
	}
	tw->resizeColumnsToContents();
	tw->setMinimumWidth( 520 );
	tw->setMinimumHeight( 300 );
	l->addWidget( tw );
	l->addWidget( new QLabel( QStringLiteral( "<small>Styl čáry: -1 plná, -2 přerušovaná, -3 zakrytá (podle vašeho seedu); 0 = bez vlastního stylu.</small>" ) ) );
	auto* bb = new QDialogButtonBox( QDialogButtonBox::Ok | QDialogButtonBox::Cancel | QDialogButtonBox::RestoreDefaults );
	l->addWidget( bb );
	connect( bb, &QDialogButtonBox::accepted, &dlg, &QDialog::accept );
	connect( bb, &QDialogButtonBox::rejected, &dlg, &QDialog::reject );
	connect( bb->button( QDialogButtonBox::RestoreDefaults ), &QPushButton::clicked, &dlg, [&]()
	{
		const Drawing::SymbologyTable d = Drawing::SymbologyTable::defaults();
		for ( int c = 0; c < Drawing::CategoryCount; ++c )
		{
			for ( int h = 0; h < 2; ++h )
			{
				const int row = c * 2 + h;
				const DgnV7::Symbology& y = d.get( c, h == 1 );
				static_cast<QSpinBox*>( tw->cellWidget( row, 1 ) )->setValue( y.level );
				static_cast<QSpinBox*>( tw->cellWidget( row, 2 ) )->setValue( y.color );
				static_cast<QSpinBox*>( tw->cellWidget( row, 3 ) )->setValue( y.weight );
				static_cast<QSpinBox*>( tw->cellWidget( row, 4 ) )->setValue( y.lineStyleId );
			}
		}
	} );

	if ( dlg.exec() != QDialog::Accepted )
	{
		return;
	}
	for ( int c = 0; c < Drawing::CategoryCount; ++c )
	{
		for ( int h = 0; h < 2; ++h )
		{
			const int row = c * 2 + h;
			DgnV7::Symbology& y = table.get( c, h == 1 );
			y.level = static_cast<QSpinBox*>( tw->cellWidget( row, 1 ) )->value();
			y.color = static_cast<QSpinBox*>( tw->cellWidget( row, 2 ) )->value();
			y.weight = static_cast<QSpinBox*>( tw->cellWidget( row, 3 ) )->value();
			const int ls = static_cast<QSpinBox*>( tw->cellWidget( row, 4 ) )->value();
			y.hasLineStyle = ( ls != 0 );
			y.lineStyleId = ls;
		}
	}
	table.save();
}
