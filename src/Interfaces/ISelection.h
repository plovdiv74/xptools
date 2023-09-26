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

#ifndef ISELECTION_H
#define ISELECTION_H

/*

    ISelection - THEORY OF OPERATION

    This is an abstract object-based selection std::set.  The selection can be teted per object
    or copied out to either a std::vector or std::set.  (These are provided to give clients whichever
    format is more useful.  We expect std::vector to be more memory efficient, particularly since
    the selection knows the size of the block up-front.)

*/

#include "IBase.h"
#include <set>
#include <vector>

using std::set;
using std::vector;

class ISelectable : public virtual IBase
{
public:
    virtual int GetSelectionID(void) const = 0;
};

class ISelection : public virtual IBase
{
public:
    virtual bool IsSelected(ISelectable* who) const = 0;

    virtual void Select(ISelectable* who) = 0;
    virtual void Clear(void) = 0;
    virtual void Toggle(ISelectable* who) = 0;
    virtual void Insert(ISelectable* who) = 0;

    virtual void Insert(const std::set<ISelectable*>& sel) = 0;
    virtual void Insert(const std::set<ISelectable*>::const_iterator& begin,
                        const std::set<ISelectable*>::const_iterator& end) = 0;

    virtual void Insert(const std::vector<ISelectable*>& sel) = 0;
    virtual void Insert(const std::vector<ISelectable*>::const_iterator& begin,
                        const std::vector<ISelectable*>::const_iterator& end) = 0;
    virtual void Erase(ISelectable* who) = 0;

    virtual int GetSelectionCount(void) const = 0;
    virtual void GetSelectionSet(std::set<ISelectable*>& sel) const = 0;
    virtual void GetSelectionVector(std::vector<ISelectable*>& sel) const = 0;
    virtual ISelectable* GetNthSelection(int n) const = 0;

    // Iterate the selection until our func returns true for at least one item.  Return true if ANY objs passed. Returns
    // false for the empty std::set.  (None passed)
    virtual int IterateSelectionOr(int (*func)(ISelectable* who, void* ref), void* ref) const = 0;
    // Iterate the selection as long as our func returns true.  Return true if they ALL passed.  Returns true for the
    // empty std::set.  (None failed)
    virtual int IterateSelectionAnd(int (*func)(ISelectable* who, void* ref), void* ref) const = 0;
};

#endif /* ISELECTION_H */
