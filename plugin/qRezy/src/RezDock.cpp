#include "RezDock.h"

#include "ccMainAppInterface.h"
#include "ccGLWindowInterface.h"

#include <ccGenericPointCloud.h>
#include <ccHObjectCaster.h>

#include <QCheckBox>
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
	lay->addStretch( 1 );

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
	clearClip();
}

void RezDock::closeEvent( QCloseEvent* event )
{
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
