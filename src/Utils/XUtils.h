/*
 * Copyright (c) 2004, Laminar Research.
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
#ifndef XUTILS_H
#define XUTILS_H

struct XObjCmd;
struct XObj;

#include <string>
#include <vector>
using namespace std;

class StTextFileScanner
{
public:
    StTextFileScanner(const char* inFileName, bool skip_blanks);
    ~StTextFileScanner();

    void skip_blanks(bool skip_blanks);
    bool done();
    void next();
    std::string get();

private:
    void read_next(void);

    FILE* mFile;
    std::string mBuf;
    bool mDone;
    bool mSkipBlanks;
};

void BreakString(const std::string& line, std::vector<std::string>& words);

void StringToUpper(std::string&);

bool HasExtNoCase(const std::string& inStr, const char* inExt);

bool GetNextNoComments(StTextFileScanner& f, std::string& s);

void StripPath(std::string& ioPath);
void StripPathCP(std::string& ioPath);
void ExtractPath(std::string& ioPath);

int PickRandom(std::vector<double>& chances);
bool RollDice(double inProb);
double RandRange(double mmin, double mmax);
double RandRangeBias(double mmin, double mmax, double biasRatio, double randomAmount);

void ExtractFixedRecordString(const std::string& inLine, int inBegin, int inEnd, std::string& outString);

bool ExtractFixedRecordLong(const std::string& inLine, int inBegin, int inEnd, long& outLong);

bool ExtractFixedRecordUnsignedLong(const std::string& inLine, int inBegin, int inEnd, unsigned long& outUnsignedLong);

class XPointPool
{
public:
    XPointPool();
    ~XPointPool();
    void clear();
    int accumulate(const float xyz[3], const float st[2]);
    int count(void);
    void get(int index, float xyz[3], float st[2]);

private:
    XPointPool(const XPointPool&);
    XPointPool& operator=(const XPointPool&);

    struct XPointPoolImp;

    XPointPoolImp* mImp;
};

#endif
