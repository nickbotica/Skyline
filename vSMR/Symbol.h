#pragma once

class Symbol
{
public:
	void static render(Graphics& graphics, CSMRRadar& radar, CRadarTarget& rt, Color& activeColor);
	void static drawPrimaryTarget(Graphics& graphics, Pen& pen, Point acPos);
	void static drawPrimaryAndSecondayTarget(Graphics& graphics, Pen& pen, Point acPos);
	void static drawSecondaryTarget(Graphics& graphics, Pen& pen, Point acPos);
	void static drawHistoryPoint(Graphics& graphics, Color& color, Point point);
private:
	Symbol() {}
};