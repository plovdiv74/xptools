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

#ifndef IOPERATION_H
#define IOPERATION_H

/*

    IOPERATION - THEORY OF OPERATION

    IOperation provides an interface to start and end "operations" on a series of objects.

    (NOTE: this interface is still a bit of a hack, since operations are implemented archive-wide but
    provided per-object. hrm.)

*/

#include "IBase.h"

#define StartOperation(x) __StartOperation(x, __FILE__, __LINE__)

class IOperation : public virtual IBase
{
public:
    virtual void __StartOperation(const char* op_name, const char* inFile, int inLine) = 0;
    virtual void CommitOperation(void) = 0;
    virtual void AbortOperation(void) = 0;
};

#endif
