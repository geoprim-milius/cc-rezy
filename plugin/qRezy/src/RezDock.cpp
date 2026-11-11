#include "RezDock.h"
#include "Extract.h"
#include "DgnV7.h"
#include "DrawTool.h"
#include "Drawing.h"
#include "Icons.h"
#include "RezLine.h"
#include "Ribbon.h"
#include "Theme.h"
#include "Tiff.h"
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
#include <QRegularExpression>
#include <QScrollArea>
#include <QScrollBar>
#include <QSettings>
#include <QShortcut>
#include <QSlider>
#include <QSpinBox>
#include <QTableWidget>
#include <QTextCodec>
#include <QTimer>
#include <QToolBar>
#include <QToolButton>
#include <QMenu>
#include <QVBoxLayout>
#include <QLineEdit>
#include <map>

#include <algorithm>
#include <functional>
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
	connect( m_draw, &DrawTool::categoryChanged, this, [this]( int c ) { const QSignalBlocker b( m_catCombo ); m_catCombo->setCurrentIndex( std::max( 0, m_catCombo->findData( c ) ) ); } );
	m_draw->setSliceProvider( [this]() { return slicePlanes(); } );
	m_draw->setZeroProvider( [this]() { return m_zero ? m_zero->value() : 0.0; } );
	{
		QSettings ks;
		m_draw->setKotaSymbology( ks.value( QStringLiteral( "qRezy/kotaLevel" ), 16 ).toInt(), ks.value( QStringLiteral( "qRezy/kotaColor" ), 135 ).toInt() );
	}
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
	connect( m_draw, &DrawTool::show3DRequested, this, [this]( double x, double y, double z ) { m_draw->clearPin(); show3DAt( x, y, z, true ); } );
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
	scroll->setHorizontalScrollBarPolicy( Qt::ScrollBarAsNeeded );
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

void RezDock::refreshCatCombo()
{
	const int cur = m_draw ? m_draw->category() : 0;
	const QSignalBlocker b( m_catCombo );
	m_catCombo->clear();
	const std::vector<Drawing::Layer>& ls = Drawing::layers();
	for ( size_t i = 0; i < ls.size(); ++i )
	{
		const QString key = i < 4 ? QStringLiteral( "F%1  " ).arg( i + 1 ) : QStringLiteral( "      " );
		m_catCombo->addItem( Icons::colorSwatch( ls[i].color, false ), key + ls[i].name, ls[i].id );
	}
	m_catCombo->setCurrentIndex( std::max( 0, m_catCombo->findData( cur ) ) );
}

std::vector<std::array<double, 4>> RezDock::slicePlanes() const
{
	// stejné roviny jako applyClip (i když je ořez mračna vypnutý)
	std::vector<std::array<double, 4>> out;
	ccGenericPointCloud* c = cloud();
	if ( !c )
	{
		return out;
	}
	const double sc = c->getGlobalScale();
	SectionInfo si;
	if ( m_activeView != 0 && sectionById( m_activeView, si ) )
	{
		const DrawFrame f = frameFor( m_activeView );
		const double d = f.O.dot( f.N ) - si.offset * sc;
		if ( si.kind == 1 )
		{
			// pohled: jen pás kolem líce fasády (jinak by se míchaly hloubky)
			double hiN = d;
			double loN = d - std::min( si.depth, 3.0 ) * sc;
			if ( si.face >= 0.0 )
			{
				const double faceN = f.O.dot( f.N ) - si.face * sc;
				hiN = faceN + 0.3 * sc;
				loN = faceN - 0.3 * sc;
			}
			out.push_back( { -f.N.x, -f.N.y, -f.N.z, hiN } );
			out.push_back( { f.N.x, f.N.y, f.N.z, -loN } );
		}
		else
		{
			const double half = m_thickness->value() * sc / 2.0;
			out.push_back( { f.N.x, f.N.y, f.N.z, -( d - half ) } );
			out.push_back( { -f.N.x, -f.N.y, -f.N.z, d + half } );
		}
		return out;
	}
	const double half = m_thickness->value() / 2.0;
	const double zLo = c->toLocal3d( CCVector3d( 0, 0, m_height->value() - half ) ).z;
	const double zHi = c->toLocal3d( CCVector3d( 0, 0, m_height->value() + half ) ).z;
	out.push_back( { 0.0, 0.0, 1.0, -zLo } );
	out.push_back( { 0.0, 0.0, -1.0, zHi } );
	return out;
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
	QTimer::singleShot( 0, this, [this]() { fitPanelWidth(); } );
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
		{ DrawTool::Mode::Line, "line", "Čára", "Body levým tlačítkem, pravé ukončí. Délka: čísla + Enter. Enter bez čísla = další úsek kolmo na předchozí." },
		{ DrawTool::Mode::Arc, "arc", "Oblouk", "Tři body podle metody vedle: začátek–střed oblouku–konec, začátek–konec–střed oblouku, nebo se středem kružnice. Do DGN jako skutečný oblouk." },
		{ DrawTool::Mode::CloudLine, "cloudline", "Čára\nz mračna", "Klik k bodům řezu (na líc zdi) → čára proložená body; klik na ni nebo Enter = potvrdit, klik jinam = potvrdit a další, pravé = zahodit. Navazující čára se napojí v rohu." },
		{ DrawTool::Mode::Move, "move", "Přesun", "Čára (nebo výběr), výchozí bod, cíl nebo vzdálenost." },
		{ DrawTool::Mode::Rotate, "rotel", "Otočit", "Prvek (nebo výběr), pak 3 body: střed otáčení, výchozí směr, cílový směr. Otočí čáry, oblouky i texty (3·4)." },
		{ DrawTool::Mode::Copy, "copy", "Kopie", "Čára (nebo výběr), výchozí bod, cíle nebo vzdálenost." },
		{ DrawTool::Mode::Offset, "offset", "Rovnoběžka", "Čára, strana; vzdálenost z pole vedle nebo napsat + Enter." },
		{ DrawTool::Mode::Modify, "modify", "Upravit prvek", "Klik u konce čáry, pak nové místo konce." },
		{ DrawTool::Mode::Break, "break", "Rozdělit", "2 body na čáře, část mezi nimi se smaže; stejný bod 2× = jen rozdělit." },
		{ DrawTool::Mode::BreakLine, "breakline", "Rozdělit čarou", "Řezná čára 2 body → všechny čáry, které protne, se v průsečíku rozdělí (nic se nemaže). Průsečíky se ukazují červeně." },
		{ DrawTool::Mode::Pin, "pin", "Špendlík", "Klik na místo ve výkresu → 3D náhled se na něj posune a přiblíží (natočení a nastavení zůstane) a ukáže špendlík. Totéž kdykoli prostředním kliknutím." },
		{ DrawTool::Mode::Extend, "extend", "Prodloužit", "Klik u konce čáry, nový konec nebo celková délka." },
		{ DrawTool::Mode::Corner, "corner", "Roh", "Dvě čáry do průsečíku; klikejte na části, které zůstanou." },
		{ DrawTool::Mode::Trim, "trim", "Oříznout", "Hraniční čára, pak čáry k oříznutí / prodloužení." },
		{ DrawTool::Mode::InsertVertex, "insvertex", "Vložit vrchol", "Klik na čáru, pak umístit nový vrchol." },
		{ DrawTool::Mode::DeleteVertex, "delvertex", "Smazat vrchol", "Klik na spoj dvou čar → jedna čára." },
		{ DrawTool::Mode::Delete, "delete", "Smazat", "Klik na čáru (s výběrem smaže výběr)." },
		{ DrawTool::Mode::Measure, "measure", "Měřit", "Vzdálenost mezi dvěma body." },
		{ DrawTool::Mode::Text, "text", "Text", "Klik a napsat (např. naměřenou výšku); přesun, kopie a mazání jako u čar." },
		{ DrawTool::Mode::EditText, "text", "Upravit text", "Klik na text → změnit jeho obsah (A·B)." },
		{ DrawTool::Mode::Kota, "kota", "Kóta", "Výšková kóta (kačena) v řezu nebo pohledu: klik na bod = hrot šipky, druhý klik = strana poličky. Výška v Bpv, místní (k ±0,000) nebo obojí; velikost podle měřítka výkresu. Přesun, kopie (výška se přepočítá) a mazání jako u čar." },
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

	auto* arcCombo = new QComboBox;
	arcCombo->addItem( QStringLiteral( "začátek–střed–konec" ) );
	arcCombo->addItem( QStringLiteral( "začátek–konec–střed" ) );
	arcCombo->addItem( QStringLiteral( "střed kružnice" ) );
	arcCombo->setItemData( 0, QStringLiteral( "Začátek, bod uprostřed oblouku, konec – oblouk prochází všemi třemi body" ), Qt::ToolTipRole );
	arcCombo->setItemData( 1, QStringLiteral( "Začátek, konec, pak bod uprostřed oblouku" ), Qt::ToolTipRole );
	arcCombo->setItemData( 2, QStringLiteral( "Začátek, střed kružnice, konec (směr podle pohybu myši) – jako Place Arc Center v MicroStationu" ), Qt::ToolTipRole );
	arcCombo->setToolTip( QStringLiteral( "Metoda oblouku" ) );
	arcCombo->setCurrentIndex( m_draw->arcMethod() );
	connect( arcCombo, qOverload<int>( &QComboBox::currentIndexChanged ), this, [this]( int i ) { m_draw->setArcMethod( i ); if ( m_draw->isActive() ) m_draw->setMode( DrawTool::Mode::Arc ); focusView(); } );
	auto* kotaFmt = new QComboBox;
	kotaFmt->addItem( QStringLiteral( "Bpv" ) );
	kotaFmt->addItem( QStringLiteral( "místní" ) );
	kotaFmt->addItem( QStringLiteral( "oboje" ) );
	kotaFmt->setItemData( 0, QStringLiteral( "Výška v Bpv (např. 394.410)" ), Qt::ToolTipRole );
	kotaFmt->setItemData( 1, QStringLiteral( "Výška k ±0,000 z panelu Řezy (např. +3.315)" ), Qt::ToolTipRole );
	kotaFmt->setItemData( 2, QStringLiteral( "Místní nad poličkou, Bpv pod ní" ), Qt::ToolTipRole );
	kotaFmt->setToolTip( QStringLiteral( "Zápis výšky u nové kóty" ) );
	kotaFmt->setCurrentIndex( m_draw->kotaFormat() );
	connect( kotaFmt, qOverload<int>( &QComboBox::currentIndexChanged ), this, [this]( int i ) { m_draw->setKotaFormat( i ); if ( m_draw->isActive() ) m_draw->setMode( DrawTool::Mode::Kota ); focusView(); } );
	auto* kotaScale = new QSpinBox;
	kotaScale->setRange( 1, 10000 );
	kotaScale->setValue( m_draw->kotaScale() );
	kotaScale->setToolTip( QStringLiteral( "Měřítko výkresu – velikost značky a textu kóty (1:70 = jako ve vašem výkresu)" ) );
	kotaScale->setFixedWidth( 70 );
	connect( kotaScale, qOverload<int>( &QSpinBox::valueChanged ), this, [this]( int v ) { m_draw->setKotaScale( v ); } );
	QSettings clSt;
	auto* clGap = makeSpin( 0.01, 1.0, clSt.value( QStringLiteral( "qRezy/clGap" ), 0.10 ).toDouble(), 0.01, 2, QStringLiteral( " m" ) );
	clGap->setToolTip( QStringLiteral( "Čára z mračna: největší mezera v bodech, kterou čára přeskočí (dveře, okna = konec čáry)" ) );
	clGap->setFixedWidth( 86 );
	auto* clTol = makeSpin( 0.005, 0.2, clSt.value( QStringLiteral( "qRezy/clTol" ), 0.02 ).toDouble(), 0.005, 3, QStringLiteral( " m" ) );
	clTol->setToolTip( QStringLiteral( "Čára z mračna: tolerance – jak daleko od čáry smí body ležet (rozptyl lící zdi)" ) );
	clTol->setFixedWidth( 86 );
	QAction* clSquare = mkAct( "corner", QStringLiteral( "Pravý úhel" ), QStringLiteral( "Čára z mračna: téměř kolmé napojení (do 3°) srovnat na přesně 90°" ), true );
	clSquare->setChecked( clSt.value( QStringLiteral( "qRezy/clSquare" ), true ).toBool() );
	auto applyCl = [this, clGap, clTol, clSquare]()
	{
		QSettings st;
		st.setValue( QStringLiteral( "qRezy/clGap" ), clGap->value() );
		st.setValue( QStringLiteral( "qRezy/clTol" ), clTol->value() );
		st.setValue( QStringLiteral( "qRezy/clSquare" ), clSquare->isChecked() );
		m_draw->setCloudLineParams( clGap->value(), clTol->value(), clSquare->isChecked() );
	};
	connect( clGap, qOverload<double>( &QDoubleSpinBox::valueChanged ), this, applyCl );
	connect( clTol, qOverload<double>( &QDoubleSpinBox::valueChanged ), this, applyCl );
	connect( clSquare, &QAction::toggled, this, applyCl );

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
	refreshCatCombo();
	connect( m_catCombo, qOverload<int>( &QComboBox::currentIndexChanged ), this, [this]( int i ) { m_draw->setCategory( m_catCombo->itemData( i ).toInt() ); focusView(); } );
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
	QAction* aux = mkAct( "auxsec", QStringLiteral( "Pomocný řez" ), QStringLiteral( "2 body v půdorysu a strana; otevře okno pro měření výšek (nic se neukládá)" ) );
	connect( aux, &QAction::triggered, this, [startDef]() { startDef( 2 ); } );
	connect( m_draw, &DrawTool::auxSectionRequested, this, [startDef]() { startDef( 2 ); } );
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

	QAction* v3 = mkAct( "3d", QStringLiteral( "3D náhled" ), QStringLiteral( "3D náhled kolem středu okna ve výchozím šikmém pohledu (klik kolečkem nebo Špendlík ukáže místo a pohled nechá)" ) );
	connect( v3, &QAction::triggered, this, [this]()
	{
		CCVector3d P;
		if ( m_draw->isActive() && m_draw->viewCenter( P ) )
		{
			show3DAt( P.x, P.y, isPlanView( m_activeView ) ? std::numeric_limits<double>::quiet_NaN() : P.z, false );
		}
		else if ( ccGenericPointCloud* c = cloud() )
		{
			const CCVector3 C = c->getOwnBB().getCenter();
			show3DAt( C.x, C.y, std::numeric_limits<double>::quiet_NaN(), false );
		}
	} );

	// --- patra
	QAction* nf = mkAct( "newfloor", QStringLiteral( "Nové\npatro" ), QStringLiteral( "Samostatný půdorys (předchozí patro se nabídne jako reference)" ) );
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
	QAction* ptHeights = mkAct( "kota", QStringLiteral( "Výšky bodů" ), QStringLiteral( "Zobrazit výšku pod číslem bodu (jen body s výškou; jen na obrazovce)" ), true );
	ptHeights->setChecked( m_draw->showHeights() );
	auto* ptHFmt = new QComboBox;
	ptHFmt->addItem( QStringLiteral( "Bpv" ) );
	ptHFmt->addItem( QStringLiteral( "místní" ) );
	ptHFmt->setToolTip( QStringLiteral( "Výška bodu v Bpv, nebo místní k ±0,000 z panelu Řezy" ) );
	ptHFmt->setCurrentIndex( m_draw->heightFormat() == 1 ? 1 : 0 );
	connect( ptHeights, &QAction::toggled, this, [this, ptHFmt]( bool on ) { m_draw->setShowHeights( on, ptHFmt->currentIndex() ); focusView(); } );
	connect( ptHFmt, qOverload<int>( &QComboBox::currentIndexChanged ), this, [this, ptHeights]( int i ) { m_draw->setShowHeights( ptHeights->isChecked(), i ); focusView(); } );
	QAction* ptColors = mkAct( "theme", QStringLiteral( "Barvy bodů…" ), QStringLiteral( "Barva značky, čísla, kódu a výšky" ) );
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
		                          { "qRezy/ptColCode", "Kód:", QColor( 0, 230, 255 ) },
		                          { "qRezy/ptColHeight", "Výška:", QColor( 150, 255, 150 ) } };
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
			refresh3DLines();
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
			refresh3DLines();
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
	QAction* xfer = mkAct( "layers", QStringLiteral( "Přenést\nvýkres" ), QStringLiteral( "Přenést celý výkres (čáry, texty, kóty, patra, řezy, pohledy, body) na jiné načtené mračno – např. podrobnější. Rozdíl v posunu souřadnic se přepočítá." ) );
	connect( xfer, &QAction::triggered, this, [this]() { transferDrawing(); } );
	QAction* impDgn = mkAct( "open", QStringLiteral( "Načíst V7" ), QStringLiteral( "Načíst výkres MicroStation V7 (rozkreslený v MicroStationu) do aktivního půdorysu nebo řezu a dál ho doplňovat" ) );
	connect( impDgn, &QAction::triggered, this, &RezDock::importDgn );

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
		g.addBig( T( DrawTool::Mode::Arc ), QStringLiteral( "E" ) );
		RibbonColumn c = g.column();
		c.widget( arcCombo );
		c.addSmall( T( DrawTool::Mode::Text ), QStringLiteral( "A·A" ) );
		c.addSmall( T( DrawTool::Mode::Measure ), QStringLiteral( "D" ) );
		RibbonColumn c2 = g.column();
		c2.addSmall( T( DrawTool::Mode::EditText ), QStringLiteral( "A·B" ) );
		c2.addSmall( aux, QStringLiteral( "C" ) );
		RibbonGroup kg = r->addGroup( pDraw, QStringLiteral( "Kóta" ) );
		kg.addBig( T( DrawTool::Mode::Kota ) );
		RibbonColumn kc = kg.column();
		kc.widget( kotaFmt );
		kc.widget( kotaScale, QStringLiteral( "1 :" ) );
		RibbonGroup cg = r->addGroup( pDraw, QStringLiteral( "Z mračna" ) );
		cg.addBig( T( DrawTool::Mode::CloudLine ) );
		RibbonColumn cc = cg.column();
		cc.widget( clGap, QStringLiteral( "mezera" ) );
		cc.widget( clTol, QStringLiteral( "tol." ) );
		cc.addSmall( clSquare );
		RibbonGroup m = r->addGroup( pDraw, QStringLiteral( "Manipulace · 3" ) );
		RibbonColumn mc = m.column();
		mc.addSmall( T( DrawTool::Mode::Copy ), QStringLiteral( "3·1" ) );
		mc.addSmall( T( DrawTool::Mode::Move ), QStringLiteral( "3·2" ) );
		mc.addSmall( T( DrawTool::Mode::Offset ), QStringLiteral( "3·9" ) );
		RibbonColumn mc2 = m.column();
		mc2.addSmall( T( DrawTool::Mode::Rotate ), QStringLiteral( "3·4" ) );
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
		u3.addSmall( T( DrawTool::Mode::Delete ), QStringLiteral( "8" ) );
		u3.addSmall( T( DrawTool::Mode::BreakLine ), QStringLiteral( "7·6" ) );
		RibbonGroup h = r->addGroup( pDraw, QStringLiteral( "Hladina" ) );
		RibbonColumn hc = h.column();
		hc.widget( m_catCombo );
		hc.addSmall( m_hiddenAct, QStringLiteral( "F5" ) );
		hc.addSmall( symb );
		RibbonGroup p = r->addGroup( pDraw, QStringLiteral( "Pomůcky" ) );
		RibbonColumn pc = p.column();
		pc.addSmall( m_orthoAct, QStringLiteral( "F8" ) );
		pc.addSmall( m_trackAct, QStringLiteral( "F11" ) );
		pc.addSmall( T( DrawTool::Mode::Pin ) );
		pc.addSmall( undo, QStringLiteral( "Ctrl+Z" ) );
		QAction* pinVis = mkAct( "pin", QStringLiteral( "Zobrazit špendlík" ), QStringLiteral( "Špendlík ve výkresu (z prostředního kliku do 3D náhledu) zobrazit / skrýt" ), true );
		pinVis->setChecked( true );
		connect( pinVis, &QAction::toggled, this, [this]( bool on ) { m_draw->setPinVisible( on ); focusView(); } );
		RibbonColumn pc2 = p.column();
		pc2.addSmall( pinVis );
		RibbonGroup bg = r->addGroup( pDraw, QStringLiteral( "Změřené body" ) );
		bg.addBig( ptLoad );
		RibbonColumn b1 = bg.column();
		b1.addSmall( ptShow );
		b1.addSmall( ptCodes );
		b1.addSmall( ptColors );
		RibbonColumn b2 = bg.column();
		b2.addSmall( ptHeights );
		b2.widget( ptHFmt );
	}
	const int pSec = r->addPage( QStringLiteral( "Řezy a pohledy" ) );
	{
		RibbonGroup g = r->addGroup( pSec, QStringLiteral( "Nový" ) );
		g.addBig( sec, QStringLiteral( "S" ) );
		g.addBig( fac );
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
		RibbonGroup fg = r->addGroup( pSec, QStringLiteral( "Patro" ) );
		fg.addBig( nf );
		RibbonColumn fc = fg.column();
		fc.addSmall( topA );
		fc.addSmall( floorEst );
		RibbonGroup vg = r->addGroup( pSec, QStringLiteral( "Zobrazení" ) );
		RibbonColumn v1 = vg.column();
		v1.widget( themeCombo );
		auto* dash = new QComboBox;
		dash->addItem( QStringLiteral( "jemné čárky" ), 1 );
		dash->addItem( QStringLiteral( "střední čárky" ), 2 );
		dash->addItem( QStringLiteral( "hrubé čárky" ), 4 );
		dash->setToolTip( QStringLiteral( "Délka čárek a mezer zakrytých čar na obrazovce (do DGN se nepřenáší – tam platí styl čáry z hladiny)" ) );
		dash->setCurrentIndex( std::max( 0, dash->findData( RezLine::dashFactor() ) ) );
		connect( dash, qOverload<int>( &QComboBox::currentIndexChanged ), this, [this, dash]( int i ) {
			RezLine::setDashFactor( dash->itemData( i ).toInt() );
			m_draw->refreshDisplay(); // 3D náhled se překreslí při další změně pohledu
		} );
		v1.widget( dash );
		RibbonColumn v2 = vg.column();
		v2.addSmall( m_bgDarkAct );
		v2.addSmall( m_bgLightAct );
		v2.addSmall( panelA );
		vg.addBig( m_workAct );
	}
	const int pOut = r->addPage( QStringLiteral( "Výstup" ) );
	{
		RibbonGroup g = r->addGroup( pOut, QStringLiteral( "Rozdělaná práce" ) );
		g.addBig( proj );
		g.addBig( openA );
		g.addBig( xfer );
		RibbonGroup d = r->addGroup( pOut, QStringLiteral( "MicroStation V7" ) );
		d.addBig( save );
		d.addBig( impDgn );
		d.column().addSmall( symb );
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
	fitPanelWidth();
}

void RezDock::fitPanelWidth()
{
	// panel nesmí být užší než jeho obsah (jinak se pravá část ořízne) – podle písma a vzhledu
	auto* sa = qobject_cast<QScrollArea*>( widget() );
	if ( !sa || !sa->widget() )
	{
		return;
	}
	sa->widget()->ensurePolished();
	const int need = sa->widget()->minimumSizeHint().width() + sa->verticalScrollBar()->sizeHint().width() + 4;
	sa->setMinimumWidth( need );
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
		// mračno skryté kvůli plátku se musí uložit jako viditelné
		const bool slab = m_slab != nullptr;
		if ( slab )
		{
			cloud()->setVisible( true );
		}
		a->trigger();
		if ( slab && cloud() )
		{
			cloud()->setVisible( false );
		}
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
	std::vector<Tuple4Tpl<double>> planes;
	double margin = 0.0;

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
				planes.push_back( p1.equation );
				planes.push_back( p2.equation );
			}
			else
			{
				// pohled: vše za rovinou do zadané hloubky
				const double depth = si.depth * sc;
				ccClipPlane p1;
				p1.equation = Tuple4Tpl<double>( -f.N.x, -f.N.y, -f.N.z, d );
				ccClipPlane p2;
				p2.equation = Tuple4Tpl<double>( f.N.x, f.N.y, f.N.z, -( d - depth ) );
				planes.push_back( p1.equation );
				planes.push_back( p2.equation );
			}
			// do stran jen v délce čáry (+ 20 cm)
			const double u0 = f.O.dot( f.U );
			const double len = ( si.B - si.A ).norm();
			const double m = 0.2 * sc;
			ccClipPlane p3;
			p3.equation = Tuple4Tpl<double>( f.U.x, f.U.y, f.U.z, -( u0 - m ) );
			ccClipPlane p4;
			p4.equation = Tuple4Tpl<double>( -f.U.x, -f.U.y, -f.U.z, u0 + len + m );
			planes.push_back( p3.equation );
			planes.push_back( p4.equation );
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
		planes.push_back( lower.equation );
		planes.push_back( upper.equation );
	}

	// rezerva výběru: posun řezu PgUp/PgDn o pár kroků nemusí vybírat znovu
	margin = ( 0.15 + m_thickness->value() + 2.0 * m_step->value() ) * c->getGlobalScale();
	showClip( c, planes, margin );
	storeState( false );
}

void RezDock::showClip( ccGenericPointCloud* c, const std::vector<Tuple4Tpl<double>>& planes, double margin )
{
	ccPointCloud* pc = ccHObjectCaster::ToPointCloud( c );
	// malé mračno nebo bez ořezu: přímo ořezové roviny na mračnu (jako dřív)
	const unsigned SlabMin = 8000000;
	if ( planes.empty() || !pc || pc->size() < SlabMin || !c->getDisplay() )
	{
		removeSlab();
		for ( const Tuple4Tpl<double>& e : planes )
		{
			ccClipPlane cp;
			cp.equation = e;
			c->addClipPlanes( cp );
		}
		c->prepareDisplayForRefresh();
		m_app->refreshAll();
		return;
	}
	m_slabExact = planes;
	m_slabMargin = margin;
	bool covered = m_slab && m_slabSrc == c->getUniqueID() && m_slabWide.size() == planes.size();
	for ( size_t i = 0; covered && i < planes.size(); ++i )
	{
		covered = Extract::inside( planes[i], m_slabWide[i] );
	}
	if ( !covered )
	{
		if ( !m_slab || m_slabSrc != c->getUniqueID() )
		{
			extractSlab();
			return;
		}
		// zatím ukázat, co je v kopii, a vybrat znovu po chvíli klidu (držení PgUp)
		if ( !m_slabTimer )
		{
			m_slabTimer = new QTimer( this );
			m_slabTimer->setSingleShot( true );
			connect( m_slabTimer, &QTimer::timeout, this, [this]() { extractSlab(); } );
		}
		m_slabTimer->start( 300 );
	}
	m_slab->removeAllClipPlanes();
	for ( const Tuple4Tpl<double>& e : planes )
	{
		ccClipPlane cp;
		cp.equation = e;
		m_slab->addClipPlanes( cp );
	}
	m_slab->prepareDisplayForRefresh();
	m_app->refreshAll();
}

void RezDock::extractSlab()
{
	ccGenericPointCloud* c = cloud();
	ccPointCloud* pc = c ? ccHObjectCaster::ToPointCloud( c ) : nullptr;
	if ( !pc || m_slabExact.empty() || !c->getDisplay() )
	{
		return;
	}
	std::vector<Tuple4Tpl<double>> wide;
	for ( const Tuple4Tpl<double>& e : m_slabExact )
	{
		wide.push_back( Extract::widened( e, m_slabMargin ) );
	}
	QApplication::setOverrideCursor( Qt::WaitCursor );
	unsigned total = 0;
	ccPointCloud* sl = Extract::byPlanes( pc, wide, 0, total );
	QApplication::restoreOverrideCursor();
	if ( !sl )
	{
		// nestačí paměť → raději ořez na celém mračnu
		removeSlab();
		const std::vector<Tuple4Tpl<double>> planes = m_slabExact;
		m_slabExact.clear();
		for ( const Tuple4Tpl<double>& e : planes )
		{
			ccClipPlane cp;
			cp.equation = e;
			c->addClipPlanes( cp );
		}
		m_app->refreshAll();
		return;
	}
	removeSlab();
	m_slab = sl;
	m_slabSrc = c->getUniqueID();
	m_slabWide = wide;
	m_slab->setName( c->getName() + QStringLiteral( " (plátek řezu)" ) );
	m_slab->setGlobalShift( c->getGlobalShift() );
	m_slab->setGlobalScale( c->getGlobalScale() );
	m_slab->showColors( c->colorsShown() && m_slab->hasColors() );
	if ( pc->sfShown() && pc->getCurrentDisplayedScalarFieldIndex() >= 0 && pc->getCurrentDisplayedScalarFieldIndex() < static_cast<int>( m_slab->getNumberOfScalarFields() ) )
	{
		m_slab->setCurrentDisplayedScalarField( pc->getCurrentDisplayedScalarFieldIndex() );
		m_slab->showSF( true );
	}
	m_slab->setPointSize( pc->getPointSize() );
	m_slab->setEnabled( true );
	m_slab->setVisible( true );
	m_slab->setDisplay( c->getDisplay() );
	for ( const Tuple4Tpl<double>& e : m_slabExact )
	{
		ccClipPlane cp;
		cp.equation = e;
		m_slab->addClipPlanes( cp );
	}
	if ( auto* win = static_cast<ccGLWindowInterface*>( c->getDisplay() ) )
	{
		win->addToOwnDB( m_slab );
	}
	// velké mračno se nekreslí (zůstává v seznamu objektů a v paměti)
	c->setVisible( false );
	m_app->dispToConsole( QStringLiteral( "[Řezy] Zobrazen plátek řezu: %1 bodů z %2." ).arg( m_slab->size() ).arg( c->size() ) );
	c->prepareDisplayForRefresh();
	m_app->refreshAll();
}

void RezDock::removeSlab()
{
	if ( m_slabTimer )
	{
		m_slabTimer->stop();
	}
	if ( !m_slab )
	{
		return;
	}
	if ( auto* win = static_cast<ccGLWindowInterface*>( m_slab->getDisplay() ) )
	{
		win->removeFromOwnDB( m_slab );
	}
	delete m_slab;
	m_slab = nullptr;
	m_slabWide.clear();
	if ( m_app && m_app->dbRootObject() )
	{
		if ( ccHObject* src = m_app->dbRootObject()->find( m_slabSrc ) )
		{
			src->setVisible( true );
			src->prepareDisplayForRefresh();
		}
	}
	m_slabSrc = 0;
}

void RezDock::clearClip()
{
	removeSlab();
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
	m_draw->setCategory( m_catCombo->currentData().toInt() );
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
		if ( !aux )
		{
			connect( v, &View3D::pinPicked, this, [this]( double x, double y, double z )
			{
				m_draw->showPin( CCVector3d( x, y, z ) );
				if ( m_viewAux && m_viewAux->isVisible() )
				{
					m_viewAux->auxPin( CCVector3d( x, y, z ), false );
				}
			} );
		}
		else
		{
			// špendlík z pomocného řezu → půdorys a 3D náhled (pohled pomocného řezu se nemění)
			connect( v, &View3D::pinPicked, this, [this]( double x, double y, double z )
			{
				m_draw->showPin( CCVector3d( x, y, z ) );
				if ( ensure3D() )
				{
					m_view3D->pinAt( CCVector3d( x, y, z ), m_viewAngle, false );
				}
			} );
			// měření v pomocném řezu → kóta i v 3D náhledu; zápis výšky do půdorysu
			connect( v, &View3D::measureDone, this, [this]( const QVector<double>& xyz, double nx, double ny )
			{
				if ( m_view3D && m_view3D->isVisible() )
				{
					std::vector<CCVector3d> pts;
					for ( int i = 0; i + 2 < xyz.size(); i += 3 )
					{
						pts.emplace_back( xyz[i], xyz[i + 1], xyz[i + 2] );
					}
					m_view3D->setMeasures( pts, CCVector3d( nx, ny, 0 ), 0.0 );
				}
			} );
			connect( v, &View3D::writeHeight, this, [this]( const QString& t, int kind ) { writeAuxHeight( t, kind ); } );
		}
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
		// změřené body ze souboru (bez výšky na podlaze patra)
		std::vector<View3D::SurveyPt> sps;
		const double floorL = c ? c->toLocal3d( CCVector3d( 0, 0, m_floorLevel->value() ) ).z : m_floorLevel->value();
		if ( group )
		{
			for ( unsigned i = 0; i < group->getChildrenNumber(); ++i )
			{
				ccHObject* ch = group->getChild( i );
				if ( !ch->isA( CC_TYPES::POINT_CLOUD ) || !ch->hasMetaData( "qRezy.points" ) || !ch->isEnabled() )
				{
					continue;
				}
				auto* pc = static_cast<ccPointCloud*>( ch );
				const QStringList names = pc->getMetaData( "qRezy.names" ).toStringList();
				const QString hasZ = pc->getMetaData( "qRezy.hasZ" ).toString();
				for ( unsigned k = 0; k < pc->size(); ++k )
				{
					View3D::SurveyPt sp;
					sp.P = CCVector3d::fromArray( pc->getPoint( k )->u );
					const bool z = ( static_cast<int>( k ) < hasZ.size() ) && hasZ[static_cast<int>( k )] == QChar( '1' );
					if ( !z )
					{
						sp.P.z = floorL;
					}
					sp.name = ( static_cast<int>( k ) < names.size() ) ? names[static_cast<int>( k )] : QString();
					sps.push_back( sp );
				}
			}
		}
		QSettings pst;
		const QColor cm = pst.value( QStringLiteral( "qRezy/ptColMarker" ), QColor( 255, 90, 255 ) ).value<QColor>();
		const QColor cn = pst.value( QStringLiteral( "qRezy/ptColName" ), QColor( 255, 235, 0 ) ).value<QColor>();
		m_view3D->setSurveyPoints( sps, ccColor::Rgb( static_cast<ColorCompType>( cm.red() ), static_cast<ColorCompType>( cm.green() ), static_cast<ColorCompType>( cm.blue() ) ),
		                           ccColor::Rgb( static_cast<ColorCompType>( cn.red() ), static_cast<ColorCompType>( cn.green() ), static_cast<ColorCompType>( cn.blue() ) ) );
	}
	if ( va )
	{
		m_viewAux->setLines( lines );
	}
}

void RezDock::show3DAt( double x, double y, double z, bool pin )
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
	if ( pin )
	{
		m_view3D->pinAt( CCVector3d( x, y, zLocal ), ang, std::isnan( z ) );
		if ( m_viewAux && m_viewAux->isVisible() )
		{
			m_viewAux->auxPin( CCVector3d( x, y, zLocal ), std::isnan( z ) );
		}
	}
	else
	{
		m_view3D->focusOn( CCVector3d( x, y, zLocal ), ang ); // tlačítko 3D náhled = výchozí šikmý pohled
	}
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

	// změřené body v řezech a pohledech
	auto* ptBox = new QGroupBox( QStringLiteral( "Změřené body v řezech a pohledech (křížek + číslo; do půdorysu ne)" ) );
	ptBox->setCheckable( true );
	ptBox->setChecked( st.value( QStringLiteral( "qRezy/ptExport" ), true ).toBool() );
	auto* pf = new QFormLayout( ptBox );
	auto* ptLevel = new QSpinBox;
	ptLevel->setRange( 1, 63 );
	ptLevel->setValue( st.value( QStringLiteral( "qRezy/ptLevel" ), 16 ).toInt() );
	pf->addRow( QStringLiteral( "Hladina:" ), ptLevel );
	auto* ptCross = makeSpin( 0.005, 1.0, st.value( QStringLiteral( "qRezy/ptCrossHalf" ), 0.02 ).toDouble(), 0.005, 3, QStringLiteral( " m" ) );
	pf->addRow( QStringLiteral( "Křížek – polovina ramene:" ), ptCross );
	auto* ptTextH = makeSpin( 0.0, 5.0, st.value( QStringLiteral( "qRezy/ptTextH" ), 0.10 ).toDouble(), 0.05, 2, QStringLiteral( " m" ) );
	ptTextH->setSpecialValueText( QStringLiteral( "bez čísla" ) );
	pf->addRow( QStringLiteral( "Číslo bodu – výška:" ), ptTextH );
	l->addWidget( ptBox );

	// výškové kóty
	auto* kBox = new QGroupBox( QStringLiteral( "Výškové kóty v řezech a pohledech" ) );
	auto* kf = new QHBoxLayout( kBox );
	auto* kLevel = new QSpinBox;
	kLevel->setRange( 1, 63 );
	kLevel->setValue( st.value( QStringLiteral( "qRezy/kotaLevel" ), 16 ).toInt() );
	auto* kColor = new QSpinBox;
	kColor->setRange( 0, 255 );
	kColor->setValue( st.value( QStringLiteral( "qRezy/kotaColor" ), 135 ).toInt() );
	kf->addWidget( new QLabel( QStringLiteral( "Hladina:" ) ) );
	kf->addWidget( kLevel );
	kf->addSpacing( 12 );
	kf->addWidget( new QLabel( QStringLiteral( "Barva:" ) ) );
	kf->addWidget( kColor );
	kf->addStretch( 1 );
	l->addWidget( kBox );

	// výšky zapsané z pomocného řezu
	auto* hBox = new QGroupBox( QStringLiteral( "Výšky z pomocného řezu (texty v půdorysu) – hladiny" ) );
	auto* hf = new QHBoxLayout( hBox );
	auto mkLvl = [&]( const char* key, int def ) {
		auto* sp = new QSpinBox;
		sp->setRange( 1, 63 );
		sp->setValue( st.value( QString::fromLatin1( key ), def ).toInt() );
		return sp;
	};
	auto* hWin = mkLvl( "qRezy/hLevelWin", 17 );
	auto* hLin = mkLvl( "qRezy/hLevelLin", 18 );
	auto* hH = mkLvl( "qRezy/hLevelH", 19 );
	auto* hCeil = mkLvl( "qRezy/hLevelCeil", 20 );
	hf->addWidget( new QLabel( QStringLiteral( "Okno:" ) ) );
	hf->addWidget( hWin );
	hf->addSpacing( 8 );
	hf->addWidget( new QLabel( QStringLiteral( "Překlad:" ) ) );
	hf->addWidget( hLin );
	hf->addSpacing( 8 );
	hf->addWidget( new QLabel( QStringLiteral( "Výška:" ) ) );
	hf->addWidget( hH );
	hf->addSpacing( 8 );
	hf->addWidget( new QLabel( QStringLiteral( "Strop:" ) ) );
	hf->addWidget( hCeil );
	hf->addStretch( 1 );
	hBox->setToolTip( QStringLiteral( "Texty zapsané u vložených dřív mají hladinu uloženou u sebe; změna platí pro nové zápisy" ) );
	l->addWidget( hBox );

	// rastr mračna
	auto* rBox = new QGroupBox( QStringLiteral( "Rastr mračna (TIFF + .tfw) k půdorysům, řezům a pohledům" ) );
	rBox->setCheckable( true );
	rBox->setChecked( st.value( QStringLiteral( "qRezy/rasterExport" ), false ).toBool() );
	rBox->setToolTip( QStringLiteral( "Barevný obrázek mračna v řezu (půdorys v S-JTSK, řezy a pohledy v místě ve výkresu). "
	                                  "V MicroStationu připojit přes Raster Manager – poloha se načte ze souboru .tfw." ) );
	auto* rf = new QFormLayout( rBox );
	auto* rPx = makeSpin( 0.002, 0.5, st.value( QStringLiteral( "qRezy/rasterPx" ), 0.01 ).toDouble(), 0.005, 3, QStringLiteral( " m / pixel" ) );
	rf->addRow( QStringLiteral( "Velikost pixelu:" ), rPx );
	auto* rDot = new QSpinBox;
	rDot->setRange( 1, 6 );
	rDot->setSuffix( QStringLiteral( " px" ) );
	rDot->setValue( st.value( QStringLiteral( "qRezy/rasterDot" ), 2 ).toInt() );
	rDot->setToolTip( QStringLiteral( "Velikost tečky bodu – větší zaplní mezery mezi body" ) );
	rf->addRow( QStringLiteral( "Velikost bodu:" ), rDot );
	l->addWidget( rBox );

	auto* onlyNew = new QCheckBox( QStringLiteral( "Uložit jen doplněné prvky (bez prvků načtených z DGN)" ) );
	onlyNew->setChecked( st.value( QStringLiteral( "qRezy/skipImported" ), false ).toBool() );
	onlyNew->setToolTip( QStringLiteral( "Pro výkres, který jste načetli tlačítkem Načíst V7: uloží se jen to, co jste dokreslili – v MicroStationu to pak zkopírujete do původního výkresu." ) );
	onlyNew->setVisible( m_draw->countImported() > 0 );
	l->addWidget( onlyNew );

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
	st.setValue( QStringLiteral( "qRezy/ptExport" ), ptBox->isChecked() );
	st.setValue( QStringLiteral( "qRezy/rasterExport" ), rBox->isChecked() );
	st.setValue( QStringLiteral( "qRezy/skipImported" ), !onlyNew->isHidden() && onlyNew->isChecked() );
	st.setValue( QStringLiteral( "qRezy/rasterPx" ), rPx->value() );
	st.setValue( QStringLiteral( "qRezy/rasterDot" ), rDot->value() );
	st.setValue( QStringLiteral( "qRezy/ptLevel" ), ptLevel->value() );
	st.setValue( QStringLiteral( "qRezy/ptCrossHalf" ), ptCross->value() );
	st.setValue( QStringLiteral( "qRezy/kotaLevel" ), kLevel->value() );
	st.setValue( QStringLiteral( "qRezy/hLevelWin" ), hWin->value() );
	st.setValue( QStringLiteral( "qRezy/hLevelLin" ), hLin->value() );
	st.setValue( QStringLiteral( "qRezy/hLevelH" ), hH->value() );
	st.setValue( QStringLiteral( "qRezy/hLevelCeil" ), hCeil->value() );
	st.setValue( QStringLiteral( "qRezy/kotaColor" ), kColor->value() );
	m_draw->setKotaSymbology( kLevel->value(), kColor->value() );
	st.setValue( QStringLiteral( "qRezy/ptTextH" ), ptTextH->value() );
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
		m_draw->setExportSkipImported( st.value( QStringLiteral( "qRezy/skipImported" ), false ).toBool() && m_draw->countImported() > 0 );
		std::vector<DgnV7::Element> elements = m_draw->exportElements( table, map, userTextLevel, userTextH );
		m_draw->setExportSkipImported( false );
		if ( t.sections && st.value( QStringLiteral( "qRezy/ptExport" ), true ).toBool() )
		{
			// změřené body sklopené do řezů a pohledů: malý křížek + číslo, vlastní hladina
			const int ptLevel = st.value( QStringLiteral( "qRezy/ptLevel" ), 16 ).toInt();
			const double cr = st.value( QStringLiteral( "qRezy/ptCrossHalf" ), 0.02 ).toDouble();
			const double ptH = st.value( QStringLiteral( "qRezy/ptTextH" ), 0.10 ).toDouble();
			QTextCodec* codec = QTextCodec::codecForName( "Windows-1250" );
			for ( const SectionInfo& si : sections )
			{
				for ( const auto& pp : m_draw->pointsInFrame( frameFor( si.id ) ) )
				{
					DgnV7::Point2 o;
					if ( !map( si.id, pp.first, o ) )
					{
						continue;
					}
					DgnV7::Element h;
					h.sym.level = ptLevel;
					h.pts = { { o.x - cr, o.y }, { o.x + cr, o.y } };
					elements.push_back( h );
					DgnV7::Element v = h;
					v.pts = { { o.x, o.y - cr }, { o.x, o.y + cr } };
					elements.push_back( v );
					if ( !pp.second.isEmpty() && ptH > 0.0 )
					{
						DgnV7::Element tx;
						tx.isText = true;
						tx.sym.level = ptLevel;
						tx.textHeight = ptH;
						tx.pts.push_back( { o.x + cr * 0.6, o.y + cr * 0.6 } );
						const QByteArray bytes = codec ? codec->fromUnicode( pp.second ) : pp.second.toLatin1();
						tx.text = std::string( bytes.constData(), static_cast<size_t>( bytes.size() ) );
						elements.push_back( tx );
					}
				}
			}
		}
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
		if ( st.value( QStringLiteral( "qRezy/rasterExport" ), false ).toBool() )
		{
			// rastr mračna (TIFF + .tfw) ke každému půdorysu / řezu / pohledu
			const double px = st.value( QStringLiteral( "qRezy/rasterPx" ), 0.01 ).toDouble();
			const int dot = st.value( QStringLiteral( "qRezy/rasterDot" ), 2 ).toInt();
			QApplication::setOverrideCursor( Qt::WaitCursor );
			for ( int view : t.views )
			{
				QString tif = path;
				tif.chop( 4 );
				if ( !isPlanView( view ) )
				{
					SectionInfo sx;
					if ( !sectionById( view, sx ) )
					{
						continue;
					}
					// název bez diakritiky a zvláštních znaků (MicroStation, starší programy)
					QString nm = sx.name.normalized( QString::NormalizationForm_D );
					QString safe;
					for ( const QChar ch : nm )
					{
						if ( ch.unicode() < 128 && ( ch.isLetterOrNumber() || ch == '-' || ch == '_' ) )
							safe += ch;
						else if ( ch == ' ' )
							safe += '_';
					}
					tif += QStringLiteral( "_" ) + ( safe.isEmpty() ? QString::number( view ) : safe );
				}
				tif += QStringLiteral( ".tif" );
				QString info;
				if ( writeCloudRaster( tif, view, px, dot, info ) )
				{
					report << QStringLiteral( "%1 – rastr %2" ).arg( QFileInfo( tif ).fileName(), info );
				}
				else
				{
					skipped << QStringLiteral( "%1 (%2)" ).arg( QFileInfo( tif ).fileName(), info );
				}
			}
			QApplication::restoreOverrideCursor();
		}
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

bool RezDock::writeCloudRaster( const QString& path, int viewId, double px, int dot, QString& info )
{
	ccGenericPointCloud* c = cloud();
	if ( !c || px <= 0.0 )
	{
		info = QStringLiteral( "není mračno" );
		return false;
	}
	const double sc = c->getGlobalScale();
	const CCVector3d shift = c->getGlobalShift();
	const bool absH = QSettings().value( QStringLiteral( "qRezy/absHeights" ), false ).toBool();
	const double zeroG = m_zero->value();

	// výběr bodů (roviny a·x+b·y+c·z+d ≥ 0), priorita (větší = blíž pozorovateli) a převod do výkresu
	std::vector<std::array<double, 4>> planes;
	CCVector3d prioN( 0, 0, 1 );
	bool plan = isPlanView( viewId );
	DrawFrame f;
	SectionInfo si;
	if ( plan )
	{
		FloorInfo fi;
		if ( !floorById( viewId, fi ) )
		{
			info = QStringLiteral( "patro nenalezeno" );
			return false;
		}
		const double half = m_thickness->value() / 2.0;
		const double zLo = c->toLocal3d( CCVector3d( 0, 0, fi.cut - half ) ).z;
		const double zHi = c->toLocal3d( CCVector3d( 0, 0, fi.cut + half ) ).z;
		planes.push_back( { 0.0, 0.0, 1.0, -zLo } );
		planes.push_back( { 0.0, 0.0, -1.0, zHi } );
	}
	else
	{
		if ( !sectionById( viewId, si ) )
		{
			info = QStringLiteral( "řez nenalezen" );
			return false;
		}
		f = frameFor( viewId );
		prioN = f.N;
		const double d = f.O.dot( f.N ) - si.offset * sc;
		if ( si.kind == 0 )
		{
			const double half = m_thickness->value() * sc / 2.0;
			planes.push_back( { f.N.x, f.N.y, f.N.z, -( d - half ) } );
			planes.push_back( { -f.N.x, -f.N.y, -f.N.z, d + half } );
		}
		else
		{
			const double depth = si.depth * sc;
			planes.push_back( { -f.N.x, -f.N.y, -f.N.z, d } );
			planes.push_back( { f.N.x, f.N.y, f.N.z, -( d - depth ) } );
		}
		const double u0 = f.O.dot( f.U );
		const double len = ( si.B - si.A ).norm();
		const double m = 0.2 * sc;
		planes.push_back( { f.U.x, f.U.y, f.U.z, -( u0 - m ) } );
		planes.push_back( { -f.U.x, -f.U.y, -f.U.z, u0 + len + m } );
	}
	auto inside = [&planes]( const CCVector3* P )
	{
		for ( const auto& e : planes )
		{
			if ( e[0] * P->x + e[1] * P->y + e[2] * P->z + e[3] < 0.0 )
			{
				return false;
			}
		}
		return true;
	};
	auto toDrawing = [&]( const CCVector3* P, double& x, double& y )
	{
		if ( plan )
		{
			x = P->x / sc - shift.x;
			y = P->y / sc - shift.y;
			return;
		}
		const CCVector2d uv = f.toUV( CCVector3d( P->x, P->y, P->z ) );
		x = si.insX + uv.x / sc;
		y = absH ? ( uv.y / sc + zeroG ) : ( si.insY + uv.y / sc );
	};

	// 1. průchod: rozsah
	const unsigned n = c->size();
	double x0 = 1e300;
	double y0 = 1e300;
	double x1 = -1e300;
	double y1 = -1e300;
	size_t cnt = 0;
	for ( unsigned i = 0; i < n; ++i )
	{
		const CCVector3* P = c->getPoint( i );
		if ( !inside( P ) )
		{
			continue;
		}
		double x;
		double y;
		toDrawing( P, x, y );
		x0 = std::min( x0, x );
		y0 = std::min( y0, y );
		x1 = std::max( x1, x );
		y1 = std::max( y1, y );
		++cnt;
	}
	if ( cnt == 0 )
	{
		info = QStringLiteral( "v řezu nejsou žádné body" );
		return false;
	}
	// mřížka zarovnaná na násobky pixelu, okraj 10 px
	const double margin = 10 * px;
	const double gx0 = std::floor( ( x0 - margin ) / px ) * px;
	const double gy1 = std::ceil( ( y1 + margin ) / px ) * px;
	const double W = std::ceil( ( x1 + margin - gx0 ) / px );
	const double H = std::ceil( ( gy1 - ( y0 - margin ) ) / px );
	if ( W * H > 400e6 || W > 60000 || H > 60000 )
	{
		info = QStringLiteral( "obrázek by měl %1 × %2 px – zvětšete velikost pixelu" ).arg( W, 0, 'f', 0 ).arg( H, 0, 'f', 0 );
		return false;
	}
	const int w = static_cast<int>( W );
	const int h = static_cast<int>( H );
	std::vector<uint8_t> rgb( static_cast<size_t>( w ) * h * 3, 255 );
	std::vector<float> zb( static_cast<size_t>( w ) * h, -std::numeric_limits<float>::max() );
	const bool colors = c->hasColors();
	const int r0 = -( dot - 1 ) / 2;
	const int r1 = dot / 2;
	// 2. průchod: body do mřížky (bližší přes vzdálenější)
	for ( unsigned i = 0; i < n; ++i )
	{
		const CCVector3* P = c->getPoint( i );
		if ( !inside( P ) )
		{
			continue;
		}
		double x;
		double y;
		toDrawing( P, x, y );
		const int cx = static_cast<int>( std::floor( ( x - gx0 ) / px ) );
		const int cy = static_cast<int>( std::floor( ( gy1 - y ) / px ) );
		const float prio = static_cast<float>( P->x * prioN.x + P->y * prioN.y + P->z * prioN.z );
		uint8_t cr = 70;
		uint8_t cg = 70;
		uint8_t cb = 70;
		if ( colors )
		{
			const ccColor::Rgba& col = c->getPointColor( i );
			cr = col.r;
			cg = col.g;
			cb = col.b;
		}
		for ( int dy = r0; dy <= r1; ++dy )
		{
			const int yy = cy + dy;
			if ( yy < 0 || yy >= h )
			{
				continue;
			}
			for ( int dx = r0; dx <= r1; ++dx )
			{
				const int xx = cx + dx;
				if ( xx < 0 || xx >= w )
				{
					continue;
				}
				const size_t k = static_cast<size_t>( yy ) * w + xx;
				if ( prio >= zb[k] )
				{
					zb[k] = prio;
					rgb[k * 3] = cr;
					rgb[k * 3 + 1] = cg;
					rgb[k * 3 + 2] = cb;
				}
			}
		}
	}
	zb.clear();
	zb.shrink_to_fit();
	QString err;
	if ( !Tiff::writeRgb( path, w, h, rgb, 0.0254 / px, err ) )
	{
		info = err;
		return false;
	}
	QString tfw = path;
	tfw.replace( QRegularExpression( QStringLiteral( "\\.tiff?$" ), QRegularExpression::CaseInsensitiveOption ), QStringLiteral( ".tfw" ) );
	if ( !Tiff::writeWorld( tfw, px, gx0 + px / 2.0, gy1 - px / 2.0, err ) )
	{
		info = err;
		return false;
	}
	info = QStringLiteral( "%1 × %2 px, %3 bodů" ).arg( w ).arg( h ).arg( cnt );
	return true;
}

void RezDock::writeAuxHeight( const QString& text, int kind )
{
	ccGenericPointCloud* c = cloud();
	if ( !c || !m_draw )
	{
		return;
	}
	CCVector2d d = m_auxPB - m_auxPA;
	if ( d.norm() < 1e-9 )
	{
		return;
	}
	d.normalize();
	// čitelně: zleva doprava (nebo zdola nahoru)
	if ( d.x < -1e-9 || ( std::abs( d.x ) <= 1e-9 && d.y < 0 ) )
	{
		d = d * -1.0;
	}
	if ( !isPlanView( m_activeView ) )
	{
		activateView( m_lastPlan );
	}
	if ( !m_draw->isActive() )
	{
		m_drawAct->setChecked( true );
	}
	QSettings st;
	const double sc = c->getGlobalScale();
	const double h = st.value( QStringLiteral( "qRezy/userTextHeight" ), 0.15 ).toDouble();
	const double w = 0.8 * h * text.size();
	const CCVector2d perp( -d.y, d.x );
	const CCVector2d M = ( m_auxPA + m_auxPB ) / 2.0;
	// text uprostřed čáry, kousek nad ní
	const CCVector2d P = M - d * ( w / 2.0 * sc ) + perp * ( 0.05 * sc );
	static const char* keys[] = { "qRezy/hLevelWin", "qRezy/hLevelLin", "qRezy/hLevelH", "qRezy/hLevelCeil" };
	static const int defs[] = { 17, 18, 19, 20 };
	const int k = std::max( 0, std::min( 3, kind ) );
	const int level = st.value( QString::fromLatin1( keys[k] ), defs[k] ).toInt();
	if ( st.value( QStringLiteral( "qRezy/auxWriteManual" ), false ).toBool() )
	{
		// ručně: text visí na kurzoru, uživatel ho položí klikem
		m_draw->startPlaceText( text, std::atan2( d.y, d.x ), level );
		m_app->dispToConsole( QStringLiteral( "[Řezy] Umístěte text %1 klikem do půdorysu." ).arg( text ) );
		return;
	}
	m_draw->addTextAt( P, text, std::atan2( d.y, d.x ), level );
	m_app->dispToConsole( QStringLiteral( "[Řezy] Do půdorysu zapsáno %1 (hladina %2)." ).arg( text ).arg( level ) );
}

void RezDock::transferDrawing()
{
	ccGenericPointCloud* src = cloud();
	ccHObject* group = m_draw ? m_draw->drawingGroup( false ) : nullptr;
	if ( !src || !group || !m_app || !m_app->dbRootObject() )
	{
		QMessageBox::information( this, QStringLiteral( "Přenést výkres" ), QStringLiteral( "Nejdřív vyberte mračno, ke kterému výkres patří (v panelu Řezy)." ) );
		return;
	}
	// kandidáti: ostatní mračna v databázi (ne vrcholy čar, ne body ze souboru, ne nic uvnitř výkresu)
	std::vector<ccGenericPointCloud*> cands;
	ccHObject::Container all;
	m_app->dbRootObject()->filterChildren( all, true, CC_TYPES::POINT_CLOUD, false );
	for ( ccHObject* o : all )
	{
		if ( o == src || o->hasMetaData( "qRezy.points" ) || ( o->getParent() && o->getParent()->isA( CC_TYPES::POLY_LINE ) ) )
		{
			continue;
		}
		bool inside = false;
		for ( ccHObject* p = o->getParent(); p; p = p->getParent() )
		{
			if ( p == group )
			{
				inside = true;
				break;
			}
		}
		if ( !inside )
		{
			cands.push_back( ccHObjectCaster::ToGenericPointCloud( o ) );
		}
	}
	if ( cands.empty() )
	{
		QMessageBox::information( this, QStringLiteral( "Přenést výkres" ), QStringLiteral( "Načtěte nejdřív nové (podrobnější) mračno – soubor otevřete v CloudCompare a při dotazu na posun souřadnic potvrďte stejný posun jako u současného mračna." ) );
		return;
	}
	QStringList names;
	for ( ccGenericPointCloud* c : cands )
	{
		names << QStringLiteral( "%1  (%2 bodů)" ).arg( c->getName() ).arg( QLocale( QLocale::Czech ).toString( static_cast<qulonglong>( c->size() ) ) );
	}
	bool ok = false;
	const QString pick = QInputDialog::getItem( this, QStringLiteral( "Přenést výkres" ),
	                                            QStringLiteral( "Výkres z mračna „%1“ přenést na:" ).arg( src->getName() ), names, 0, false, &ok );
	if ( !ok )
	{
		return;
	}
	ccGenericPointCloud* dst = cands[static_cast<size_t>( std::max( 0, names.indexOf( pick ) ) )];
	for ( unsigned i = 0; i < dst->getChildrenNumber(); ++i )
	{
		if ( dst->getChild( i )->isA( CC_TYPES::HIERARCHY_OBJECT ) && dst->getChild( i )->getName() == Drawing::GroupName )
		{
			QMessageBox::warning( this, QStringLiteral( "Přenést výkres" ), QStringLiteral( "Mračno „%1“ už svůj výkres má. Nejdřív ho smažte nebo vyberte jiné mračno." ).arg( dst->getName() ) );
			return;
		}
	}
	// posun souřadnic: lokální = (globální + posun) × měřítko
	const double sS = src->getGlobalScale();
	const double sD = dst->getGlobalScale();
	if ( std::abs( sS - sD ) > 1e-9 * std::max( 1.0, std::abs( sS ) ) )
	{
		QMessageBox::warning( this, QStringLiteral( "Přenést výkres" ), QStringLiteral( "Mračna mají rozdílné měřítko souřadnic (Global scale %1 × %2) – přenos nejde. Načtěte nové mračno se stejným měřítkem." ).arg( sS ).arg( sD ) );
		return;
	}
	const CCVector3d d = ( dst->getGlobalShift() - src->getGlobalShift() ) * sS;
	const bool moved = d.norm() > 1e-9;
	if ( moved )
	{
		if ( QMessageBox::question( this, QStringLiteral( "Přenést výkres" ),
		                            QStringLiteral( "Mračna mají jiný posun souřadnic (Global shift). Výkres se přepočítá o %1 / %2 / %3 m, aby seděl na stejném místě.\n\nPokračovat?" )
		                                .arg( d.x / sS, 0, 'f', 3 ).arg( d.y / sS, 0, 'f', 3 ).arg( d.z / sS, 0, 'f', 3 ) ) != QMessageBox::Yes )
		{
			return;
		}
		// všechny body (vrcholy čar, body ze souboru) + definice řezů
		ccHObject::Container clouds;
		group->filterChildren( clouds, true, CC_TYPES::POINT_CLOUD, false );
		const CCVector3 df = d.toFloat();
		for ( ccHObject* o : clouds )
		{
			auto* pc = static_cast<ccPointCloud*>( o );
			for ( unsigned k = 0; k < pc->size(); ++k )
			{
				*const_cast<CCVector3*>( pc->getPoint( k ) ) += df;
			}
			pc->invalidateBoundingBox();
		}
		ccHObject::Container polys;
		group->filterChildren( polys, true, CC_TYPES::POLY_LINE, false );
		for ( ccHObject* o : polys )
		{
			auto* p = static_cast<ccPolyline*>( o );
			p->invalidateBoundingBox();
			if ( p->hasMetaData( "qRezy.secAx" ) )
			{
				p->setMetaData( "qRezy.secAx", p->getMetaData( "qRezy.secAx" ).toDouble() + d.x );
				p->setMetaData( "qRezy.secAy", p->getMetaData( "qRezy.secAy" ).toDouble() + d.y );
				p->setMetaData( "qRezy.secBx", p->getMetaData( "qRezy.secBx" ).toDouble() + d.x );
				p->setMetaData( "qRezy.secBy", p->getMetaData( "qRezy.secBy" ).toDouble() + d.y );
			}
		}
	}
	// uložit stav panelu do skupiny, odpojit od starého mračna a připojit k novému
	storeState( false );
	const bool wasDrawing = m_drawAct && m_drawAct->isChecked();
	if ( wasDrawing )
	{
		m_drawAct->setChecked( false );
	}
	clearClip();
	m_app->removeFromDB( group, false );
	dst->addChild( group );
	m_app->addToDB( group, false, true, false, false );
	m_cloudID = 0;
	setCloud( dst );
	src->setEnabled( false );
	if ( wasDrawing )
	{
		m_drawAct->setChecked( true );
	}
	m_app->dispToConsole( QStringLiteral( "[Řezy] Výkres přenesen z mračna %1 na %2%3." ).arg( src->getName(), dst->getName(), moved ? QStringLiteral( " (přepočten posun souřadnic)" ) : QString() ) );
	QMessageBox::information( this, QStringLiteral( "Přenést výkres" ),
	                          QStringLiteral( "Výkres je teď u mračna „%1“. Staré mračno „%2“ je skryté – když vše sedí, můžete ho zavřít a práci uložit (Uložit práci)." ).arg( dst->getName(), src->getName() ) );
	m_app->refreshAll();
}

void RezDock::importDgn()
{
	ccGenericPointCloud* c = cloud();
	if ( !c )
	{
		QMessageBox::information( this, QStringLiteral( "Řezy" ), QStringLiteral( "Nejdřív vyberte mračno." ) );
		return;
	}
	if ( !m_draw->isActive() )
	{
		m_drawAct->setChecked( true );
	}
	QSettings st;
	const QString path = QFileDialog::getOpenFileName( this, QStringLiteral( "Načíst výkres MicroStation V7 do aktivního půdorysu / řezu" ),
	                                                   st.value( QStringLiteral( "qRezy/importDir" ), st.value( QStringLiteral( "qRezy/lastOut" ) ) ).toString(),
	                                                   QStringLiteral( "MicroStation V7 (*.dgn);;Všechny soubory (*)" ) );
	if ( path.isEmpty() )
	{
		return;
	}
	st.setValue( QStringLiteral( "qRezy/importDir" ), QFileInfo( path ).absolutePath() );
	QFile f( path );
	if ( !f.open( QIODevice::ReadOnly ) )
	{
		QMessageBox::warning( this, QStringLiteral( "Řezy" ), QStringLiteral( "Soubor nelze otevřít: %1" ).arg( path ) );
		return;
	}
	const QByteArray data = f.readAll();
	std::vector<uint8_t> bytes( data.begin(), data.end() );
	std::vector<DgnV7::ReadItem> items;
	DgnV7::ReadStats stats;
	std::string err;
	if ( !DgnV7::readDesign( bytes, items, stats, err ) )
	{
		QMessageBox::warning( this, QStringLiteral( "Řezy" ), QString::fromStdString( err ) );
		return;
	}
	if ( items.empty() )
	{
		QMessageBox::information( this, QStringLiteral( "Řezy" ), QStringLiteral( "Ve výkresu nejsou žádné čáry, oblouky ani texty." ) );
		return;
	}

	// převod souřadnic výkresu → aktuální pohled
	const double sc = c->getGlobalScale();
	const bool plan = isPlanView( m_activeView );
	const DrawFrame fr = frameFor( m_activeView );
	SectionInfo si;
	if ( !plan && !sectionById( m_activeView, si ) )
	{
		return;
	}
	const bool absH = st.value( QStringLiteral( "qRezy/absHeights" ), false ).toBool();
	const double zeroG = m_zero->value();
	auto toUV = [&]( const DgnV7::Point2& P ) -> CCVector2d
	{
		if ( plan )
		{
			const CCVector3d L = c->toLocal3d( CCVector3d( P.x, P.y, 0.0 ) );
			return fr.toUV( CCVector3d( L.x, L.y, fr.O.z ) );
		}
		return CCVector2d( ( P.x - si.insX ) * sc, absH ? ( P.y - zeroG ) * sc : ( P.y - si.insY ) * sc );
	};

	// kontrola polohy (půdorys musí ležet u mračna)
	if ( plan )
	{
		double x0 = 1e300, y0 = 1e300, x1 = -1e300, y1 = -1e300;
		for ( const DgnV7::ReadItem& it : items )
		{
			for ( const DgnV7::Point2& P : it.pts )
			{
				x0 = std::min( x0, P.x );
				y0 = std::min( y0, P.y );
				x1 = std::max( x1, P.x );
				y1 = std::max( y1, P.y );
			}
		}
		ccBBox bb = c->getOwnBB();
		const CCVector3d gMin = c->toGlobal3d( bb.minCorner() );
		const CCVector3d gMax = c->toGlobal3d( bb.maxCorner() );
		const double m = 50.0;
		if ( x1 < gMin.x - m || x0 > gMax.x + m || y1 < gMin.y - m || y0 > gMax.y + m )
		{
			if ( QMessageBox::question( this, QStringLiteral( "Řezy" ),
			                            QStringLiteral( "Výkres leží mimo mračno (výkres X %1…%2, Y %3…%4; mračno X %5…%6, Y %7…%8).\n"
			                                            "Je výkres ve stejném souřadnicovém systému? Přesto načíst?" )
			                                .arg( x0, 0, 'f', 1 ).arg( x1, 0, 'f', 1 ).arg( y0, 0, 'f', 1 ).arg( y1, 0, 'f', 1 )
			                                .arg( gMin.x, 0, 'f', 1 ).arg( gMax.x, 0, 'f', 1 ).arg( gMin.y, 0, 'f', 1 ).arg( gMax.y, 0, 'f', 1 ) ) != QMessageBox::Yes )
			{
				return;
			}
		}
	}

	// hladiny: číslo hladiny DGN → hladina pluginu
	std::vector<Drawing::Layer> layers = Drawing::layers();
	auto findLayer = [&layers]( int level ) -> const Drawing::Layer*
	{
		for ( const Drawing::Layer& L : layers )
		{
			if ( L.sym[0].level == level || L.sym[1].level == level )
			{
				return &L;
			}
		}
		return nullptr;
	};
	struct LevelInfo
	{
		int count = 0;
		std::map<int, int> colors;
		std::map<int, int> weights;
		std::map<int, int> styles;
	};
	std::map<int, LevelInfo> unknown;
	for ( const DgnV7::ReadItem& it : items )
	{
		if ( it.isText || findLayer( it.level ) )
		{
			continue;
		}
		LevelInfo& li = unknown[it.level];
		++li.count;
		++li.colors[it.color];
		++li.weights[it.weight];
		++li.styles[it.lineStyleId];
	}
	std::map<int, int> levelToLayer;
	if ( !unknown.empty() )
	{
		QStringList lst;
		for ( const auto& u : unknown )
		{
			lst << QStringLiteral( "%1 (%2)" ).arg( u.first ).arg( u.second.count );
		}
		QMessageBox box( QMessageBox::Question, QStringLiteral( "Načíst DGN – hladiny" ),
		                 QStringLiteral( "Výkres má čáry na hladinách, které v pluginu nejsou:\n%1\n\n"
		                                 "Založit pro ně nové hladiny (stejné číslo, barva a tloušťka ve V7)? "
		                                 "Jinak se dají na aktuální hladinu „%2“." )
		                     .arg( lst.join( QStringLiteral( ", " ) ), Drawing::categoryName( m_draw->category() ) ),
		                 QMessageBox::Yes | QMessageBox::No | QMessageBox::Cancel, this );
		box.button( QMessageBox::Yes )->setText( QStringLiteral( "Založit hladiny" ) );
		box.button( QMessageBox::No )->setText( QStringLiteral( "Na aktuální hladinu" ) );
		box.button( QMessageBox::Cancel )->setText( QStringLiteral( "Zrušit" ) );
		const int r = box.exec();
		if ( r == QMessageBox::Cancel )
		{
			return;
		}
		if ( r == QMessageBox::Yes )
		{
			auto most = []( const std::map<int, int>& m )
			{
				int best = 0;
				int bc = -1;
				for ( const auto& kv : m )
				{
					if ( kv.second > bc )
					{
						bc = kv.second;
						best = kv.first;
					}
				}
				return best;
			};
			int hue = 0;
			int nextId = Drawing::newLayerId();
			for ( const auto& u : unknown )
			{
				Drawing::Layer L;
				L.id = nextId++;
				L.name = QStringLiteral( "Hladina %1" ).arg( u.first );
				L.color = QColor::fromHsv( ( hue += 47 ) % 360, 170, 235 );
				DgnV7::Symbology y;
				y.level = u.first;
				y.color = most( u.second.colors );
				y.weight = most( u.second.weights );
				const int ls = most( u.second.styles );
				y.hasLineStyle = ( ls != 0 );
				y.lineStyleId = ls;
				L.sym[0] = y;
				L.sym[1] = y;
				L.sym[1].hasLineStyle = true;
				L.sym[1].lineStyleId = -3;
				layers.push_back( L );
				levelToLayer[u.first] = L.id;
			}
			Drawing::setLayers( layers );
			refreshCatCombo();
		}
		else
		{
			for ( const auto& u : unknown )
			{
				levelToLayer[u.first] = m_draw->category();
			}
		}
	}

	QTextCodec* codec = QTextCodec::codecForName( "Windows-1250" );
	std::vector<DrawTool::ImportPrim> prims;
	prims.reserve( items.size() );
	int nLines = 0, nArcs = 0, nTexts = 0;
	for ( const DgnV7::ReadItem& it : items )
	{
		DrawTool::ImportPrim ip;
		if ( it.isText )
		{
			ip.kind = 2;
			ip.pts.push_back( toUV( it.pts.front() ) );
			ip.text = codec ? codec->toUnicode( it.text.c_str(), static_cast<int>( it.text.size() ) ) : QString::fromLatin1( it.text.c_str() );
			ip.textLevel = it.level;
			ip.textHeight = it.textHeight;
			++nTexts;
			prims.push_back( ip );
			continue;
		}
		const Drawing::Layer* L = findLayer( it.level );
		if ( L )
		{
			ip.cat = L->id;
			ip.hidden = it.lineStyleId != 0 && it.lineStyleId == L->sym[1].lineStyleId && L->sym[1].lineStyleId != L->sym[0].lineStyleId;
		}
		else
		{
			ip.cat = levelToLayer.count( it.level ) ? levelToLayer[it.level] : m_draw->category();
		}
		if ( it.isArc )
		{
			ip.kind = 1;
			const CCVector2d C = toUV( it.center );
			// poloměr a úhly v pohledu (půdorys i řez jsou shodné zobrazení, jen posun a měřítko)
			const DgnV7::Point2 E = { it.center.x + it.radius, it.center.y };
			ip.center = C;
			ip.radius = ( toUV( E ) - C ).norm();
			ip.a0 = it.startDeg * 3.14159265358979323846 / 180.0;
			ip.sweep = it.sweepDeg * 3.14159265358979323846 / 180.0;
			++nArcs;
		}
		else
		{
			ip.kind = 0;
			for ( const DgnV7::Point2& P : it.pts )
			{
				ip.pts.push_back( toUV( P ) );
			}
			if ( it.type == 6 && it.pts.size() > 2 )
			{
				ip.pts.push_back( ip.pts.front() ); // tvar je uzavřený
			}
			nLines += static_cast<int>( ip.pts.size() ) - 1;
		}
		prims.push_back( ip );
	}
	QApplication::setOverrideCursor( Qt::WaitCursor );
	const int n = m_draw->importPrims( prims );
	QApplication::restoreOverrideCursor();
	m_draw->refreshLooks();
	refresh3DLines();

	QStringList skippedTxt;
	for ( const auto& sk : stats.skippedTypes )
	{
		QString name;
		switch ( sk.first )
		{
		case 34: name = QStringLiteral( "sdílené buňky" ); break;
		case 35: name = QStringLiteral( "definice buněk" ); break;
		case 90:
		case 92:
		case 87:
		case 88: name = QStringLiteral( "rastry (typ %1)" ).arg( sk.first ); break;
		case 22: name = QStringLiteral( "řady bodů" ); break;
		case 66: name = QStringLiteral( "aplikační data" ); break;
		case 33: name = QStringLiteral( "kóty" ); break;
		case 37: name = QStringLiteral( "tagy" ); break;
		default: name = QStringLiteral( "typ %1" ).arg( sk.first ); break;
		}
		skippedTxt << QStringLiteral( "%1: %2" ).arg( name ).arg( sk.second );
	}
	const QString msg = QStringLiteral( "Načteno z %1 do „%2“:\n%3 úseček, %4 oblouků/kružnic, %5 textů (celkem %6 prvků).%7\n\n"
	                                    "Načtené prvky jdou upravovat jako nakreslené (jeden krok Zpět vrátí celý import). "
	                                    "Buňky a rastry plugin nenačítá – při ukládání proto použijte nový soubor, nebo v okně Uložit "
	                                    "zaškrtněte „uložit jen doplněné prvky“." )
	                        .arg( QFileInfo( path ).fileName(), m_viewCombo->currentText() )
	                        .arg( nLines )
	                        .arg( nArcs )
	                        .arg( nTexts )
	                        .arg( n )
	                        .arg( skippedTxt.isEmpty() ? QString() : QStringLiteral( "\nVynecháno: " ) + skippedTxt.join( QStringLiteral( ", " ) ) );
	m_app->dispToConsole( QStringLiteral( "[Řezy] " ) + msg.section( '\n', 0, 2 ) );
	QMessageBox::information( this, QStringLiteral( "Načíst DGN V7" ), msg );
	focusView();
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
			m_auxPA = CCVector2d( ax, ay );
			m_auxPB = CCVector2d( bx, by );
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
	// manažer hladin: název, barva na obrazovce a výstup V7 (plná / zakrytá čára); přidat, smazat, pořadí (F1–F4)
	std::vector<Drawing::Layer> work = Drawing::layers();
	std::vector<int> removed;

	QDialog dlg( this );
	dlg.setWindowTitle( QStringLiteral( "Hladiny – manažer" ) );
	auto* l = new QVBoxLayout( &dlg );
	auto* row = new QHBoxLayout;
	l->addLayout( row, 1 );
	auto* tw = new QTableWidget( 0, 10 );
	tw->setHorizontalHeaderLabels( { QStringLiteral( "Název" ), QStringLiteral( "Barva" ),
	                                 QStringLiteral( "Hladina" ), QStringLiteral( "Barva V7" ), QStringLiteral( "Tl." ), QStringLiteral( "Styl" ),
	                                 QStringLiteral( "Zakr. hladina" ), QStringLiteral( "Zakr. barva" ), QStringLiteral( "Zakr. tl." ), QStringLiteral( "Zakr. styl" ) } );
	tw->verticalHeader()->setVisible( true );
	tw->setSelectionBehavior( QAbstractItemView::SelectRows );
	tw->setSelectionMode( QAbstractItemView::SingleSelection );
	row->addWidget( tw, 1 );
	auto* side = new QVBoxLayout;
	auto* addB = new QPushButton( QStringLiteral( "Přidat" ) );
	auto* delB = new QPushButton( QStringLiteral( "Smazat" ) );
	auto* upB = new QPushButton( QStringLiteral( "Nahoru" ) );
	auto* downB = new QPushButton( QStringLiteral( "Dolů" ) );
	auto* defB = new QPushButton( QStringLiteral( "Výchozí" ) );
	for ( QPushButton* b : { addB, delB, upB, downB, defB } )
	{
		side->addWidget( b );
	}
	side->addStretch( 1 );
	row->addLayout( side );

	auto spinAt = [tw]( int r, int c ) { return static_cast<QSpinBox*>( tw->cellWidget( r, c ) ); };
	// tabulka → seznam
	auto sync = [&]()
	{
		for ( int r = 0; r < tw->rowCount() && r < static_cast<int>( work.size() ); ++r )
		{
			Drawing::Layer& L = work[static_cast<size_t>( r )];
			if ( QTableWidgetItem* it = tw->item( r, 0 ) )
			{
				const QString n = it->text().trimmed();
				if ( !n.isEmpty() )
				{
					L.name = n;
				}
			}
			L.color = QColor( static_cast<QPushButton*>( tw->cellWidget( r, 1 ) )->property( "col" ).toString() );
			for ( int h = 0; h < 2; ++h )
			{
				DgnV7::Symbology& y = L.sym[static_cast<size_t>( h )];
				y.level = spinAt( r, 2 + 4 * h )->value();
				y.color = spinAt( r, 3 + 4 * h )->value();
				y.weight = spinAt( r, 4 + 4 * h )->value();
				const int ls = spinAt( r, 5 + 4 * h )->value();
				y.hasLineStyle = ( ls != 0 );
				y.lineStyleId = ls;
			}
		}
	};
	// seznam → tabulka
	std::function<void( int )> fill = [&]( int selRow )
	{
		tw->setRowCount( static_cast<int>( work.size() ) );
		QStringList vh;
		for ( int r = 0; r < static_cast<int>( work.size() ); ++r )
		{
			const Drawing::Layer& L = work[static_cast<size_t>( r )];
			vh << ( r < 4 ? QStringLiteral( "F%1" ).arg( r + 1 ) : QString() );
			tw->setItem( r, 0, new QTableWidgetItem( L.name ) );
			auto* cb = new QPushButton;
			cb->setProperty( "col", L.color.name() );
			cb->setIcon( Icons::colorSwatch( L.color, false ) );
			cb->setToolTip( QStringLiteral( "Barva čáry na obrazovce" ) );
			connect( cb, &QPushButton::clicked, &dlg, [cb, &dlg]()
			{
				const QColor c = QColorDialog::getColor( QColor( cb->property( "col" ).toString() ), &dlg );
				if ( c.isValid() )
				{
					cb->setProperty( "col", c.name() );
					cb->setIcon( Icons::colorSwatch( c, false ) );
				}
			} );
			tw->setCellWidget( r, 1, cb );
			for ( int h = 0; h < 2; ++h )
			{
				const DgnV7::Symbology& y = L.sym[static_cast<size_t>( h )];
				auto spin = [&]( int col, int mn, int mx, int v )
				{
					auto* sb = new QSpinBox;
					sb->setRange( mn, mx );
					sb->setValue( v );
					tw->setCellWidget( r, col, sb );
				};
				spin( 2 + 4 * h, 1, 63, y.level );
				spin( 3 + 4 * h, 0, 255, y.color );
				spin( 4 + 4 * h, 0, 31, y.weight );
				spin( 5 + 4 * h, -1000, 1000, y.hasLineStyle ? y.lineStyleId : 0 );
			}
		}
		tw->setVerticalHeaderLabels( vh );
		tw->resizeColumnsToContents();
		tw->setColumnWidth( 0, std::max( 140, tw->columnWidth( 0 ) ) );
		if ( selRow >= 0 && selRow < tw->rowCount() )
		{
			tw->selectRow( selRow );
		}
	};
	fill( 0 );
	tw->setMinimumWidth( 860 );
	tw->setMinimumHeight( 260 );

	connect( addB, &QPushButton::clicked, &dlg, [&]()
	{
		sync();
		Drawing::Layer L;
		L.id = Drawing::newLayerId();
		for ( const Drawing::Layer& x : work )
		{
			L.id = std::max( L.id, x.id + 1 );
		}
		for ( int id : removed )
		{
			L.id = std::max( L.id, id + 1 );
		}
		L.name = QStringLiteral( "Nová hladina" );
		L.color = QColor( 255, 160, 0 );
		int lvl = 7;
		for ( const Drawing::Layer& x : work )
		{
			lvl = std::max( lvl, x.sym[0].level + 1 );
		}
		L.sym[0].level = std::min( 63, lvl );
		L.sym[0].hasLineStyle = true;
		L.sym[0].lineStyleId = -1;
		L.sym[1] = L.sym[0];
		L.sym[1].lineStyleId = -3;
		work.push_back( L );
		fill( static_cast<int>( work.size() ) - 1 );
		tw->editItem( tw->item( static_cast<int>( work.size() ) - 1, 0 ) );
	} );
	connect( delB, &QPushButton::clicked, &dlg, [&]()
	{
		const int r = tw->currentRow();
		if ( r < 0 || r >= static_cast<int>( work.size() ) )
		{
			return;
		}
		if ( work.size() <= 1 )
		{
			QMessageBox::information( &dlg, QStringLiteral( "Hladiny" ), QStringLiteral( "Musí zůstat aspoň jedna hladina." ) );
			return;
		}
		sync();
		removed.push_back( work[static_cast<size_t>( r )].id );
		work.erase( work.begin() + r );
		fill( std::min( r, static_cast<int>( work.size() ) - 1 ) );
	} );
	auto moveRow = [&]( int dir )
	{
		const int r = tw->currentRow();
		const int t = r + dir;
		if ( r < 0 || t < 0 || t >= static_cast<int>( work.size() ) )
		{
			return;
		}
		sync();
		std::swap( work[static_cast<size_t>( r )], work[static_cast<size_t>( t )] );
		fill( t );
	};
	connect( upB, &QPushButton::clicked, &dlg, [&]() { moveRow( -1 ); } );
	connect( downB, &QPushButton::clicked, &dlg, [&]() { moveRow( +1 ); } );
	connect( defB, &QPushButton::clicked, &dlg, [&]()
	{
		if ( QMessageBox::question( &dlg, QStringLiteral( "Hladiny" ), QStringLiteral( "Vrátit výchozí čtyři hladiny (Zdi, Překlady, Trámy, Drobné prvky)? Přidané hladiny se smažou." ) ) != QMessageBox::Yes )
		{
			return;
		}
		for ( const Drawing::Layer& x : work )
		{
			if ( x.id > Drawing::Details )
			{
				removed.push_back( x.id );
			}
		}
		work = Drawing::defaultLayers();
		fill( 0 );
	} );

	l->addWidget( new QLabel( QStringLiteral( "<small>Pořadí = klávesy F1–F4. Styl čáry: -1 plná, -2 přerušovaná, -3 zakrytá (podle vašeho seedu); 0 = bez vlastního stylu. "
	                                          "Barva V7 = číslo barvy MicroStationu (0–255), Barva = jen zobrazení zde.</small>" ) ) );
	auto* bb = new QDialogButtonBox( QDialogButtonBox::Ok | QDialogButtonBox::Cancel );
	l->addWidget( bb );
	connect( bb, &QDialogButtonBox::accepted, &dlg, &QDialog::accept );
	connect( bb, &QDialogButtonBox::rejected, &dlg, &QDialog::reject );

	if ( dlg.exec() != QDialog::Accepted )
	{
		return;
	}
	sync();

	// smazané hladiny s nakreslenými čarami → přesunout jinam (názvy ještě ze starého seznamu)
	struct Gone
	{
		int id;
		QString name;
		int count;
	};
	std::vector<Gone> gone;
	for ( int id : removed )
	{
		if ( std::any_of( work.begin(), work.end(), [id]( const Drawing::Layer& x ) { return x.id == id; } ) )
		{
			continue; // vrácená výchozí hladina
		}
		const int n = m_draw->countCategory( id );
		if ( n > 0 )
		{
			gone.push_back( { id, Drawing::categoryName( id ), n } );
		}
	}
	Drawing::setLayers( work );
	for ( const Gone& g : gone )
	{
		QStringList names;
		for ( const Drawing::Layer& x : work )
		{
			names << x.name;
		}
		bool ok = false;
		const QString pick = QInputDialog::getItem( this, QStringLiteral( "Smazaná hladina" ),
		                                            QStringLiteral( "Na smazané hladině „%1“ je %2 čar. Přesunout je na hladinu:" ).arg( g.name ).arg( g.count ),
		                                            names, 0, false, &ok );
		const int idx = ok ? names.indexOf( pick ) : 0;
		m_draw->moveCategory( g.id, work[static_cast<size_t>( std::max( 0, idx ) )].id );
	}
	refreshCatCombo();
	m_draw->refreshLooks();
	refresh3DLines();
}
