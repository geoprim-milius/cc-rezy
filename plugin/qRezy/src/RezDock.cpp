#include "RezDock.h"
#include "DgnV7.h"
#include "DrawTool.h"
#include "Drawing.h"
#include "Icons.h"
#include "RezLine.h"
#include "View3D.h"

#include "ccGLUtils.h"
#include "ccGLWindowInterface.h"
#include "ccMainAppInterface.h"

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

	auto* zeroRow = new QWidget;
	auto* zl = new QHBoxLayout( zeroRow );
	zl->setContentsMargins( 0, 0, 0, 0 );
	m_zero = makeSpin( -1.0e6, 1.0e6, 0.0, 0.01, 3, QStringLiteral( " m" ) );
	auto* floorBtn = iconButton( QStringLiteral( "floor" ), QStringLiteral( "±0,000 = podlaha (odhad nejhustší vodorovné plochy)" ) );
	zl->addWidget( m_zero, 1 );
	zl->addWidget( floorBtn );
	form->addRow( QStringLiteral( "±0,000 =" ), zeroRow );
	lay->addLayout( form );

	m_status = new QLabel;
	m_status->setWordWrap( true );
	m_status->setStyleSheet( QStringLiteral( "color: #333; background: #f3f3f3; padding: 4px; border-radius: 3px;" ) );
	m_status->setMinimumHeight( 48 );
	m_status->setAlignment( Qt::AlignTop | Qt::AlignLeft );
	lay->addWidget( m_status );

	auto* help = new QLabel( QStringLiteral(
		"<small>Levé = bod · pravé = reset · <b>Shift + tažení</b> nebo tažení kolečkem = posun · kolečko = zoom · "
		"<b>prostřední klik = 3D náhled</b> · Ctrl+kolečko = tloušťka · Esc = zrušit<br>"
		"Klávesy jako MicroStation: Q čára · 3 manipulace · 7 úpravy · 8 mazat · D měřit. "
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
	connect( m_zero, qOverload<double>( &QDoubleSpinBox::valueChanged ), this, [this]()
	{
		updateLabels();
		if ( m_activeView != 0 )
		{
			m_draw->setFrame( frameFor( m_activeView ) );
		}
	} );
	syncThicknessSlider();
}

void RezDock::buildToolbar()
{
	m_toolbar = new QToolBar( QStringLiteral( "Řezy – kreslení" ), parentWidget() );
	m_toolbar->setObjectName( QStringLiteral( "qRezyToolbar" ) );
	m_toolbar->setIconSize( QSize( 24, 24 ) );

	m_drawAct = m_toolbar->addAction( Icons::get( QStringLiteral( "draw" ) ), QStringLiteral( "Kreslit (zapnout/vypnout)" ) );
	m_drawAct->setCheckable( true );
	connect( m_drawAct, &QAction::toggled, this, &RezDock::setDrawing );
	m_toolbar->addSeparator();

	// nástroje
	m_toolGroup = new QActionGroup( this );
	m_toolGroup->setExclusive( true );
	struct ToolDef
	{
		DrawTool::Mode mode;
		const char* icon;
		const char* tip;
	};
	const ToolDef tools[] = {
		{ DrawTool::Mode::Line, "line", "Čára (Q) – body, pravé kliknutí ukončí" },
		{ DrawTool::Mode::Move, "move", "Přesun (3 → 2) – čára, výchozí bod, cíl nebo vzdálenost" },
		{ DrawTool::Mode::Copy, "copy", "Kopie (3 → 1) – čára, výchozí bod, cíle nebo vzdálenost" },
		{ DrawTool::Mode::Offset, "offset", "Rovnoběžně (3 → 9) – čára, strana; vzdálenost z pole vedle nebo napsat + Enter" },
		{ DrawTool::Mode::Break, "break", "Rozdělit (7 → 2) – 2 body na čáře, část mezi nimi se smaže; stejný bod 2× = jen rozdělit" },
		{ DrawTool::Mode::Extend, "extend", "Prodloužit / zkrátit (7 → 3) – klik u konce čáry, nový konec nebo celková délka" },
		{ DrawTool::Mode::Corner, "corner", "Roh (7 → 4) – dvě čáry do průsečíku; klikejte na části, které zůstanou" },
		{ DrawTool::Mode::Trim, "trim", "Oříznout k prvku (7 → 5) – hraniční čára, pak čáry k oříznutí/prodloužení" },
		{ DrawTool::Mode::Delete, "delete", "Mazat (8 nebo Del) – klik na čáru" },
		{ DrawTool::Mode::Measure, "measure", "Měřit vzdálenost (D) – dva body" },
		{ DrawTool::Mode::Text, "text", "Vložit text – klik a napsat (např. naměřenou výšku); přesun, kopie a mazání jako u čar" },
	};
	for ( const ToolDef& t : tools )
	{
		QAction* a = m_toolbar->addAction( Icons::get( QString::fromUtf8( t.icon ) ), QString::fromUtf8( t.tip ) );
		a->setCheckable( true );
		a->setData( static_cast<int>( t.mode ) );
		m_toolGroup->addAction( a );
		if ( t.mode == DrawTool::Mode::Offset )
		{
			m_offsetSpin = makeSpin( 0.0, 1000.0, 0.0, 0.05, 3, QStringLiteral( " m" ) );
			m_offsetSpin->setSpecialValueText( QStringLiteral( "myší" ) );
			m_offsetSpin->setToolTip( QStringLiteral( "Vzdálenost rovnoběžky (0 = určit myší)" ) );
			m_offsetSpin->setMaximumWidth( 90 );
			m_toolbar->addWidget( m_offsetSpin );
			connect( m_offsetSpin, qOverload<double>( &QDoubleSpinBox::valueChanged ), this, [this]( double v ) { m_draw->setOffsetDistance( v ); } );
		}
	}
	m_toolGroup->actions().first()->setChecked( true );
	connect( m_toolGroup, &QActionGroup::triggered, this, [this]( QAction* a )
	{
		if ( !m_draw->isActive() )
		{
			m_drawAct->setChecked( true );
		}
		m_draw->setMode( static_cast<DrawTool::Mode>( a->data().toInt() ) );
		focusView();
	} );

	QAction* undo = m_toolbar->addAction( Icons::get( QStringLiteral( "undo" ) ), QStringLiteral( "Zpět (Ctrl+Z)" ) );
	connect( undo, &QAction::triggered, this, [this]() { m_draw->undo(); focusView(); } );
	m_toolbar->addSeparator();

	// přepínače
	m_orthoAct = m_toolbar->addAction( Icons::get( QStringLiteral( "ortho" ) ), QStringLiteral( "Ortogonálně podle pohledu (F8)" ) );
	m_orthoAct->setCheckable( true );
	connect( m_orthoAct, &QAction::toggled, this, [this]( bool b ) { m_draw->setOrtho( b ); focusView(); } );
	m_trackAct = m_toolbar->addAction( Icons::get( QStringLiteral( "tracking" ) ), QStringLiteral( "Trasování (F11) – podržte kurzor na konci/středu čáry, pak se ukážou pomocné čáry" ) );
	m_trackAct->setCheckable( true );
	m_trackAct->setChecked( true );
	connect( m_trackAct, &QAction::toggled, this, [this]( bool b ) { m_draw->setTracking( b ); focusView(); } );
	m_hiddenAct = m_toolbar->addAction( Icons::get( QStringLiteral( "hidden" ) ), QStringLiteral( "Zakrytá čára / nad řezem (F5)" ) );
	m_hiddenAct->setCheckable( true );
	connect( m_hiddenAct, &QAction::toggled, this, [this]( bool b ) { m_draw->setHidden( b ); focusView(); } );

	m_catCombo = new QComboBox;
	m_catCombo->setToolTip( QStringLiteral( "Hladina (F1–F4)" ) );
	for ( int c = 0; c < Drawing::CategoryCount; ++c )
	{
		m_catCombo->addItem( Icons::colorSwatch( Drawing::displayColor( c, false ), false ), QStringLiteral( "F%1 %2" ).arg( c + 1 ).arg( Drawing::categoryName( c ) ) );
	}
	m_toolbar->addWidget( m_catCombo );
	connect( m_catCombo, qOverload<int>( &QComboBox::currentIndexChanged ), this, [this]( int i ) { m_draw->setCategory( i ); focusView(); } );
	m_toolbar->addSeparator();

	// pohled
	QAction* rot = m_toolbar->addAction( Icons::get( QStringLiteral( "rotate" ) ), QStringLiteral( "Natočit pohled – 2 body (směr 1. → 2. bod bude vodorovně doprava)" ) );
	connect( rot, &QAction::triggered, this, [this]()
	{
		if ( m_activeView != 0 )
		{
			activateView( 0 );
		}
		if ( !m_draw->isActive() )
		{
			m_drawAct->setChecked( true );
		}
		m_draw->setMode( DrawTool::Mode::RotateView );
		focusView();
	} );
	QAction* north = m_toolbar->addAction( Icons::get( QStringLiteral( "north" ) ), QStringLiteral( "Sever nahoru (zrušit natočení)" ) );
	connect( north, &QAction::triggered, this, [this]()
	{
		m_viewAngle = 0.0;
		if ( m_activeView == 0 )
		{
			topView();
		}
		focusView();
	} );

	QAction* sec = m_toolbar->addAction( Icons::get( QStringLiteral( "section" ) ), QStringLiteral( "Nový svislý řez (S) – 2 body v půdorysu, pak strana pohledu" ) );
	QAction* fac = m_toolbar->addAction( Icons::get( QStringLiteral( "facade" ) ), QStringLiteral( "Nový pohled (fasáda) – rovina před objektem: 2 body, pak strana pohledu" ) );
	auto startDef = [this]( int kind )
	{
		if ( m_activeView != 0 )
		{
			activateView( 0 );
		}
		if ( !m_draw->isActive() )
		{
			m_drawAct->setChecked( true );
		}
		m_draw->startSectionDef( kind );
		focusView();
	};
	connect( sec, &QAction::triggered, this, [startDef]() { startDef( 0 ); } );
	connect( fac, &QAction::triggered, this, [startDef]() { startDef( 1 ); } );
	QAction* aux = m_toolbar->addAction( Icons::get( QStringLiteral( "auxsec" ) ), QStringLiteral( "Pomocný řez – 2 body v půdorysu a strana; otevře okno pro měření výšek (nic se neukládá)" ) );
	connect( aux, &QAction::triggered, this, [startDef]() { startDef( 2 ); } );
	m_toolbar->addSeparator();

	// změřené body
	auto* ptBtn = new QToolButton;
	ptBtn->setIcon( Icons::get( QStringLiteral( "points" ) ) );
	ptBtn->setToolTip( QStringLiteral( "Změřené body (S-JTSK): načíst, zobrazit, kódy" ) );
	ptBtn->setPopupMode( QToolButton::InstantPopup );
	auto* ptMenu = new QMenu( ptBtn );
	QAction* ptLoad = ptMenu->addAction( QStringLiteral( "Načíst body… (číslo Y X [Z] [kód])" ) );
	QAction* ptShow = ptMenu->addAction( QStringLiteral( "Zobrazit body (a uchopovat na ně)" ) );
	ptShow->setCheckable( true );
	ptShow->setChecked( true );
	QAction* ptCodes = ptMenu->addAction( QStringLiteral( "Zobrazit kódy" ) );
	ptCodes->setCheckable( true );
	ptCodes->setChecked( true );
	ptBtn->setMenu( ptMenu );
	m_toolbar->addWidget( ptBtn );
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
	m_toolbar->addSeparator();

	QAction* v3 = m_toolbar->addAction( Icons::get( QStringLiteral( "3d" ) ), QStringLiteral( "3D náhled (prostřední klik do řezu ukáže místo)" ) );
	connect( v3, &QAction::triggered, this, [this]()
	{
		// 3D náhled kolem středu okna (nebo středu mračna)
		CCVector3d P;
		if ( m_draw->isActive() && m_draw->viewCenter( P ) )
		{
			show3DAt( P.x, P.y, m_activeView == 0 ? std::numeric_limits<double>::quiet_NaN() : P.z );
		}
		else if ( ccGenericPointCloud* c = cloud() )
		{
			const CCVector3 C = c->getOwnBB().getCenter();
			show3DAt( C.x, C.y, std::numeric_limits<double>::quiet_NaN() );
		}
	} );
	QAction* save = m_toolbar->addAction( Icons::get( QStringLiteral( "save" ) ), QStringLiteral( "Uložit výkres V7 a nastavení výstupu" ) );
	connect( save, &QAction::triggered, this, &RezDock::openSaveDialog );
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

	m_activeView = 0;
	refreshViewCombo();
	activateView( 0 );

	if ( m_draw->isActive() )
	{
		m_draw->start( c );
		m_draw->setFrame( frameFor( 0 ) );
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
	if ( m_activeView == 0 )
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
	if ( m_activeView == 0 )
	{
		if ( std::abs( m_height->value() - ( v + m_zero->value() ) ) > 0.004 )
		{
			m_height->setValue( v + m_zero->value() );
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
		lo = c->toGlobal3d( CCVector3d( 0, 0, mn.z ) ).z - m_zero->value();
		hi = c->toGlobal3d( CCVector3d( 0, 0, mx.z ) ).z - m_zero->value();
		cur = m_height->value() - m_zero->value();
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
		m_relLabel->setText( fmt( m_height->value() - m_zero->value() ) );
		m_upBtn->setToolTip( QStringLiteral( "Nahoru (PgUp)" ) );
		m_downBtn->setToolTip( QStringLiteral( "Dolů (PgDn)" ) );
	}
	m_height->setEnabled( !sec );
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

bool RezDock::ensure3D()
{
	ccGenericPointCloud* g = cloud();
	ccPointCloud* pc = g ? ccHObjectCaster::ToPointCloud( g ) : nullptr;
	if ( !pc )
	{
		m_app->dispToConsole( QStringLiteral( "[Řezy] Nejdřív vyberte mračno." ), ccMainAppInterface::WRN_CONSOLE_MESSAGE );
		return false;
	}
	if ( !m_view3D )
	{
		m_view3D = new View3D( m_app, m_app->getMainWindow() );
		m_app->getMainWindow()->addDockWidget( Qt::RightDockWidgetArea, m_view3D );
		m_view3D->setFloating( true );
		m_view3D->resize( 760, 600 );
	}
	m_view3D->show();
	m_view3D->raise();
	QApplication::setOverrideCursor( Qt::WaitCursor );
	const bool ok = m_view3D->setSource( pc, 20000000 );
	QApplication::restoreOverrideCursor();
	if ( ok )
	{
		refresh3DLines();
	}
	return ok;
}

void RezDock::refresh3DLines()
{
	if ( !m_view3D || !m_view3D->isVisible() )
	{
		return;
	}
	std::vector<View3D::Line> lines;
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
			// skutečná poloha čáry (bez posunu k pozorovateli)
			const CCVector3d f( ch->getMetaData( "qRezy.fx" ).toDouble(), ch->getMetaData( "qRezy.fy" ).toDouble(), ch->getMetaData( "qRezy.fz" ).toDouble() );
			const int cat = ch->getMetaData( Drawing::MetaCategory ).toInt();
			const bool hid = ch->getMetaData( Drawing::MetaHidden ).toBool();
			View3D::Line l;
			const QColor qc = Drawing::displayColor( cat, hid );
			l.color = ccColor::Rgb( static_cast<ColorCompType>( qc.red() ), static_cast<ColorCompType>( qc.green() ), static_cast<ColorCompType>( qc.blue() ) );
			l.dashed = hid;
			for ( unsigned k = 0; k < poly->size(); ++k )
			{
				l.pts.push_back( CCVector3d::fromArray( poly->getPoint( k )->u ) - f );
			}
			lines.push_back( l );
		}
	}
	m_view3D->setLines( lines );
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
		exportV7();
	}
}

void RezDock::exportV7()
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
	const double zeroG = m_zero->value();
	const std::vector<SectionInfo> sections = loadSections();
	std::vector<int> usedViews;

	auto map = [&]( int view, const CCVector3d& P, DgnV7::Point2& out ) -> bool
	{
		if ( view == 0 )
		{
			out.x = P.x / sc - shift.x;
			out.y = P.y / sc - shift.y;
			return true;
		}
		for ( const SectionInfo& si : sections )
		{
			if ( si.id == view )
			{
				const DrawFrame f = frameFor( view );
				const CCVector2d uv = f.toUV( P );
				out.x = si.insX + uv.x / sc;
				out.y = absH ? ( uv.y / sc + zeroG ) : ( si.insY + uv.y / sc );
				if ( std::find( usedViews.begin(), usedViews.end(), view ) == usedViews.end() )
				{
					usedViews.push_back( view );
				}
				return true;
			}
		}
		return false; // řez byl smazán
	};
	std::vector<DgnV7::Element> elements = m_draw->exportElements( table, map, st.value( QStringLiteral( "qRezy/userTextLevel" ), 15 ).toInt(),
	                                                               st.value( QStringLiteral( "qRezy/userTextHeight" ), 0.15 ).toDouble() );
	if ( elements.empty() )
	{
		QMessageBox::information( this, QStringLiteral( "Řezy" ), QStringLiteral( "Zatím není nic nakreslené (u vybraného mračna)." ) );
		return;
	}

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
			DgnV7::Element t;
			t.isText = true;
			t.sym.level = 14;
			t.sym.color = 0;
			t.textHeight = textH;
			t.pts.push_back( { si.insX, y0 - ( i + 2 ) * textH * 1.6 } );
			const QByteArray bytes = codec ? codec->fromUnicode( texts[i] ) : texts[i].toLatin1();
			t.text = std::string( bytes.constData(), static_cast<size_t>( bytes.size() ) );
			elements.push_back( t );
		}
	}

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
	QString out = QFileDialog::getSaveFileName( this, QStringLiteral( "Uložit NOVÝ výkres V7 jako…" ), lastOut, QStringLiteral( "MicroStation V7 (*.dgn)" ) );
	if ( out.isEmpty() )
	{
		return;
	}
	if ( !out.endsWith( QStringLiteral( ".dgn" ), Qt::CaseInsensitive ) )
	{
		out += QStringLiteral( ".dgn" );
	}
	if ( QFileInfo( out ).absoluteFilePath() == QFileInfo( seed ).absoluteFilePath() )
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
	QFile f( out );
	if ( !f.open( QIODevice::WriteOnly | QIODevice::Truncate ) || f.write( reinterpret_cast<const char*>( bytes.data() ), static_cast<qint64>( bytes.size() ) ) != static_cast<qint64>( bytes.size() ) )
	{
		QMessageBox::warning( this, QStringLiteral( "Řezy" ), QStringLiteral( "Soubor nelze zapsat: %1" ).arg( out ) );
		return;
	}
	st.setValue( QStringLiteral( "qRezy/lastOut" ), out );
	m_app->dispToConsole( QStringLiteral( "[Řezy] Uloženo %1 prvků do %2" ).arg( elements.size() ).arg( out ) );
	QMessageBox::information( this, QStringLiteral( "Řezy" ), QStringLiteral( "Uloženo %1 prvků do\n%2" ).arg( elements.size() ).arg( out ) );
}

// ================================================================ svislé řezy a pohledy

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
		si.A = CCVector2d( ch->getMetaData( "qRezy.secAx" ).toDouble(), ch->getMetaData( "qRezy.secAy" ).toDouble() );
		si.B = CCVector2d( ch->getMetaData( "qRezy.secBx" ).toDouble(), ch->getMetaData( "qRezy.secBy" ).toDouble() );
		si.N = CCVector2d( ch->getMetaData( "qRezy.secNx" ).toDouble(), ch->getMetaData( "qRezy.secNy" ).toDouble() );
		si.offset = ch->getMetaData( "qRezy.secOffset" ).toDouble();
		si.depth = ch->hasMetaData( "qRezy.secDepth" ) ? ch->getMetaData( "qRezy.secDepth" ).toDouble() : 15.0;
		si.insX = ch->getMetaData( "qRezy.secInsX" ).toDouble();
		si.insY = ch->getMetaData( "qRezy.secInsY" ).toDouble();
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
		if ( ensure3D() )
		{
			m_view3D->auxSection( CCVector3d( ax, ay, 0 ), CCVector3d( bx, by, 0 ), CCVector3d( N.x, N.y, 0 ), planZLocal(), m_zero->value(), m_thickness->value() );
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
	// výchozí umístění ve výkresu: pod mračnem, pod sebou
	const CCVector3d gmin = c->toGlobal3d( bb.minCorner() );
	const CCVector3d gmax = c->toGlobal3d( bb.maxCorner() );
	const double zRange = std::min( 30.0, gmax.z - gmin.z );
	si.insX = gmin.x;
	si.insY = gmin.y - 5.0 - static_cast<double>( existing.size() ) * ( zRange + 5.0 );
	saveSection( si );

	refreshViewCombo();
	activateView( id );
}

void RezDock::deleteActiveSection()
{
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
	if ( viewId == 0 || !c || !sectionById( viewId, si ) )
	{
		// půdorys ve výšce řezu
		f.O = CCVector3d( 0, 0, planZLocal() );
		return f;
	}
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
	m_viewCombo->addItem( QStringLiteral( "Půdorys" ), 0 );
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
	if ( viewId != 0 && !sectionById( viewId, si ) )
	{
		viewId = 0;
	}
	m_activeView = viewId;
	m_draw->setFrame( frameFor( viewId ) );
	refreshViewCombo();
	applyClip();
	if ( viewId == 0 )
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
