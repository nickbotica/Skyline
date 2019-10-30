#include "SMRRadar.hpp"
#pragma once

enum class TagType { Departure, Arrival, Airborne, Uncorrelated, Uncorrelated_C, Uncorrelated_S, Correlated };

class Tag
{
public:
	void static render(Graphics& graphics, CSMRRadar& radar);
	map<string, string> static generateTagData(CSMRRadar& radar, CRadarTarget& rt);
	vector<vector<char>> static buildTagText(Graphics& graphics, CSMRRadar& radar, map<string, string> tagDataMap, TagType tagType);
	CRect static getTagBlockPosition(Graphics& graphics, Gdiplus::Font& font, vector<vector<char>>& tagText, POINT tagCentre);
	void static drawTagBorder(Graphics& graphics, Color& green, CRect tagRect);
	void static drawTagText(Graphics& graphics, Gdiplus::Font& font, Color& green, CRect& tagPosition, vector<vector<char>>& tagText);
	void static drawTagLine(Graphics& graphics, Pen& pen, Point& acPos, CRect tagBlock);
	POINT static getTagPosition(CSMRRadar& radar, CRadarTarget& rt);
private:
	Tag() {};
};

TagType static getTagType(CSMRRadar& radar, CRadarTarget& rt);
std::string static getTagTypeString(TagType type);

void static insertStringIntoVector(string& stringToInsert, int startingIndex, vector<char>& array);