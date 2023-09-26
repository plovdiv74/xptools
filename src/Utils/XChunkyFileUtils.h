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
#ifndef XCHUNKYFILEUTILS_H
#define XCHUNKYFILEUTILS_H

#include <stdio.h>
#include <stdint.h>

#if BIG
#if APL
#include <libkern/OSByteOrder.h>
#define SWAP16(x) (OSSwapConstInt16(x))
#define SWAP32(x) (OSSwapConstInt32(x))
#define SWAP64(x) (OSSwapConstInt64(x))
#else
#error we do not have non-apple big endian swapping routines.
#endif
#elif LIL
#define SWAP16(x) (x)
#define SWAP32(x) (x)
#define SWAP64(x) (x)
#else
#error endian not defined
#endif

struct XAtomHeader_t
{
    uint32_t id;
    uint32_t length;
};

enum
{
    xpna_Mode_Raw = 0,
    xpna_Mode_Differenced = 1,
    xpna_Mode_RLE = 2,
    xpna_Mode_RLE_Differenced = 3
};

/********************************************************************************
 * CHUNKY FILE READING UTILITIES
 ********************************************************************************
 * All of our reading utilities work on memory - we just use XMappedFile to memory-
 * map the file and away we go.
 *
 */

/*
 * XSpan - this is just a range of memory.  'begin' points to the first byte,
 * and 'end' points to one byte AFTER the last byte in the span.  Its length is
 * end - begin.
 *
 */
struct XSpan
{

    XSpan();

    char* begin;
    char* end;
};

/*
 * An atom is a span...the first 8 bytes are the header, and the rest
 * are the contents.  The contents can be returned as a span, which
 * can be handy...
 *
 */
struct XAtom : public XSpan
{

    uint32_t GetID(void);
    uint32_t GetContentLength(void);
    uint32_t GetContentLengthWithHeader(void);
    void GetContents(XSpan& outContents);

    bool GetNext(const XSpan& inContainer, XAtom& outNext);
};

/*
 * An atom container is a span as well...it is simply the memory for
 * all of the atoms in a row.
 * From this we can extract individual atoms.
 *
 */
struct XAtomContainer : public XSpan
{

    bool GetFirst(XAtom& outAtom);

    int CountAtoms(void);
    int CountAtomsOfID(uint32_t inID);

    bool GetNthAtom(int inIndex, XAtom& outAtom);
    bool GetNthAtomOfID(uint32_t inID, int inIndex, XAtom& outAtom);
};

/*
 * An atom of null-terminated C strings.
 *
 */
struct XAtomStringTable : public XAtom
{

    const char* GetFirstString(void);
    const char* GetNextString(const char* inString);
    const char* GetNthString(int inIndex);
};

/*
 * An atom of compressed numeric data.
 *
 */
struct XAtomPlanerNumericTable : public XAtom
{

    int GetArraySize(void);
    int GetPlaneCount(void);

    // doc this!!
    int DecompressShortToDoubleInterleaved(int numberOfPlanes, int planeSize, double* ioPlaneBuffer, double* ioScales,
                                           double inReduce, double* ioOffsets);

    int DecompressIntToDoubleInterleaved(int numberOfPlanes, int planeSize, double* ioPlaneBuffer, double* ioScales,
                                         double inReduce, double* ioOffsets);

    /* These routines decompress the data into a std::set of planes.
     * They return the number of planes filled, but will never
     * exceed numberOfPlanes. */
    int DecompressShort(int numberOfPlanes, int planeSize, int interleaved, int16_t* ioPlaneBuffer);
    int DecompressInt(int numberOfPlanes, int planeSize, int interleaved, int32_t* ioPlaneBuffer);
    int DecompressFloat(int numberOfPlanes, int planeSize, int interleaved, float* ioPlaneBuffer);
    int DecompressDouble(int numberOfPlanes, int planeSize, int interleaved, double* ioPlaneBuffer);
};

/*
 * An atom of packed data...useful for reading by type
 * and dealing with endian swaps.
 *
 */
struct XAtomPackedData : public XAtom
{

    void Reset(void)
    {
        position = begin + sizeof(XAtomHeader_t);
    }
    bool Done(void)
    {
        return position >= end;
    }
    bool Overrun(void)
    {
        return position > end;
    }

    uint8_t ReadUInt8(void)
    {
        uint8_t v = *((uint8_t*)position);
        position += sizeof(v);
        return v;
    }
    int8_t ReadSInt8(void)
    {
        int8_t v = *((int8_t*)position);
        position += sizeof(v);
        return v;
    }
    uint16_t ReadUInt16(void)
    {
        uint16_t v = *((uint16_t*)position);
        position += sizeof(v);
        return SWAP16(v);
    }
    int16_t ReadSInt16(void)
    {
        int16_t v = *((int16_t*)position);
        position += sizeof(v);
        return SWAP16(v);
    }
    uint32_t ReadUInt32(void)
    {
        uint32_t v = *((uint32_t*)position);
        position += sizeof(v);
        return SWAP32(v);
    }
    int32_t ReadSInt32(void)
    {
        int32_t v = *((int32_t*)position);
        position += sizeof(v);
        return SWAP32(v);
    }
    float ReadFloat32(void)
    {
        float v = *((float*)position);
        *((int32_t*)&v) = SWAP32(*((int32_t*)&v));
        position += sizeof(v);
        return v;
    }
    double ReadFloat64(void)
    {
        double v = *((double*)position);
        *((int64_t*)&v) = SWAP64(*((int64_t*)&v));
        position += sizeof(v);
        return v;
    }

    void Advance(int bytes)
    {
        position += bytes;
    }

    char* position;
};

/********************************************************************************
 * CHUNKY FILE WRITING UTILITIES
 ********************************************************************************
 *
 *
 */

struct StFileSizeDebugger
{
    StFileSizeDebugger(FILE* inFile, const char* label);
    ~StFileSizeDebugger();

    FILE* mFile;
    int32_t mAtomStart;
    const char* mLabel;
};

struct StAtomWriter
{
    StAtomWriter(FILE* inFile, uint32_t inID, bool no_show_size_debug = false);
    ~StAtomWriter();

    bool mNoSize;
    FILE* mFile;
    int32_t mAtomStart;
    uint32_t mID;
};

void WritePlanarNumericAtomShort(FILE* file, int numberOfPlanes, int planeSize, int encodeMode, int interleaved,
                                 int16_t* ioData);

void WritePlanarNumericAtomInt(FILE* file, int numberOfPlanes, int planeSize, int encodeMode, int interleaved,
                               int32_t* ioData);

void WritePlanarNumericAtomFloat(FILE* file, int numberOfPlanes, int planeSize, int encodeMode, int interleaved,
                                 float* ioData);

void WritePlanarNumericAtomDouble(FILE* file, int numberOfPlanes, int planeSize, int encodeMode, int interleaved,
                                  double* ioData);

void WriteUInt8(FILE* fi, uint8_t v);
void WriteSInt8(FILE* fi, int8_t v);
void WriteUInt16(FILE* fi, uint16_t v);
void WriteSInt16(FILE* fi, int16_t v);
void WriteUInt32(FILE* fi, uint32_t v);
void WriteSInt32(FILE* fi, int32_t v);
void WriteFloat32(FILE* fi, float v);
void WriteFloat64(FILE* fi, double v);

#endif
