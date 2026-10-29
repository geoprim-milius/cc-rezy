#pragma once

// Nakreslená čára – ccPolyline, která umí zobrazit zakrytou čáru čárkovaně

#include <ccPolyline.h>

class RezLine : public ccPolyline
{
public:
	explicit RezLine( GenericIndexedCloudPersist* vertices )
		: ccPolyline( vertices )
	{
	}

	void setDashed( bool d ) { m_dashed = d; }
	bool dashed() const { return m_dashed; }

protected:
	void drawMeOnly( CC_DRAW_CONTEXT& context ) override;

private:
	bool m_dashed = false;
};
