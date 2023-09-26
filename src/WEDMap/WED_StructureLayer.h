#ifndef WED_STRUCTURELAYER_H
#define WED_STRUCTURELAYER_H

#include "WED_MapLayer.h"

class WED_StructureLayer : public WED_MapLayer
{
public:
    WED_StructureLayer(GUI_Pane* host, WED_MapZoomerNew* zoomer, IResolver* resolver);
    virtual ~WED_StructureLayer();

    bool GetRealLinesShowing(void) const;
    void SetRealLinesShowing(bool show);
    bool GetVerticesShowing(void) const;
    void SetVerticesShowing(bool show);

    virtual bool DrawEntityStructure(bool inCurrent, IGISEntity* entity, GUI_GraphState* g, bool selected, bool locked);
    virtual bool DrawEntityVisualization(bool inCurrent, IGISEntity* entity, GUI_GraphState* g, int selected);
    virtual void DrawStructure(bool inCurrent, GUI_GraphState* g);
    virtual void GetCaps(bool& draw_ent_v, bool& draw_ent_s, bool& cares_about_sel, bool& wants_clicks);

private:
    bool mRealLines;
    bool mVertices;

    std::vector<short> mAirportIconsXY;
    std::vector<unsigned> mAirportIconsC;
    std::vector<short> mSeaportIconsXY;
    std::vector<unsigned> mSeaportIconsC;
    std::vector<short> mHeliportIconsXY;
    std::vector<unsigned> mHeliportIconsC;
};

#endif
