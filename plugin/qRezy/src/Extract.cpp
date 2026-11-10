#include "Extract.h"

#include <ReferenceCloud.h>
#include <ccPointCloud.h>

#include <algorithm>
#include <cmath>
#include <thread>

namespace Extract
{
	bool inside( const Plane& q, const Plane& p )
	{
		const double dot = q.x * p.x + q.y * p.y + q.z * p.z;
		return dot > 0.999999 && q.w <= p.w + 1e-9;
	}

	ccPointCloud* byPlanes( ccPointCloud* src, const std::vector<Plane>& planes, unsigned cap, unsigned& total )
	{
		total = 0;
		if ( !src )
		{
			return nullptr;
		}
		const unsigned n = src->size();
		const unsigned threads = std::max( 1u, std::min( 16u, std::thread::hardware_concurrency() ) );
		std::vector<std::vector<unsigned>> parts( threads );
		std::vector<std::thread> pool;
		const unsigned chunk = ( n + threads - 1 ) / threads;
		for ( unsigned t = 0; t < threads; ++t )
		{
			pool.emplace_back( [&, t]()
			{
				const unsigned a = t * chunk;
				const unsigned b = std::min( n, a + chunk );
				std::vector<unsigned>& out = parts[t];
				for ( unsigned i = a; i < b; ++i )
				{
					const CCVector3* P = src->getPoint( i );
					bool in = true;
					for ( const Plane& pl : planes )
					{
						if ( pl.x * P->x + pl.y * P->y + pl.z * P->z + pl.w < 0.0 )
						{
							in = false;
							break;
						}
					}
					if ( in )
					{
						out.push_back( i );
					}
				}
			} );
		}
		for ( std::thread& th : pool )
		{
			th.join();
		}
		size_t count = 0;
		for ( const auto& p : parts )
		{
			count += p.size();
		}
		total = static_cast<unsigned>( count );
		CCCoreLib::ReferenceCloud sel( src );
		const size_t step = ( cap > 0 && count > cap ) ? static_cast<size_t>( std::ceil( static_cast<double>( count ) / cap ) ) : 1;
		if ( !sel.reserve( static_cast<unsigned>( count / step + 1 ) ) )
		{
			return nullptr;
		}
		size_t k = 0;
		for ( const auto& p : parts )
		{
			for ( unsigned idx : p )
			{
				if ( k++ % step == 0 )
				{
					sel.addPointIndex( idx );
				}
			}
		}
		if ( sel.size() == 0 )
		{
			// prázdný výběr – prázdná kopie
			auto* empty = new ccPointCloud( src->getName() );
			return empty;
		}
		int err = 0;
		ccPointCloud* cl = src->partialClone( &sel, &err, false );
		return cl;
	}
}
