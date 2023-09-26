/*
 * Copyright (c) 2007, Laminar Research.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 *
 */

#include "WED_AirportChain.h"
#include "WED_AirportNode.h"
#include "IODefs.h"
#include "WED_EnumSystem.h"
#include "WED_Errors.h"
#include "AptDefs.h"
#include "WED_XMLWriter.h"

DEFINE_PERSISTENT(WED_AirportChain)

WED_AirportChain::WED_AirportChain(WED_Archive* a, int i)
    : WED_GISChain(a, i), closed(0), lines(this, PROP_Name("Line Attributes", XML_Name("", "")), "Line Attributes", 1),
      lights(this, PROP_Name("Light Attributes", XML_Name("", "")), "Light Attributes", 1)
{
}

WED_AirportChain::~WED_AirportChain()
{
}

void WED_AirportChain::CopyFrom(const WED_AirportChain* rhs)
{
    WED_GISChain::CopyFrom(rhs);
    StateChanged();
    closed = rhs->closed;
}

void WED_AirportChain::SetClosed(int closure)
{
    if (closed != closure)
    {
        StateChanged();
        closed = closure;
    }
}
/*
IGISPoint *	WED_AirportChain::SplitSide   (int n)
{
    int c = GetNumSides();

    if (n > c) return NULL;

    Bezier2		b;
    if (GetSide(n, b))
    {
        WED_AirportNode * node = WED_AirportNode::CreateTyped(GetArchive());

        Bezier2	b1, b2;
        b.partition(b1,b2,0.5);

        node->SetLocation(b2.p1);
        node->SetControlHandleHi(b2.c1);

        node->SetParent(this, n+1);
        return node;

    } else {

        WED_AirportNode * node = WED_AirportNode::CreateTyped(GetArchive());

        node->SetLocation(b.as_segment().midpoint(0.5));
        node->SetControlHandleHi(b.as_segment().midpoint(0.5));

        node->SetParent(this, n+1);
        return node;
    }

}
*/
bool WED_AirportChain::IsClosed(void) const
{
    return closed;
}

bool WED_AirportChain::ReadFrom(IOReader* reader)
{
    bool r = WED_GISChain::ReadFrom(reader);
    reader->ReadInt(closed);
    return r;
}

void WED_AirportChain::WriteTo(IOWriter* writer)
{
    WED_GISChain::WriteTo(writer);
    writer->WriteInt(closed);
}

void WED_AirportChain::AddExtraXML(WED_XMLElement* obj)
{
    WED_XMLElement* xml = obj->add_sub_element("airport_chain");
    xml->add_attr_int("closed", closed);
}

void WED_AirportChain::StartElement(WED_XMLReader* reader, const XML_Char* name, const XML_Char** atts)
{
    if (strcmp(name, "airport_chain") == 0)
    {
        const XML_Char* c = get_att("closed", atts);
        if (c)
            closed = atoi(c);
        else
            reader->FailWithError("closed is missing.");
    }
    else
        WED_GISChain::StartElement(reader, name, atts);
}

void WED_AirportChain::EndElement(void)
{
}

void WED_AirportChain::PopHandler(void)
{
}

void WED_AirportChain::Import(const AptMarking_t& x, void (*print_func)(void*, const char*, ...), void* ref)
{
    SetName(x.name);
}

void WED_AirportChain::Export(AptMarking_t& x) const
{
    GetName(x.name);
}

void WED_AirportChain::GetResource(std::string& r) const
{
    r.clear(); // return a std::string ONLY if lines and light are std::set uniformly throughout all nodes

    PropertyVal_t line;
    lines.GetProperty(line);
    PropertyVal_t light;
    lights.GetProperty(light);

    //	std::string n; this->GetName(n);
    //	printf("%s: lin=%d lgt=%d\n",n.c_str(), line.set_val.size(), light.set_val.size());

    if (line.set_val.size() == 1)
    {
        const char* c = ENUM_Desc(*line.set_val.begin());
        if (c)
            r += c;
    }
    else if (line.set_val.size() > 1)
        return;

    if (light.set_val.size() == 1)
    {
        const char* c = ENUM_Desc(*light.set_val.begin());
        if (c)
        {
            if (!r.empty())
                r += "$^";
            r += c;
        }
    }
    else
        return;
}
