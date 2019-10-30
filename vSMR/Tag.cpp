#include "stdafx.h"
#include "EuroScopePlugIn.h"
#include "SMRRadar.hpp"
#include "ESHelper.h"
#include "Tag.h"

using namespace EuroScopePlugIn;

void Tag::render(Graphics& graphics, CSMRRadar& radar)
{
	CRadarTarget rt;
	for (rt = radar.GetPlugIn()->RadarTargetSelectFirst(); rt.IsValid(); rt = radar.GetPlugIn()->RadarTargetSelectNext(rt))
	{
		CRadarTargetPositionData rtPos = rt.GetPosition();
		Point acPosPix = radar.GetPoint(rtPos.GetPosition());
		int reportedGs = rtPos.GetReportedGS();

		// No tag for no radar or primary radar
		if (rt.GetPosition().GetRadarFlags() <= RADAR_POSITION_PRIMARY)
			continue;

		auto font = radar.customFonts[radar.currentFontSize];
		Color green(108, 245, 113);
		Pen linePen(green, 1.0F);

		auto tagType = getTagType(radar, rt);

		// keeps track of flights that the controller has manually released
		if (std::find(ReleasedTracks.begin(), ReleasedTracks.end(), rt.GetSystemID()) != ReleasedTracks.end())
			continue;

		// Getting the tag center/offset
		auto tagCenterPosition = getTagPosition(radar, rt);

		auto tagDataMap = generateTagData(radar, rt);

		// ----- Generating the clickable map -----
		map<string, int> TagClickableMap;
		TagClickableMap[tagDataMap["callsign"]] = TAG_CITEM_CALLSIGN;
		TagClickableMap[tagDataMap["actype"]] = TAG_CITEM_FPBOX;
		TagClickableMap[tagDataMap["sctype"]] = TAG_CITEM_FPBOX;
		TagClickableMap[tagDataMap["sqerror"]] = TAG_CITEM_FPBOX;
		TagClickableMap[tagDataMap["deprwy"]] = TAG_CITEM_RWY;
		TagClickableMap[tagDataMap["seprwy"]] = TAG_CITEM_RWY;
		TagClickableMap[tagDataMap["gate"]] = TAG_CITEM_GATE;
		TagClickableMap[tagDataMap["sate"]] = TAG_CITEM_GATE;
		TagClickableMap[tagDataMap["flightlevel"]] = TAG_CITEM_NO;
		TagClickableMap[tagDataMap["gs"]] = TAG_CITEM_NO;
		TagClickableMap[tagDataMap["vsi"]] = TAG_CITEM_NO;
		TagClickableMap[tagDataMap["wake"]] = TAG_CITEM_FPBOX;
		TagClickableMap[tagDataMap["tssr"]] = TAG_CITEM_NO;
		TagClickableMap[tagDataMap["asid"]] = TagClickableMap[tagDataMap["ssid"]] = TAG_CITEM_SID;
		TagClickableMap[tagDataMap["systemid"]] = TAG_CITEM_NO;
		TagClickableMap[tagDataMap["groundstatus"]] = TAG_CITEM_GROUNDSTATUS;


		vector<vector<char>> tagText = buildTagText(graphics, radar, tagDataMap, tagType);
		
		auto tagRect = getTagBlockPosition(graphics, *font, tagText, tagCenterPosition);

		drawTagText(graphics, *font, green, tagRect, tagText);

		if (radar.mouseWithin(tagRect) || radar.IsTagBeingDragged(rt.GetCallsign()))
		{
			drawTagBorder(graphics, green, tagRect);
			radar.RequestRefresh();
		}

		drawTagLine(graphics, linePen, acPosPix, tagRect);

		// Adding the tag click screen object
		radar.tagAreas[rt.GetCallsign()] = tagRect;
		radar.AddScreenObject(DRAWING_TAG, rt.GetCallsign(), tagRect, true, radar.GetBottomLine(rt.GetCallsign()).c_str());
	}
}

vector<vector<char>> Tag::buildTagText(Graphics& graphics, CSMRRadar& radar, map<string, string> tagDataMap, TagType tagType)
{
	const Value& labelsSettings = radar.CurrentConfig->getActiveProfile()["labels"];
	const Value& tagDefinition = labelsSettings[getTagTypeString(tagType).c_str()]["definition"];
	const int tagBlockWidth = labelsSettings[getTagTypeString(tagType).c_str()]["width"].GetInt();

	vector<vector<char>> tagTextBlock(0);

	for (int i = 0; i < tagDefinition.Size(); i++)
	{
		// An empty array of the defined width
		vector<char> tagTextLine(tagBlockWidth, ' ');

		const Value& definitionLine = tagDefinition[i];

		for (int j = 0; j < definitionLine.Size(); j++)
		{
			auto element = definitionLine[j].GetString();
			auto elementParts = split(element, '|');
			int position = stoi(elementParts[0]);
			string data = elementParts[1];

			// swaps element with actual data e.g. "callsign" for "ANZ123"
			for (auto& kv : tagDataMap)
				replaceAll(data, kv.first, kv.second);

			insertStringIntoVector(data, position, tagTextLine);
		}
		tagTextBlock.push_back(tagTextLine);
	}
	return tagTextBlock;
}

CRect Tag::getTagBlockPosition(Graphics& graphics, Gdiplus::Font& font, vector<vector<char>>& tagText, POINT tagCentre)
{
	int numLines = tagText.size();
	int numChars = tagText[0].size();

	auto measure = RectF();
	graphics.MeasureString(L"A", 1, &font, PointF(), &StringFormat(), &measure);
	int charWidth = measure.GetRight();
	int charHeight = measure.GetBottom();

	int tagWidth = numChars * charWidth;
	int tagHeight = numLines * charHeight;

	return CRect(tagCentre.x - (tagWidth / 2), tagCentre.y - (tagHeight / 2), tagCentre.x + (tagWidth / 2), tagCentre.y + (tagHeight / 2));
}

map<string, string> Tag::generateTagData(CSMRRadar& radar, CRadarTarget& rt)
{
	// TODO: refactor this, got half way moving the map into each section. Maybe get away from strings as the key
	map<string, string> TagReplacingMap;

	auto fp = rt.GetCorrelatedFlightPlan();

	// ----- SSR -------
	TagReplacingMap["ssr"] = rt.GetPosition().GetSquawk();

	// ----- Callsign -------
	TagReplacingMap["callsign"] = rt.GetCallsign();

	// ----- Wake category -------
	if (fp.IsValid()) {
		TagReplacingMap["wake"] = fp.GetFlightPlanData().GetAircraftWtc();
	}

	// ----- Aircraft type -------

	if (fp.IsValid() && fp.GetFlightPlanData().IsReceived())
	{
		string actype = fp.GetFlightPlanData().GetAircraftFPType();
		actype = actype.substr(0, 4);
		TagReplacingMap["actype"] = actype;
	}

	// ----- Flightlevel -------

	// Transition altitude is 13000 ft
	// A switchs for F >=140
	// Transition level is 15,000 ft

	int fl = rt.GetPosition().GetFlightLevel();
	int padding = 5;
	string pfls = "F";
	if (fl <= 14000) {
		fl = rt.GetPosition().GetPressureAltitude();
		pfls = "A";
		padding = 5;
	}
	string flightlevel = (pfls + padWithZeros(padding, fl)).substr(0, 4);
	TagReplacingMap["flightlevel"] = flightlevel;

	// ----- Vertical speed indicator -------
	string vsi = " ";
	int delta_fl = rt.GetPosition().GetFlightLevel() - rt.GetPreviousPosition(rt.GetPosition()).GetFlightLevel();
	if (abs(delta_fl) >= 50) {
		if (delta_fl < 0) {
			vsi = "|";
		}
		else {
			vsi = "^";
		}
	}

	// ----- Temp Altitude -----
	//TODO: not sure this is efficient
	string tempAlt;

	if (fp.IsValid()) {

		int clearedAlt = fp.GetControllerAssignedData().GetClearedAltitude();

		// no temp alt set display FP final alt
		if (clearedAlt <= 0)
		{
			int fa = fp.GetFinalAltitude();
			tempAlt = padWithZeros(5, fp.GetFinalAltitude()).substr(0, 3);
		}
		// if cleared for instrument approach
		if (clearedAlt == 1)
		{
			tempAlt = "APP";
		}
		// if cleared visual approach
		else if (clearedAlt == 2)
		{
			tempAlt = "VIS";
		}
		// if a cleared altitude is set display it
		else if (clearedAlt > 2)
		{
			tempAlt = padWithZeros(5, (clearedAlt / 100)).substr(0, 3);
		}

		// if aircraft is cruising at final altitude +-50ft display nothing
		if ((rt.GetPosition().GetFlightLevel() >= fp.GetFinalAltitude() - 50) &&
			(rt.GetPosition().GetFlightLevel() < fp.GetFinalAltitude() + 50))
		{
			tempAlt = "   ";
		}
	}

	// ----- Groundspeed -------
	string speed = std::to_string(rt.GetPosition().GetReportedGS());

	// ----- Controller ID -------
	string controller = "";
	if (fp.IsValid())
	{
		controller = fp.GetTrackingControllerId();
	}

	// ----- Destination -------
	string dest = "    ";
	if (fp.IsValid())
	{
		dest = fp.GetFlightPlanData().GetDestination();
	}

	// ----- Assigned Squawk -------
	string assr = "    ";
	if (fp.IsValid())
	{
		assr = fp.GetControllerAssignedData().GetSquawk();
	}

	// ----- Scratchpad -------
	string scratchpad = "";
	if (fp.IsValid())
	{
		scratchpad = fp.GetControllerAssignedData().GetScratchPadString();
	}

	// ----- STAR -------
	string star = "       ";
	if ((rt.GetPosition().GetPressureAltitude() > 4000) && fp.IsValid())
	{
		star = fp.GetFlightPlanData().GetStarName();
	}


	// System ID for uncorrelated
	TagReplacingMap["systemid"] = "T:";
	string tpss = rt.GetSystemID();
	TagReplacingMap["systemid"].append(tpss.substr(1, 6));
	
	TagReplacingMap["vsi"] = vsi;
	TagReplacingMap["tempalt"] = tempAlt;
	TagReplacingMap["gs"] = speed;
	TagReplacingMap["controller"] = controller;
	TagReplacingMap["dest"] = dest;
	TagReplacingMap["assr"] = assr;
	TagReplacingMap["scratchpad"] = scratchpad;
	TagReplacingMap["star"] = star;

	return TagReplacingMap;
}

POINT Tag::getTagPosition(CSMRRadar &radar, CRadarTarget &rt)
{
	POINT tagCentre;
	auto acPosPix = radar.ConvertCoordFromPositionToPixel(rt.GetPosition().GetPosition());

	auto itr = radar.TagsOffsets.find(rt.GetCallsign());
	if (itr != radar.TagsOffsets.end()) {
		tagCentre = { acPosPix.x + itr->second.x, acPosPix.y + itr->second.y };
	}
	else
	{
		// Use angle
		if (radar.TagAngles.find(rt.GetCallsign()) == radar.TagAngles.end())
			radar.TagAngles[rt.GetCallsign()] = 270.0f;

		int lenght = 100;
		if (radar.TagLeaderLineLength.find(rt.GetCallsign()) != radar.TagLeaderLineLength.end())
			lenght = radar.TagLeaderLineLength[rt.GetCallsign()];

		tagCentre.x = long(acPosPix.x + float(lenght * cos(DegToRad(radar.TagAngles[rt.GetCallsign()]))));
		tagCentre.y = long(acPosPix.y + float(lenght * sin(DegToRad(radar.TagAngles[rt.GetCallsign()]))));
	}
	return tagCentre;
}

void Tag::drawTagText(Graphics& graphics, Gdiplus::Font& font,  Color& green, CRect& tagPosition, vector<vector<char>>& tagText)
{
	SolidBrush brush(green);
	
	auto measure = RectF();
	graphics.MeasureString(L"A", 1, &font, PointF(), &StringFormat(), &measure);
	int charHeight = measure.GetBottom();

	int heightOffset = 0;

	for each (auto line in tagText)
	{
		auto wLine = wstring{ line.begin(), line.end() };

		auto point = PointF(REAL(tagPosition.left), REAL(tagPosition.top + heightOffset) );

		graphics.DrawString(wLine.c_str(), wcslen(wLine.c_str()), &font, point, &brush);

		heightOffset += charHeight;
	}
}

void Tag::drawTagBorder(Graphics& graphics, Color& green, CRect tagRect)
{
		Pen pw(green);
		tagRect.InflateRect(3, 3);
		graphics.DrawRectangle(&pw, CopyRect(tagRect));
}

// Drawing the line from the radar target to the tag
void Tag::drawTagLine(Graphics &graphics, Pen &pen, Point &acPos, CRect tagBlock)
{
	tagBlock.InflateRect(8, 8);

	CPoint clipFrom1, clipTo1, clipFrom2, clipTo2;

	auto tabBlockCentre = CPoint(tagBlock.CenterPoint().x, tagBlock.CenterPoint().y);
	auto acPosRect = CRect(acPos.X - 10, acPos.Y - 10, acPos.X + 10, acPos.Y + 10);

	// This gives the to and from points of the line inside the rect, we want the outside points
	LiangBarsky(acPosRect, CPoint(acPos.X, acPos.Y), tabBlockCentre, clipFrom1, clipTo1);
	
	LiangBarsky(tagBlock, clipTo1, tabBlockCentre, clipFrom2, clipTo2);
		
	graphics.DrawLine(&pen, Point(clipTo1.x, clipTo1.y), Point(clipFrom2.x, clipFrom2.y));
}


void insertStringIntoVector(string& stringToInsert, int startingIndex, vector<char>& array)
{
	int stringIndex = 0;
	for (int arrayIndex = startingIndex; arrayIndex < array.size(); arrayIndex++)
	{
		if (stringIndex < stringToInsert.length())
			array[arrayIndex] = stringToInsert[stringIndex];
		else
			return;

		stringIndex++;
	}
}

// TagType Helpers
TagType getTagType(CSMRRadar& radar, CRadarTarget& rt)
{
	if (ESHelper::isCorrelated(rt))
	{
		return TagType::Correlated;
	}
	else
	{
		// Primary + Sec_C and Sec_C
		if (rt.GetPosition().GetRadarFlags() < RADAR_POSITION_SECONDARY_S)
		{
			return TagType::Uncorrelated_C;
		}
		// Primary + Sec_S, Sec_C + Sec_S and Sec_S
		else
		{
			return TagType::Uncorrelated_S;
		}
	}
}

std::string getTagTypeString(TagType type)
{
	if (type == TagType::Departure)
		return "departure";
	if (type == TagType::Arrival)
		return "arrival";
	if (type == TagType::Uncorrelated)
		return "uncorrelated";
	if (type == TagType::Correlated)
		return "correlated";
	if (type == TagType::Uncorrelated_C)
		return "uncorrelated_c";
	if (type == TagType::Uncorrelated_S)
		return "uncorrelated_s";
	return "airborne";
}