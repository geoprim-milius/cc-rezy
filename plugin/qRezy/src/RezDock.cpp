#include "RezDock.h"
#include "DgnV7.h"
#include "DrawTool.h"
#include "Drawing.h"

#include "ccMainAppInterface.h"
#include "ccGLWindowInterface.h"

#include <ccGenericPointCloud.h>
#include <ccHObjectCaster.h>

#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QGroupBox>
#include <QHeaderView>
#include <QMessageBox>
#include <QSettings>
#include <QSpinBox>
#include <QTableWidget>
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

	// řez
	m_enabled = new QCheckBox( QStringLiteral( "Vodorovný řez zapnutý" ) );
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
	m_orthoChk = new QCheckBox( QStringLiteral( "Ortogonálně (F8)" ) );
	dl->addWidget( m_hiddenChk );
	dl->addWidget( m_orthoChk );

	auto* drow = new QHBoxLayout;
	auto* axisBtn = new QPushButton( QStringLiteral( "Osy podle čáry" ) );
	axisBtn->setToolTip( QStringLiteral( "Klikněte potom na nakreslenou čáru – ortogonální kreslení se natočí podle ní" ) );
	m_deleteBtn = new QPushButton( QStringLiteral( "Mazat (Del)" ) );
	m_deleteBtn->setCheckable( true );
	auto* undoBtn = new QPushButton( QStringLiteral( "Zpět (Ctrl+Z)" ) );
	drow->addWidget( axisBtn );
	drow->addWidget( m_deleteBtn );
	drow->addWidget( undoBtn );
	dl->addLayout( drow );

	m_status = new QLabel;
	m_status->setWordWrap( true );
	m_status->setStyleSheet( QStringLiteral( "color: #555;" ) );
	dl->addWidget( m_status );

	auto* help = new QLabel( QStringLiteral(
		"<small>Levé = bod, pravé kliknutí = reset (konec čáry), pravé tažení = posun, kolečko = zoom.<br>"
		"Délka z klávesnice: napište číslo a Enter. Esc = zrušit čáru.</small>" ) );
	help->setWordWrap( true );
	dl->addWidget( help );
	lay->addWidget( drawBox );

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
	auto* exportBtn = new QPushButton( QStringLiteral( "Uložit výkres V7 (.dgn)…" ) );
	ol->addWidget( exportBtn );
	lay->addWidget( outBox );

	lay->addStretch( 1 );

	m_draw = new DrawTool( m_app, this );
	connect( m_drawBtn, &QPushButton::toggled, this, &RezDock::toggleDrawing );
	connect( m_catCombo, qOverload<int>( &QComboBox::currentIndexChanged ), this, [this]( int i ) { m_draw->setCategory( i ); focusView(); } );
	connect( m_hiddenChk, &QCheckBox::toggled, this, [this]( bool b ) { m_draw->setHidden( b ); focusView(); } );
	connect( m_orthoChk, &QCheckBox::toggled, this, [this]( bool b ) { m_draw->setOrtho( b ); focusView(); } );
	connect( axisBtn, &QPushButton::clicked, this, [this]() { m_draw->pickAxisFromLine(); m_status->setText( QStringLiteral( "Klikněte na čáru, podle které se natočí osy." ) ); focusView(); } );
	connect( m_deleteBtn, &QPushButton::toggled, this, [this]( bool b ) { m_draw->setMode( b ? DrawTool::Mode::Delete : DrawTool::Mode::Line ); focusView(); } );
	connect( undoBtn, &QPushButton::clicked, this, [this]() { m_draw->undo(); focusView(); } );
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

	setWidget( w );

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
	connect( m_zero, qOverload<double>( &QDoubleSpinBox::valueChanged ), this, [this]() { updateLabels(); } );

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
	m_height->setValue( m_height->value() + direction * m_step->value() );
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

	if ( m_enabled->isChecked() )
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
	m_relLabel->setText( QStringLiteral( "Řez %1" ).arg( fmt( m_height->value() - m_zero->value() ) ) );
}

void RezDock::topView()
{
	ccGLWindowInterface* win = m_app ? m_app->getActiveGLWindow() : nullptr;
	if ( !win )
	{
		return;
	}
	win->setPerspectiveState( false, true ); // pravoúhlé zobrazení
	m_app->setView( CC_TOP_VIEW );
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
	const QString f = QFileDialog::getOpenFileName( this, QStringLiteral( "Seed výkres MicroStation V7" ), old, QStringLiteral( "MicroStation V7 (*.dgn);;Všechny soubory (*)" ) );
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
	const std::vector<DgnV7::Element> elements = m_draw->exportElements( table );
	if ( elements.empty() )
	{
		QMessageBox::information( this, QStringLiteral( "Řezy" ), QStringLiteral( "Zatím není nic nakreslené (u vybraného mračna)." ) );
		return;
	}

	QSettings st;
	QString seed = st.value( QStringLiteral( "qRezy/seed" ) ).toString();
	if ( seed.isEmpty() || !QFileInfo::exists( seed ) )
	{
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
	QString out = QFileDialog::getSaveFileName( this, QStringLiteral( "Uložit výkres V7" ), lastOut, QStringLiteral( "MicroStation V7 (*.dgn)" ) );
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
