#include "Ribbon.h"

#include <QAction>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QSettings>
#include <QStackedWidget>
#include <QTabBar>
#include <QToolButton>
#include <QVBoxLayout>

namespace
{
	constexpr int BigW = 70;
	constexpr int BigH = 76;
	constexpr int SmallH = 24;
	constexpr int BodyH = 104;
}

QToolButton* RibbonColumn::small( QAction* a, const QString& key )
{
	auto* row = new QWidget;
	auto* h = new QHBoxLayout( row );
	h->setContentsMargins( 0, 0, 0, 0 );
	h->setSpacing( 6 );
	auto* b = new QToolButton;
	b->setObjectName( QStringLiteral( "rezySmall" ) );
	b->setDefaultAction( a );
	b->setToolButtonStyle( Qt::ToolButtonTextBesideIcon );
	b->setIconSize( QSize( 18, 18 ) );
	b->setFixedHeight( SmallH );
	b->setAutoRaise( true );
	h->addWidget( b, 1 );
	if ( !key.isEmpty() )
	{
		auto* k = new QLabel( key );
		k->setObjectName( QStringLiteral( "rezyKey" ) );
		k->setAlignment( Qt::AlignRight | Qt::AlignVCenter );
		h->addWidget( k );
	}
	m_layout->addWidget( row );
	return b;
}

void RibbonColumn::widget( QWidget* w, const QString& label )
{
	if ( label.isEmpty() )
	{
		m_layout->addWidget( w );
		return;
	}
	auto* row = new QWidget;
	auto* h = new QHBoxLayout( row );
	h->setContentsMargins( 2, 0, 0, 0 );
	h->setSpacing( 6 );
	auto* l = new QLabel( label );
	h->addWidget( l );
	h->addWidget( w, 1 );
	m_layout->addWidget( row );
}

QToolButton* RibbonGroup::big( QAction* a, const QString& key )
{
	auto* b = new QToolButton;
	b->setObjectName( QStringLiteral( "rezyBig" ) );
	b->setDefaultAction( a );
	b->setToolButtonStyle( Qt::ToolButtonTextUnderIcon );
	b->setIconSize( QSize( 32, 32 ) );
	b->setFixedSize( BigW, BigH );
	b->setAutoRaise( true );
	if ( !key.isEmpty() )
	{
		auto* k = new QLabel( key, b );
		k->setObjectName( QStringLiteral( "rezyKey" ) );
		k->setAttribute( Qt::WA_TransparentForMouseEvents );
		k->adjustSize();
		k->move( BigW - k->width() - 4, 2 );
	}
	m_layout->addWidget( b, 0, Qt::AlignTop );
	return b;
}

RibbonColumn RibbonGroup::column()
{
	auto* w = new QWidget;
	auto* v = new QVBoxLayout( w );
	v->setContentsMargins( 0, 0, 0, 0 );
	v->setSpacing( 1 );
	v->setAlignment( Qt::AlignTop );
	m_layout->addWidget( w, 0, Qt::AlignTop );
	return RibbonColumn( v );
}

Ribbon::Ribbon( QWidget* parent )
	: QWidget( parent )
{
	setObjectName( QStringLiteral( "rezyRibbon" ) );
	setAttribute( Qt::WA_StyledBackground, true );
	auto* v = new QVBoxLayout( this );
	v->setContentsMargins( 0, 0, 0, 0 );
	v->setSpacing( 0 );

	auto* tabRow = new QWidget;
	tabRow->setObjectName( QStringLiteral( "rezyTabRow" ) );
	tabRow->setAttribute( Qt::WA_StyledBackground, true );
	auto* th = new QHBoxLayout( tabRow );
	th->setContentsMargins( 0, 0, 0, 0 );
	th->setSpacing( 0 );
	m_app = new QToolButton;
	m_app->setObjectName( QStringLiteral( "rezyApp" ) );
	m_app->setToolButtonStyle( Qt::ToolButtonTextBesideIcon );
	m_app->setIconSize( QSize( 18, 18 ) );
	th->addWidget( m_app );
	m_tabs = new QTabBar;
	m_tabs->setDrawBase( false );
	m_tabs->setExpanding( false );
	m_tabs->setToolTip( QStringLiteral( "Dvojklik na kartu = sbalit / rozbalit pás" ) );
	th->addWidget( m_tabs );
	th->addStretch( 1 );
	v->addWidget( tabRow );

	m_stack = new QStackedWidget;
	m_stack->setFixedHeight( BodyH );
	v->addWidget( m_stack );

	connect( m_tabs, &QTabBar::currentChanged, this, [this]( int i )
	{
		m_stack->setCurrentIndex( i );
		QSettings().setValue( QStringLiteral( "qRezy/ribbonPage" ), i );
	} );
	connect( m_tabs, &QTabBar::tabBarClicked, this, [this]( int i )
	{
		if ( m_stack->isHidden() && i >= 0 )
		{
			m_stack->show();
		}
	} );
	connect( m_tabs, &QTabBar::tabBarDoubleClicked, this, [this]( int )
	{
		m_stack->setVisible( m_stack->isHidden() );
	} );
}

void Ribbon::setAppAction( QAction* a )
{
	m_app->setDefaultAction( a );
}

int Ribbon::addPage( const QString& title )
{
	auto* page = new QWidget;
	auto* h = new QHBoxLayout( page );
	h->setContentsMargins( 4, 4, 4, 0 );
	h->setSpacing( 0 );
	m_stack->addWidget( page );
	m_pages.push_back( h );
	return m_tabs->addTab( title );
}

RibbonGroup Ribbon::addGroup( int page, const QString& title )
{
	QHBoxLayout* pl = m_pages[static_cast<size_t>( page )];
	auto* g = new QWidget;
	auto* gv = new QVBoxLayout( g );
	gv->setContentsMargins( 6, 0, 6, 0 );
	gv->setSpacing( 0 );
	auto* content = new QHBoxLayout;
	content->setSpacing( 3 );
	content->setAlignment( Qt::AlignLeft | Qt::AlignTop );
	gv->addLayout( content, 1 );
	auto* t = new QLabel( title );
	t->setObjectName( QStringLiteral( "rezyGroupTitle" ) );
	t->setAlignment( Qt::AlignCenter );
	gv->addWidget( t );
	pl->addWidget( g );
	auto* sep = new QFrame;
	sep->setObjectName( QStringLiteral( "rezySep" ) );
	sep->setFixedWidth( 1 );
	sep->setFrameShape( QFrame::NoFrame );
	pl->addWidget( sep );
	return RibbonGroup( content );
}

void Ribbon::finishPages()
{
	for ( QHBoxLayout* h : m_pages )
	{
		h->addStretch( 1 );
	}
	const int p = QSettings().value( QStringLiteral( "qRezy/ribbonPage" ), 0 ).toInt();
	if ( p >= 0 && p < m_tabs->count() )
	{
		m_tabs->setCurrentIndex( p );
	}
}

void Ribbon::setCurrentPage( int page )
{
	m_tabs->setCurrentIndex( page );
	m_stack->show();
}

int Ribbon::currentPage() const
{
	return m_tabs->currentIndex();
}
