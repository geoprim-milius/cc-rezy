#include "View3D.h"
#include "RezLine.h"
#include "RezLayer.h"
#include "DrawTool.h"

#include "ccGLUtils.h"
#include "ccGLWindowInterface.h"
#include "ccMainAppInterface.h"
#include "ccGuiParameters.h"

#include <ccHObjectCaster.h>
#include <ccPointCloud.h>
#include <ccPolyline.h>
#include <ccSphere.h>

#include <CloudSamplingTools.h>
#include <ReferenceCloud.h>

#include <QApplication>
#include <QButtonGroup>
#include <QCheckBox>
#include <QToolButton>
#include <QComboBox>
#include <QShortcut>
#include <QTimer>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPushButton>
#include <QSlider>
#include <QOpenGLFunctions_2_1>
#include <QWheelEvent>
#include <QSettings>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>
#include <functional>

//! skupina, jejíž potomci (mračno) se kreslí průhledně
class BlendGroup : public ccHObject
{
public:
	BlendGroup()
		: ccHObject( "blend" )
	{
		setVisible( true );
		setEnabled( true );
	}
	float alpha = 1.0f;

	void draw( CC_DRAW_CONTEXT& context ) override
	{
		QOpenGLFunctions_2_1* gl = context.glFunctions<QOpenGLFunctions_2_1>();
		const bool blend = gl && MACRO_Draw3D( context ) && alpha < 0.999f;
		if ( blend )
		{
			gl->glPushAttrib( GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT );
			gl->glEnable( GL_BLEND );
			gl->glBlendColor( 0.0f, 0.0f, 0.0f, alpha );
			gl->glBlendFunc( GL_CONSTANT_ALPHA, GL_ONE_MINUS_CONSTANT_ALPHA );
			gl->glDepthMask( GL_FALSE ); // body se navzájem nezakrývají („rentgen“)
		}
		ccHObject::draw( context );
		if ( blend )
		{
			gl->glPopAttrib();
		}
	}
};

//! posuvník se dvěma jezdci (od – do)
class RangeSlider : public QWidget
{
public:
	explicit RangeSlider( QWidget* parent = nullptr )
		: QWidget( parent )
	{
		setMinimumHeight( 22 );
		setMinimumWidth( 120 );
		setSizePolicy( QSizePolicy::Expanding, QSizePolicy::Fixed );
		setMouseTracking( true );
	}

	std::function<void()> onChanged;

	void setRange( double mn, double mx )
	{
		m_min = mn;
		m_max = std::max( mx, mn + 0.01 );
		m_v0 = clampV( m_v0 );
		m_v1 = clampV( m_v1 );
		update();
	}
	void setValues( double v0, double v1 )
	{
		m_v0 = clampV( std::min( v0, v1 ) );
		m_v1 = clampV( std::max( v0, v1 ) );
		update();
	}
	double v0() const { return m_v0; }
	double v1() const { return m_v1; }
	double minimum() const { return m_min; }
	double maximum() const { return m_max; }

protected:
	void paintEvent( QPaintEvent* ) override
	{
		QPainter p( this );
		p.setRenderHint( QPainter::Antialiasing );
		const QPalette& pal = palette();
		const int cy = height() / 2;
		const QColor dis = pal.color( QPalette::Mid );
		p.setPen( Qt::NoPen );
		p.setBrush( dis );
		p.drawRoundedRect( QRectF( Margin, cy - 2, width() - 2 * Margin, 4 ), 2, 2 );
		const double x0 = toX( m_v0 );
		const double x1 = toX( m_v1 );
		p.setBrush( isEnabled() ? pal.color( QPalette::Highlight ) : dis );
		p.drawRect( QRectF( x0, cy - 3, x1 - x0, 6 ) );
		// nula = místo kliknutí
		if ( m_min < 0 && m_max > 0 )
		{
			p.setPen( QPen( pal.color( QPalette::Text ), 1 ) );
			const double xz = toX( 0.0 );
			p.drawLine( QPointF( xz, cy - 7 ), QPointF( xz, cy + 7 ) );
		}
		p.setPen( QPen( pal.color( QPalette::Dark ), 1 ) );
		p.setBrush( pal.color( QPalette::Button ) );
		for ( double x : { x0, x1 } )
		{
			p.drawRoundedRect( QRectF( x - 5, cy - 8, 10, 16 ), 3, 3 );
		}
	}
	void mousePressEvent( QMouseEvent* e ) override
	{
		const double x = e->pos().x();
		const double d0 = std::abs( x - toX( m_v0 ) );
		const double d1 = std::abs( x - toX( m_v1 ) );
		if ( std::abs( d0 - d1 ) < 1.0 )
		{
			m_drag = ( x < toX( m_v0 ) ) ? 0 : 1; // jezdci na sobě → podle směru
		}
		else
		{
			m_drag = ( d0 < d1 ) ? 0 : 1;
		}
		moveTo( x );
	}
	void mouseMoveEvent( QMouseEvent* e ) override
	{
		if ( m_drag >= 0 && ( e->buttons() & Qt::LeftButton ) )
		{
			moveTo( e->pos().x() );
		}
	}
	void mouseReleaseEvent( QMouseEvent* ) override
	{
		m_drag = -1;
	}
	void wheelEvent( QWheelEvent* e ) override
	{
		e->ignore();
	}

private:
	static constexpr int Margin = 8;
	double clampV( double v ) const { return std::max( m_min, std::min( m_max, v ) ); }
	double toX( double v ) const
	{
		return Margin + ( v - m_min ) / ( m_max - m_min ) * ( width() - 2 * Margin );
	}
	double toV( double x ) const
	{
		return m_min + ( x - Margin ) / std::max( 1, width() - 2 * Margin ) * ( m_max - m_min );
	}
	void moveTo( double x )
	{
		// krok 5 cm
		double v = std::round( clampV( toV( x ) ) * 20.0 ) / 20.0;
		v = clampV( v );
		if ( m_drag == 0 )
		{
			m_v0 = std::min( v, m_v1 - 0.05 );
		}
		else
		{
			m_v1 = std::max( v, m_v0 + 0.05 );
		}
		m_v0 = clampV( m_v0 );
		m_v1 = clampV( m_v1 );
		update();
		if ( onChanged )
		{
			onChanged();
		}
	}

	double m_min = -10.0;
	double m_max = 10.0;
	double m_v0 = -1.5;
	double m_v1 = 1.5;
	int m_drag = -1;
};

namespace
{
	const char* const AxisKey[3] = { "U", "V", "Z" };
	const double AxisDefault[3][2] = { { -1.5, 1.5 }, { -1.5, 1.5 }, { -2.0, 2.0 } };

	QString fmtM( double v )
	{
		return QString::number( v, 'f', 2 ).replace( '.', ',' );
	}
}

View3D::View3D( ccMainAppInterface* app, QWidget* parent, bool auxOnly )
	: QDockWidget( auxOnly ? QStringLiteral( "Řezy – pomocný řez (měření)" ) : QStringLiteral( "Řezy – 3D náhled" ), parent )
	, m_app( app )
	, m_auxOnly( auxOnly )
{
	setObjectName( auxOnly ? QStringLiteral( "qRezyAuxView" ) : QStringLiteral( "qRezy3DView" ) );

	auto* w = new QWidget;
	auto* lay = new QVBoxLayout( w );
	lay->setContentsMargins( 2, 2, 2, 2 );

	// posuvníky ořezu (od – do, metry od místa kliknutí)
	QSettings st;
	auto* grid = new QGridLayout;
	grid->setContentsMargins( 4, 0, 4, 0 );
	grid->setVerticalSpacing( 0 );
	const QString names[3] = { QStringLiteral( "Šířka" ), QStringLiteral( "Hloubka" ), QStringLiteral( "Výška" ) };
	const QString tips[3] = { QStringLiteral( "Do stran (vodorovně na obrazovce řezu/půdorysu)" ),
	                          QStringLiteral( "Do hloubky (kolmo, ve směru pohledu)" ),
	                          QStringLiteral( "Výška od místa kliknutí (horní jezdec doleva = uříznout střechu)" ) };
	for ( int i = 0; i < 3; ++i )
	{
		m_rs[i] = new RangeSlider;
		m_rs[i]->setToolTip( tips[i] );
		m_rs[i]->setEnabled( false );
		const QString k = QStringLiteral( "qRezy/3d%1" ).arg( QLatin1String( AxisKey[i] ) );
		m_rs[i]->setRange( -50.0, 50.0 );
		m_rs[i]->setValues( st.value( k + QStringLiteral( "0" ), AxisDefault[i][0] ).toDouble(), st.value( k + QStringLiteral( "1" ), AxisDefault[i][1] ).toDouble() );
		m_rsLabel[i] = new QLabel;
		m_rsLabel[i]->setMinimumWidth( 110 );
		auto* nl = new QLabel( names[i] );
		nl->setToolTip( tips[i] );
		grid->addWidget( nl, i, 0 );
		grid->addWidget( m_rs[i], i, 1 );
		grid->addWidget( m_rsLabel[i], i, 2 );
		m_rs[i]->onChanged = [this]()
		{
			updateValueLabels();
			applyClip();
		};
	}
	grid->setColumnStretch( 1, 1 );
	auto* side = new QVBoxLayout;
	m_showLines = new QCheckBox( QStringLiteral( "čáry" ) );
	m_showLines->setChecked( true );
	m_showLines->setToolTip( QStringLiteral( "Zobrazit nakreslené čáry" ) );
	auto* resetBtn = new QPushButton( QStringLiteral( "výchozí" ) );
	resetBtn->setToolTip( QStringLiteral( "Šířka a hloubka ±1,5 m, výška ±2 m" ) );
	auto* allBtn = new QPushButton( QStringLiteral( "vše" ) );
	allBtn->setToolTip( QStringLiteral( "Celé mračno" ) );
	m_showPoints = new QCheckBox( QStringLiteral( "body" ) );
	m_showPoints->setChecked( QSettings().value( QStringLiteral( "qRezy/3dPoints" ), true ).toBool() );
	m_showPoints->setToolTip( QStringLiteral( "Zobrazit body mračna (vypnout = jen nakreslené čáry)" ) );
	m_showSurvey = new QCheckBox( QStringLiteral( "měřené body" ) );
	m_showSurvey->setChecked( QSettings().value( QStringLiteral( "qRezy/3dSurvey" ), true ).toBool() );
	m_showSurvey->setToolTip( QStringLiteral( "Změřené body načtené ze souboru (křížek a číslo); body bez výšky na podlaze patra" ) );
	m_showPin = new QCheckBox( QStringLiteral( "špendlík" ) );
	m_showPin->setChecked( true );
	m_showPin->setToolTip( QStringLiteral( "Zobrazit špendlík" ) );
	side->addWidget( m_showPoints );
	side->addWidget( m_showLines );
	side->addWidget( m_showSurvey );
	side->addWidget( m_showPin );
	side->addWidget( resetBtn );
	side->addWidget( allBtn );
	m_alpha = new QSlider( Qt::Horizontal );
	m_alpha->setRange( 5, 100 );
	m_alpha->setValue( st.value( auxOnly ? QStringLiteral( "qRezy/auxAlpha" ) : QStringLiteral( "qRezy/3dAlpha" ), 100 ).toInt() );
	m_alpha->setToolTip( QStringLiteral( "Neprůhlednost mračna (vlevo = průhledné, čáry zůstanou plné)" ) );
	m_alpha->setMaximumWidth( 90 );
	side->addWidget( new QLabel( QStringLiteral( "průhlednost" ) ) );
	side->addWidget( m_alpha );
	// pomocný řez: druh zápisu do půdorysu – sloupec vpravo (okno je pak užší)
	m_modeCol = new QWidget;
	auto* modeLay = new QVBoxLayout( m_modeCol );
	modeLay->setContentsMargins( 0, 0, 0, 0 );
	m_modeCol->setFixedWidth( 96 );
	modeLay->setSpacing( 2 );
	modeLay->addWidget( new QLabel( QStringLiteral( "Do půdorysu:" ) ) );
	m_modeCol->setVisible( false );
	m_backBtn = new QPushButton( QStringLiteral( "3D" ) );
	m_backBtn->setToolTip( QStringLiteral( "Zpět z pomocného řezu do 3D náhledu" ) );
	m_backBtn->setVisible( false );
	side->addWidget( m_backBtn );
	auto* top = new QHBoxLayout;
	top->addLayout( grid, 1 );
	top->addLayout( side );
	lay->addLayout( top );

	// posun kresby (půdorys nahoru/dolů – na překlad, k oknu…; řez/pohled do hloubky – na líc)
	auto* shRow = new QHBoxLayout;
	shRow->setContentsMargins( 4, 0, 4, 0 );
	auto* shName = new QLabel( QStringLiteral( "Posun kresby" ) );
	shName->setToolTip( QStringLiteral( "Půdorys: nahoru/dolů (např. na překlady). Řez/pohled: do hloubky (např. na líc fasády). Na výkres nemá vliv." ) );
	m_shift = new QSlider( Qt::Horizontal );
	m_shift->setRange( -600, 600 );
	m_shift->setValue( 0 );
	m_shift->setToolTip( shName->toolTip() );
	m_shiftLabel = new QLabel( QStringLiteral( "0,00 m" ) );
	m_shiftLabel->setMinimumWidth( 110 );
	auto* shZero = new QPushButton( QStringLiteral( "0" ) );
	shZero->setMaximumWidth( 30 );
	shZero->setToolTip( QStringLiteral( "Kresba zpět do své roviny" ) );
	shRow->addWidget( shName );
	shRow->addWidget( m_shift, 1 );
	shRow->addWidget( m_shiftLabel );
	shRow->addWidget( shZero );
	lay->addLayout( shRow );
	connect( m_shift, &QSlider::valueChanged, this, [this]( int v )
	{
		m_shiftLabel->setText( QStringLiteral( "%1 m" ).arg( QString::number( v / 100.0, 'f', 2 ).replace( '.', ',' ) ) );
		buildLines();
	} );
	connect( shZero, &QPushButton::clicked, this, [this]() { m_shift->setValue( 0 ); } );
	if ( auxOnly )
	{
		shName->setVisible( false );
		m_shift->setVisible( false );
		m_shiftLabel->setVisible( false );
		shZero->setVisible( false );
	}
	updateValueLabels();

	m_info = new QLabel( QStringLiteral( "Prostřední klik do řezu = ukázat místo. Levé tažení = otáčení, kolečko = zoom." ) );
	m_info->setWordWrap( true );
	lay->addWidget( m_info );

	// pomocný řez: zapsat změřenou výšku do půdorysu
	m_writeRow = new QWidget;
	{
		auto* wl = new QHBoxLayout( m_writeRow );
		wl->setContentsMargins( 0, 0, 0, 0 );
		// podlaha: jednou kliknout na podlahu, další měření pak o klik méně
		m_floorBtn = new QToolButton;
		m_floorBtn->setCheckable( true );
		m_floorBtn->setFocusPolicy( Qt::NoFocus );
		m_floorBtn->setText( QStringLiteral( "Def. podlahy" ) );
		m_floorBtn->setToolTip( QStringLiteral( "Definovat podlahu: klikněte na tlačítko a pak na podlahu v řezu. Potom se podlaha už neklikne – "
		                                        "Strop / Překlad / Výška = 1 klik, Okno = 2 kliky (parapet, horní hrana). Znovu kliknout na tlačítko = zrušit podlahu." ) );
		wl->addWidget( m_floorBtn );
		connect( m_floorBtn, &QToolButton::clicked, this, [this]()
		{
			if ( m_hasFloor || m_floorPick )
			{
				m_hasFloor = false;
				m_floorPick = false;
			}
			else
			{
				m_floorPick = true;
			}
			m_mp.clear();
			m_measStep = 0;
			clearMeasure();
			updateFloor();
			if ( m_floorPick )
			{
				m_info->setText( QStringLiteral( "<b>Def. podlahy</b> – klikněte na podlahu v řezu." ) );
			}
			else if ( m_aux )
			{
				updateMeasure( false );
			}
		} );
		wl->addSpacing( 10 );
		// režimy jako přepínací tlačítka (rychlejší než seznam); id = qRezy/auxWriteMode
		m_writeMode = new QButtonGroup( this );
		m_writeMode->setExclusive( true );
		struct WM
		{
			int id;
			const char* name;
			const char* tip;
		};
		const WM modes[] = {
			{ 1, "Překlad", "Výška překladu – 2 kliky (podlaha, spodní hrana překladu)  →  Vp=2060" },
			{ 3, "Strop", "Výška stropu – 2 kliky (podlaha, strop)  →  Vs=2650" },
			{ 2, "Výška", "Výška – 2 kliky  →  2060" },
			{ 0, "Okno", "Okno – 3 kliky (podlaha, parapet, horní hrana okna)  →  2300(850) = okno nad parapetem (parapet nad podlahou)" },
			{ 4, "Podlaha", "Výška podlahy – 1 klik na podlahu  →  +0,150 (245,320) = relativně k ±0,000 a v Bpv" },
		};
		int cur = QSettings().value( QStringLiteral( "qRezy/auxWriteMode" ), 0 ).toInt();
		if ( cur < 0 || cur > 4 )
		{
			cur = 0;
		}
		for ( const WM& m : modes )
		{
			auto* b = new QToolButton;
			b->setText( QString::fromUtf8( m.name ) );
			b->setToolTip( QString::fromUtf8( m.tip ) );
			b->setCheckable( true );
			b->setChecked( m.id == cur );
			b->setMinimumWidth( 64 );
			b->setFocusPolicy( Qt::NoFocus );
			m_writeMode->addButton( b, m.id );
			b->setSizePolicy( QSizePolicy::Expanding, QSizePolicy::Fixed );
			modeLay->addWidget( b );
		}
		modeLay->addStretch( 1 );
		wl->addStretch( 1 );
		auto* manual = new QCheckBox( QStringLiteral( "ručně" ) );
		manual->setToolTip( QStringLiteral( "Text do půdorysu neumístit automaticky doprostřed čáry řezu, ale nechat ho na kurzoru a položit klikem (např. když je víc údajů přes sebe)" ) );
		manual->setChecked( QSettings().value( QStringLiteral( "qRezy/auxWriteManual" ), false ).toBool() );
		connect( manual, &QCheckBox::toggled, this, []( bool on ) { QSettings().setValue( QStringLiteral( "qRezy/auxWriteManual" ), on ); } );
		auto* rot = new QToolButton;
		rot->setText( QStringLiteral( "Text 90°" ) );
		rot->setCheckable( true );
		rot->setFocusPolicy( Qt::NoFocus );
		rot->setToolTip( QStringLiteral( "Zapnuto = text do půdorysu otočený o 90° (kolmo na čáru řezu), vypnuto = text podél čáry" ) );
		rot->setChecked( QSettings().value( QStringLiteral( "qRezy/auxWriteRot90" ), false ).toBool() );
		connect( rot, &QToolButton::toggled, this, []( bool on ) { QSettings().setValue( QStringLiteral( "qRezy/auxWriteRot90" ), on ); } );
		wl->addWidget( rot );
		wl->addWidget( manual );
		auto* wb = new QPushButton( QStringLiteral( "Zapsat (Enter)" ) );
		wb->setToolTip( QStringLiteral( "Svislou vzdálenost posledního měření (2 body) zaokrouhlit na cm a vložit v mm jako text doprostřed čáry pomocného řezu v půdorysu" ) );
		wl->addWidget( wb );
		connect( wb, &QPushButton::clicked, this, [this]() { writeMeasured(); } );
		// Enter kdekoli v okně pomocného řezu
		for ( int key : { int( Qt::Key_Return ), int( Qt::Key_Enter ) } )
		{
			auto* sc = new QShortcut( QKeySequence( key ), this );
			sc->setContext( Qt::WidgetWithChildrenShortcut );
			connect( sc, &QShortcut::activated, this, [this]() { writeMeasured(); } );
		}
		connect( m_writeMode, &QButtonGroup::idToggled, this, [this]( int i, bool on )
		{
			if ( !on )
			{
				return;
			}
			QSettings().setValue( QStringLiteral( "qRezy/auxWriteMode" ), i );
			m_parapetMM = -1;
			m_mp.clear();
			m_measStep = 0;
			clearMeasure();
			if ( m_aux )
			{
				updateMeasure( false );
			}
		} );
	}
	m_writeRow->setVisible( false );
	lay->addWidget( m_writeRow );

	m_app->createGLWindow( m_win, m_widget );
	if ( m_win && m_widget )
	{
		m_widget->setMinimumSize( 300, 250 );
		m_widget->setFocusPolicy( Qt::StrongFocus );
		auto* viewRow = new QHBoxLayout;
		viewRow->setSpacing( 4 );
		viewRow->addWidget( m_widget, 1 );
		viewRow->addWidget( m_modeCol ); // sloupec režimů zápisu vpravo od pohledu (jen pomocný řez)
		lay->addLayout( viewRow, 1 );
		m_win->setPerspectiveState( true, true );
		m_win->setPivotVisibility( ccGLWindowInterface::PIVOT_SHOW_ON_MOVE );
	}
	else
	{
		m_info->setText( QStringLiteral( "3D okno se nepodařilo vytvořit." ) );
	}
	setWidget( w );

	connect( m_backBtn, &QPushButton::clicked, this, [this]() { leaveAux( true ); } );
	connect( m_alpha, &QSlider::valueChanged, this, [this]( int v )
	{
		QSettings().setValue( m_auxOnly ? QStringLiteral( "qRezy/auxAlpha" ) : QStringLiteral( "qRezy/3dAlpha" ), v );
		if ( m_blend )
		{
			m_blend->alpha = v / 100.0f;
		}
		if ( m_win )
		{
			m_win->redraw();
		}
	} );
	if ( m_widget )
	{
		m_widget->installEventFilter( this );
	}
	connect( resetBtn, &QPushButton::clicked, this, [this]()
	{
		for ( int i = 0; i < 3; ++i )
		{
			m_rs[i]->setValues( AxisDefault[i][0], AxisDefault[i][1] );
		}
		updateValueLabels();
		applyClip();
	} );
	connect( allBtn, &QPushButton::clicked, this, [this]()
	{
		for ( int i = 0; i < 3; ++i )
		{
			m_rs[i]->setValues( m_rs[i]->minimum(), m_rs[i]->maximum() );
		}
		updateValueLabels();
		applyClip();
	} );
	connect( m_showSurvey, &QCheckBox::toggled, this, [this]( bool on )
	{
		QSettings().setValue( QStringLiteral( "qRezy/3dSurvey" ), on );
		rebuildSurvey();
	} );
	connect( m_showPin, &QCheckBox::toggled, this, [this]( bool on )
	{
		if ( m_pinGroup )
		{
			m_pinGroup->setEnabled( on );
		}
		if ( m_win )
		{
			m_win->redraw();
		}
	} );
	connect( m_showPoints, &QCheckBox::toggled, this, [this]( bool on )
	{
		QSettings().setValue( QStringLiteral( "qRezy/3dPoints" ), on );
		if ( m_clone )
		{
			m_clone->setVisible( on );
		}
		if ( m_win )
		{
			m_win->redraw();
		}
	} );
	connect( m_showLines, &QCheckBox::toggled, this, [this]( bool on )
	{
		if ( m_linesGroup )
		{
			m_linesGroup->setEnabled( on );
		}
		if ( m_win )
		{
			m_win->redraw();
		}
	} );
}

void View3D::saveSettings() const
{
	QSettings s;
	for ( int i = 0; i < 3; ++i )
	{
		const QString k = QStringLiteral( "qRezy/3d%1" ).arg( QLatin1String( AxisKey[i] ) );
		s.setValue( k + QStringLiteral( "0" ), m_rs[i]->v0() );
		s.setValue( k + QStringLiteral( "1" ), m_rs[i]->v1() );
	}
}

void View3D::updateValueLabels()
{
	for ( int i = 0; i < 3; ++i )
	{
		m_rsLabel[i]->setText( QStringLiteral( "%1 … %2 m" ).arg( fmtM( m_rs[i]->v0() ), fmtM( m_rs[i]->v1() ) ) );
	}
}

void View3D::updateRanges()
{
	if ( m_sourceID == 0 || !m_hasFocus )
	{
		return;
	}
	// rozsah = celé mračno v osách pohledu, relativně k bodu (metry)
	const ccBBox box = m_srcBB;
	const CCVector3 mn = box.minCorner();
	const CCVector3 mx = box.maxCorner();
	const double c = std::cos( m_angle );
	const double s = std::sin( m_angle );
	const CCVector3d axes[3] = { CCVector3d( c, s, 0 ), CCVector3d( -s, c, 0 ), CCVector3d( 0, 0, 1 ) };
	for ( int i = 0; i < 3; ++i )
	{
		double lo = 1e30;
		double hi = -1e30;
		for ( int k = 0; k < 8; ++k )
		{
			const CCVector3d P( ( k & 1 ) ? mx.x : mn.x, ( k & 2 ) ? mx.y : mn.y, ( k & 4 ) ? mx.z : mn.z );
			const double v = ( P - m_focus ).dot( axes[i] ) / m_scale;
			lo = std::min( lo, v );
			hi = std::max( hi, v );
		}
		lo = std::floor( std::min( lo, -0.5 ) * 10.0 ) / 10.0;
		hi = std::ceil( std::max( hi, 0.5 ) * 10.0 ) / 10.0;
		m_rs[i]->setRange( lo, hi );
		m_rs[i]->setEnabled( true );
	}
	updateValueLabels();
}

View3D::~View3D()
{
	for ( ccPolyline*& c : m_cross )
	{
		if ( c )
		{
			if ( m_win )
			{
				m_win->removeFromOwnDB( c );
			}
			delete c;
			c = nullptr;
		}
	}
	if ( m_measPoly )
	{
		if ( m_win )
		{
			m_win->removeFromOwnDB( m_measPoly );
		}
		delete m_measPoly;
		m_measPoly = nullptr;
	}
	clearLines();
	release();
	if ( m_blend )
	{
		if ( m_win )
		{
			m_win->removeFromOwnDB( m_blend );
		}
		delete m_blend;
		m_blend = nullptr;
	}
	if ( m_win )
	{
		m_app->destroyGLWindow( m_win );
		m_win = nullptr;
	}
}

void View3D::release()
{
	if ( m_clone && m_blend )
	{
		m_blend->detachChild( m_clone );
	}
	delete m_clone;
	m_clone = nullptr;
	m_sourceID = 0;
}

void View3D::clearLines()
{
	if ( m_linesGroup )
	{
		if ( m_win )
		{
			m_win->removeFromOwnDB( m_linesGroup );
		}
		delete m_linesGroup;
		m_linesGroup = nullptr;
	}
}

bool View3D::setSource( ccPointCloud* cloud, unsigned maxPoints )
{
	Q_UNUSED( maxPoints );
	if ( !m_win || !cloud )
	{
		return false;
	}
	if ( m_sourceID == cloud->getUniqueID() )
	{
		return true;
	}
	release();
	// žádná zředěná kopie celého mračna: náhled bere body jen z ukazovaného výřezu, v plné hustotě (extractRegion)
	m_sourceID = cloud->getUniqueID();
	m_scale = cloud->getGlobalScale();
	m_srcBB = cloud->getOwnBB();
	m_regPlanes.clear();
	m_hasFocus = false;
	if ( !m_blend )
	{
		m_blend = new BlendGroup;
		m_blend->setDisplay( m_win );
		m_win->addToOwnDB( m_blend );
	}
	m_blend->alpha = m_alpha->value() / 100.0f;
	m_info->setText( QStringLiteral( "Prostřední klik do výkresu = ukázat místo. Náhled bere body jen z výřezu (posuvníky), v plné hustotě." ) );
	return true;
}

std::vector<Extract::Plane> View3D::wantedPlanes( double margin ) const
{
	std::vector<Extract::Plane> pl;
	const double c = std::cos( m_angle );
	const double s = std::sin( m_angle );
	const CCVector3d axes[3] = { CCVector3d( c, s, 0 ), CCVector3d( -s, c, 0 ), CCVector3d( 0, 0, 1 ) };
	for ( int i = 0; i < 3; ++i )
	{
		const CCVector3d& N = axes[i];
		const double d = N.dot( m_focus );
		const double lo = d + m_rs[i]->v0() * m_scale;
		const double hi = d + m_rs[i]->v1() * m_scale;
		const double m = margin > 0 ? std::max( 0.3, 0.25 * ( m_rs[i]->v1() - m_rs[i]->v0() ) ) * m_scale : 0.0;
		pl.emplace_back( N.x, N.y, N.z, -lo + m );
		pl.emplace_back( -N.x, -N.y, -N.z, hi + m );
	}
	return pl;
}

void View3D::updateRegion( bool now )
{
	if ( !m_hasFocus || m_sourceID == 0 || m_extracting )
	{
		return;
	}
	const std::vector<Extract::Plane> want = wantedPlanes( 0.0 );
	bool covered = m_clone && m_regPlanes.size() == want.size();
	for ( size_t i = 0; covered && i < want.size(); ++i )
	{
		covered = Extract::inside( want[i], m_regPlanes[i] );
	}
	if ( covered )
	{
		// výřez je mnohem větší, než je potřeba (po zúžení posuvníků) → po chvíli vybrat menší a uvolnit paměť
		const std::vector<Extract::Plane> wide = wantedPlanes( 1.0 );
		bool tooBig = false;
		for ( size_t i = 0; i + 1 < wide.size(); i += 2 )
		{
			const double have = m_regPlanes[i].w + m_regPlanes[i + 1].w;
			const double need = wide[i].w + wide[i + 1].w;
			tooBig = tooBig || have > 3.0 * need;
		}
		if ( !tooBig )
		{
			return;
		}
	}
	if ( now || !m_clone )
	{
		extractRegion();
		return;
	}
	if ( !m_regTimer )
	{
		m_regTimer = new QTimer( this );
		m_regTimer->setSingleShot( true );
		connect( m_regTimer, &QTimer::timeout, this, [this]() { extractRegion(); } );
	}
	m_regTimer->start( 250 );
}

void View3D::extractRegion()
{
	ccHObject* obj = ( m_app && m_app->dbRootObject() ) ? m_app->dbRootObject()->find( m_sourceID ) : nullptr;
	ccPointCloud* src = obj ? ccHObjectCaster::ToPointCloud( obj ) : nullptr;
	if ( !src || !m_win )
	{
		return;
	}
	m_extracting = true;
	QApplication::setOverrideCursor( Qt::WaitCursor );
	const std::vector<Extract::Plane> planes = wantedPlanes( 1.0 );
	const unsigned cap = 60000000;
	unsigned total = 0;
	ccPointCloud* cl = Extract::byPlanes( src, planes, cap, total );
	QApplication::restoreOverrideCursor();
	m_extracting = false;
	if ( !cl )
	{
		m_app->dispToConsole( QStringLiteral( "[Řezy] Na výřez 3D náhledu nestačí paměť – zužte posuvníky." ), ccMainAppInterface::WRN_CONSOLE_MESSAGE );
		return;
	}
	if ( m_clone && m_blend )
	{
		m_blend->detachChild( m_clone );
	}
	delete m_clone;
	m_clone = cl;
	m_regPlanes = planes;
	m_clone->setName( src->getName() + QStringLiteral( " (3D náhled)" ) );
	m_clone->setGlobalShift( src->getGlobalShift() );
	m_clone->setGlobalScale( src->getGlobalScale() );
	if ( src->hasColors() )
	{
		m_clone->showColors( true );
	}
	m_clone->setPointSize( 2 );
	m_clone->setEnabled( true );
	m_clone->setVisible( !m_showPoints || m_showPoints->isChecked() );
	m_clone->setDisplay( m_win );
	m_blend->addChild( m_clone, ccHObject::DP_NONE );
	if ( !m_aux )
	{
		m_info->setText( total > cap ? QStringLiteral( "Výřez má %1 bodů – pro náhled zředěno na %2. Zužte posuvníky pro plnou hustotu." ).arg( total ).arg( m_clone->size() )
		                             : QStringLiteral( "%1 bodů ve výřezu (plná hustota). Prostřední klik do mračna = špendlík ve výkresu." ).arg( m_clone->size() ) );
	}
	applyClip(); // výřez už pokrývá → jen ořez a překreslení
}

void View3D::setLines( const std::vector<Line>& lines )
{
	m_lines = lines;
	buildLines();
}

void View3D::buildLines()
{
	if ( !m_win )
	{
		return;
	}
	clearLines();
	const double sh = m_shift ? m_shift->value() / 100.0 : 0.0;
	m_linesGroup = new ccHObject( "lines" );
	for ( const Line& l : m_lines )
	{
		if ( l.pts.size() < 2 )
		{
			continue;
		}
		const CCVector3d d = l.shiftDir * sh;
		auto* verts = new ccPointCloud( "v" );
		verts->reserve( static_cast<unsigned>( l.pts.size() ) );
		for ( const CCVector3d& P : l.pts )
		{
			verts->addPoint( ( P + d ).toFloat() );
		}
		verts->setEnabled( false );
		auto* poly = new RezLine( verts );
		poly->addChild( verts );
		poly->addPointIndex( 0, static_cast<unsigned>( l.pts.size() ) );
		poly->setColor( l.color );
		poly->showColors( true );
		poly->setWidth( 2 );
		poly->setDashed( l.dashed );
		m_linesGroup->addChild( poly );
	}
	m_linesGroup->setDisplay_recursive( m_win );
	m_linesGroup->setEnabled( m_showLines->isChecked() );
	m_win->addToOwnDB( m_linesGroup );
	applyClip();
	m_win->redraw();
}

void View3D::applyClip()
{
	if ( !m_win || !m_hasFocus )
	{
		return;
	}
	updateRegion( false ); // výřez v plné hustotě (hned, nebo po chvíli klidu)
	if ( !m_clone )
	{
		return;
	}
	if ( !m_aux )
	{
		saveSettings();
	}
	const CCVector3d& P = m_focus;
	const double c = std::cos( m_angle );
	const double s = std::sin( m_angle );
	const CCVector3d axes[3] = { CCVector3d( c, s, 0 ), CCVector3d( -s, c, 0 ), CCVector3d( 0, 0, 1 ) };

	m_clone->removeAllClipPlanes();
	m_clipSet.clear();
	for ( int i = 0; i < 3; ++i )
	{
		const CCVector3d& N = axes[i];
		const double d = N.dot( P );
		const double lo = d + m_rs[i]->v0() * m_scale;
		const double hi = d + m_rs[i]->v1() * m_scale;
		ccClipPlane pos;
		pos.equation = Tuple4Tpl<double>( N.x, N.y, N.z, -lo );
		ccClipPlane neg;
		neg.equation = Tuple4Tpl<double>( -N.x, -N.y, -N.z, hi );
		m_clipSet.push_back( pos );
		m_clipSet.push_back( neg );
	}
	for ( const ccClipPlane& cp : m_clipSet )
	{
		m_clone->addClipPlanes( cp );
	}
	// stejný ořez i pro nakreslené čáry
	if ( m_linesGroup )
	{
		for ( unsigned i = 0; i < m_linesGroup->getChildrenNumber(); ++i )
		{
			ccHObject* ch = m_linesGroup->getChild( i );
			ch->removeAllClipPlanes();
			for ( const ccClipPlane& cp : m_clipSet )
			{
				ch->addClipPlanes( cp );
			}
		}
	}
	m_clone->prepareDisplayForRefresh();
	rebuildSurvey(); // překreslí
}

void View3D::focusOn( const CCVector3d& P, double viewAngle )
{
	if ( !m_win || m_sourceID == 0 )
	{
		return;
	}
	if ( m_aux )
	{
		leaveAux( false );
	}
	m_focus = P;
	m_angle = viewAngle;
	m_hasFocus = true;
	updateRanges();
	applyClip();

	// pohled šikmo shora, ve směru natočení pohledu
	const double r = std::max( { std::abs( m_rs[0]->v0() ), std::abs( m_rs[0]->v1() ), std::abs( m_rs[1]->v0() ), std::abs( m_rs[1]->v1() ) } ) * m_scale;
	const double zLo = P.z + m_rs[2]->v0() * m_scale;
	const double zHi = P.z + m_rs[2]->v1() * m_scale;
	CCVector3 mn( static_cast<PointCoordinateType>( P.x - r ), static_cast<PointCoordinateType>( P.y - r ), static_cast<PointCoordinateType>( zLo ) );
	CCVector3 mx( static_cast<PointCoordinateType>( P.x + r ), static_cast<PointCoordinateType>( P.y + r ), static_cast<PointCoordinateType>( zHi ) );
	ccBBox box( mn, mx, true );

	m_win->setPerspectiveState( true, true );
	ccGLMatrixd rotZ;
	rotZ.initFromParameters( -viewAngle, CCVector3d( 0, 0, 1 ), CCVector3d( 0, 0, 0 ) );
	ccGLMatrixd view = ccGLUtils::GenerateViewMat( CC_ISO_VIEW_1 ) * rotZ;
	m_win->setBaseViewMat( view );
	m_win->updateConstellationCenterAndZoom( &box );
	m_win->setPivotPoint( CCVector3d( P.x, P.y, P.z ), false, false );
	m_win->redraw();
	if ( !isVisible() )
	{
		show();
	}
}

// ---------------------------------------------------------------- změřené body

void View3D::setSurveyPoints( const std::vector<SurveyPt>& pts, const ccColor::Rgb& marker, const ccColor::Rgb& name )
{
	m_survey = pts;
	m_surveyMarker = marker;
	m_surveyName = name;
	rebuildSurvey();
}

void View3D::rebuildSurvey()
{
	if ( !m_win )
	{
		return;
	}
	if ( !m_surveyLayer )
	{
		m_surveyLayer = new RezLayer;
		m_surveyLayer->setDisplay( m_win );
		m_win->addToOwnDB( m_surveyLayer );
	}
	m_surveyLayer->items.clear();
	if ( m_showSurvey && m_showSurvey->isChecked() )
	{
		// jen body v ořezu (jako mračno)
		const double c = std::cos( m_angle );
		const double sn = std::sin( m_angle );
		const CCVector3d axes[3] = { CCVector3d( c, sn, 0 ), CCVector3d( -sn, c, 0 ), CCVector3d( 0, 0, 1 ) };
		for ( const SurveyPt& sp : m_survey )
		{
			bool in = true;
			if ( m_hasFocus && !m_aux )
			{
				for ( int i = 0; i < 3 && in; ++i )
				{
					const double v = axes[i].dot( sp.P - m_focus ) / m_scale;
					in = v >= m_rs[i]->v0() && v <= m_rs[i]->v1();
				}
			}
			if ( !in )
			{
				continue;
			}
			RezLayer::Item it;
			it.P = sp.P;
			it.marker = true;
			it.markerColor = m_surveyMarker;
			it.color = m_surveyName;
			it.text = sp.name;
			it.background = true;
			m_surveyLayer->items.push_back( it );
		}
	}
	m_win->redraw();
}

// ---------------------------------------------------------------- špendlík

void View3D::clearPin()
{
	if ( m_pinGroup )
	{
		if ( m_win )
		{
			m_win->removeFromOwnDB( m_pinGroup );
		}
		delete m_pinGroup;
		m_pinGroup = nullptr;
	}
}

void View3D::buildPin()
{
	clearPin();
	if ( !m_win || !m_hasPin || !m_hasFocus )
	{
		return;
	}
	// velikost podle viditelného okolí
	const double r = std::max( { std::abs( m_rs[0]->v0() ), std::abs( m_rs[0]->v1() ), std::abs( m_rs[1]->v0() ), std::abs( m_rs[1]->v1() ) } ) * m_scale;
	const double h = std::max( 0.15 * m_scale, 0.3 * r );
	double head = 0.12 * h;
	CCVector3d bottom = m_pinP;
	CCVector3d top = m_pinP + CCVector3d( 0, 0, h );
	if ( m_aux )
	{
		// pomocný řez: jen když místo leží v řezu (kamera se nehýbe); jehla před mračnem, ať ji body nezakryjí
		const double sc = m_scale;
		const double u = ( m_pinP - m_auxA ).dot( m_auxU );
		const double dn = std::abs( ( m_pinP - m_focus ).dot( m_auxN ) );
		const double halfT = std::max( std::abs( m_rs[1]->v0() ), std::abs( m_rs[1]->v1() ) ) * sc;
		if ( u < -0.3 * sc || u > m_auxLen + 0.3 * sc || dn > halfT + 1.0 * sc )
		{
			return;
		}
		const CCVector3d F = m_auxN * ( 2.0 * sc - ( m_pinP - m_focus ).dot( m_auxN ) );
		head = 0.08 * sc;
		if ( m_pinPlan )
		{
			bottom = CCVector3d( m_pinP.x, m_pinP.y, m_auxFloor ) + F;
			top = CCVector3d( m_pinP.x, m_pinP.y, m_auxCeil ) + F;
		}
		else
		{
			bottom = m_pinP + F;
			top = bottom + CCVector3d( 0, 0, 0.8 * sc );
		}
	}
	else if ( m_pinPlan )
	{
		// půdorys: výška místa není dána → jehla přes celou výšku ořezu
		bottom.z = m_focus.z + m_rs[2]->v0() * m_scale;
		top.z = m_focus.z + m_rs[2]->v1() * m_scale + head;
	}
	m_pinGroup = new ccHObject( "pin" );
	auto* verts = new ccPointCloud( "v" );
	verts->reserve( 2 );
	verts->addPoint( bottom.toFloat() );
	verts->addPoint( top.toFloat() );
	verts->setEnabled( false );
	auto* needle = new ccPolyline( verts );
	needle->addChild( verts );
	needle->addPointIndex( 0, 2 );
	needle->setColor( ccColor::Rgb( 255, 40, 40 ) );
	needle->showColors( true );
	needle->setWidth( 3 );
	m_pinGroup->addChild( needle );
	ccGLMatrix tr;
	tr.setTranslation( ( top + CCVector3d( 0, 0, head ) ).toFloat() );
	auto* ball = new ccSphere( static_cast<PointCoordinateType>( head ), &tr, QStringLiteral( "pin head" ), 16 );
	ball->setColor( ccColor::Rgb( 255, 40, 40 ) );
	ball->showColors( true );
	m_pinGroup->addChild( ball );
	m_pinGroup->setDisplay_recursive( m_win );
	m_pinGroup->setEnabled( !m_showPin || m_showPin->isChecked() );
	m_win->addToOwnDB( m_pinGroup );
}

void View3D::pinAt( const CCVector3d& P, double viewAngle, bool plan )
{
	if ( !m_win || m_sourceID == 0 )
	{
		return;
	}
	m_pinP = P;
	m_pinPlan = plan;
	m_hasPin = true;
	if ( !m_hasFocus || m_aux )
	{
		// první ukázání: výchozí šikmý pohled
		focusOn( P, viewAngle );
		buildPin();
		m_win->redraw();
		return;
	}
	// natočení, perspektivu, směr ořezu i šířky posuvníků nechat – jen přesunout ořez a kameru
	m_focus = P;
	updateRanges();
	applyClip();
	const double r = std::max( { std::abs( m_rs[0]->v0() ), std::abs( m_rs[0]->v1() ), std::abs( m_rs[1]->v0() ), std::abs( m_rs[1]->v1() ) } ) * m_scale;
	const double zLo = P.z + m_rs[2]->v0() * m_scale;
	const double zHi = P.z + m_rs[2]->v1() * m_scale;
	CCVector3 mn( static_cast<PointCoordinateType>( P.x - r ), static_cast<PointCoordinateType>( P.y - r ), static_cast<PointCoordinateType>( zLo ) );
	CCVector3 mx( static_cast<PointCoordinateType>( P.x + r ), static_cast<PointCoordinateType>( P.y + r ), static_cast<PointCoordinateType>( zHi ) );
	ccBBox box( mn, mx, true );
	Q_UNUSED( box );
	// přiblížení nechat: kamera se jen posune o rozdíl starého a nového středu otáčení
	const ccViewportParameters& vp = m_win->getViewportParameters();
	const CCVector3d shift = P - vp.getPivotPoint();
	const CCVector3d eye = vp.getCameraCenter() + shift;
	m_win->setPivotPoint( CCVector3d( P.x, P.y, P.z ), false, false );
	m_win->setCameraPos( eye );
	buildPin();
	m_win->redraw();
	if ( !isVisible() )
	{
		show();
	}
}

void View3D::auxPin( const CCVector3d& P, bool plan )
{
	if ( !m_win || !m_aux )
	{
		return;
	}
	m_pinP = P;
	m_pinPlan = plan;
	m_hasPin = true;
	buildPin();
	m_win->redraw();
}

// ---------------------------------------------------------------- pomocný řez

void View3D::auxSection( const CCVector3d& A, const CCVector3d& B, const CCVector3d& N, double planZ, double zeroGlobal, double thickness )
{
	if ( !m_win || m_sourceID == 0 )
	{
		return;
	}
	const CCVector3d V( 0, 0, 1 );
	CCVector3d n( N.x, N.y, 0.0 );
	if ( n.norm() < 1e-9 )
	{
		return;
	}
	n.normalize();
	const CCVector3d U = V.cross( n );
	const double uA = A.dot( U );
	const double uB = B.dot( U );
	const CCVector3d left = ( uA <= uB ) ? A : B;
	const double len = std::abs( uB - uA );

	m_aux = true;
	m_auxA = CCVector3d( left.x, left.y, planZ );
	m_auxU = U;
	m_auxN = n;
	m_zeroG = zeroGlobal;
	m_measStep = 0;
	m_mp.clear();
	m_focus = m_auxA + U * ( len / 2.0 );
	m_angle = std::atan2( U.y, U.x );
	m_hasFocus = true;
	updateRanges();
	const double halfLen = len / 2.0 / m_scale + 0.3;
	const double halfT = std::max( 0.005, thickness / 2.0 );
	m_rs[0]->setValues( -halfLen, halfLen );
	m_rs[1]->setValues( -halfT, halfT );
	m_rs[2]->setValues( m_rs[2]->minimum(), m_rs[2]->maximum() );
	updateValueLabels();
	applyClip();

	// pravoúhlý pohled proti N
	ccGLMatrixd view;
	view.toIdentity();
	double* m = view.data();
	m[0] = U.x;
	m[4] = U.y;
	m[8] = U.z;
	m[1] = V.x;
	m[5] = V.y;
	m[9] = V.z;
	m[2] = n.x;
	m[6] = n.y;
	m[10] = n.z;
	m_win->setPerspectiveState( false, true );
	m_win->setBaseViewMat( view );
	m_win->setInteractionMode( ccGLWindowInterface::MODE_PAN_ONLY );
	m_win->setPivotVisibility( ccGLWindowInterface::PIVOT_HIDE );
	m_win->setWindowCursor( Qt::BlankCursor ); // nitkový kříž kreslíme sami
	updateRegion( true ); // body výřezu hned (kvůli odhadu podlahy a stropu)
	auxZoom( len );
	if ( m_hasPin )
	{
		buildPin(); // špendlík zůstává, pokud leží v novém řezu
	}
	m_floorPick = false;
	updateFloor(); // definovaná podlaha platí i pro další pomocný řez
	if ( m_writeRow )
	{
		m_writeRow->setVisible( true );
		m_modeCol->setVisible( true );
	}
	m_parapetMM = -1;
	clearMeasure();
	m_backBtn->setVisible( !m_auxOnly );
	setWindowTitle( QStringLiteral( "Řezy – pomocný řez (měření)" ) );
	m_info->setText( QStringLiteral( "<b>Pomocný řez</b> – klikněte 2 body = vzdálenost a převýšení; u kurzoru výška nad ±0,000 a v Bpv. "
	                                 "Pravé tažení = posun, pravý klik = zrušit měření, kolečko = zoom." ) );
	m_win->redraw();
	if ( !isVisible() )
	{
		show();
	}
	raise();
}

void View3D::leaveAux( bool refocus )
{
	if ( !m_aux )
	{
		return;
	}
	m_aux = false;
	m_measStep = 0;
	m_mp.clear();
	updateCross( QPoint(), false );
	clearMeasure();
	m_backBtn->setVisible( false );
	setWindowTitle( QStringLiteral( "Řezy – 3D náhled" ) );
	if ( m_writeRow )
	{
		m_writeRow->setVisible( false );
		m_modeCol->setVisible( false );
	}
	if ( m_win )
	{
		m_win->setInteractionMode( ccGLWindowInterface::MODE_TRANSFORM_CAMERA );
		m_win->setWindowCursor( Qt::ArrowCursor );
		m_win->setPivotVisibility( ccGLWindowInterface::PIVOT_SHOW_ON_MOVE );
		m_win->setPerspectiveState( true, true );
	}
	// zpět uložené hodnoty posuvníků 3D náhledu
	QSettings st;
	for ( int i = 0; i < 3; ++i )
	{
		const QString k = QStringLiteral( "qRezy/3d%1" ).arg( QLatin1String( AxisKey[i] ) );
		m_rs[i]->setValues( st.value( k + QStringLiteral( "0" ), AxisDefault[i][0] ).toDouble(), st.value( k + QStringLiteral( "1" ), AxisDefault[i][1] ).toDouble() );
	}
	updateValueLabels();
	m_info->setText( QStringLiteral( "Prostřední klik do řezu = ukázat místo. Levé tažení = otáčení, kolečko = zoom." ) );
	if ( refocus && m_hasFocus )
	{
		focusOn( m_focus, m_angle );
	}
}

bool View3D::pickPoint( const QPoint& pos, CCVector3d& P ) const
{
	if ( !m_win || !m_clone || !m_clone->isVisible() )
	{
		return false;
	}
	// bod mračna nejblíž kliknutí na obrazovce (v ořezu), z nich nejblíž k oku
	ccGLCameraParameters cam;
	m_win->getGLCameraParameters( cam );
	const double dpr = m_win->getDevicePixelRatio();
	const double cx = pos.x() * dpr;
	const double cy = cam.viewport[3] - 1 - pos.y() * dpr;
	const double tol = 6.0 * dpr;
	const double c = std::cos( m_angle );
	const double sn = std::sin( m_angle );
	const CCVector3d axes[3] = { CCVector3d( c, sn, 0 ), CCVector3d( -sn, c, 0 ), CCVector3d( 0, 0, 1 ) };
	double lo[3];
	double hi[3];
	for ( int i = 0; i < 3; ++i )
	{
		const double d = axes[i].dot( m_focus );
		lo[i] = m_hasFocus ? d + m_rs[i]->v0() * m_scale : -1e300;
		hi[i] = m_hasFocus ? d + m_rs[i]->v1() * m_scale : 1e300;
	}
	bool found = false;
	double bestZ = 2.0;
	for ( unsigned k = 0; k < m_clone->size(); ++k )
	{
		const CCVector3d Q = CCVector3d::fromArray( m_clone->getPoint( k )->u );
		bool in = true;
		for ( int i = 0; i < 3 && in; ++i )
		{
			const double v = axes[i].dot( Q );
			in = v >= lo[i] && v <= hi[i];
		}
		if ( !in )
		{
			continue;
		}
		CCVector3d S;
		if ( !cam.project( Q, S ) || std::abs( S.x - cx ) > tol || std::abs( S.y - cy ) > tol || S.z < 0.0 || S.z > 1.0 )
		{
			continue;
		}
		if ( S.z < bestZ )
		{
			bestZ = S.z;
			P = Q;
			found = true;
		}
	}
	return found;
}

bool View3D::auxPoint( const QPoint& pos, CCVector3d& P ) const
{
	if ( !m_win )
	{
		return false;
	}
	ccGLCameraParameters cam;
	m_win->getGLCameraParameters( cam );
	const QPointF g = m_win->toCornerGLCoordinates( pos.x(), pos.y() );
	CCVector3d Q;
	if ( !cam.unproject( CCVector3d( g.x(), g.y(), 0.5 ), Q ) )
	{
		return false;
	}
	// průmět do roviny řezu
	P = Q - m_auxN * ( ( Q - m_auxA ).dot( m_auxN ) );
	return true;
}

QString View3D::heightText( const CCVector3d& P ) const
{
	const double zG = m_clone ? m_clone->toGlobal3d( P ).z : P.z;
	const double rel = zG - m_zeroG;
	QString r = QString::number( std::abs( rel ), 'f', 3 ).replace( '.', ',' );
	r = ( std::abs( rel ) < 0.0005 ) ? QStringLiteral( "±0,000" ) : ( ( rel > 0 ? QStringLiteral( "+" ) : QStringLiteral( "-" ) ) + r );
	return QStringLiteral( "%1 (Bpv %2)" ).arg( r, QString::number( zG, 'f', 3 ).replace( '.', ',' ) );
}

bool View3D::floorMode() const
{
	return m_writeMode && m_writeMode->checkedId() == 4;
}

int View3D::measTotal() const
{
	if ( floorMode() )
	{
		return 1; // výška podlahy = 1 klik
	}
	return ( m_writeMode && m_writeMode->checkedId() == 0 ) ? 3 : 2;
}

int View3D::measNeeded() const
{
	return floorMode() ? 1 : measTotal() - ( m_hasFloor ? 1 : 0 );
}

QString View3D::floorText( const CCVector3d& P ) const
{
	// „+0,150 (245,320)“ – relativně k ±0,000 a v Bpv
	const double zG = m_clone ? m_clone->toGlobal3d( P ).z : P.z;
	const double rel = zG - m_zeroG;
	QString r = QString::number( std::abs( rel ), 'f', 3 ).replace( '.', ',' );
	r = ( std::abs( rel ) < 0.0005 ) ? QStringLiteral( "±0,000" ) : ( ( rel > 0 ? QStringLiteral( "+" ) : QStringLiteral( "-" ) ) + r );
	return QStringLiteral( "%1 (%2)" ).arg( r, QString::number( zG, 'f', 3 ).replace( '.', ',' ) );
}

std::vector<CCVector3d> View3D::measPts( const std::vector<CCVector3d>& clicks ) const
{
	std::vector<CCVector3d> r;
	if ( m_hasFloor && !clicks.empty() && !floorMode() )
	{
		r.push_back( CCVector3d( clicks.front().x, clicks.front().y, m_floorZ ) ); // podlaha pod prvním klikem
	}
	r.insert( r.end(), clicks.begin(), clicks.end() );
	return r;
}

void View3D::updateFloor()
{
	if ( m_floorBtn )
	{
		m_floorBtn->setChecked( m_hasFloor || m_floorPick );
		if ( m_hasFloor )
		{
			const QString h = heightText( CCVector3d( m_auxA.x, m_auxA.y, m_floorZ ) );
			m_floorBtn->setText( QStringLiteral( "Def. podlahy %1" ).arg( h.section( ' ', 0, 0 ) ) );
		}
		else
		{
			m_floorBtn->setText( m_floorPick ? QStringLiteral( "Def. podlahy – klikněte…" ) : QStringLiteral( "Def. podlahy" ) );
		}
	}
	if ( !m_win )
	{
		return;
	}
	if ( m_floorGroup )
	{
		m_win->removeFromOwnDB( m_floorGroup );
		delete m_floorGroup;
		m_floorGroup = nullptr;
	}
	if ( !m_hasFloor || !m_aux )
	{
		m_win->redraw();
		return;
	}
	// zelená vodorovná čára podlahy přes celý řez (před mračnem)
	const double sc = m_scale;
	const CCVector3d F = m_auxN * ( 2.0 * sc );
	const CCVector3d A0( m_auxA.x, m_auxA.y, m_floorZ );
	auto* verts = new ccPointCloud( "v" );
	verts->reserve( 2 );
	verts->addPoint( ( A0 - m_auxU * ( 0.3 * sc ) + F ).toFloat() );
	verts->addPoint( ( A0 + m_auxU * ( m_auxLen + 0.3 * sc ) + F ).toFloat() );
	verts->setEnabled( false );
	auto* line = new ccPolyline( verts );
	line->addChild( verts );
	line->addPointIndex( 0, 2 );
	line->setColor( ccColor::Rgb( 40, 220, 80 ) );
	line->showColors( true );
	line->setWidth( 2 );
	m_floorGroup = new ccHObject( "floor" );
	m_floorGroup->addChild( line );
	m_floorGroup->setDisplay_recursive( m_win );
	m_win->addToOwnDB( m_floorGroup );
	m_win->redraw();
}

void View3D::updateMeasure( bool live )
{
	if ( !m_win )
	{
		return;
	}
	QString txt = QStringLiteral( "<b>Pomocný řez</b> – kurzor: výška %1" ).arg( heightText( m_mCursor ) );
	std::vector<CCVector3d> clicks = m_mp;
	if ( ( !clicks.empty() || m_hasFloor ) && !floorMode() && static_cast<int>( clicks.size() ) < measNeeded() )
	{
		clicks.push_back( m_mCursor ); // náhled ke kurzoru (s podlahou hned od první čáry)
	}
	const std::vector<CCVector3d> pts = measPts( clicks );
	const std::vector<CCVector3d> full = measPts( m_mp );
	auto f = []( double v ) { return QString::number( v, 'f', 3 ).replace( '.', ',' ); };
	const bool okno = measTotal() == 3;
	for ( size_t i = 0; i + 1 < pts.size(); ++i )
	{
		const CCVector3d& A = pts[i];
		const CCVector3d& B = pts[i + 1];
		const double du = std::abs( ( B - A ).dot( m_auxU ) ) / m_scale;
		const double dz = std::abs( B.z - A.z ) / m_scale;
		const QString what = okno ? ( i == 0 ? QStringLiteral( "parapet" ) : QStringLiteral( "okno" ) ) : QStringLiteral( "svisle" );
		txt += QStringLiteral( "<br><b>%1 %2 m</b> · vodorovně %3 m · šikmo %4 m" ).arg( what, f( dz ), f( du ), f( std::sqrt( du * du + dz * dz ) ) );
	}
	if ( okno && full.size() == 3 )
	{
		const int par = static_cast<int>( std::lround( std::abs( full[1].z - full[0].z ) / m_scale * 100.0 ) ) * 10;
		const int win = static_cast<int>( std::lround( std::abs( full[2].z - full[1].z ) / m_scale * 100.0 ) ) * 10;
		txt += QStringLiteral( "<br>→ <b>%1(%2)</b> – Enter = zapsat do půdorysu" ).arg( win ).arg( par );
	}
	else if ( floorMode() )
	{
		txt += m_mp.empty() ? QStringLiteral( "<br>Podlaha: klikněte na podlahu, pak Enter." )
		                    : QStringLiteral( "<br>→ <b>%1</b> – Enter = zapsat do půdorysu" ).arg( floorText( m_mp.front() ) );
	}
	else if ( okno && m_mp.empty() )
	{
		txt += m_hasFloor ? QStringLiteral( "<br>Okno (podlaha definovaná): 1. klik parapet, 2. horní hrana okna, pak Enter." )
		                  : QStringLiteral( "<br>Okno: 1. klik podlaha, 2. parapet, 3. horní hrana okna, pak Enter." );
	}
	setMeasures( pts, m_auxN, 2.0 * m_scale );
	m_info->setText( txt );
	m_win->redraw( !live || m_mp.empty(), false );
}

bool View3D::eventFilter( QObject* obj, QEvent* event )
{
	if ( obj == m_widget && event->type() == QEvent::Wheel )
	{
		// zoom k místu pod kurzorem
		auto* e = static_cast<QWheelEvent*>( event );
		const int d = e->angleDelta().y();
		if ( d != 0 && !( e->modifiers() & ( Qt::ControlModifier | Qt::ShiftModifier | Qt::AltModifier ) ) )
		{
			zoomAtCursor( m_win, e->pos(), d / 8.0f );
			if ( m_aux )
			{
				updateCross( e->pos(), true );
			}
			return true;
		}
	}
	if ( obj == m_widget && !m_aux )
	{
		// prostřední klik (bez tažení) do mračna = špendlík ve výkresu
		if ( event->type() == QEvent::MouseButtonPress && static_cast<QMouseEvent*>( event )->button() == Qt::MiddleButton )
		{
			m_midPress = static_cast<QMouseEvent*>( event )->pos();
			m_midDown = true;
		}
		else if ( event->type() == QEvent::MouseButtonRelease && static_cast<QMouseEvent*>( event )->button() == Qt::MiddleButton && m_midDown )
		{
			m_midDown = false;
			const QPoint pos = static_cast<QMouseEvent*>( event )->pos();
			CCVector3d P;
			if ( ( pos - m_midPress ).manhattanLength() < 5 && pickPoint( pos, P ) )
			{
				m_pinP = P;
				m_pinPlan = false;
				m_hasPin = true;
				buildPin();
				m_win->redraw();
				Q_EMIT pinPicked( P.x, P.y, P.z );
			}
		}
		return QDockWidget::eventFilter( obj, event );
	}
	if ( obj != m_widget || !m_aux )
	{
		return QDockWidget::eventFilter( obj, event );
	}
	// pravý klik (bez tažení – pravé tažení = posun) zruší rozpracované měření, když není definovaná podlaha;
	// při výběru podlahy (Def. podlahy) zruší výběr
	if ( event->type() == QEvent::MouseButtonPress && static_cast<QMouseEvent*>( event )->button() == Qt::RightButton )
	{
		m_rightPress = static_cast<QMouseEvent*>( event )->pos();
		m_rightDown = true;
	}
	else if ( event->type() == QEvent::MouseButtonRelease && static_cast<QMouseEvent*>( event )->button() == Qt::RightButton && m_rightDown )
	{
		m_rightDown = false;
		if ( ( static_cast<QMouseEvent*>( event )->pos() - m_rightPress ).manhattanLength() < 5 )
		{
			if ( m_floorPick )
			{
				m_floorPick = false;
				updateFloor();
				updateMeasure( false );
				m_info->setText( m_info->text() + QStringLiteral( "<br>Výběr podlahy zrušen." ) );
			}
			else if ( !m_hasFloor && !m_mp.empty() )
			{
				m_mp.clear();
				m_measStep = 0;
				clearMeasure();
				Q_EMIT measureDone( QVector<double>(), m_auxN.x, m_auxN.y ); // smaže kótu i ve 3D náhledu
				updateMeasure( false );
				m_info->setText( m_info->text() + QStringLiteral( "<br>Měření zrušeno." ) );
			}
		}
	}
	// prostřední klik (bez tažení) do pomocného řezu = špendlík tady, v půdorysu i v 3D náhledu
	if ( event->type() == QEvent::MouseButtonPress && static_cast<QMouseEvent*>( event )->button() == Qt::MiddleButton )
	{
		m_midPress = static_cast<QMouseEvent*>( event )->pos();
		m_midDown = true;
	}
	else if ( event->type() == QEvent::MouseButtonRelease && static_cast<QMouseEvent*>( event )->button() == Qt::MiddleButton && m_midDown )
	{
		m_midDown = false;
		const QPoint pos = static_cast<QMouseEvent*>( event )->pos();
		CCVector3d P;
		if ( ( pos - m_midPress ).manhattanLength() < 5 && ( pickPoint( pos, P ) || auxPoint( pos, P ) ) )
		{
			auxPin( P, false );
			Q_EMIT pinPicked( P.x, P.y, P.z );
		}
	}
	switch ( event->type() )
	{
	case QEvent::KeyPress:
	{
		auto* k = static_cast<QKeyEvent*>( event );
		if ( k->key() == Qt::Key_Return || k->key() == Qt::Key_Enter )
		{
			writeMeasured();
			return true;
		}
		break;
	}
	case QEvent::MouseButtonPress:
	case QEvent::MouseButtonDblClick:
	{
		auto* e = static_cast<QMouseEvent*>( event );
		if ( e->button() != Qt::LeftButton )
		{
			break;
		}
		m_widget->setFocus( Qt::MouseFocusReason ); // Enter = Zapsat
		CCVector3d P;
		if ( m_floorPick && auxPoint( e->pos(), P ) )
		{
			m_floorPick = false;
			m_hasFloor = true;
			m_floorZ = P.z;
			m_mp.clear();
			m_measStep = 0;
			clearMeasure();
			updateFloor();
			m_mCursor = P;
			updateMeasure( false );
			return true;
		}
		if ( auxPoint( e->pos(), P ) )
		{
			m_mCursor = P;
			if ( static_cast<int>( m_mp.size() ) >= measNeeded() )
			{
				m_mp.clear(); // nové měření
			}
			m_mp.push_back( P );
			m_measStep = static_cast<int>( m_mp.size() );
			if ( static_cast<int>( m_mp.size() ) == measNeeded() )
			{
				QVector<double> xyz;
				for ( const CCVector3d& Q : measPts( m_mp ) )
				{
					xyz << Q.x << Q.y << Q.z;
				}
				Q_EMIT measureDone( xyz, m_auxN.x, m_auxN.y );
			}
			updateMeasure( false );
		}
		return true; // levé tlačítko neotáčí
	}
	case QEvent::MouseButtonRelease:
	{
		auto* e = static_cast<QMouseEvent*>( event );
		if ( e->button() == Qt::LeftButton )
		{
			return true;
		}
		break;
	}
	case QEvent::MouseMove:
	{
		auto* e = static_cast<QMouseEvent*>( event );
		CCVector3d P;
		updateCross( e->pos(), true );
		m_win->setWindowCursor( Qt::BlankCursor );
		if ( e->buttons() == Qt::NoButton && auxPoint( e->pos(), P ) )
		{
			m_mCursor = P;
			updateMeasure( true );
		}
		else
		{
			m_win->redraw( true, false );
		}
		break;
	}
	case QEvent::Leave:
		updateCross( QPoint(), false );
		m_win->redraw( true, false );
		break;
	default:
		break;
	}
	return QDockWidget::eventFilter( obj, event );
}

void View3D::writeMeasured()
{
	if ( !m_aux )
	{
		return;
	}
	const int need = measNeeded();
	if ( static_cast<int>( m_mp.size() ) < need )
	{
		if ( floorMode() )
		{
			m_info->setText( QStringLiteral( "<b>Podlaha</b> – nejdřív klikněte na podlahu, pak Enter." ) );
		}
		else if ( measTotal() == 3 )
		{
			m_info->setText( m_hasFloor ? QStringLiteral( "<b>Okno</b> – klikněte parapet a horní hranu okna (2 body), pak Enter." )
			                            : QStringLiteral( "<b>Okno</b> – klikněte podlahu, parapet a horní hranu okna (3 body), pak Enter." ) );
		}
		else
		{
			m_info->setText( m_hasFloor ? QStringLiteral( "<b>Pomocný řez</b> – nejdřív klikněte měřené místo (podlaha je definovaná), pak Enter." )
			                            : QStringLiteral( "<b>Pomocný řez</b> – nejdřív změřte 2 body (klik, klik), pak Enter." ) );
		}
		return;
	}
	const std::vector<CCVector3d> P = measPts( m_mp );
	const CCVector3d at = m_mp.back(); // místo odečtu výšky (na čáře řezu)
	// svislé vzdálenosti, zaokrouhlené na cm, v mm
	auto mm = [this]( const CCVector3d& A, const CCVector3d& B ) { return static_cast<int>( std::lround( std::abs( B.z - A.z ) / m_scale * 100.0 ) ) * 10; };
	const int mode = m_writeMode ? m_writeMode->checkedId() : 2;
	QString txt;
	if ( mode == 0 )
	{
		txt = QStringLiteral( "%1(%2)" ).arg( mm( P[1], P[2] ) ).arg( mm( P[0], P[1] ) ); // okno nad parapetem (parapet nad podlahou)
	}
	else if ( mode == 1 )
	{
		txt = QStringLiteral( "Vp=%1" ).arg( mm( P[0], P[1] ) );
	}
	else if ( mode == 3 )
	{
		txt = QStringLiteral( "Vs=%1" ).arg( mm( P[0], P[1] ) );
	}
	else if ( mode == 4 )
	{
		txt = floorText( P[0] );
	}
	else
	{
		txt = QString::number( mm( P[0], P[1] ) );
	}
	m_mp.clear();
	m_measStep = 0;
	clearMeasure();
	m_info->setText( QStringLiteral( "<b>Zapsáno do půdorysu: %1</b>" ).arg( txt ) );
	m_win->redraw();
	Q_EMIT writeHeight( txt, mode, at.x, at.y );
}

void View3D::clearMeasure()
{
	if ( m_measGroup )
	{
		if ( m_win )
		{
			m_win->removeFromOwnDB( m_measGroup );
		}
		delete m_measGroup;
		m_measGroup = nullptr;
	}
	if ( m_measLayer )
	{
		m_measLayer->items.clear();
	}
}

void View3D::setMeasures( const std::vector<CCVector3d>& pts, const CCVector3d& N, double front )
{
	if ( !m_win )
	{
		return;
	}
	clearMeasure();
	if ( pts.size() < 2 )
	{
		m_win->redraw();
		return;
	}
	CCVector3d n( N.x, N.y, 0.0 );
	if ( n.norm() < 1e-9 )
	{
		n = CCVector3d( 1, 0, 0 );
	}
	n.normalize();
	const CCVector3d U( -n.y, n.x, 0.0 ); // vodorovně v rovině řezu
	const CCVector3d Z( 0, 0, 1 );
	const CCVector3d F = n * front;
	const double t = 0.08 * m_scale;
	struct Seg
	{
		CCVector3d a;
		CCVector3d b;
		int width;
	};
	std::vector<Seg> segs;
	if ( !m_measLayer )
	{
		m_measLayer = new RezLayer;
		m_measLayer->setDisplay( m_win );
		m_win->addToOwnDB( m_measLayer );
	}
	m_measLayer->items.clear();
	for ( size_t i = 0; i + 1 < pts.size(); ++i )
	{
		const CCVector3d A = pts[i] + F;
		const CCVector3d B = pts[i + 1] + F;
		// svislá kóta v místě druhého bodu, od výšky prvního bodu
		const CCVector3d C = A + U * ( ( B - A ).dot( U ) );
		const CCVector3d B2 = C + Z * ( B.z - C.z );
		if ( ( C - A ).norm() > 1e-6 )
		{
			segs.push_back( { A, C, 1 } ); // vynášecí čára
		}
		segs.push_back( { C, B2, 3 } );
		for ( const CCVector3d& E : { C, B2 } )
		{
			segs.push_back( { E - U * t, E + U * t, 3 } );            // patka
			const CCVector3d sl = ( U + Z ) * ( t / std::sqrt( 2.0 ) );
			segs.push_back( { E - sl, E + sl, 2 } );                 // šikmá čárka
		}
		RezLayer::Item it;
		it.P = ( C + B2 ) / 2.0;
		it.text = QStringLiteral( "↕ %1" ).arg( QString::number( std::abs( B.z - A.z ) / m_scale, 'f', 3 ).replace( '.', ',' ) );
		it.color = ccColor::Rgb( 255, 220, 0 );
		it.background = true;
		m_measLayer->items.push_back( it );
	}
	m_measGroup = new ccHObject( "measure" );
	for ( const Seg& sg : segs )
	{
		auto* verts = new ccPointCloud( "v" );
		verts->reserve( 2 );
		verts->addPoint( sg.a.toFloat() );
		verts->addPoint( sg.b.toFloat() );
		verts->setEnabled( false );
		auto* poly = new ccPolyline( verts );
		poly->addChild( verts );
		poly->addPointIndex( 0, 2 );
		poly->setColor( ccColor::Rgb( 255, 220, 0 ) );
		poly->showColors( true );
		poly->setWidth( static_cast<PointCoordinateType>( sg.width ) );
		m_measGroup->addChild( poly );
	}
	m_measGroup->setDisplay_recursive( m_win );
	m_win->addToOwnDB( m_measGroup );
	m_win->redraw();
}

void View3D::auxZoom( double len )
{
	// pohled od podlahy ke stropu: nejhustší vodorovné vrstvy pod a nad výškou řezu
	const double sc = m_scale;
	const double z0 = m_auxA.z;
	double floorZ = z0 - 1.2 * sc;
	double ceilZ = z0 + 2.3 * sc;
	if ( m_clone && m_clone->size() > 0 )
	{
		const double bin = 0.03 * sc;
		const double lo = z0 - 3.0 * sc;
		const int nb = static_cast<int>( 9.0 * sc / bin ) + 1;
		std::vector<unsigned> hist( static_cast<size_t>( nb ), 0 );
		for ( unsigned k = 0; k < m_clone->size(); ++k )
		{
			const CCVector3* P = m_clone->getPoint( k );
			const double u = ( CCVector3d::fromArray( P->u ) - m_auxA ).dot( m_auxU );
			if ( u < 0 || u > len )
			{
				continue;
			}
			const int b = static_cast<int>( ( P->z - lo ) / bin );
			if ( b >= 0 && b < nb )
			{
				++hist[static_cast<size_t>( b )];
			}
		}
		auto best = [&]( double zFrom, double zTo, double& out )
		{
			const int b0 = std::max( 0, static_cast<int>( ( zFrom - lo ) / bin ) );
			const int b1 = std::min( nb - 1, static_cast<int>( ( zTo - lo ) / bin ) );
			unsigned mx = 0;
			for ( int b = b0; b <= b1; ++b )
			{
				if ( hist[static_cast<size_t>( b )] > mx )
				{
					mx = hist[static_cast<size_t>( b )];
					out = lo + ( b + 0.5 ) * bin;
				}
			}
			return mx > 20;
		};
		double f = floorZ;
		double c = ceilZ;
		if ( best( z0 - 2.5 * sc, z0 - 0.2 * sc, f ) )
		{
			floorZ = f;
		}
		if ( best( std::max( z0 + 0.3 * sc, floorZ + 2.0 * sc ), floorZ + 6.0 * sc, c ) )
		{
			ceilZ = c;
		}
	}
	m_auxLen = len;
	m_auxFloor = floorZ;
	m_auxCeil = ceilZ;
	const CCVector3d P0 = m_auxA - m_auxU * ( 0.2 * sc );
	const CCVector3d P1 = m_auxA + m_auxU * ( len + 0.2 * sc );
	CCVector3 mn( static_cast<PointCoordinateType>( std::min( P0.x, P1.x ) ), static_cast<PointCoordinateType>( std::min( P0.y, P1.y ) ), static_cast<PointCoordinateType>( floorZ - 0.15 * sc ) );
	CCVector3 mx( static_cast<PointCoordinateType>( std::max( P0.x, P1.x ) ), static_cast<PointCoordinateType>( std::max( P0.y, P1.y ) ), static_cast<PointCoordinateType>( ceilZ + 0.15 * sc ) );
	ccBBox box( mn, mx, true );
	m_win->updateConstellationCenterAndZoom( &box );
}

void View3D::updateCross( const QPoint& pos, bool show )
{
	if ( !m_win )
	{
		return;
	}
	if ( !m_cross[0] )
	{
		for ( ccPolyline*& c : m_cross )
		{
			auto* verts = new ccPointCloud( "cross" );
			verts->reserve( 2 );
			verts->addPoint( CCVector3( 0, 0, 0 ) );
			verts->addPoint( CCVector3( 0, 0, 0 ) );
			verts->setEnabled( false );
			c = new ccPolyline( verts );
			c->addChild( verts );
			c->addPointIndex( 0, 2 );
			c->set2DMode( true );
			c->setForeground( true );
			c->showColors( true );
			c->setWidth( 1 );
			c->setDisplay( m_win );
			m_win->addToOwnDB( c );
		}
	}
	if ( !show )
	{
		m_cross[0]->setEnabled( false );
		m_cross[1]->setEnabled( false );
		return;
	}
	// ±45 px jako v kreslení; na světlém pozadí černý
	const ccColor::Rgbub& bg = ccGui::Parameters().backgroundCol;
	const double lum = 0.299 * bg.r + 0.587 * bg.g + 0.114 * bg.b;
	const ccColor::Rgb col = ( lum > 140.0 ) ? ccColor::Rgb( 0, 0, 0 ) : ccColor::Rgb( 255, 255, 255 );
	ccGLCameraParameters cam;
	m_win->getGLCameraParameters( cam );
	const QPointF g = m_win->toCornerGLCoordinates( pos.x(), pos.y() );
	const double cx = g.x() - cam.viewport[2] / 2.0;
	const double cy = g.y() - cam.viewport[3] / 2.0;
	const double r = 45.0 * m_win->getDevicePixelRatio();
	const CCVector3 ends[2][2] = { { CCVector3( static_cast<PointCoordinateType>( cx - r ), static_cast<PointCoordinateType>( cy ), 0 ), CCVector3( static_cast<PointCoordinateType>( cx + r ), static_cast<PointCoordinateType>( cy ), 0 ) },
	                               { CCVector3( static_cast<PointCoordinateType>( cx ), static_cast<PointCoordinateType>( cy - r ), 0 ), CCVector3( static_cast<PointCoordinateType>( cx ), static_cast<PointCoordinateType>( cy + r ), 0 ) } };
	for ( int i = 0; i < 2; ++i )
	{
		auto* verts = static_cast<ccPointCloud*>( m_cross[i]->getAssociatedCloud() );
		*const_cast<CCVector3*>( verts->getPointPersistentPtr( 0 ) ) = ends[i][0];
		*const_cast<CCVector3*>( verts->getPointPersistentPtr( 1 ) ) = ends[i][1];
		m_cross[i]->setColor( col );
		m_cross[i]->setEnabled( true );
	}
}
