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

#ifndef WED_PackageMgr_H
#define WED_PackageMgr_H

#include "GUI_Broadcaster.h"

struct WED_PackageInfo;

class WED_PackageMgr : public GUI_Broadcaster
{
public:
    WED_PackageMgr(const char* in_xplane_folder);
    ~WED_PackageMgr();

    bool HasSystemFolder(void) const;
    bool GetXPlaneFolder(std::string& root) const;
    bool SetXPlaneFolder(const std::string& root);

    void GetRecentName(std::string& name) const;
    void SetRecentName(const std::string& name);

    int CountCustomPackages(void) const;
    std::pair<int, int> GlobalPackages(void) const;

    int CountPackages(void) const;
    void GetNthPackageName(int n, std::string& package) const;
    /*Get the a package's path by passing in a number and the name of said package,
    changes the std::string passed in into the real physical filepath.*/
    void GetNthPackagePath(int n, std::string& package) const;

    bool IsPackageDefault(int n) const; // library is a LR default Library, i.e. not Global or Custom Scenery
    bool HasPublicItems(int n) const;   // library has at least one public item declared in it

    // functions only effective on custom packages
    bool HasXML(int n) const;     // includes earth.wed.xml
    bool HasAPT(int n) const;     // includes apt.dat
    bool IsDisabled(int n) const; // marked as disabled in the scenerypacks.ini
    bool HasLibrary(int n) const; // includes library.txt
    void AddPublicItems(int n);
    void RenameCustomPackage(int n, const std::string& new_name);
    int CreateNewCustomPackage(void);

    void Rescan(bool alwaysBroadcast = false);

    std::string ComputePath(const std::string& package, const std::string& rel_file) const;
    std::string ReducePath(const std::string& package, const std::string& full_file) const;

    const char* GetXPversion() const; // report apparent XP installation version by looking at Log.txt
    bool IsSameXPVersion(const std::string& version) const;

private:
    static bool AccumAnyDir(const char* fileName, bool isDir, void* ref);
    static bool AccumLibDir(const char* fileName, bool isDir, void* ref);

    std::string system_path;
    bool system_exists;

    std::vector<WED_PackageInfo> custom_packages;
    std::vector<WED_PackageInfo> global_packages;
    std::vector<WED_PackageInfo> default_packages;

    std::string XPversion; // apparent version of XP install, from examining Log.txt
    std::string RecentPkgName;
};

extern WED_PackageMgr* gPackageMgr;

#endif /* WED_PackageMgr_H */
