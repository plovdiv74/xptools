/*
 * Copyright (c) 2011, Laminar Research.
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

#include "WED_ATCTimeRule.h"
#include "AptDefs.h"

DEFINE_PERSISTENT(WED_ATCTimeRule)
TRIVIAL_COPY(WED_ATCTimeRule, WED_Thing)

WED_ATCTimeRule::WED_ATCTimeRule(WED_Archive* a, int id)
    : WED_Thing(a, id), start_time_zulu(this, PROP_Name("Start (Zulu)", XML_Name("atc_timerule", "start_zulu")), 0, 4),
      end_time_zulu(this, PROP_Name("End (Zulu)", XML_Name("atc_timerule", "end_zulu")), 0, 4)
{
}

WED_ATCTimeRule::~WED_ATCTimeRule()
{
}

void WED_ATCTimeRule::Import(const AptTimeRule_t& info, void (*print_func)(void*, const char*, ...), void* ref)
{
    start_time_zulu = info.start_zulu;
    end_time_zulu = info.end_zulu;
    PropEditCallback(0); // give it a meaningfull name on Gateway/apt.dat import (it has no name in that data, anyways)
}

void WED_ATCTimeRule::Export(AptTimeRule_t& info) const
{
    info.start_zulu = start_time_zulu.value;
    info.end_zulu = end_time_zulu.value;
}

void WED_ATCTimeRule::PropEditCallback(int before)
{
    if (before)
        StateChanged(wed_Change_Properties);
    else
    {
        char buf[20];
        snprintf(buf, 20, "Time %04d-%04dz", start_time_zulu.value, end_time_zulu.value);
        std::string old_name;
        GetName(old_name);
        if (old_name != buf) // Prevent infinite recursion by calling SetName() if name actually changes
            SetName(buf);
    }
}
