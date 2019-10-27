#include <array>
#pragma once

class Symbol
{
public:
	void static render(Graphics* graphics, CSMRRadar* radar);
	void static drawPrimaryTarget(Graphics* graphics, Pen* pen, Point acPos);
	void static drawPrimaryAndSecondayTarget(Graphics* graphics, Pen* pen, Point acPos);
	void static drawSecondaryTarget(Graphics* graphics, Pen* pen, Point acPos);
	void static drawHistoryPoint(Graphics* graphics, Color color, Point point);
	std::array<CPosition, 5> static getPreviousPositions(CRadarTarget rt);
private:
	Symbol() {}
};