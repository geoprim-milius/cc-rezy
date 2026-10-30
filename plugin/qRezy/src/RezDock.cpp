#include "RezDock.h"
#include "DgnV7.h"
#include "DrawTool.h"
#include "Drawing.h"
#include "View3D.h"
#include "RezLine.h"

#include "ccMainAppInterface.h"
#include "ccGLWindowInterface.h"
#include "ccGLUtils.h"

#include <ccGenericPointCloud.h>
#include <ccHObjectCaster.h>
#include <ccPointCloud.h>

#include <QApplication>
#include <QButtonGroup>
#include <QCheckBox>
#include <QGridLayout>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QGroupBox>
#include <QHeaderView>
#include <QMessageBox>
#include <QScrollArea>
#include <QScrollBar>
#include <QSettings>
#include <QSpinBox>
#include <QAbstractSpinBox>
#include <QTableWidget>
#include <QTextCodec>
#include <QCloseEvent>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QMainWindow>
#include <QPushButton>
#include <QShortcut>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>
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
}

RezDock::RezDock( ccMainAppInterface* app, QWidget* parent )
	: QDockWidget( QStringLiteral( "Řezy" ), parent )
	, m_app( app )
{
	setObjectName( QStringLiteral( "qRezyDock" ) );

	auto* w = new QWidget;
	auto* lay = new QVBoxLayout( w );

	// mračno
	m_cloudLabel = new QLabel( QStringLiteral( "Mračno: (žádné)" ) );
	m_cloudLabel->setWordWrap( true );
	m_useSelectedBtn = new QPushButton( QStringLiteral( "Použít vybrané mračno" ) );
	lay->addWidget( m_cloudLabel );
	lay->addWidget( m_useSelectedBtn );

	// pohled: půdorys / svislé řezy
	auto* viewBox = new QGroupBox( QStringLiteral( "Pohled" ) );
	auto* vl = new QVBoxLayout( viewBox );
	m_viewCombo = new QComboBox;
	m_viewCombo->addItem( QStringLiteral( "Půdorys" ), 0 );
	vl->addWidget( m_viewCombo );
	auto* secRow = new QHBoxLayout;
	auto* newSecBtn = new QPushButton( QStringLiteral( "Nový svislý řez (S)" ) );
	newSecBtn->setToolTip( QStringLiteral( "V půdorysu klikněte 2 body čáry řezu a pak stranu, kam se díváte" ) );
	auto* delSecBtn = new QPushButton( QStringLiteral( "Smazat řez" ) );
	secRow->addWidget( newSecBtn );
	secRow->addWidget( delSecBtn );
	vl->addLayout( secRow );
	m_secWidgets = new QWidget;
	auto* sf = new QFormLayout( m_secWidgets );
	sf->setContentsMargins( 0, 0, 0, 0 );
	m_insX = makeSpin( -1.0e8, 1.0e8, 0.0, 1.0, 3, QStringLiteral( " m" ) );
	m_insY = makeSpin( -1.0e8, 1.0e8, 0.0, 1.0, 3, QStringLiteral( " m" ) );
	sf->addRow( QStringLiteral( "Ve výkresu X:" ), m_insX );
	sf->addRow( QStringLiteral( "Ve výkresu Y:" ), m_insY );
	m_absHeights = new QCheckBox( QStringLiteral( "Výšky absolutně (Bpv)" ) );
	m_absHeights->setToolTip( QStringLiteral( "Jinak relativně k ±0,000 (do výkresu se připíše ±0,000 = … Bpv)" ) );
	sf->addRow( m_absHeights );
	vl->addWidget( m_secWidgets );
	lay->addWidget( viewBox );

	// řez
	m_enabled = new QCheckBox( QStringLiteral( "Řez zapnutý (ořez mračna)" ) );
	m_enabled->setChecked( true );
	lay->addWidget( m_enabled );

	auto* form = new QFormLayout;
	m_height = makeSpin( -1.0e6, 1.0e6, 0.0, 0.05, 3, QStringLiteral( " m" ) );
	m_thickness = makeSpin( 0.005, 100.0, 0.10, 0.01, 3, QStringLiteral( " m" ) );
	m_step = makeSpin( 0.001, 100.0, 0.05, 0.01, 3, QStringLiteral( " m" ) );
	m_zero = makeSpin( -1.0e6, 1.0e6, 0.0, 0.01, 3, QStringLiteral( " m" ) );
	form->addRow( QStringLiteral( "Výška řezu (abs.):" ), m_height );
	form->addRow( QStringLiteral( "Tloušťka:" ), m_thickness );
	form->addRow( QStringLiteral( "Krok PgUp/PgDn:" ), m_step );
	form->addRow( QStringLiteral( "±0,000 =" ), m_zero );
	lay->addLayout( form );

	m_relLabel = new QLabel;
	QFont f = m_relLabel->font();
	f.setPointSizeF( f.pointSizeF() * 1.6 );
	f.setBold( true );
	m_relLabel->setFont( f );
	m_relLabel->setAlignment( Qt::AlignCenter );
	lay->addWidget( m_relLabel );

	auto* row = new QHBoxLayout;
	auto* upBtn = new QPushButton( QStringLiteral( "▲ Nahoru (PgUp)" ) );
	auto* downBtn = new QPushButton( QStringLiteral( "▼ Dolů (PgDn)" ) );
	upBtn->setObjectName( QStringLiteral( "qRezyUp" ) );
	downBtn->setObjectName( QStringLiteral( "qRezyDown" ) );
	row->addWidget( upBtn );
	row->addWidget( downBtn );
	lay->addLayout( row );

	auto* topBtn = new QPushButton( QStringLiteral( "Pohled shora" ) );
	auto* floorBtn = new QPushButton( QStringLiteral( "±0,000 = podlaha (odhad)" ) );
	floorBtn->setToolTip( QStringLiteral( "Nastaví nulu na nejhustší vodorovnou plochu v mračnu (obvykle podlaha)" ) );
	lay->addWidget( topBtn );
	lay->addWidget( floorBtn );

	// --- kreslení
	auto* drawBox = new QGroupBox( QStringLiteral( "Kreslení" ) );
	auto* dl = new QVBoxLayout( drawBox );
	m_drawBtn = new QPushButton( QStringLiteral( "✎ Kreslit nad řezem" ) );
	m_drawBtn->setCheckable( true );
	dl->addWidget( m_drawBtn );

	auto* df = new QFormLayout;
	m_catCombo = new QComboBox;
	for ( int c = 0; c < Drawing::CategoryCount; ++c )
	{
		m_catCombo->addItem( QStringLiteral( "F%1  %2" ).arg( c + 1 ).arg( Drawing::categoryName( c ) ) );
	}
	df->addRow( QStringLiteral( "Hladina:" ), m_catCombo );
	dl->addLayout( df );

	m_hiddenChk = new QCheckBox( QStringLiteral( "Zakrytá / nad řezem (F5)" ) );
	m_orthoChk = new QCheckBox( QStringLiteral( "Ortogonálně podle pohledu (F8)" ) );
	dl->addWidget( m_hiddenChk );
	dl->addWidget( m_orthoChk );

	// nástroje
	auto* toolGrid = new QGridLayout;
	m_toolGroup = new QButtonGroup( this );
	m_toolGroup->setExclusive( true );
	struct ToolDef
	{
		DrawTool::Mode mode;
		const char* text;
		const char* tip;
	};
	const ToolDef tools[] = {
		{ DrawTool::Mode::Line, "Čára (L)", "Navazující úsečky; pravé kliknutí ukončí" },
		{ DrawTool::Mode::Move, "Přesun (M)", "Čára → výchozí bod → cílový bod (nebo vzdálenost)" },
		{ DrawTool::Mode::Copy, "Kopie (C)", "Čára → výchozí bod → cílové body (nebo vzdálenost)" },
		{ DrawTool::Mode::Offset, "Rovnoběžně (O)", "Čára → strana; vzdálenost podle pole níže nebo napsat" },
		{ DrawTool::Mode::Extend, "Prodloužit (E)", "Klik u konce čáry → nový konec nebo celková délka" },
		{ DrawTool::Mode::Corner, "Roh (R)", "Dvě čáry do průsečíku; klikejte na části, které zůstanou" },
		{ DrawTool::Mode::Delete, "Mazat (Del)", "Klik na čáru ji smaže" },
	};
	int idx = 0;
	for ( const ToolDef& t : tools )
	{
		auto* b = new QPushButton( QString::fromUtf8( t.text ) );
		b->setToolTip( QString::fromUtf8( t.tip ) );
		b->setCheckable( true );
		m_toolGroup->addButton( b, static_cast<int>( t.mode ) );
		toolGrid->addWidget( b, idx / 3, idx % 3 );
		++idx;
	}
	auto* undoBtn = new QPushButton( QStringLiteral( "Zpět (Ctrl+Z)" ) );
	toolGrid->addWidget( undoBtn, idx / 3, idx % 3 );
	dl->addLayout( toolGrid );
	m_toolGroup->button( static_cast<int>( DrawTool::Mode::Line ) )->setChecked( true );

	auto* of = new QFormLayout;
	m_offsetSpin = makeSpin( 0.0, 1000.0, 0.0, 0.05, 3, QStringLiteral( " m" ) );
	m_offsetSpin->setSpecialValueText( QStringLiteral( "bodem (myší)" ) );
	of->addRow( QStringLiteral( "Vzdálenost rovnoběžky:" ), m_offsetSpin );
	dl->addLayout( of );

	auto* vrow = new QHBoxLayout;
	auto* rotBtn = new QPushButton( QStringLiteral( "Natočit pohled podle čáry" ) );
	rotBtn->setToolTip( QStringLiteral( "Klikněte potom na čáru – pohled se natočí, aby byla vodorovně. Souřadnice zůstávají." ) );
	auto* northBtn = new QPushButton( QStringLiteral( "Sever nahoru" ) );
	vrow->addWidget( rotBtn );
	vrow->addWidget( northBtn );
	dl->addLayout( vrow );

	m_status = new QLabel;
	m_status->setWordWrap( true );
	m_status->setStyleSheet( QStringLiteral( "color: #444;" ) );
	m_status->setMinimumHeight( 40 );
	dl->addWidget( m_status );

	auto* help = new QLabel( QStringLiteral(
		"<small>Levé = bod, pravé kliknutí = reset, pravé tažení = posun, kolečko = zoom, "
		"<b>prostřední kliknutí = ukázat místo ve 3D</b>.<br>"
		"Délka/vzdálenost: napište číslo a Enter. Esc = zrušit / zpět na čáru.</small>" ) );
	help->setWordWrap( true );
	dl->addWidget( help );
	lay->addWidget( drawBox );

	// --- 3D náhled
	auto* box3D = new QGroupBox( QStringLiteral( "3D náhled" ) );
	auto* l3 = new QFormLayout( box3D );
	m_radius3D = makeSpin( 0.2, 100.0, 1.5, 0.25, 2, QStringLiteral( " m" ) );
	l3->addRow( QStringLiteral( "Okolí (poloměr):" ), m_radius3D );
	auto* open3DBtn = new QPushButton( QStringLiteral( "Otevřít 3D okno" ) );
	l3->addRow( open3DBtn );
	lay->addWidget( box3D );

	// --- výstup
	auto* outBox = new QGroupBox( QStringLiteral( "Výstup MicroStation V7" ) );
	auto* ol = new QVBoxLayout( outBox );
	m_seedLabel = new QLabel;
	m_seedLabel->setWordWrap( true );
	ol->addWidget( m_seedLabel );
	auto* orow = new QHBoxLayout;
	auto* seedBtn = new QPushButton( QStringLiteral( "Seed výkres…" ) );
	auto* symBtn = new QPushButton( QStringLiteral( "Hladiny a barvy…" ) );
	orow->addWidget( seedBtn );
	orow->addWidget( symBtn );
	ol->addLayout( orow );
	auto* tf = new QFormLayout;
	m_textHeight = makeSpin( 0.01, 10.0, 0.25, 0.05, 2, QStringLiteral( " m" ) );
	tf->addRow( QStringLiteral( "Výška textu (popisy řezů):" ), m_textHeight );
	ol->addLayout( tf );
	auto* exportBtn = new QPushButton( QStringLiteral( "Uložit výkres V7 (.dgn)…" ) );
	ol->addWidget( exportBtn );
	lay->addWidget( outBox );

	lay->addStretch( 1 );

	m_draw = new DrawTool( m_app, this );
	connect( m_drawBtn, &QPushButton::toggled, this, &RezDock::toggleDrawing );
	connect( m_catCombo, qOverload<int>( &QComboBox::currentIndexChanged ), this, [this]( int i ) { m_draw->setCategory( i ); focusView(); } );
	connect( m_hiddenChk, &QCheckBox::toggled, this, [this]( bool b ) { m_draw->setHidden( b ); focusView(); } );
	connect( m_orthoChk, &QCheckBox::toggled, this, [this]( bool b ) { m_draw->setOrtho( b ); focusView(); } );
	connect( m_toolGroup, qOverload<int>( &QButtonGroup::buttonClicked ), this, [this]( int id ) { m_draw->setMode( static_cast<DrawTool::Mode>( id ) ); focusView(); } );
	connect( m_draw, &DrawTool::modeChanged, this, [this]( DrawTool::Mode m )
	{
		if ( QAbstractButton* b = m_toolGroup->button( static_cast<int>( m ) ) )
		{
			const QSignalBlocker bl( m_toolGroup );
			b->setChecked( true );
		}
		else
		{
			// dočasný režim (natočení pohledu) – žádné tlačítko
			const bool ex = m_toolGroup->exclusive();
			m_toolGroup->setExclusive( false );
			if ( m_toolGroup->checkedButton() )
				m_toolGroup->checkedButton()->setChecked( false );
			m_toolGroup->setExclusive( ex );
		}
	} );
	connect( m_offsetSpin, qOverload<double>( &QDoubleSpinBox::valueChanged ), this, [this]( double v ) { m_draw->setOffsetDistance( v ); } );
	connect( undoBtn, &QPushButton::clicked, this, [this]() { m_draw->undo(); focusView(); } );
	connect( rotBtn, &QPushButton::clicked, this, [this]()
	{
		if ( !m_draw->isActive() )
		{
			m_drawBtn->setChecked( true );
		}
		m_draw->setMode( DrawTool::Mode::RotateView );
		focusView();
	} );
	connect( northBtn, &QPushButton::clicked, this, [this]() { m_viewAngle = 0.0; topView(); focusView(); } );
	connect( m_draw, &DrawTool::rotateViewRequested, this, [this]( double a ) { m_viewAngle = a; topView(); focusView(); } );
	connect( m_draw, &DrawTool::show3DRequested, this, &RezDock::show3DAt );
	connect( m_draw, &DrawTool::sectionDefined, this, &RezDock::createSection );
	connect( newSecBtn, &QPushButton::clicked, this, [this]()
	{
		if ( m_activeView != 0 )
		{
			activateView( 0 );
		}
		if ( !m_draw->isActive() )
		{
			m_drawBtn->setChecked( true );
		}
		m_draw->setMode( DrawTool::Mode::SectionDef );
		focusView();
	} );
	connect( delSecBtn, &QPushButton::clicked, this, &RezDock::deleteActiveSection );
	connect( m_viewCombo, qOverload<int>( &QComboBox::activated ), this, [this]( int i ) { activateView( m_viewCombo->itemData( i ).toInt() ); focusView(); } );
	auto saveIns = [this]()
	{
		SectionInfo si;
		if ( sectionById( m_activeView, si ) )
		{
			si.insX = m_insX->value();
			si.insY = m_insY->value();
			saveSection( si );
		}
	};
	connect( m_insX, qOverload<double>( &QDoubleSpinBox::valueChanged ), this, saveIns );
	connect( m_insY, qOverload<double>( &QDoubleSpinBox::valueChanged ), this, saveIns );
	connect( m_absHeights, &QCheckBox::toggled, this, []( bool b ) { QSettings().setValue( QStringLiteral( "qRezy/absHeights" ), b ); } );
	connect( m_textHeight, qOverload<double>( &QDoubleSpinBox::valueChanged ), this, []( double v ) { QSettings().setValue( QStringLiteral( "qRezy/textHeight" ), v ); } );
	{
		QSettings st;
		m_absHeights->setChecked( st.value( QStringLiteral( "qRezy/absHeights" ), false ).toBool() );
		m_textHeight->setValue( st.value( QStringLiteral( "qRezy/textHeight" ), 0.25 ).toDouble() );
	}
	updateSectionWidgets();
	connect( open3DBtn, &QPushButton::clicked, this, [this]() { ensure3D(); } );
	connect( m_draw, &DrawTool::statusChanged, m_status, &QLabel::setText );
	connect( m_draw, &DrawTool::categoryChanged, this, [this]( int c ) { const QSignalBlocker b( m_catCombo ); m_catCombo->setCurrentIndex( c ); } );
	connect( m_draw, &DrawTool::hiddenChanged, this, [this]( bool h ) { const QSignalBlocker b( m_hiddenChk ); m_hiddenChk->setChecked( h ); } );
	connect( m_draw, &DrawTool::orthoChanged, this, [this]( bool o ) { const QSignalBlocker b( m_orthoChk ); m_orthoChk->setChecked( o ); } );
	connect( seedBtn, &QPushButton::clicked, this, &RezDock::chooseSeed );
	connect( symBtn, &QPushButton::clicked, this, &RezDock::editSymbology );
	connect( exportBtn, &QPushButton::clicked, this, &RezDock::exportV7 );

	{
		QSettings st;
		const QString seed = st.value( QStringLiteral( "qRezy/seed" ) ).toString();
		m_seedLabel->setText( seed.isEmpty() ? QStringLiteral( "Seed: (nevybrán)" ) : QStringLiteral( "Seed: %1" ).arg( QFileInfo( seed ).fileName() ) );
		m_seedLabel->setToolTip( seed );
	}

	// panel je vysoký – na menších obrazovkách se posouvá
	auto* scroll = new QScrollArea;
	scroll->setWidgetResizable( true );
	scroll->setFrameShape( QFrame::NoFrame );
	scroll->setHorizontalScrollBarPolicy( Qt::ScrollBarAlwaysOff );
	scroll->setWidget( w );
	setWidget( scroll );

	// kolečko myši nad poli nemá měnit hodnoty, když se jen posouvá panel
	for ( QWidget* ch : w->findChildren<QWidget*>() )
	{
		if ( qobject_cast<QAbstractSpinBox*>( ch ) || qobject_cast<QComboBox*>( ch ) )
		{
			ch->setFocusPolicy( Qt::StrongFocus );
			ch->installEventFilter( this );
		}
	}

	// klávesy fungují v celém okně CloudCompare
	m_up = new QShortcut( QKeySequence( Qt::Key_PageUp ), parent );
	m_down = new QShortcut( QKeySequence( Qt::Key_PageDown ), parent );
	m_up->setContext( Qt::ApplicationShortcut );
	m_down->setContext( Qt::ApplicationShortcut );

	connect( m_up, &QShortcut::activated, this, [this]() { step( +1 ); } );
	connect( m_down, &QShortcut::activated, this, [this]() { step( -1 ); } );
	connect( upBtn, &QPushButton::clicked, this, [this]() { step( +1 ); } );
	connect( downBtn, &QPushButton::clicked, this, [this]() { step( -1 ); } );
	connect( m_useSelectedBtn, &QPushButton::clicked, this, &RezDock::useSelected );
	connect( topBtn, &QPushButton::clicked, this, &RezDock::topView );
	connect( floorBtn, &QPushButton::clicked, this, &RezDock::zeroFromFloor );
	connect( m_enabled, &QCheckBox::toggled, this, [this]() { applyClip(); } );
	connect( m_height, qOverload<double>( &QDoubleSpinBox::valueChanged ), this, [this]() { applyClip(); } );
	connect( m_thickness, qOverload<double>( &QDoubleSpinBox::valueChanged ), this, [this]() { applyClip(); } );
	connect( m_zero, qOverload<double>( &QDoubleSpinBox::valueChanged ), this, [this]()
	{
		updateLabels();
		if ( m_activeView != 0 )
		{
			m_draw->setFrame( frameFor( m_activeView ) );
		}
	} );

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

void RezDock::closeEvent( QCloseEvent* event )
{
	if ( m_drawBtn )
	{
		m_drawBtn->setChecked( false );
	}
	clearClip();
	m_cloudID = 0;
	updateLabels();
	QDockWidget::closeEvent( event );
}

void RezDock::onSelectionChanged( const ccHObject::Container& selected )
{
	m_lastSelection = selected;

	// pokud ještě nemáme mračno, vezmeme první vybrané
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
	if ( !c )
	{
		return;
	}
	if ( c->getUniqueID() == m_cloudID )
	{
		return;
	}

	clearClip();
	m_cloudID = c->getUniqueID();
	m_draw->setCloud( c );
	m_activeView = 0;
	m_draw->setFrame( frameFor( 0 ) );
	refreshViewCombo();

	const double floorZ = estimateFloor( c );

	const QSignalBlocker b1( m_height );
	const QSignalBlocker b2( m_zero );
	m_zero->setValue( floorZ );
	m_height->setValue( floorZ + 1.0 );

	m_app->dispToConsole( QStringLiteral( "[Řezy] Mračno %1, odhad podlahy %2 m" ).arg( c->getName() ).arg( floorZ, 0, 'f', 3 ) );

	applyClip();
	topView();

	if ( m_draw && m_draw->isActive() )
	{
		m_draw->start( c );
	}
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
		size_t k = static_cast<size_t>( ( P->z - zMinL ) / bin );
		if ( k < bins )
		{
			++h[k];
		}
	}
	size_t best = std::max_element( h.begin(), h.end() ) - h.begin();
	CCVector3d local( 0.0, 0.0, zMinL + ( best + 0.5 ) * bin );
	return c->toGlobal3d( local ).z;
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
	// svislý řez: PgUp = dál ve směru pohledu, PgDn = zpět
	SectionInfo si;
	if ( sectionById( m_activeView, si ) )
	{
		si.offset += direction * m_step->value();
		saveSection( si );
		applyClip();
	}
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
			const double half = m_thickness->value() * sc / 2.0;
			// rovina řezu: O − N·offset (posun ve směru pohledu)
			const double d = f.O.dot( f.N ) - si.offset * sc;
			ccClipPlane p1;
			p1.equation = Tuple4Tpl<double>( f.N.x, f.N.y, f.N.z, -( d - half ) );
			ccClipPlane p2;
			p2.equation = Tuple4Tpl<double>( -f.N.x, -f.N.y, -f.N.z, d + half );
			c->addClipPlanes( p1 );
			c->addClipPlanes( p2 );
			// do stran jen v délce čáry řezu (+ 20 cm)
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
	m_cloudLabel->setText( c ? QStringLiteral( "Mračno: %1 (%2 bodů)" ).arg( c->getName() ).arg( c->size() )
	                         : QStringLiteral( "Mračno: (žádné – vyberte mračno v seznamu)" ) );
	SectionInfo si;
	const bool sec = m_activeView != 0 && sectionById( m_activeView, si );
	if ( sec )
	{
		m_relLabel->setText( QStringLiteral( "%1  posun %2" ).arg( si.name, fmt( si.offset ) ) );
	}
	else
	{
		m_relLabel->setText( QStringLiteral( "Řez %1" ).arg( fmt( m_height->value() - m_zero->value() ) ) );
	}
	if ( auto* up = findChild<QPushButton*>( QStringLiteral( "qRezyUp" ) ) )
	{
		up->setText( sec ? QStringLiteral( "▲ Dál (PgUp)" ) : QStringLiteral( "▲ Nahoru (PgUp)" ) );
	}
	if ( auto* down = findChild<QPushButton*>( QStringLiteral( "qRezyDown" ) ) )
	{
		down->setText( sec ? QStringLiteral( "▼ Blíž (PgDn)" ) : QStringLiteral( "▼ Dolů (PgDn)" ) );
	}
	m_height->setEnabled( !sec );
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
	if ( m_draw && m_draw->isActive() )
	{
		m_status->setText( QStringLiteral( "Pohled natočen o %1°" ).arg( QString::number( m_viewAngle * 57.29577951308232, 'f', 2 ).replace( '.', ',' ) ) );
	}
}

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
		m_view3D->resize( 700, 550 );
	}
	m_view3D->show();
	m_view3D->raise();
	QApplication::setOverrideCursor( Qt::WaitCursor );
	const bool ok = m_view3D->setSource( pc, 20000000 );
	QApplication::restoreOverrideCursor();
	return ok;
}

void RezDock::show3DAt( double x, double y, double z )
{
	if ( !ensure3D() )
	{
		return;
	}
	ccGenericPointCloud* c = cloud();
	const double zLocal = std::isnan( z ) ? ( c ? c->toLocal3d( CCVector3d( 0, 0, m_height->value() ) ).z : 0.0 ) : z;
	double ang = m_viewAngle;
	SectionInfo si;
	if ( m_activeView != 0 && sectionById( m_activeView, si ) )
	{
		// pohled z místa pozorovatele řezu
		ang = std::atan2( -si.N.x, si.N.y );
	}
	m_view3D->focusOn( CCVector3d( x, y, zLocal ), m_radius3D->value() * ( c ? c->getGlobalScale() : 1.0 ), ang );
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

void RezDock::toggleDrawing( bool on )
{
	if ( !on )
	{
		m_draw->stop();
		m_drawBtn->setText( QStringLiteral( "✎ Kreslit nad řezem" ) );
		return;
	}
	ccGenericPointCloud* c = cloud();
	if ( !c )
	{
		m_app->dispToConsole( QStringLiteral( "[Řezy] Nejdřív vyberte mračno." ), ccMainAppInterface::WRN_CONSOLE_MESSAGE );
		const QSignalBlocker b( m_drawBtn );
		m_drawBtn->setChecked( false );
		return;
	}
	topView();
	if ( !m_draw->start( c ) )
	{
		const QSignalBlocker b( m_drawBtn );
		m_drawBtn->setChecked( false );
		return;
	}
	m_draw->setCategory( m_catCombo->currentIndex() );
	m_draw->setHidden( m_hiddenChk->isChecked() );
	m_draw->setOrtho( m_orthoChk->isChecked() );
	m_drawBtn->setText( QStringLiteral( "■ Ukončit kreslení" ) );
	focusView();
}

void RezDock::chooseSeed()
{
	QSettings st;
	const QString old = st.value( QStringLiteral( "qRezy/seed" ) ).toString();
	const QString f = QFileDialog::getOpenFileName( this, QStringLiteral( "Vyberte SEED – existující výkres V7 (šablona, nemění se)" ), old, QStringLiteral( "MicroStation V7 (*.dgn);;Všechny soubory (*)" ) );
	if ( f.isEmpty() )
	{
		return;
	}
	QFile file( f );
	if ( !file.open( QIODevice::ReadOnly ) )
	{
		QMessageBox::warning( this, QStringLiteral( "Řezy" ), QStringLiteral( "Soubor nelze otevřít." ) );
		return;
	}
	const QByteArray data = file.readAll();
	std::vector<uint8_t> bytes( data.begin(), data.end() );
	DgnV7::Writer w;
	std::string err;
	if ( !w.loadSeedBytes( bytes, err ) )
	{
		QMessageBox::warning( this, QStringLiteral( "Řezy" ), QString::fromStdString( err ) );
		return;
	}
	st.setValue( QStringLiteral( "qRezy/seed" ), f );
	m_seedLabel->setText( QStringLiteral( "Seed: %1 (%2, %3 UOR/m)" ).arg( QFileInfo( f ).fileName(), w.is3D() ? QStringLiteral( "3D" ) : QStringLiteral( "2D" ) ).arg( w.uorPerMaster() ) );
	m_seedLabel->setToolTip( f );
}

void RezDock::exportV7()
{
	const Drawing::SymbologyTable table = Drawing::SymbologyTable::load();
	ccGenericPointCloud* c = cloud();
	if ( !c )
	{
		return;
	}
	const CCVector3d shift = c->getGlobalShift();
	const double sc = c->getGlobalScale();
	const bool absH = m_absHeights->isChecked();
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
	std::vector<DgnV7::Element> elements = m_draw->exportElements( table, map );

	// popisy řezů
	QTextCodec* codec = QTextCodec::codecForName( "Windows-1250" );
	for ( const SectionInfo& si : sections )
	{
		if ( std::find( usedViews.begin(), usedViews.end(), si.id ) == usedViews.end() )
		{
			continue;
		}
		const double th = m_textHeight->value();
		const double y0 = absH ? zeroG : si.insY;
		QStringList lines;
		lines << QStringLiteral( "Řez %1" ).arg( si.name );
		if ( !absH )
		{
			lines << QStringLiteral( "±0,000 = %1 m n. m. Bpv" ).arg( QString::number( zeroG, 'f', 3 ).replace( '.', ',' ) );
		}
		for ( int i = 0; i < lines.size(); ++i )
		{
			DgnV7::Element t;
			t.isText = true;
			t.sym = table.get( 0, false );
			t.sym.level = 14;
			t.sym.color = 0;
			t.sym.weight = 0;
			t.sym.hasLineStyle = false;
			t.textHeight = th;
			t.pts.push_back( { si.insX, y0 - ( i + 2 ) * th * 1.6 } );
			const QByteArray bytes = codec ? codec->fromUnicode( lines[i] ) : lines[i].toLatin1();
			t.text = std::string( bytes.constData(), static_cast<size_t>( bytes.size() ) );
			elements.push_back( t );
		}
	}
	if ( elements.empty() )
	{
		QMessageBox::information( this, QStringLiteral( "Řezy" ), QStringLiteral( "Zatím není nic nakreslené (u vybraného mračna)." ) );
		return;
	}

	QSettings st;
	QString seed = st.value( QStringLiteral( "qRezy/seed" ) ).toString();
	if ( seed.isEmpty() || !QFileInfo::exists( seed ) )
	{
		QMessageBox::information( this, QStringLiteral( "Řezy" ),
			QStringLiteral( "Nejdřív vyberte <b>seed</b> – existující výkres V7, ze kterého se převezmou jednotky a nastavení "
			                "(např. váš vzorový výkres). Seed se nezmění a zapamatuje se.<br><br>"
			                "Potom se otevře okno pro uložení nového výkresu." ) );
		chooseSeed();
		seed = st.value( QStringLiteral( "qRezy/seed" ) ).toString();
		if ( seed.isEmpty() )
		{
			return;
		}
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

// ---------------------------------------------------------------- svislé řezy

namespace
{
	const char* KSecId = "qRezy.secId";

	QString sectionName( int id )
	{
		// A–A', B–B', … po Z pak čísla
		if ( id >= 1 && id <= 26 )
		{
			const QChar ch( 'A' + id - 1 );
			return QStringLiteral( "%1-%1'" ).arg( ch );
		}
		return QStringLiteral( "%1-%1'" ).arg( id );
	}
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
		si.name = sectionName( si.id );
		si.A = CCVector2d( ch->getMetaData( "qRezy.secAx" ).toDouble(), ch->getMetaData( "qRezy.secAy" ).toDouble() );
		si.B = CCVector2d( ch->getMetaData( "qRezy.secBx" ).toDouble(), ch->getMetaData( "qRezy.secBy" ).toDouble() );
		si.N = CCVector2d( ch->getMetaData( "qRezy.secNx" ).toDouble(), ch->getMetaData( "qRezy.secNy" ).toDouble() );
		si.offset = ch->getMetaData( "qRezy.secOffset" ).toDouble();
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
	s.poly->setMetaData( "qRezy.secAx", s.A.x );
	s.poly->setMetaData( "qRezy.secAy", s.A.y );
	s.poly->setMetaData( "qRezy.secBx", s.B.x );
	s.poly->setMetaData( "qRezy.secBy", s.B.y );
	s.poly->setMetaData( "qRezy.secNx", s.N.x );
	s.poly->setMetaData( "qRezy.secNy", s.N.y );
	s.poly->setMetaData( "qRezy.secOffset", s.offset );
	s.poly->setMetaData( "qRezy.secInsX", s.insX );
	s.poly->setMetaData( "qRezy.secInsY", s.insY );
	updateLabels();
}

void RezDock::createSection( double ax, double ay, double bx, double by, double cx, double cy )
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

	int id = 1;
	for ( const SectionInfo& si : loadSections() )
	{
		id = std::max( id, si.id + 1 );
	}

	// čára řezu v půdorysu
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
	poly->setColor( ccColor::Rgb( 0, 230, 230 ) );
	poly->showColors( true );
	poly->setWidth( 2 );
	poly->setName( QStringLiteral( "Řez %1" ).arg( sectionName( id ) ) );
	poly->setMetaData( Drawing::MetaView, -1 );
	group->addChild( poly );
	m_app->addToDB( poly, false, false, false, false );

	SectionInfo si;
	si.id = id;
	si.name = sectionName( id );
	si.A = A;
	si.B = B;
	si.N = N;
	si.poly = poly;
	// výchozí umístění ve výkresu: pod mračnem, řezy pod sebou
	const CCVector3d gmin = c->toGlobal3d( bb.minCorner() );
	const CCVector3d gmax = c->toGlobal3d( bb.maxCorner() );
	const double zRange = std::min( 30.0, gmax.z - gmin.z );
	si.insX = gmin.x;
	si.insY = gmin.y - 5.0 - ( id - 1 ) * ( zRange + 5.0 );
	saveSection( si );

	refreshViewCombo();
	activateView( id );
}

void RezDock::deleteActiveSection()
{
	SectionInfo si;
	if ( !sectionById( m_activeView, si ) )
	{
		QMessageBox::information( this, QStringLiteral( "Řezy" ), QStringLiteral( "Vyberte nahoře v seznamu řez, který chcete smazat." ) );
		return;
	}
	if ( QMessageBox::question( this, QStringLiteral( "Řezy" ), QStringLiteral( "Smazat řez %1 i se vším, co je v něm nakreslené?" ).arg( si.name ) ) != QMessageBox::Yes )
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
	m_app->refreshAll();
}

DrawFrame RezDock::frameFor( int viewId ) const
{
	DrawFrame f;
	SectionInfo si;
	ccGenericPointCloud* c = cloud();
	if ( viewId == 0 || !c || !sectionById( viewId, si ) )
	{
		return f; // půdorys
	}
	f.viewId = viewId;
	f.N = CCVector3d( si.N.x, si.N.y, 0.0 );
	f.V = CCVector3d( 0.0, 0.0, 1.0 );
	f.U = f.V.cross( f.N ); // vpravo při pohledu proti N
	// počátek: levý konec čáry řezu ve výšce ±0,000
	const double uA = CCVector3d( si.A.x, si.A.y, 0 ).dot( f.U );
	const double uB = CCVector3d( si.B.x, si.B.y, 0 ).dot( f.U );
	const CCVector2d left = ( uA <= uB ) ? si.A : si.B;
	const double zeroLocal = c->toLocal3d( CCVector3d( 0, 0, m_zero->value() ) ).z;
	f.O = CCVector3d( left.x, left.y, zeroLocal );
	// čáry před rovinou řezu (k pozorovateli), aby je body nezakryly
	f.front = 20.0 * c->getGlobalScale() - si.offset * c->getGlobalScale();
	return f;
}

void RezDock::refreshViewCombo()
{
	const QSignalBlocker b( m_viewCombo );
	m_viewCombo->clear();
	m_viewCombo->addItem( QStringLiteral( "Půdorys" ), 0 );
	for ( const SectionInfo& si : loadSections() )
	{
		m_viewCombo->addItem( QStringLiteral( "Řez %1" ).arg( si.name ), si.id );
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
	m[0] = f.U.x; m[4] = f.U.y; m[8] = f.U.z;
	m[1] = f.V.x; m[5] = f.V.y; m[9] = f.V.z;
	m[2] = f.N.x; m[6] = f.N.y; m[10] = f.N.z;
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
	updateSectionWidgets();
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

void RezDock::updateSectionWidgets()
{
	SectionInfo si;
	const bool sec = sectionById( m_activeView, si );
	m_secWidgets->setVisible( sec );
	if ( sec )
	{
		const QSignalBlocker b1( m_insX );
		const QSignalBlocker b2( m_insY );
		m_insX->setValue( si.insX );
		m_insY->setValue( si.insY );
	}
	updateLabels();
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
