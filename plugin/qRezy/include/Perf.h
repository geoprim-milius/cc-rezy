#pragma once

// Měření času (jen pro ladění): proměnná prostředí QREZY_TIMING → výpis do stderr
#include <QElapsedTimer>
#include <QtGlobal>

#include <cstdio>

struct PerfScope
{
	explicit PerfScope( const char* name )
		: m_name( name )
	{
		if ( enabled() )
		{
			m_t.start();
		}
	}
	~PerfScope()
	{
		if ( enabled() )
		{
			std::fprintf( stderr, "[perf] %-28s %8.2f ms\n", m_name, static_cast<double>( m_t.nsecsElapsed() ) / 1e6 );
			std::fflush( stderr );
		}
	}
	static bool enabled()
	{
		static const bool e = qEnvironmentVariableIsSet( "QREZY_TIMING" );
		return e;
	}

private:
	const char* m_name;
	QElapsedTimer m_t;
};
