/* 
 * Copyright (c) 2014, Laminar Research.
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


#include "WED_VerTable.h"
#include "GUI_Messages.h"
#include "STLUtils.h"
#include <sstream>

const int kDefCols[7] = { 110, 100, 110, 120,   120, 110, 110 };
const char * kHeaders[7] = { "Scenery ID", "Parent ID", "User Name", "Status",
							"Date Uploaded", "Artist Comments", "Moderator Comments" };

std::string ChooseStatus(const VerInfo_t & info)
{
	if(info.isRecommended == true)
	{
		return "Recommended";
	}
	else return info.status;
}

std::string ChooseDate(const VerInfo_t & info)
{	
	//Dates come in the format YYYY-MM-DDTHH:MM:SS.000Z, which we'll be shortening to YY-MM-DD HH:MM:SS
	//The total length is 24
	std::string s = info.dateAccepted.substr(2,info.dateAccepted.size()-7);//cut of the .000Z
	s[8] = ' ';//Cut out the T
	return s;
}


WED_VerTable::WED_VerTable(const VerVector * apts) :
	GUI_SimpleTableGeometry(7,kDefCols),
	mVers(apts),
	mSortColumn(3),       //Start with sorting by the date
	mInvertSort(false)
{
}	


WED_VerTable::~WED_VerTable()
{
}

	
void	WED_VerTable::SetFilter(
						const std::string&				new_filter)
{
	mFilter = new_filter;
	resort();
}
			
void	WED_VerTable::VerVectorChanged(void)
{
	resort();
}
			
void	WED_VerTable::GetSelection(
						std::set<int>&					out_selection)
{
	out_selection = mSelected;
}

void	WED_VerTable::SelectHeaderCell(
						int							cell_x)
{
	if(cell_x == mSortColumn)
		mInvertSort = !mInvertSort;
	else
	{
		mSortColumn = cell_x;
		mInvertSort = true;
	}
	resort();
}

void	WED_VerTable::GetHeaderContent(int cell_x, GUI_HeaderContent& the_content)
{
	the_content.is_selected = (cell_x == mSortColumn);
	the_content.can_resize = 1;
	the_content.can_select = 1;

	if (cell_x < 7) 
		the_content.title = kHeaders[cell_x];
}

int		WED_VerTable::GetColCount(void)
{
/*#if DEV TODO - The developer menu shows every single field
	return 11;//Derived from HTTP Get for airport
#endif*/
	return 7;//Derived from the catagories USERS will care about
}

int		WED_VerTable::GetRowCount(void)
{
	return mSorted.size();
}


void	WED_VerTable::GetCellContent(
					int							cell_x,
					int							cell_y,
					GUI_CellContent&			the_content)
{
	the_content.content_type = gui_Cell_EditText;
	the_content.can_delete = false;
	the_content.can_edit = 0;
	the_content.can_disclose = 0;
	the_content.can_select = 1;
	the_content.can_drag = 0;

	int ver_id = mSorted[cell_y];

	the_content.is_disclosed = 0;
	the_content.is_selected = mSelected.count(ver_id);
	the_content.indent_level = 0;


	switch(cell_x) {
	case 0:
		the_content.text_val = std::to_string(mVers->at(ver_id).sceneryId);
		break;
	case 1:
		the_content.text_val = std::to_string(mVers->at(ver_id).parentId);
		break;
	case 2:		
		the_content.text_val = mVers->at(ver_id).userName;
		break;
	case 3:		
		the_content.text_val = ChooseStatus(mVers->at(ver_id));
		break;
	case 4:
		the_content.text_val = ChooseDate(mVers->at(ver_id));
		break;
	case 5:
		the_content.text_val = mVers->at(ver_id).artistComments;
		break;
	case 6:
		the_content.text_val = mVers->at(ver_id).moderatorComments;
		break;
	}
	the_content.string_is_resource = 0;
}

void	WED_VerTable::SelectionStart(
					int							clear)
{
	if(clear)
		mSelected.clear();
	mSelectedOrig = mSelected;
}

int		WED_VerTable::SelectGetExtent(
					int&						low_x,
					int&						low_y,
					int&						high_x,
					int&						high_y)
{
	if(mSorted.empty())	
		return 0;
	low_x = 0;
	high_x = 0;
	low_y = mSorted.size();
	high_y = 0;
	for(int i = 0; i < mSorted.size(); ++i)
	if(mSelected.count(mSorted[i]))
	{
		low_y = min(low_y,i);
		high_y = max(high_y,i);
	}
	if(low_y <= high_y)
		return 1;
	else
		return 0;
}

int		WED_VerTable::SelectGetLimits(
					int&						low_x,
					int&						low_y,
					int&						high_x,
					int&						high_y)
{
	if(mSorted.empty())	return 0;
	low_x = 0;
	low_y = 0;
	high_x = 0;
	high_y = mSorted.size()-1;
	return 1;
}

void	WED_VerTable::SelectRange(
					int							start_x,
					int							start_y,
					int							end_x,
					int							end_y,
					int							is_toggle)
{
	mSelected = mSelectedOrig;

	for(int x = start_y; x <= end_y; ++x)
	{
		int apt_id = mSorted[x];
		if(is_toggle && mSelected.count(apt_id))		mSelected.erase (apt_id);
		else											mSelected.insert(apt_id);		
	}
	BroadcastMessage(GUI_TABLE_CONTENT_CHANGED,0);
}

void	WED_VerTable::SelectionEnd(void)
{
}

int		WED_VerTable::SelectDisclose(
					int							open_it,
					int							all)
{
	return 0;
}

int		WED_VerTable::TabAdvance(
					int&						io_x,
					int&						io_y,
					int							reverse,
					GUI_CellContent&			the_content)
{
	return 0;
}

int		WED_VerTable::DoubleClickCell(
					int							cell_x,
					int							cell_y)
{
	return 0;
}

struct sort_by_ver {
	sort_by_ver(const VerVector * vers, int sort_column, int invert_sort) : vers_(vers), sort_column_(sort_column), invert_sort_(invert_sort) { }

	bool operator()(int x, int y) const {
		
		std::string xs;
		std::string ys;
		//Select the strings to compare based on the sort column chosen
		switch(sort_column_)
		{
		case 0:
			return (invert_sort_) ? (vers_->at(y).sceneryId < vers_->at(x).sceneryId) : (vers_->at(x).sceneryId < vers_->at(y).sceneryId);
		case 1:
			xs = vers_->at(x).userName;
			ys = vers_->at(y).userName;
			break;
		case 2:
			xs = ChooseStatus(vers_->at(x));
			ys = ChooseStatus(vers_->at(y));
			break;
		case 3:
			xs = ChooseDate(vers_->at(x));
			ys = ChooseDate(vers_->at(y));
			break;
		case 4:
			xs = vers_->at(x).artistComments;
			ys = vers_->at(y).artistComments;
			break;
		case 5:
			xs = vers_->at(x).moderatorComments;
			ys = vers_->at(y).moderatorComments;
			break;
		case 6:
			return (invert_sort_) ? (vers_->at(y).parentId < vers_->at(x).parentId) : (vers_->at(x).parentId < vers_->at(y).parentId);

		}

		for(auto &c: xs) c = toupper(c);
		for(auto &c: ys) c = toupper(c);
	
		if(invert_sort_)
			return ys < xs;
		else
			return xs < ys;
	}

	int sort_column_;
	int invert_sort_;
	const VerVector * vers_;
};


void		WED_VerTable::resort(void)
{
	std::vector<std::string>	filters;
	tokenize_string_func(mFilter.begin(),mFilter.end(),back_inserter(filters),::isspace);

	mSorted.clear();
	for(int i = 0; i < mVers->size(); ++i)
	{
		std::string idStr = std::to_string(mVers->at(i).sceneryId);
		std::string parentIdStr = std::to_string(mVers->at(i).parentId);
	
		if (filters.empty() || 
				filter_match(idStr, filters.begin(),filters.end())		||
				filter_match(parentIdStr, filters.begin(),filters.end())		||
				filter_match(mVers->at(i).userName, filters.begin(),filters.end())		||
				filter_match(ChooseStatus(mVers->at(i)), filters.begin(),filters.end()) ||
				filter_match(ChooseDate(mVers->at(i)),filters.begin(),filters.end())	||
				filter_match(mVers->at(i).artistComments,filters.begin(),filters.end())	||
				filter_match(mVers->at(i).moderatorComments,filters.begin(),filters.end())
		)
		{
			mSorted.push_back(i);
		}
	}
	sort(mSorted.begin(),mSorted.end(), sort_by_ver(mVers, mSortColumn,mInvertSort));
	BroadcastMessage(GUI_TABLE_CONTENT_RESIZED,0);
}
