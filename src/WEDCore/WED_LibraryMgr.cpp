/*
 * Copyright (c) 2008, Laminar Research.
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

#include "WED_LibraryMgr.h"
#include "WED_PackageMgr.h"
#include "WED_Messages.h"
#include "WED_EnumSystem.h"
#include "AssertUtils.h"
#include "FileUtils.h"
#include "PlatformUtils.h"
#include "MemFileUtils.h"
#include <time.h>

void WED_clean_vpath(std::string& s)
{
    for (std::string::size_type p = 0; p < s.size(); ++p)
        if (s[p] == '\\' || s[p] == ':')
            s[p] = '/';
}

void WED_clean_rpath(std::string& s)
{
    for (std::string::size_type p = 0; p < s.size(); ++p)
        if (s[p] == '\\' || s[p] == ':' || s[p] == '/')
            s[p] = DIR_CHAR;

    while (s.size() && (s.back() < '!' || s.back() > 'z')) // will also truncate strings ending in UTF-8 characters. But
                                                           // all legal art assets end in 3-ASCII letter suffixes !
        s.pop_back(); // trailing spaces or CTRL characters cause chaos in the ResourceMgr when assembling relative
                      // paths
}

// checks if path includes enough '..' to possibly not be a true subdirectory of the current directory
// i.e. dir/../x  or  d/../x or ./x      are fine
//      ../x  or  dir/../../x  or ./../x  or  dir/./../../x get flagged
static bool is_no_true_subdir_path(std::string& s)
{
    int subdir_levels = 0;
    for (std::string::size_type p = 0; p < s.size(); ++p)
        if (s[p] == '\\' || s[p] == ':' || s[p] == '/')
        {
            if (p >= 1 && s[p - 1] != '.')
                subdir_levels++;
            else if (p >= 2 && s[p - 1] == '.')
            {
                if (s[p - 2] == '.')
                    subdir_levels--;
                else if (s[p - 2] != '\\' && s[p - 2] != ':' && s[p - 2] != '/')
                    subdir_levels++;
            }

            if (subdir_levels < 0)
                return true;
        }
    return false;
}

static void split_path(const std::string& i, std::string& p, std::string& f)
{
    std::string::size_type n = i.rfind('/');
    if (n == i.npos)
    {
        f = i;
        p.clear();
    }
    else
    {
        p = i.substr(0, n);
        f = i.substr(n + 1);
    }
}

static int is_direct_parent(const std::string& parent, const std::string& child)
{
    if (parent.empty())
        return child.find('/', 1) == child.npos;

    if ((parent.size() + 1) >= child.size())
        return false; // Not a child if parent is longer than child - remember we need '/' too.
    if (strncasecmp(parent.c_str(), child.c_str(), parent.size()) != 0)
        return false; // Not a child if doesn't contain parent path
    if (child[parent.size()] != '/')
        return false; // Not a child if parent name has gunk after it
    if (child.find('/', parent.size() + 1) != child.npos)
        return false; // Not a child if child contains subdirs beyond parent
    return true;
}

// Library manager constructor
WED_LibraryMgr::WED_LibraryMgr(const std::string& ilocal_package) : local_package(ilocal_package)
{
    DebugAssert(gPackageMgr != NULL);
    gPackageMgr->AddListener(this);
    Rescan();
}

WED_LibraryMgr::~WED_LibraryMgr()
{
}

std::string WED_LibraryMgr::GetLocalPackage() const
{
    return local_package;
}

bool WED_LibraryMgr::GetLineVpath(int lt, std::string& vpath)
{
    auto l = default_lines.find(lt);
    if (l == default_lines.end())
        return false;
    else
    {
        vpath = l->second;
        return true;
    }
}

std::string WED_LibraryMgr::GetResourceParent(const std::string& r)
{
    std::string p, f;
    split_path(r, p, f);
    return p;
}

struct
{
    bool operator()(const std::string& lhs, const std::string& rhs) const
    {
        std::string::size_type pl = lhs.find_last_of('/');
        std::string::size_type pr = rhs.find_last_of('/');

        if (pl == pr && pl < lhs.length() - 2 && pr < rhs.length() - 2)
        {
            if (pl == std::string::npos)
                pl = 0;
            else
            {
                int path_cmp = strncasecmp(lhs.c_str(), rhs.c_str(), pl);
                if (path_cmp != 0)
                    return path_cmp < 0;
                pl += 1;
            }
            const char* l = lhs.c_str() + pl;
            const char* r = rhs.c_str() + pl;

            if ((*l >= '0' && *l <= '9') || (*r >= '0' && *r <= '9'))
            {
                int int_l, int_r;
                if (sscanf(l, "%d", &int_l) > 0)
                    if (sscanf(r, "%d", &int_r) > 0)
                        if (int_l != int_r)
                            return int_l < int_r;
            }
            return strcasecmp(l, r) < 0;
        }
        return strcasecmp(lhs.c_str(), rhs.c_str()) < 0;
    }
} special_compare;

void WED_LibraryMgr::GetResourceChildren(const std::string& r, int filter_package, std::vector<std::string>& children,
                                         bool no_dirs)
{
    children.clear();
    res_map_t::iterator me = r.empty() ? res_table.begin() : res_table.find(r);
    if (me == res_table.end())
        return;
    if (me->second.res_type != res_Directory)
        return;

    if (!r.empty())
        ++me;

    while (me != res_table.end())
    {
        if (no_dirs && me->second.res_type == res_Directory)
        {
            ++me;
            continue;
        }
        if (me->first.size() < r.size())
            break;
        if (strncasecmp(me->first.c_str(), r.c_str(), r.size()) != 0)
            break;
        // Ben says: even in WED 1.6 we still don't show private or deprecated stuff
        if (me->second.status >= status_Public)
            if (is_direct_parent(r, me->first))
            {
                bool want_it = true;
                switch (filter_package)
                {
                case pack_Library:
                    want_it = me->second.packages.size() > 1 ||
                              !me->second.packages.count(pack_Local); // Lib if we are in two packs or we are NOT in
                                                                      // local.  (We are always SOMEWHERE)
                case pack_All:
                    break;
                case pack_Default:
                    want_it = me->second.is_default;
                    break;
                case pack_New:
                    want_it = me->second.status == status_New;
                    break;
                case pack_Local: // Since "local" is a virtal index, the search for Nth pack works for local too.
                default:
                    want_it = me->second.packages.count(filter_package);
                }
                if (want_it)
                    children.push_back(me->first);
            }
        ++me;
    }
std:
    sort(children.begin(), children.end(), special_compare);
}

res_type WED_LibraryMgr::GetResourceType(const std::string& r) const
{
    auto me = res_table.find(r);
    if (me == res_table.end())
        return res_None;
    return (res_type)me->second.res_type;
}

std::string WED_LibraryMgr::GetResourcePath(const std::string& r, int variant)
{
    auto me = res_table.find(r);
    if (me == res_table.end() ||
        r != me->first) // this prevents the case-insensitive compare (needed for desired sort order in libmgr list)
        return std::string(); // to deliver a match if the cases mis-match - which is X-Plane behavior
    DebugAssert(variant < me->second.real_paths.size());
    return me->second.real_paths[variant];
}

bool WED_LibraryMgr::IsResourceDefault(const std::string& r) const
{
    auto me = res_table.find(r);
    if (me == res_table.end())
        return false;
    return me->second.is_default;
}

bool WED_LibraryMgr::IsResourceLocal(const std::string& r) const
{
    auto me = res_table.find(r);
    if (me == res_table.end())
        return false;
    return me->second.packages.count(pack_Local) && me->second.packages.size() == 1;
}

bool WED_LibraryMgr::IsResourceLibrary(const std::string& r) const
{
    auto me = res_table.find(r);
    if (me == res_table.end())
        return false;
    return !me->second.packages.count(pack_Local) || me->second.packages.size() > 1;
}

bool WED_LibraryMgr::IsResourceDeprecatedOrPrivate(const std::string& r) const
{
    auto me = res_table.find(r);
    if (me == res_table.end())
        return true; // library list == never public exported = not public !
    return me->second.status <
           status_SemiDeprecated; // status "Yellow' is still deemed public wrt validation, i.e. allowed on the gateway
}

bool WED_LibraryMgr::IsSeasonal(const std::string& r) const
{
    auto me = res_table.find(r);
    if (me == res_table.end() || me->second.res_type == res_Directory)
        return false;
    return me->second.has_seasons;
}

bool WED_LibraryMgr::IsRegional(const std::string& r) const
{
    auto me = res_table.find(r);
    if (me == res_table.end() || me->second.res_type == res_Directory)
        return false;
    return me->second.has_regions;
}

bool WED_LibraryMgr::DoesPackHaveLibraryItems(int package) const
{
    for (auto& i : res_table)
        if (i.second.packages.count(package))
        {
            //	The problem here is that a resource can be defined in multiple libraries,
            //  some of those definitions may be deprecated or private, but others not.
            //  If there is at least one public definition, the resource has status >= status_Public.
            //  So its impossible to find out this way if a given library has no public items ...

            // if  (i.second.status > status_Public)
            //			printf("Pack %d '%s' status = %d\n",package,i.second.real_path.c_str(),i.second.status);
            //			if ( i.second.status >= status_Public)

            return true;
        }
    return false;
}

int WED_LibraryMgr::GetNumVariants(const std::string& r) const
{
    res_map_t::const_iterator me = res_table.find(r);
    if (me == res_table.end())
        return 1;
    return me->second.real_paths.size();
}

std::string WED_LibraryMgr::CreateLocalResourcePath(const std::string& r)
{
    return gPackageMgr->ComputePath(local_package, r);
}

void WED_LibraryMgr::ReceiveMessage(GUI_Broadcaster* inSrc, intptr_t inMsg, intptr_t inParam)
{
    if (inMsg == msg_SystemFolderChanged || inMsg == msg_SystemFolderUpdated)
    {
        Rescan();
    }
}

struct local_scan_t
{
    std::string partial;
    std::string full;
    WED_LibraryMgr* who;
};

void WED_LibraryMgr::Rescan()
{
    res_table.clear();
    int np = gPackageMgr->CountPackages();

    for (int p = 0; p < np; ++p)
    {
        if (gPackageMgr->IsDisabled(p))
            continue;
        // the physical directory of the scenery pack
        std::string pack_base;
        // Get the pack's physical location
        gPackageMgr->GetNthPackagePath(p, pack_base);

        pack_base += DIR_STR "library.txt";

        bool is_default_pack = gPackageMgr->IsPackageDefault(p);
        bool in_region = false;
        std::string all_region, current_region;

        MFMemFile* lib = MemFile_Open(pack_base.c_str());

        if (lib)
        {
            gPackageMgr->GetNthPackagePath(p, pack_base);
            MFScanner s;
            MFS_init(&s, lib);

            res_status cur_status = status_Public;
            int lib_version[] = {800, 1200, 0};

            if (MFS_xplane_header(&s, lib_version, "LIBRARY", NULL) == 0)
            {
                LOG_MSG("E/LIB unsupported version or header data in %s\n", pack_base.c_str());
            }
            else
                while (!MFS_done(&s))
                {
                    std::string vpath, rpath;
                    bool is_export_backup = false;
                    bool is_season = false;

                    if (MFS_string_match(&s, "EXPORT", false) || MFS_string_match(&s, "EXPORT_EXTEND", false) ||
                        MFS_string_match(&s, "EXPORT_EXCLUDE", false) ||
                        (is_season = (MFS_string_match(&s, "EXPORT_SEASON", false) ||
                                      MFS_string_match(&s, "EXPORT_EXTEND_SEASON", false) ||
                                      MFS_string_match(&s, "EXPORT_EXCLUDE_SEASON", false))) ||
                        (is_export_backup = MFS_string_match(&s, "EXPORT_BACKUP", false)))
                    {
                        if (is_season)
                        {
                            std::string season;
                            MFS_string(&s, &season);
                            if (season.find("sum") == std::string::npos)
                            {
                                MFS_string_eol(&s, NULL);
                                continue;
                            }
                        }
                        MFS_string(&s, &vpath);
                        MFS_string_eol(&s, &rpath);
                        WED_clean_vpath(vpath);
                        WED_clean_rpath(rpath);

                        if (is_no_true_subdir_path(rpath))
                            break; // ignore paths that lead outside current scenery directory
                        rpath = pack_base + DIR_STR + rpath;
                        FILE_case_correct((char*)rpath.c_str()); /* yeah - I know I'm overriding the 'const' protection
                          of the c_str() here. But I know this operation is never going to change the strings length, so
                          thats OK to do. And I have to case-correct the path right here, as this path later is not only
                          used by the case insensitive MF_open() but also to derive the paths to the textures referenced
                          in those assets. And those textures are loaded with case-sensitive fopen.
                          */
                        AccumResource(vpath, p, rpath, is_default_pack, cur_status, is_export_backup, is_season,
                                      in_region);
                    }
                    else if (MFS_string_match(&s, "EXPORT_RATIO", false))
                    {
                        double x = MFS_double(&s);
                        MFS_string(&s, &vpath);
                        MFS_string_eol(&s, &rpath);
                        WED_clean_vpath(vpath);
                        WED_clean_rpath(rpath);
                        if (is_no_true_subdir_path(rpath))
                            break; // ignore paths that lead outside current scenery directory
                        rpath = pack_base + DIR_STR + rpath;
                        FILE_case_correct(
                            (char*)rpath
                                .c_str()); // yeah - I know I'm overriding the 'const' protection of the c_str() here.
                        AccumResource(vpath, p, rpath, is_default_pack, cur_status);
                    }
                    else
                    {
                        if (MFS_string_match(&s, "PUBLIC", true))
                        {
                            cur_status = status_Public;

                            int new_until = 0;
                            new_until = MFS_int(&s);
                            if (new_until > 20170101)
                            {
                                time_t rawtime;
                                struct tm* timeinfo;
                                time(&rawtime);
                                timeinfo = localtime(&rawtime);
                                int now =
                                    10000 * (timeinfo->tm_year + 1900) + 100 * timeinfo->tm_mon + timeinfo->tm_mday;
                                if (new_until >= now)
                                {
                                    cur_status = status_New;
                                }
                            }
                        }
                        else if (MFS_string_match(&s, "PRIVATE", true))
                            cur_status = status_Private;
                        else if (MFS_string_match(&s, "DEPRECATED", true))
                            cur_status = status_Deprecated;
                        else if (MFS_string_match(&s, "SEMI_DEPRECATED", true))
                            cur_status = status_SemiDeprecated;
                        else if (MFS_string_match(&s, "REGION_DEFINE", false))
                            MFS_string(&s, &current_region);
                        else if (MFS_string_match(&s, "REGION_RECT", false))
                        {
                            int west = MFS_int(&s);
                            int south = MFS_int(&s);
                            int east = MFS_int(&s);
                            int north = MFS_int(&s);
                            if (west == -180 && east == 179 && south == -90 && north == 89)
                            {
                                all_region = current_region;
                                LOG_MSG("I/Lib %s has global region '%s'\n", pack_base.c_str(), all_region.c_str());
                            }
                        }
                        else if (MFS_string_match(&s, "REGION", false))
                        {
                            std::string r;
                            MFS_string(&s, &r);
                            in_region = r != all_region;
                        }

                        MFS_string_eol(&s, NULL);
                    }
                }
            MemFile_Close(lib);
        }
    }
    RescanLines();
    RescanSurfaces();

    std::string package_base;
    package_base = gPackageMgr->ComputePath(local_package, "");
    if (!package_base.empty())
    {
        package_base.erase(package_base.length() - 1);

        local_scan_t info;
        info.who = this;
        info.full = package_base;
        MF_IterateDirectory(package_base.c_str(), AccumLocalFile, reinterpret_cast<void*>(&info));
    }
    BroadcastMessage(msg_LibraryChanged, 0);
    LOG_MSG("I/Lib scan finished, %d vpaths\n", (int)res_table.size());
}

void WED_LibraryMgr::RescanLines()
{
    std::vector<int> existing_line_enums;
    DOMAIN_Members(LinearFeature, existing_line_enums);

    std::set<int> existing_line_types;
    for (std::vector<int>::iterator e = existing_line_enums.begin(); e != existing_line_enums.end(); ++e)
    {
        existing_line_types.insert(ENUM_Export(*e));
    }
    default_lines.clear();

    res_map_t::iterator m = res_table.begin();
    while (m != res_table.end() && m->first.find("lib/airport/lines/", 0) == std::string::npos)
        ++m;

    while (m != res_table.end() && m->first.find("lib/airport/lines/", 0) != std::string::npos)
    {
        std::string resnam(m->first);
        resnam.erase(0, strlen("lib/airport/lines/"));

        if (resnam[0] >= '0' && resnam[0] <= '9' &&
            //		   m->second.is_default &&
            m->second.status >= status_Public && resnam.substr(resnam.size() - 4) == ".lin")
        {
            resnam.erase(resnam.size() - 4);

            // create human readable Description (also used as XML keyword) from resource file name
            int linetype;
            char nice_name[40];
            sscanf(resnam.c_str(), "%d%*c%29s", &linetype, nice_name);
            for (int i = 0; i < 30; ++i)
            {
                if (nice_name[i] == 0)
                    break;
                if (i == 0)
                    nice_name[0] = toupper(nice_name[0]);
                if (nice_name[i] == '_')
                {
                    nice_name[i] = ' ';
                    if (nice_name[i + 1] != 0)
                    {
                        nice_name[i + 1] = toupper(nice_name[i + 1]);
                        if (nice_name[i + 2] == 0)
                        {
                            nice_name[i + 1] = 0;
                            strcat(nice_name, "(Black)");
                        }
                    }
                }
            }

            if (linetype > 0 && linetype < 100)
            {
                default_lines[linetype] = m->first;
                if (existing_line_types.count(linetype) == 0)
                {
                    const char* icon = "line_Unknown";
                    // try to find the right icon, in case the particular number wasn't yet added to the ENUMS.h
#if 0
					// that would be nice - but we can't parse the .lin statement here, the ResourceMgr isn't available
					lin_info_t linfo;
					if (rmgr->GetLin(m->first,linfo))
					{
						// determine Chroma & Hue for the preview color line
						float R = linfo.rgb[0], G = linfo.rgb[1], B = linfo.rgb[2];
						float M = fltmax3(R,G,B);
						float m = fltmin3(R,G,B);
						float C = M-m;
						if(C < 0.3)        icon = linetype < 50 ? "line_SolidWite"  : "line_BSolidWhite";
						else
						{
							float H = 0.0;
							if     (R == M) H = fmod((G-B) / C, 6.0);
							else if(G == M) H = (B-R) / C + 2.0;
							else if(B == M) H = (R-G) / C + 4.0;
							H = fltwrap(H * 60.0, 0.0, 360.0);

							if     (H > 330.0 ||
									H < 20.0)  icon = linetype < 50 ? "line_SolidRed"   : "line_BSolidRed";
							else if(H < 45.0)  icon = linetype < 50 ? "line_SolidOrange": "line_BSolidOrange";
							else if(H < 70.0)  icon = linetype < 50 ? "line_SolidYellow": "line_BSolidYellow";
							else if(H < 135.0) icon = linetype < 50 ? "line_SolidGreen" : "line_BSolidGreen";
							else               icon = linetype < 50 ? "line_SolidBlue"  : "line_BSolidBlue";
						}
					}
#else
                    for (int i = 0; i < resnam.length(); ++i)
                        resnam[i] = tolower(resnam[i]); // C11 would make this so much easier ...

                    if (resnam.find("_red") != std::string::npos)
                    {
                        if (resnam.find("_dash") != std::string::npos)
                            icon = linetype < 50 ? "line_BrokenRed" : "line_BBrokenRed";
                        else
                            icon = linetype < 50 ? "line_SolidRed" : "line_BSolidRed";
                    }
                    else if (resnam.find("_orange") != std::string::npos)
                        icon = linetype < 50 ? "line_SolidOrange" : "line_BSolidOrange";
                    else if (resnam.find("_green") != std::string::npos)
                        icon = linetype < 50 ? "line_SolidGreen" : "line_BSolidGreen";
                    else if (resnam.find("_blue") != std::string::npos)
                        icon = linetype < 50 ? "line_SolidBlue" : "line_BSolidBlue";
                    else if (resnam.find("_yellow") != std::string::npos || resnam.find("_taxi") != std::string::npos ||
                             resnam.find("_hold") != std::string::npos)
                    {
                        if (resnam.find("_hold") != std::string::npos)
                        {
                            if (resnam.find("_ils") != std::string::npos)
                                icon = linetype < 50 ? "line_ILSHold" : "line_BILSHold";
                            else if (resnam.find("_double") != std::string::npos ||
                                     resnam.find("_runway") != std::string::npos)
                                icon = linetype < 50 ? "line_RunwayHold" : "line_BRunwayHold";
                            else if (resnam.find("_taxi") != std::string::npos)
                                icon = linetype < 50 ? "line_ILSCriticalCenter" : "line_BILSCriticalCenter";
                            else
                                icon = linetype < 50 ? "line_OtherHold" : "line_BOtherHold";
                        }
                        else if (resnam.find("_wide") != std::string::npos)
                            icon = linetype < 50 ? "line_SolidYellowW" : "line_BSolidYellowW";
                        else
                            icon = linetype < 50 ? "line_SolidYellow" : "line_BSolidYellow";
                    }
                    else if (resnam.find("_white") != std::string::npos || resnam.find("_road") != std::string::npos)
                    {
                        if (resnam.find("_dash") != std::string::npos)
                            icon = "line_BrokenWhite";
                        else
                            icon = linetype < 50 ? "line_SolidWhite" : "line_BSolidWhite";
                    }
#endif
                    ENUM_Create(LinearFeature, icon, nice_name, linetype);
                    existing_line_types.insert(
                        linetype); // keep track in case of erroneously supplied duplicate vpath's
                }
            }
        }
        m++;
    }

    m = res_table.begin();
    while (m != res_table.end() && m->first.find("lib/airport/lights/slow/", 0) == std::string::npos)
        ++m;

    while (m != res_table.end() && m->first.find("lib/airport/lights/slow/", 0) != std::string::npos)
    {
        std::string resnam(m->first);
        resnam.erase(0, strlen("lib/airport/lights/slow/"));

        if (resnam[0] >= '0' && resnam[0] <= '9' &&
            //		   m->second.is_default &&
            m->second.status >= status_Public && resnam.substr(resnam.size() - 4) == ".str")
        {
            resnam.erase(resnam.size() - 4);

            // create human readable Description (also used as XML keyword) from resource file name
            int lighttype;
            char nice_name[60];
            sscanf(resnam.c_str(), "%d%*c%29s", &lighttype, nice_name);
            for (int i = 0; i < 30; ++i)
            {
                if (nice_name[i] == 0)
                    break;
                if (i == 0)
                    nice_name[0] = toupper(nice_name[0]);
                if (nice_name[i] == '_')
                {
                    nice_name[i] = ' ';
                    if (nice_name[i + 1] != 0)
                    {
                        if (strcmp(nice_name + i + 1, "G_uni") == 0)
                            strcpy(nice_name + i + 1, "(Unidirectional Green)");
                        else if (strcmp(nice_name + i + 1, "YG_uni") == 0)
                            strcpy(nice_name + i + 1, "(Unidirectional Amber/Green)");
                        else
                            nice_name[i + 1] = toupper(nice_name[i + 1]);
                    }
                }
            }

            if (lighttype > 100 && lighttype < 200)
            {
                default_lines[lighttype] = m->first;
                if (existing_line_types.count(lighttype) == 0)
                {
                    const char* icon = "line_Unknown";
                    // try to find the right icon, in case the particular number wasn't yet added to the ENUMS.h
                    if (resnam.find("_G_uni") != std::string::npos)
                        icon = "line_TaxiCenterUni";
                    else if (resnam.find("_YG_uni") != std::string::npos)
                        icon = "line_HoldShortCenterUni";

                    ENUM_Create(LinearFeature, icon, nice_name, lighttype);
                    existing_line_types.insert(
                        lighttype); // keep track in case of erroneously supplied duplicate vpath's
                }
            }
        }
        m++;
    }
    LOG_MSG("I/Lib found %d XP1130 line types\n", (int)default_lines.size());
}

void WED_LibraryMgr::RescanSurfaces()
{
    const std::map<int, std::pair<std::string, std::string>> xp12_surfaces = {
        {20, {"asphalt_L/taxiway.pol", "asphalt_L/strips.pol"}},
        {21, {"asphalt_L/taxiway_patch.pol", "asphalt_L/patched.pol"}},
        {22, {"asphalt_L/taxiway_plain.pol", "asphalt_L/plain.pol"}},
        {23, {"asphalt_L/taxiway_worn.pol", "asphalt_L/worn.pol"}},
        {1, {"asphalt/taxiway.pol", "asphalt/strips.pol"}},
        {24, {"asphalt/taxiway_patch.pol", "asphalt/patched.pol"}},
        {25, {"asphalt/taxiway_plain.pol", "asphalt/plain.pol"}},
        {26, {"asphalt/taxiway_worn.pol", "asphalt/worn.pol"}},
        {27, {"asphalt_D/taxiway.pol", "asphalt_D/strips.pol"}},
        {28, {"asphalt_D/taxiway_patch.pol", "asphalt_D/patched.pol"}},
        {29, {"asphalt_D/taxiway_plain.pol", "asphalt_D/plain.pol"}},
        {30, {"asphalt_D/taxiway_worn.pol", "asphalt_D/worn.pol"}},
        {31, {"asphalt_D2/taxiway.pol", "asphalt_D2/strips.pol"}},
        {32, {"asphalt_D2/taxiway_patch.pol", "asphalt_D2/patched.pol"}},
        {33, {"asphalt_D2/taxiway_plain.pol", "asphalt_D2/plain.pol"}},
        {34, {"asphalt_D2/taxiway_worn.pol", "asphalt_D2/worn.pol"}},
        {35, {"asphalt_D3/taxiway.pol", "asphalt_D3/strips.pol"}},
        {36, {"asphalt_D3/taxiway_patch.pol", "asphalt_D3/patched.pol"}},
        {37, {"asphalt_D3/taxiway_plain.pol", "asphalt_D3/plain.pol"}},
        {38, {"asphalt_D3/taxiway_worn.pol", "asphalt_D3/worn.pol"}},
        {50, {"concrete_L/taxiway.pol", "concrete_L/new.pol"}},
        {51, {"concrete_L/taxiway_dirty.pol", "concrete_L/dirty.pol"}},
        {52, {"concrete_L/taxiway_worn.pol", "concrete_L/worn.pol"}},
        {2, {"concrete/taxiway.pol", "concrete/new.pol"}},
        {53, {"concrete/taxiway_dirty.pol", "concrete/dirty.pol"}},
        {54, {"concrete/taxiway_worn.pol", "concrete/worn.pol"}},
        {55, {"concrete_D/taxiway.pol", "concrete_D/new.pol"}},
        {56, {"concrete_D/taxiway_dirty.pol", "concrete_D/dirty.pol"}},
        {57, {"concrete_D/taxiway_worn.pol", "concrete_D/worn.pol"}},
        {3, {"grass/taxiway.pol", ""}},
        {4, {"dirt/taxiway.pol", ""}},
        {5, {"gravel/taxiway.pol", ""}},
        {12, {"lakebed/taxiway.pol", ""}},
        {14, {"snow/taxiway.pol", ""}}};

    const char* surf_pfx = "lib/airport/default_runways/";
    const char* dpol_pfx = "lib/airport/ground/pavement/";

    default_surfaces.clear();

    for (auto& surf : xp12_surfaces)
    {
        std::string surf_vpath = std::string(surf_pfx) + surf.second.first;
        auto rt = res_table.find(surf_vpath);
        if (rt != res_table.end() && rt->second.is_default)
        {
            std::string dpol_vpath = std::string(dpol_pfx) + surf.second.second;
            auto rt_dpol = res_table.find(dpol_vpath);
            if (rt_dpol != res_table.end() && rt_dpol->second.is_default && rt_dpol->second.status >= status_Public)
                default_surfaces[ENUM_Import(Surface_Type, surf.first)] = std::make_pair(dpol_vpath, true);
            else
                default_surfaces[ENUM_Import(Surface_Type, surf.first)] =
                    std::make_pair(surf_vpath, false); // no public .pol equivalent
        }
    }

    // this should only occurr with XP11 or older, XP11 user can at least get nice pavement and the "convert To" uses
    // this info, too.
    if (default_surfaces.size() == 0)
    {
        if (res_table.count("lib/airport/pavement/asphalt_3D.pol") > 0)
            default_surfaces[surf_Asphalt] = std::make_pair("lib/airport/pavement/asphalt_3D.pol", true);
        if (res_table.count("lib/airport/pavement/concrete_1D.pol") > 0)
            default_surfaces[surf_Concrete] = std::make_pair("lib/airport/pavement/concrete_1D.pol", true);
    }
    else
        LOG_MSG("I/Lib found %d XP12 style surface types\n", (int)default_surfaces.size());
}

bool WED_LibraryMgr::GetSurfVpath(int surf, std::string& res)
{
    auto it = default_surfaces.find(surf);
    if (it != default_surfaces.end())
    {
        res = it->second.first;
        return it->second.second;
    }
    else
    {
        res.clear();
        return false;
    }
}

int WED_LibraryMgr::GetSurfEnum(const std::string& res)
{
    for (auto& v : default_surfaces)
    {
        if (v.second.first == res)
            return v.first;
    }
    return -1;
}

void WED_LibraryMgr::AccumResource(const std::string& path, int package, const std::string& rpath, bool is_default,
                                   res_status status, bool is_backup, bool is_seasonal, bool is_regional)
{

    // surprise: This function is called 60,300 time upon loading any scenery. Yep, XP11 has that many items in the
    // libraries. Resultingly the full path was converted to lower case 0.6 million times => 24 million calls to
    // tolower() ... time to optimize

    std::string suffix;
    suffix = FILE_get_file_extension(path);

    res_type rt;

    if (suffix == "obj")
        rt = res_Object;
    else if (suffix == "agp")
        rt = res_Object;
    else if (suffix == "fac")
        rt = res_Facade;
    else if (suffix == "for")
        rt = res_Forest;
    else if (suffix == "str")
        rt = res_String;
    else if (suffix == "lin")
        rt = res_Line;
    else if (suffix == "pol")
        rt = res_Polygon;
    else if (suffix == "ags")
        rt = res_Autogen;
    else if (suffix == "agb")
        rt = res_Autogen;
#if ROAD_EDITING
    else if (suffix == "net")
        rt = res_Road;
#endif
    else
        return;

    if (package >= 0 && status >= status_Public && !is_backup)
        gPackageMgr->AddPublicItems(package);

    std::string p(path);
    while (!p.empty())
    {
        res_map_t::iterator i = res_table.find(p);
        if (i == res_table.end())
        {
            res_info_t new_info;
            new_info.status = status;
            new_info.res_type = rt;
            new_info.packages.insert(package);
            if (rt > res_Directory) // speedup/memory saver: no need to store this for directories
                new_info.real_paths.push_back(rpath);
            new_info.is_backup = is_backup;
            new_info.is_default = is_default;
            new_info.has_seasons = is_seasonal;
            new_info.has_regions = is_regional;
            res_table.insert(res_map_t::value_type(p, new_info));
        }
        else
        {
            DebugAssert(i->second.res_type == rt);
            if (i->second.is_backup && !is_backup)
            {
                i->second.is_backup = false;
                i->second.real_paths.clear();
                i->second.packages.clear();
            }
            else if (is_backup)
                break; // avoid adding backups as variants

            i->second.packages.insert(package);
            if (is_default && !i->second.is_default)
            {
                i->second.status = status;   // LR libs will always override/downgrade Custom Libs visibility
                i->second.is_default = true; // But they can still elevate any prior LR lib's visiblity, as some do
            }
            else
                i->second.status =
                    std::max((int)i->second.status, (int)status); // upgrade status if we just found a public version!
            // add only unique paths, but need to preserve first path added as first element, so deliberately not using
            // a std::set<std::string> !
            if (rt > res_Directory) // speedup/memory saver: no need to store this for directories
                if (std::find(i->second.real_paths.begin(), i->second.real_paths.end(), rpath) ==
                    i->second.real_paths.end())
                    i->second.real_paths.push_back(rpath);
            i->second.has_seasons |= is_seasonal;
            i->second.has_regions |= is_regional;
        }

        std::string par, f;
        split_path(p, par, f);
        p = par;
        rt = res_Directory;
    }
}

bool WED_LibraryMgr::AccumLocalFile(const char* filename, bool is_dir, void* ref)
{
    local_scan_t* info = reinterpret_cast<local_scan_t*>(ref);
    if (is_dir)
    {
        if (strcmp(filename, ".") != 0 && strcmp(filename, "..") != 0)
        {
            local_scan_t sub_info;
            sub_info.who = info->who;
            sub_info.partial = info->partial + "/" + filename;
            sub_info.full = info->full + DIR_STR + filename;
            MF_IterateDirectory(sub_info.full.c_str(), AccumLocalFile, reinterpret_cast<void*>(&sub_info));
        }
    }
    else
    {
        std::string r = info->partial + "/" + filename;
        std::string f = info->full + DIR_STR + filename;
        r.erase(0, 1);
        info->who->AccumResource(r, pack_Local, f, false, status_Public);
    }
    return false;
}
