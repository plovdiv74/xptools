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
#include "DEMAlgs.h"
#include "ParamDefs.h"
#include "PolyRasterUtils.h"
#include "NetTables.h"
#include "AssertUtils.h"
#include "DEMTables.h"
#include "MeshAlgs.h"
#include "PolyRasterUtils.h"
#include "WED_Document.h"
#include "XESConstants.h"
#include "XUtils.h"
#include "DEMAlgs.h"
#include "WED_Globals.h"
#include <math.h>
#include "AptAlgs.h"
#include "MemFileUtils.h"
#include "XESIO.h"
#include "ForestTables.h"
#include "MathUtils.h"
#include "MapAlgs.h"
#include "MapTopology.h"
#include "Zoning.h"

// Minimum bathymetric depth from water surface at any point!
#define MIN_DEPTH 1.0f
#define MAX_DEPTH 50.0f

#define WATER_SURF_DIM 256

DEMPrefs_t gDemPrefs = {3, 0.5, 1.0};

struct SnowLineInfo_t
{
    float lat;
    float sh_dry; // meters
    float sh_wet;
    float nh_dry;
    float nh_wet;
};
/*
http://www-das.uwyo.edu/~geerts/cwx/notes/chap10/snowline.html
units in meters
Table 1. Effects of hemisphere, latitude and climatic dryness on the average snowline�s elevation, in metres (1). SH =
southern hemisphere; NH = northern hemisphere. The figures for South America are from (2).
*/
static const SnowLineInfo_t kSnowLineInfo[] = {
    //	Lati	SHdry	SHmoist	NHdry	NHmoist
    {90, 0, 0, 400, 100},         {80, 0, 0, 400, 100},        {60, 700, 200, 2500, 600}, {40, 3300, 1200, 5100, 2400},
    {20, 6200, 4900, 5500, 4800}, {0, 5200, 4500, 5200, 4500}, {-9999, 0, 0, 0, 0}};

// Spread is approx URBAN_KERN_SIZE / 2 KM
#define URBAN_DENSE_KERN_SIZE 3 // Tried 17 before
#define URBAN_RADIAL_KERN_SIZE 33
#define URBAN_TRANS_KERN_SIZE 5

static float sUrbanDenseSpreaderKernel[URBAN_DENSE_KERN_SIZE * URBAN_DENSE_KERN_SIZE];
static float sUrbanRadialSpreaderKernel[URBAN_RADIAL_KERN_SIZE * URBAN_RADIAL_KERN_SIZE];
static float sUrbanTransSpreaderKernel[URBAN_TRANS_KERN_SIZE * URBAN_TRANS_KERN_SIZE];

float local_deltas_x[9] = {-1, 0, 1, -1, 0, 1, -1, 0, 1};
float local_deltas_y[9] = {-1, -1, -1, 0, 0, 0, 1, 1, 1};

inline bool non_integral(float f)
{
    return (f != DEM_NO_DATA && f != 0.0 && f != 1.0);
}

/*
 * SpreadDEMValues
 *
 * Fill every point in the DEM that contains DEM_NO_DATA with the nearest valid value from any direction.
 *
 */
void SpreadDEMValues(DEMGeo& ioDem)
{
    DEMGeo half_size;
    ioDem.derez_nearest(half_size);

    if (half_size.mWidth != 1 || half_size.mHeight != 1)
    {
        SpreadDEMValues(half_size);
    }

    for (int y = 0; y < ioDem.mHeight; ++y)
        for (int x = 0; x < ioDem.mWidth; ++x)
        {
            if (ioDem.get(x, y) == DEM_NO_DATA)
                ioDem(x, y) = half_size.xy_nearest(ioDem.x_to_lon(x), ioDem.y_to_lat(y));
        }
}

void SpreadDEMValuesTotal(DEMGeo& ioDem)
{
    // Note: we can't do this in place because what will happen is values will be smeared
    // - let's say we have a whole row of no value.  the left most coord is resolved first
    // and will be closest as we go right on the row.  Since we want to smear inward, we
    // find the nearest value from the old dem, copy to a new dem and then swap.
    DEMGeo temp(ioDem);
    for (int y = 0; y < ioDem.mHeight; ++y)
        for (int x = 0; x < ioDem.mWidth; ++x)
        {
            float h = temp(x, y);
            if (h == DEM_NO_DATA)
            {
                for (int r = 1; r <= max(ioDem.mWidth, ioDem.mHeight); ++r)
                {
                    for (int rd = 1; rd <= r; ++rd)
                    {
                        h = ioDem.get(x - r, y - rd);
                        if (h != DEM_NO_DATA)
                            goto foo;
                        h = ioDem.get(x - r, y + rd);
                        if (h != DEM_NO_DATA)
                            goto foo;
                        h = ioDem.get(x + r, y - rd);
                        if (h != DEM_NO_DATA)
                            goto foo;
                        h = ioDem.get(x + r, y + rd);
                        if (h != DEM_NO_DATA)
                            goto foo;
                        h = ioDem.get(x - rd, y - r);
                        if (h != DEM_NO_DATA)
                            goto foo;
                        h = ioDem.get(x + rd, y - r);
                        if (h != DEM_NO_DATA)
                            goto foo;
                        h = ioDem.get(x - rd, y - r);
                        if (h != DEM_NO_DATA)
                            goto foo;
                        h = ioDem.get(x + rd, y - r);
                        if (h != DEM_NO_DATA)
                            goto foo;
                    }
                }
            }

        foo:
            if (h != DEM_NO_DATA)
                temp(x, y) = h;
        }
    ioDem.swap(temp);
}

bool SpreadDEMValuesIterate(DEMGeo& ioDem)
{
    bool did_any = false;
    DEMGeo temp(ioDem);
    for (int y = 0; y < ioDem.mHeight; ++y)
        for (int x = 0; x < ioDem.mWidth; ++x)
        {
            float h = temp(x, y);
            if (h == DEM_NO_DATA)
            {
                int n = rand() % 4;
                switch (n)
                {
                case 0:
                    if (h == DEM_NO_DATA)
                        h = ioDem.get(x, y - 1);
                    if (h == DEM_NO_DATA)
                        h = ioDem.get(x, y + 1);
                    if (h == DEM_NO_DATA)
                        h = ioDem.get(x - 1, y);
                    if (h == DEM_NO_DATA)
                        h = ioDem.get(x + 1, y);
                    break;
                case 1:
                    if (h == DEM_NO_DATA)
                        h = ioDem.get(x, y + 1);
                    if (h == DEM_NO_DATA)
                        h = ioDem.get(x, y - 1);
                    if (h == DEM_NO_DATA)
                        h = ioDem.get(x + 1, y);
                    if (h == DEM_NO_DATA)
                        h = ioDem.get(x - 1, y);
                    break;
                case 2:
                    if (h == DEM_NO_DATA)
                        h = ioDem.get(x + 1, y);
                    if (h == DEM_NO_DATA)
                        h = ioDem.get(x - 1, y);
                    if (h == DEM_NO_DATA)
                        h = ioDem.get(x, y - 1);
                    if (h == DEM_NO_DATA)
                        h = ioDem.get(x, y + 1);
                    break;
                default:
                    if (h == DEM_NO_DATA)
                        h = ioDem.get(x - 1, y);
                    if (h == DEM_NO_DATA)
                        h = ioDem.get(x + 1, y);
                    if (h == DEM_NO_DATA)
                        h = ioDem.get(x, y + 1);
                    if (h == DEM_NO_DATA)
                        h = ioDem.get(x, y - 1);
                    break;
                }
                if (h != DEM_NO_DATA)
                {
                    temp(x, y) = h;
                    did_any = true;
                }
            }
        }
    if (did_any)
        ioDem.swap(temp);
    return did_any;
}

/*
 * Same idea as above but lcoalized.
 *
 */
void SpreadDEMValues(DEMGeo& ioDem, int dist, int x1, int y1, int x2, int y2)
{
    if (x1 < 0)
        x1 = 0;
    if (y1 < 0)
        y1 = 0;
    if (x2 > ioDem.mWidth)
        x2 = ioDem.mWidth;
    if (y2 > ioDem.mHeight)
        y2 = ioDem.mHeight;
    DEMGeo temp(ioDem);
    for (int y = y1; y < y2; ++y)
        for (int x = x1; x < x2; ++x)
        {
            float h = ioDem.get(x, y);
            if (h == DEM_NO_DATA)
            {
                int n = 1;
                while (n <= dist)
                {
                    h = ioDem.get(x - n, y);
                    if (h != DEM_NO_DATA)
                        break;
                    h = ioDem.get(x + n, y);
                    if (h != DEM_NO_DATA)
                        break;
                    h = ioDem.get(x, y - n);
                    if (h != DEM_NO_DATA)
                        break;
                    h = ioDem.get(x, y + n);
                    if (h != DEM_NO_DATA)
                        break;
                    h = ioDem.get(x + n, y - n);
                    if (h != DEM_NO_DATA)
                        break;
                    h = ioDem.get(x + n, y + n);
                    if (h != DEM_NO_DATA)
                        break;
                    h = ioDem.get(x - n, y - n);
                    if (h != DEM_NO_DATA)
                        break;
                    h = ioDem.get(x - n, y + n);
                    if (h != DEM_NO_DATA)
                        break;
                    ++n;
                }
                if (h != DEM_NO_DATA)
                    temp(x, y) = h;
            }
        }
    ioDem.swap(temp);
}

/*
 * CalculateFilter
 *
 * Calculate a dimxdim filter kernel and load into k, given a kind.
 * If normalize is true, the sum of all points of K will be 1.0.
 *
 */
void CalculateFilter(int dim, float* k, int kind, bool normalize)
{
    int hdim = dim / 2;
    int x, y;
    double max_dist = hdim + 1; // sqrt((float)hdim * hdim * 2);
    for (y = 0; y < dim; ++y)
        for (x = 0; x < dim; ++x)
        {
            float dx = std::abs(hdim - x);
            float dy = std::abs(hdim - y);
            float d = sqrt((float)dx * dx + dy * dy);
            switch (kind)
            {
            case demFilter_Linear:
                d = (max_dist - d) / max_dist;
                k[x + y * dim] = d;
                break;
            case demFilter_Spread:
                k[x + y * dim] = 1.0;
                break;
            }
        }
    if (normalize)
    {
        double sum = 0;
        for (y = 0; y < dim; ++y)
            for (x = 0; x < dim; ++x)
            {
                sum += k[x + y * dim];
            }
        sum = 1.0 / sum;
        for (y = 0; y < dim; ++y)
            for (x = 0; x < dim; ++x)
            {
                (k[x + y * dim]) *= sum;
            }
    }
}

/*
 * Produce a DEM that is 1:ratio smaller, using averaging.
 *
 */
void DownsampleDEM(const DEMGeo& ioDem, DEMGeo& smaller, int ratio)
{
    smaller.resize((ioDem.mWidth - ioDem.mPost) / ratio + ioDem.mPost,
                   (ioDem.mHeight - ioDem.mPost) / ratio + ioDem.mPost);
    smaller.mNorth = ioDem.mNorth;
    smaller.mSouth = ioDem.mSouth;
    smaller.mEast = ioDem.mEast;
    smaller.mWest = ioDem.mWest;
    smaller.mPost = ioDem.mPost;

    for (int y = 0; y < smaller.mHeight; ++y)
        for (int x = 0; x < smaller.mWidth; ++x)
        {
            float c = 0;
            float h = 0.0;
            for (int dy = y * ratio - (ratio / 2); dy < (y * ratio + (ratio / 2)); ++dy)
                for (int dx = x * ratio - (ratio / 2); dx < (x * ratio + (ratio / 2)); ++dx)
                {
                    float lh = ioDem.get(dx, dy);
                    if (lh != DEM_NO_DATA)
                        c += 1.0, h += lh;
                }
            if (c > 0)
                h /= c;
            else
                h = DEM_NO_DATA;

            smaller(x, y) = h;
        }
}
void UpsampleDEM(const DEMGeo& ioDem, DEMGeo& bigger, int ratio)
{
    bigger.resize((ioDem.mWidth - ioDem.mPost) * ratio + ioDem.mPost,
                  (ioDem.mHeight - ioDem.mPost) * ratio + ioDem.mPost);
    bigger.mNorth = ioDem.mNorth;
    bigger.mSouth = ioDem.mSouth;
    bigger.mEast = ioDem.mEast;
    bigger.mWest = ioDem.mWest;
    for (int y = 0; y < bigger.mHeight; ++y)
        for (int x = 0; x < bigger.mWidth; ++x)
            bigger(x, y) = ioDem(x / ratio, y / ratio);
}

void ResampleDEM(const DEMGeo& inSrc, DEMGeo& inDst)
{
    for (int y = 0; y < inDst.mHeight; ++y)
        for (int x = 0; x < inDst.mWidth; ++x)
        {
            double lon = inDst.x_to_lon(x);
            double lat = inDst.y_to_lat(y);

            double e = inSrc.value_linear(lon, lat);
            inDst(x, y) = e;
        }
}

void ResampleDEMmedian(const DEMGeo& inSrc, DEMGeo& inDst, int radius)
{
    double xstep = (inDst.mEast - inDst.mWest) / inDst.x_res();
    double ystep = (inDst.mNorth - inDst.mSouth) / inDst.y_res();

    for (int y = 0; y < inDst.mHeight; ++y)
        for (int x = 0; x < inDst.mWidth; ++x)
        {
            double lon = inDst.x_to_lon(x);
            double lat = inDst.y_to_lat(y);

            double e = inSrc.get_median(lon, lat, xstep, ystep, radius);
            inDst(x, y) = e;
        }
}

void InterpDoubleDEM(const DEMGeo& inDEM, DEMGeo& bigger)
{
    bigger.resize((inDEM.mWidth - 1) * 2 + 1, (inDEM.mHeight - 1) * 2 + 1);
    bigger.copy_geo_from(inDEM);

    int x, y, n;
    float e;
    for (y = 0; y < inDEM.mHeight; ++y)
        for (x = 0; x < inDEM.mWidth; ++x)
            bigger(x * 2, y * 2) = inDEM.get(x, y);

    static int x_dir[8] = {0, 1, 0, -1, 1, 1, -1, -1};
    static int y_dir[8] = {1, 0, -1, 0, 1, -1, 1, -1};

    for (y = 0; y < bigger.mHeight; ++y)
        for (x = 0; x < bigger.mWidth; ++x)
            if ((x % 2) || (y % 2))
            {
                double num = 0.0;
                double tot = 0.0;
                bool has_direct = false;
                for (n = 0; n < 8; ++n)
                {
                    int px = x - x_dir[n];
                    int py = y - y_dir[n];
                    if ((px % 2) == 0 && (py % 2) == 0)
                    {
                        e = inDEM.get(px / 2, py / 2);
                        if (e != DEM_NO_DATA)
                        {
                            if (n < 4)
                                has_direct = true;
                            num += 1.0;
                            tot += e;
                        }
                    }
                }
                if (has_direct || num > 1.0)
                    bigger(x, y) = tot / num;
                else
                    bigger(x, y) = DEM_NO_DATA;
            }
}

void ReduceToBorder(const DEMGeo& inDEM, DEMGeo& outDEM)
{
    outDEM.resize(inDEM.mWidth, inDEM.mHeight);
    outDEM.copy_geo_from(inDEM);
    static int x_dir[4] = {0, 1, 0, -1};
    static int y_dir[4] = {1, 0, -1, 0};
    for (int y = 0; y < inDEM.mHeight; ++y)
        for (int x = 0; x < inDEM.mWidth; ++x)
        {
            float e = inDEM.get(x, y);
            if (e != DEM_NO_DATA)
            {
                int n;
                for (n = 0; n < 4; ++n)
                {
                    if (inDEM.get(x + x_dir[n], y + y_dir[n]) == DEM_NO_DATA)
                        break;
                }
                if (n == 4)
                    e = DEM_NO_DATA;
            }
            outDEM(x, y) = e;
        }
}

// This routine takes a low res datasource and upsamples it.  It varies within a linear interpolation block
// from the min to max seen in the corners based on another DEM used for 'noise' (usually relative elevation).
// We blend to make sure we have linear interp at the edge of the linear interp block, so we get good tiling.
// A weight factor also tunes this in and out.
void BlobifyEnvironment(const DEMGeo& variant_source, const DEMGeo& base, DEMGeo& derived, int xmult, int ymult)
{
    derived.resize((base.mWidth - 1) * xmult + 1, (base.mHeight - 1) * ymult + 1);
    derived.copy_geo_from(base);

    // for every 'block' to be usampled
    for (int yiz = 0; yiz < base.mHeight - 1; ++yiz)
        for (int xiz = 0; xiz < base.mWidth - 1; ++xiz)
        {
            // fer each point
            for (int dy = 0; dy <= ymult; ++dy)
                for (int dx = 0; dx <= xmult; ++dx)
                {
                    float dx_fac = (float)dx / (float)xmult;
                    float dy_fac = (float)dy / (float)ymult;

                    // This is the weights for a linear blend
                    double q1 = dx_fac * dy_fac;
                    double q2 = (1.0 - dx_fac) * dy_fac;
                    double q3 = dx_fac * (1.0 - dy_fac);
                    double q4 = (1.0 - dx_fac) * (1.0 - dy_fac);

                    // Four corner values
                    float v1 = base.get(xiz + 1, yiz + 1);
                    float v2 = base.get(xiz, yiz + 1);
                    float v3 = base.get(xiz + 1, yiz);
                    float v4 = base.get(xiz, yiz);

                    // clean interp
                    float v_linear = q1 * v1 + q2 * v2 + q3 * v3 + q4 * v4;

                    // Scaling factor to blend to linear at edges, blob at edge
                    float x_weird = (0.5 - fabs(dx_fac - 0.5)) * 2.0;
                    float y_weird = (0.5 - fabs(dy_fac - 0.5)) * 2.0;
                    float weird_mix = min(x_weird, y_weird) * gDemPrefs.rain_disturb;

                    // This is the 'noise' ratio from the variant source
                    float weird_ratio = variant_source.value_linear(derived.x_to_lon(xiz * xmult + dx),
                                                                    derived.y_to_lat(yiz * ymult + dy));
                    // How much to mix in this noise
                    weird_ratio = min(max(weird_ratio, 0.0f), 1.0f);
                    float max_ever = max(max(v1, v2), max(v3, v4));
                    float min_ever = min(min(v1, v2), min(v3, v4));

                    // Generated weird value
                    float v_weird = min_ever + weird_ratio * (max_ever - min_ever);

                    // mix werid and linear
                    derived(xiz * xmult + dx, yiz * ymult + dy) = v_linear * (1.0 - weird_mix) + v_weird * weird_mix;
                }
        }
}

// Same idea as above, but...try to "snap" enums.
void BlobifyEnvironmentEnum(const DEMGeo& variant_source, const DEMGeo& base, DEMGeo& derived, int xmult, int ymult)
{
    derived.resize((base.mWidth - 1) * xmult + 1, (base.mHeight - 1) * ymult + 1);
    derived.copy_geo_from(base);

    // for every 'block' to be usampled
    for (int yiz = 0; yiz < base.mHeight - 1; ++yiz)
        for (int xiz = 0; xiz < base.mWidth - 1; ++xiz)
        {
            // fer each point
            for (int dy = 0; dy <= ymult; ++dy)
                for (int dx = 0; dx <= xmult; ++dx)
                {
                    float dx_fac = (float)dx / (float)xmult;
                    float dy_fac = (float)dy / (float)ymult;

                    // This is the weights for a linear blend
                    double q1 = dx_fac * dy_fac;
                    double q2 = (1.0 - dx_fac) * dy_fac;
                    double q3 = dx_fac * (1.0 - dy_fac);
                    double q4 = (1.0 - dx_fac) * (1.0 - dy_fac);

                    // Four corner values
                    float v1 = base.get(xiz + 1, yiz + 1);
                    float v2 = base.get(xiz, yiz + 1);
                    float v3 = base.get(xiz + 1, yiz);
                    float v4 = base.get(xiz, yiz);

                    float w1 = variant_source.value_linear(base.x_to_lon(xiz + 1), base.y_to_lat(yiz + 1));
                    float w2 = variant_source.value_linear(base.x_to_lon(xiz), base.y_to_lat(yiz + 1));
                    float w3 = variant_source.value_linear(base.x_to_lon(xiz + 1), base.y_to_lat(yiz));
                    float w4 = variant_source.value_linear(base.x_to_lon(xiz), base.y_to_lat(yiz));

                    float w = variant_source.value_linear(derived.x_to_lon(xiz * xmult + dx),
                                                          derived.y_to_lat(yiz * ymult + dy));

                    float d1 = fabsf(w1 - w);
                    float d2 = fabsf(w2 - w);
                    float d3 = fabsf(w3 - w);
                    float d4 = fabsf(w4 - w);

                    if (d1 > d2 && d1 > d3 && d1 > d4)
                        derived(xiz * xmult + dx, yiz * ymult + dy) = v1;
                    else if (d2 > d3 && d2 > d4)
                        derived(xiz * xmult + dx, yiz * ymult + dy) = v2;
                    else if (d3 > d4)
                        derived(xiz * xmult + dx, yiz * ymult + dy) = v3;
                    else
                        derived(xiz * xmult + dx, yiz * ymult + dy) = v4;
                }
        }
}

/*
 * UpsampleFromParamLinear
 *
 * Upsample linear map.  Given three maps: master data at low and hi res and slave data at low res,
 * fake the slave data at high res by trying to grok linear relationships.
 * This isn't exactly an ideal algorithm...
 *
 */
void UpsampleFromParamLinear(DEMGeo& masterOrig, DEMGeo& masterDeriv, DEMGeo& slaveOrig, DEMGeo& slaveDeriv)
{
    float slaveMin = slaveOrig(0, 0);
    float slaveMax = slaveOrig(0, 0);
    float masterMin = masterOrig(0, 0);
    float masterMax = masterOrig(0, 0);

    int x, y;
    for (y = 0; y < masterOrig.mHeight; ++y)
        for (x = 0; x < masterOrig.mWidth; ++x)
        {
            masterMin = min(masterMin, masterOrig(x, y));
            masterMax = max(masterMax, masterOrig(x, y));
        }
    for (y = 0; y < slaveOrig.mHeight; ++y)
        for (x = 0; x < slaveOrig.mWidth; ++x)
        {
            slaveMin = min(slaveMin, slaveOrig(x, y));
            slaveMax = max(slaveMax, slaveOrig(x, y));
        }

    // Here comes the whacko part: we're basically going to vary the slope of the relationship
    // between the master and slave param over the DEM so that we hit an exact match on the grid
    // points from the original.

    float offset = slaveMin - masterMin;

    DEMGeo slope(slaveOrig), hack_offset(slaveOrig);
    for (y = 0; y < slope.mHeight; ++y)
        for (x = 0; x < slope.mWidth; ++x)
        {
            float sVal = slaveOrig(x, y);
            float mVal = masterOrig(x, y);
            sVal -= slaveMin;
            mVal -= masterMin;
            if (mVal == 0.0)
                slope(x, y) = 0.0;
            else
                slope(x, y) = sVal / mVal;

            // Hack offset: if the slope of the master data is 0, e.g. the
            // slave is changing without the master changing, we simply use
            // a linear offset to interpolate the slave - otherwise we get
            // no interpolation and it looks like ass.
            if (mVal == 0.0)
                hack_offset(x, y) = sVal - mVal;
            else
                hack_offset(x, y) = 0.0;
        }

    slaveDeriv = masterDeriv;

    for (y = 0; y < slaveDeriv.mHeight; ++y)
        for (x = 0; x < slaveDeriv.mWidth; ++x)
        {
            float m = slope.value_linear(slaveDeriv.x_to_lon(x), slaveDeriv.y_to_lat(y));
            float ho = hack_offset.value_linear(slaveDeriv.x_to_lon(x), slaveDeriv.y_to_lat(y));
            float v = slaveDeriv(x, y);
            slaveDeriv(x, y) = slaveMin + (m * (v - masterMin) + ho);
        }
}

/*
 * BinarayDEMFromEnum
 *
 * Replace each point in the DEM with 1.0 if the value matches the
 * passed in param, or 0.0 if it does not.  Return the number of
 * found instances.
 *
 */
int BinaryDEMFromEnum(DEMGeo& dem, float value, float inAccept, float inFail)
{
    int ct = 0;
    for (int y = 0; y < dem.mHeight; ++y)
        for (int x = 0; x < dem.mWidth; ++x)
        {
            int v;
            dem(x, y) = v = (dem(x, y) == value) ? inAccept : inFail;
            ct += v;
        }
    return ct;
}

#pragma mark -

static float GetSnowLine(float lat, float moisture)
{
    if (moisture < 0.0)
        moisture = 0.0;
    if (moisture > 1.0)
        moisture = 1.0;
    int n = 0;
    float alat = fabs(lat);
    float dry = 1.0 - moisture;
    while (kSnowLineInfo[n].lat != -9999)
    {
        if (alat == kSnowLineInfo[n].lat)
        {
            if (lat > 0)
                return kSnowLineInfo[n].nh_dry * dry + kSnowLineInfo[n].nh_wet * moisture;
            else
                return kSnowLineInfo[n].sh_dry * dry + kSnowLineInfo[n].sh_wet * moisture;
        }
        if (alat < kSnowLineInfo[n].lat && alat > kSnowLineInfo[n + 1].lat)
        {
            float rat = (alat - kSnowLineInfo[n + 1].lat) / (kSnowLineInfo[n].lat - kSnowLineInfo[n + 1].lat);
            float v1, v2;
            if (lat > 0)
                v1 = kSnowLineInfo[n].nh_dry * dry + kSnowLineInfo[n].nh_wet * moisture;
            else
                v1 = kSnowLineInfo[n].sh_dry * dry + kSnowLineInfo[n].sh_wet * moisture;
            if (lat > 0)
                v2 = kSnowLineInfo[n + 1].nh_dry * dry + kSnowLineInfo[n + 1].nh_wet * moisture;
            else
                v2 = kSnowLineInfo[n + 1].sh_dry * dry + kSnowLineInfo[n + 1].sh_wet * moisture;

            return rat * v1 + (1.0 - rat) * v2;
        }
        ++n;
    }
    return 6000;
}

static float GetRoadDensity(Pmwx::Halfedge_const_handle he)
{
    float best = 0;
    for (GISNetworkSegmentVector::const_iterator i = he->data().mSegments.begin(); i != he->data().mSegments.end(); ++i)
    {
        best = max(best, gNetFeatures[i->mFeatType].density_factor);
    }
    return best;
}

// We are passed in a rough urban density calculation, basically a ballpark esetimate.  We then
// make it more detailed via roads.
//
// URBAN DENSITY - HOW THIS WORKS
// 1. we are passed in a normalized global density map, meaning it's the urban density from 0 to 1.
// it is formed by taking the landuse urban data from the USGS (this is a thermal return that indicates
// serious urban area) and putting it through a max-distance filter, which basically spreads the data
// outward so that the value is your proximity to a USGS urban square (1.0 means you are one, 0.1 means
// you're almost at our max distance from one to still be called urban.
//
// We then build a map of road junctions weighted by a factor from the config files-this is basically
// a heuristic transportation net density.  We normalize that from 0-1 once done, and then we run
// it through a power curve to accentuate the mid-levels of density (otherwise fairly dense areas are
// considerably less dense than a few freak areas that have a ton of junctions.)
//
// Finally we average them together weighted...essentialy this makes the macro density be a function
// of USGS and the micro density be a function of transportation, and of course you have to be both
// to be full density.  Right now we use a 50-50 split.
//
// Note: doesn't normalizing the transportation density cause you to have urban "hot spots" in the
// middle of nowhere?  Well...yes...we don't have a good solution for that yet, but also it is worth
// noting that this tends to happen anyway, because a bunch of logging roads will be marked as
// single lane local, work up a junction density, and poof...you have a little Aspen in the middle of
// a national forest.  (I really can't think of a good solution for this...USGS thermal returns are
// lousy at distinguishing a small suburban town (population 5000-10000) from the deep woods, and apparently
// neither is the transportation network. :-(
static void BuildRoadDensityDEM(const Pmwx& inMap, DEMGeo& ioTransport)
{
    int xp, yp;
    float max_road_density_vecs = 0.0;

    fprintf(stderr, "BuildRoadDensityDEM");
    for (yp = 0; yp < ioTransport.mHeight; ++yp)
        for (xp = 0; xp < ioTransport.mWidth; ++xp)
            ioTransport(xp, yp) = (ioTransport(xp, yp) == lu_globcover_WATER) ? 1.0 : 0.0;

    PolyRasterizer<double> rasterizer;
    SetupWaterRasterizer(inMap, ioTransport, rasterizer, terrain_Water);
    int x, y = 0;
    rasterizer.StartScanline(y);
    while (!rasterizer.DoneScan())
    {
        int x1, x2;
        while (rasterizer.GetRange(x1, x2))
        {
            for (x = x1; x < x2; ++x)
            {
                if (ioTransport.get(x, y) != DEM_NO_DATA)
                    ioTransport(x, y) = max(ioTransport(x, y), 1.0f);
            }
        }
        ++y;
        if (y >= ioTransport.mHeight)
            break;
        rasterizer.AdvanceScanline(y);
    }

    for (Pmwx::Halfedge_const_iterator iter = inMap.halfedges_begin(); iter != inMap.halfedges_end(); ++iter)
    {
        if (!iter->data().mSegments.empty())
        {
            int tsx, tsy, tdx, tdy;
            ioTransport.xy_nearest(CGAL::to_double(iter->source()->point().x()),
                                   CGAL::to_double(iter->source()->point().y()), tsx, tsy);
            ioTransport.xy_nearest(CGAL::to_double(iter->target()->point().x()),
                                   CGAL::to_double(iter->target()->point().y()), tdx, tdy);

            for (GISNetworkSegmentVector::const_iterator seg = iter->data().mSegments.begin();
                 seg != iter->data().mSegments.end(); ++seg)
            {

                switch (seg->mFeatType)
                {
                case road_MotorwayOneway:
                case train_RailwayOneway:
                    ioTransport(tsx, tsy) = max(ioTransport(tsx, tsy), 0.5f);
                    ioTransport(tdx, tdy) = max(ioTransport(tdx, tdy), 0.5f);
                    break;
                }
            }
        }
    }
}

#if 0
/*
 * RasterizePolyGreen
 *
 */
void RasterizePolyGreen(Pmwx::Face_const_handle face, DEMGeo& landuse, bool trees)
{
	PolyRasterizer<double> rasterizer;
	Pmwx::Ccb_halfedge_const_circulator	iter, stop;
	iter = stop = face->outer_ccb();
	do {
		double x1 = landuse.lon_to_x(iter->source()->point().x());
		double y1 = landuse.lat_to_y(iter->source()->point().y());
		double x2 = landuse.lon_to_x(iter->target()->point().x());
		double y2 = landuse.lat_to_y(iter->target()->point().y());

		if (y1 != y2)
		{
			if (y1 < y2)
				rasterizer.masters.push_back(PolyRasterSeg_t(x1,y1,x2,y2));
			else
				rasterizer.masters.push_back(PolyRasterSeg_t(x2,y2,x1,y1));
		}

		++iter;
	} while (iter != stop);
	for (Pmwx::Holes_const_iterator hole = face->holes_begin(); hole != face->holes_end(); ++hole)
	{
		iter = stop = *hole;
		do {
			double x1 = landuse.lon_to_x(iter->source()->point().x());
			double y1 = landuse.lat_to_y(iter->source()->point().y());
			double x2 = landuse.lon_to_x(iter->target()->point().x());
			double y2 = landuse.lat_to_y(iter->target()->point().y());

			if (y1 != y2)
			{
				if (y1 < y2)
					rasterizer.masters.push_back(PolyRasterSeg_t(x1,y1,x2,y2));
				else
					rasterizer.masters.push_back(PolyRasterSeg_t(x2,y2,x1,y1));
			}

			++iter;
		} while (iter != stop);
	}
	rasterizer.SortMasters();
	int y = 0;
	rasterizer.StartScanline(y);
	while (!rasterizer.DoneScan())
	{
		int x1, x2;
		while (rasterizer.GetRange(x1, x2))
		{
			for (int x = x1; x < x2; ++x)
			{
				if (x > 0 && x <= landuse.mWidth)
				{
					landuse(x,y) = trees ? lu_usgs_FOREST_AND_FIELD : lu_usgs_IRRIGATED_GRASSLAND;
				}
			}
		}
		++y;
		if (y >= landuse.mHeight) break;
		rasterizer.AdvanceScanline(y);
	}

}
#endif

#pragma mark -

/*
 * UpsampleEnvironmentalParams
 *
 * Given our DEM parameters, upsample the environmental ones (rainfall,
 * biomass, climate, and temperature) based on elevation variations.
 * This produces a high-res environmental model with local variations
 * based on the high res DEMs and low-res global climate info.
 *
 */
void UpsampleEnvironmentalParams(DEMGeoMap& ioDEMs, ProgressFunc inProg)
{
    if (!gReplacementClimate.empty())
    {
        Assert(!"Why is this code path used?");
        string fname = gReplacementClimate;
        if (!fname.empty())
        {
            MFMemFile* fi = MemFile_Open(fname.c_str());
            if (fi)
            {
                DEMGeoMap new_dems;
                ReadXESFile(fi, NULL, NULL, &new_dems, NULL, inProg);

                double west = ioDEMs[dem_Elevation].mWest;
                double east = ioDEMs[dem_Elevation].mSouth;
                double south = ioDEMs[dem_Elevation].mEast;
                double north = ioDEMs[dem_Elevation].mNorth;

                for (DEMGeoMap::iterator dem_iter = new_dems.begin(); dem_iter != new_dems.end(); ++dem_iter)
                {
                    if (dem_iter->first != dem_Elevation)
                    {
                        DEMGeo& target(ioDEMs[dem_iter->first]);
                        dem_iter->second.subset(target, dem_iter->second.x_lower(west), dem_iter->second.y_lower(east),
                                                dem_iter->second.x_upper(south), dem_iter->second.y_upper(north));
                    }
                }

                MemFile_Close(fi);
            }
        }
    }

    DEMGeo& soil_style = ioDEMs[dem_SoilStyle];
    DEMGeo& agri_style = ioDEMs[dem_AgriStyle];
    DEMGeo& clim_style = ioDEMs[dem_ClimStyle];
    DEMGeo derived_clim, derived_soil, derived_agri;

    BlobifyEnvironmentEnum(ioDEMs[dem_RelativeElevation], clim_style, derived_clim, 60, 60);
    BlobifyEnvironmentEnum(ioDEMs[dem_RelativeElevation], soil_style, derived_soil, 60, 60);
    BlobifyEnvironmentEnum(ioDEMs[dem_RelativeElevation], agri_style, derived_agri, 60, 60);
    soil_style.swap(derived_soil);
    clim_style.swap(derived_clim);
    agri_style.swap(derived_agri);

    return;

    int x, y, c;
    float real_temp, expected;
    // Envrionmental resampling:
    if (ioDEMs.find(dem_Elevation) == ioDEMs.end())
        return;
    if (ioDEMs.find(dem_Temperature) == ioDEMs.end() && ioDEMs.find(dem_TemperatureSeaLevel) == ioDEMs.end())
        return;
    if (ioDEMs.find(dem_Climate) == ioDEMs.end())
        return;
    if (ioDEMs.find(dem_Rainfall) == ioDEMs.end())
        return;
    if (ioDEMs.find(dem_Biomass) == ioDEMs.end())
        return;
    if (ioDEMs.find(dem_TemperatureRange) == ioDEMs.end())
        return;

    bool has_sealevel = ioDEMs.find(dem_TemperatureSeaLevel) != ioDEMs.end();

    DEMGeo& elevation = ioDEMs[dem_Elevation];
    DEMGeo& temperature = ioDEMs[has_sealevel ? dem_TemperatureSeaLevel : dem_Temperature];
    DEMGeo& climate = ioDEMs[dem_Climate];
    DEMGeo& rainfall = ioDEMs[dem_Rainfall];
    DEMGeo& biomass = ioDEMs[dem_Biomass];
    DEMGeo& temp_msl = ioDEMs[dem_TemperatureSeaLevel];
    DEMGeo& temprange = ioDEMs[dem_TemperatureRange];
    DEMGeo elevation_reduced, elevation_general;

    if (inProg)
        inProg(0, 1, "Upsampling Environment", 0.0);

    int reduc = elevation.mWidth / 240;
    DownsampleDEM(elevation, elevation_reduced, reduc);
    DownsampleDEM(elevation_reduced, elevation_general, (elevation_reduced.mWidth - 1) / (temperature.mWidth - 1));
    SpreadDEMValues(temperature);
    SpreadDEMValues(climate);
    SpreadDEMValues(rainfall);
    SpreadDEMValues(biomass);
    SpreadDEMValues(temprange);

    /*************** STEP 1 - INTERPOLATE TEMPERATURE DATA ***************/

    if (inProg)
        inProg(0, 1, "Upsampling Environment", 0.1);

    // We need to interperate temperature based on standard lapse rate!
    // So we build a temperature deviation map - this is how much more the local
    // temperature is than the std lapse rate.

    if (has_sealevel)
    {
        DEMGeo derived_temperature(elevation_reduced);
        for (y = 0; y < derived_temperature.mHeight; ++y)
            for (x = 0; x < derived_temperature.mWidth; ++x)
                derived_temperature(x, y) =
                    derived_temperature(x, y) * kStdLapseRate +
                    temperature.value_linear(derived_temperature.x_to_lon(x), derived_temperature.y_to_lat(y));

        ioDEMs[dem_Temperature].swap(derived_temperature);
    }
    else
    {

        DEMGeo temperature_deviation(temperature);
        for (y = 0; y < temperature_deviation.mHeight; ++y)
            for (x = 0; x < temperature_deviation.mWidth; ++x)
            {
                real_temp = temperature_deviation(x, y);

                map<float, int> histo;
                int samples;
                int xp = elevation.lon_to_x(temperature_deviation.x_to_lon(x));
                int yp = elevation.lat_to_y(temperature_deviation.y_to_lat(y));
                samples = DEMMakeHistogram(elevation, histo, xp - 300, yp - 300, xp + 300, yp + 300);
                expected = HistogramGetPercentile(histo, samples, gDemPrefs.temp_percentile) * kStdLapseRate;

                //		expected =
                // elevation_general.value_linear(temperature_deviation.x_to_lon(x),temperature_deviation.y_to_lat(y)) *
                // kStdLapseRate;
                temperature_deviation(x, y) = real_temp - expected;
            }

        // Now we can convert elevation to temperature.
        DEMGeo derived_temperature(elevation_reduced);
        temp_msl.resize(elevation_reduced.mWidth, elevation_reduced.mHeight);
        temp_msl.copy_geo_from(derived_temperature);
        for (y = 0; y < derived_temperature.mHeight; ++y)
            for (x = 0; x < derived_temperature.mWidth; ++x)
            {
                expected = derived_temperature(x, y) * kStdLapseRate;
                derived_temperature(x, y) =
                    expected + temperature_deviation.value_linear(derived_temperature.x_to_lon(x),
                                                                  derived_temperature.y_to_lat(y));
                temp_msl(x, y) = temperature_deviation.value_linear(derived_temperature.x_to_lon(x),
                                                                    derived_temperature.y_to_lat(y));
            }
        temperature.swap(derived_temperature);
    }

    /*************** STEP 2 - INTERPOLATE OTHER CONTINUOUS PARAMs ***************/

    if (inProg)
        inProg(0, 1, "Upsampling Environment", 0.3);

    DEMGeo final_temperature(ioDEMs[dem_Temperature]);

    // Other continuous parameters are easy - we just do an upsample based on the apparent
    // relationship to temperature.  See comments from UpsampleFromParamLinear on whether
    // this is really a good idea in practice or not.
    DEMGeo derived_rainfall, derived_biomass, derived_temprange;
    //	UpsampleFromParamLinear(temperature, final_temperature, biomass, derived_biomass);
    BlobifyEnvironment(ioDEMs[dem_RelativeElevation], rainfall, derived_rainfall, 60, 60);
    BlobifyEnvironment(ioDEMs[dem_RelativeElevation], temprange, derived_temprange, 60, 60);
    //	BlobifyEnvironment(ioDEMs[dem_RelativeElevation], temprange, derived_temprange, 60, 60);

    /*************** STEP 3 - INTERPOLATE CLIMATE! ***************/

    if (inProg)
        inProg(0, 1, "Upsampling Environment", 0.5);

    // Climate is trickier since it is an enum.  What we do is we treat climate as a blendable
    // attribute by creating linear-interpolated values for this climate from 0-1.  We then pick
    // the max climate for any given point as our final climate.
    int climate_enums[] = {
        climate_TropicalRainForest, climate_DrySteppe,       climate_TemperateAny, climate_ColdAny,
        climate_PolarTundra,        climate_TropicalMonsoon, climate_DryDesert,    climate_TemperateSummerDry,
        climate_ColdSummerDry,      climate_PolarFrozen,     climate_TropicalDry,  climate_TemperateWinterDry,
        climate_ColdWinterDry,      climate_TemperateWet,
    };

    const int climate_count = sizeof(climate_enums) / sizeof(int);
    DEMGeo climates_orig[climate_count];
    bool has_climate[climate_count];
    DEMGeo climates_deriv[climate_count];

    for (c = 0; c < climate_count; ++c)
    {
        climates_orig[c] = climate;
        has_climate[c] = BinaryDEMFromEnum(climates_orig[c], climate_enums[c], 1.0, 0.0) > 0;
        if (has_climate[c])
            UpsampleFromParamLinear(temperature, final_temperature, climates_orig[c], climates_deriv[c]);
    }

    if (inProg)
        inProg(0, 1, "Upsampling Environment", 0.7);

    DEMGeo derived_climate(final_temperature);
    for (y = 0; y < derived_climate.mHeight; ++y)
        for (x = 0; x < derived_climate.mWidth; ++x)
        {
            int best_climate = climate_enums[0];
            float best_value = -9.9e9;
            for (c = 0; c < climate_count; ++c)
            {
                if (has_climate[c])
                    if (climates_deriv[c](x, y) > best_value)
                    {
                        best_climate = climate_enums[c];
                        best_value = climates_deriv[c](x, y);
                    }
            }
            derived_climate(x, y) = best_climate;
        }

    /************ POST PROCESSING - STUFF ALL DEMS **********/

    if (inProg)
        inProg(0, 1, "Upsampling Environment", 0.9);

    //	temperature.swap(temperature_deviation);
    //	elevation.swap(elevation_general);
    climate.swap(derived_climate);
    rainfall.swap(derived_rainfall);
    temprange.swap(derived_temprange);
    biomass.swap(derived_biomass);

    if (inProg)
        inProg(0, 1, "Upsampling Environment", 1.0);
}

/*
 * DeriveDEMs
 *
 * Given a std::set of DEMs for all of the input parameters, calculate all
 * of the derived parameters.  We also need a vector map to do this of course.
 *
 * Input DEMS:
 *
 *	climate, biomass, landuse, temp, temp range, elevation, rainfall
 *
 * Output DEMs:
 *
 *  terrain and vege phenom, 2d and 3d vege density, urban density and prop values
 *  nude terrain color, x-plane terrain type.
 *
 */
void DeriveDEMs(Pmwx& inMap, DEMGeoMap& ioDEMs, AptVector& ioApts, AptIndex& ioAptIndex, int do_translate,
                ProgressFunc inProg)
{
    int x, y;

    {
        //		ioDEMs[dem_OrigLandUse] = ioDEMs[dem_LandUse];
        DEMGeo& lu_t = ioDEMs[dem_LandUse];
        if (do_translate)
            for (int y = 0; y < lu_t.mHeight; ++y)
                for (int x = 0; x < lu_t.mWidth; ++x)
                {
                    int luv = lu_t.get(x, y);
                    if (gLandUseTransTable.count(luv))
                        lu_t(x, y) = gLandUseTransTable[luv];
                }
    }

    //	const DEMGeo&		climate = 	ioDEMs[dem_Climate];
    //	const DEMGeo&		biomass = 	ioDEMs[dem_Biomass];
    const DEMGeo& landuse = ioDEMs[dem_LandUse];
    const DEMGeo& temp = ioDEMs[dem_Temperature];
    //	const DEMGeo&		tempRange = ioDEMs[dem_TemperatureRange];
    const DEMGeo& elevation = ioDEMs[dem_Elevation];
    //	const DEMGeo&		slope = 	ioDEMs[dem_Slope];
    //	const DEMGeo&		slopeHeading = ioDEMs[dem_SlopeHeading];
    const DEMGeo& rainfall = ioDEMs[dem_Rainfall];
    DEMGeo& urbanSquare = ioDEMs[dem_UrbanSquare];

    int reduce_1 = elevation.mWidth / 200;
    DEMGeo elevation_reduced;
    DownsampleDEM(elevation, elevation_reduced, reduce_1);
    //	DEMGeo	landuseBig;
    //	int reduce_2 = elevation.mWidth / 600;
    //	UpsampleDEM(landuse, landuseBig, reduce_2);

    //	DEMGeo	values(landuse);
    //	DEMGeo	nudeColor(landuse);
    //	DEMGeo	vegetation(elevation_reduced);
    DEMGeo urban;
    DEMGeo urbanRadial;
    DEMGeo urbanTrans;
    urbanSquare = landuse;
    DEMGeo forests(landuse);

    urban.copy_geo_from(landuse);
    urbanRadial.copy_geo_from(landuse);
    urbanTrans.copy_geo_from(landuse);

    //	double lon, lat;

    /********************************************************************************************************
     * CALCULATE URBAN DENSITY AND PROPERTY VALUES
     ********************************************************************************************************/

    if (inProg)
        inProg(0, 1, "Calculating Derived Raster Data", 0.0);

    CalculateFilter(URBAN_DENSE_KERN_SIZE, sUrbanDenseSpreaderKernel, demFilter_Spread, true);
    CalculateFilter(URBAN_RADIAL_KERN_SIZE, sUrbanRadialSpreaderKernel, demFilter_Linear, false);
    CalculateFilter(URBAN_TRANS_KERN_SIZE, sUrbanTransSpreaderKernel, demFilter_Spread, true);

    double radial_max = 0.0;

    {
        DEMGeo urbanTemp(landuse.mWidth, landuse.mHeight);
        for (y = 0; y < landuse.mHeight; ++y)
            for (x = 0; x < landuse.mWidth; ++x)
            {
                float e = landuse.get(x, y);

                LandClassInfoTable::iterator i = gLandClassInfo.find(e);
                if (i != gLandClassInfo.end())
                    e = i->second.urban_density;
                else if (e == lu_globcover_URBAN_HIGH)
                    e = 1.0;
                else if (e == lu_globcover_URBAN_TOWN)
                    e = 0.25;
                else if (e == lu_globcover_URBAN_LOW)
                    e = 0.5;
                else if (e == lu_globcover_URBAN_MEDIUM)
                    e = 0.75;

                else if (e == lu_globcover_URBAN_SQUARE_HIGH)
                    e = 1.0;
                else if (e == lu_globcover_URBAN_SQUARE_TOWN)
                    e = 0.25;
                else if (e == lu_globcover_URBAN_SQUARE_LOW)
                    e = 0.5;
                else if (e == lu_globcover_URBAN_SQUARE_MEDIUM)
                    e = 0.75;

                else if (e == lu_globcover_URBAN_CROP_TOWN)
                    e = 0.1;
                else if (e == lu_globcover_URBAN_SQUARE_CROP_TOWN)
                    e = 0.1;
                else if (e == lu_globcover_INDUSTRY_SQUARE)
                    e = 1.0;
                else if (e == lu_globcover_INDUSTRY)
                    e = 1.0;
                else if (e == lu_usgs_URBAN_IRREGULAR)
                    e = 1.0;
                else if (e == lu_usgs_URBAN_SQUARE)
                    e = 1.0;

                else
                    e = 0.0;
                urbanTemp(x, y) = e;
            }

        urbanTemp.derez(8);

        urban.resize(urbanTemp.mWidth, urbanTemp.mHeight);
        urbanRadial.resize(urbanTemp.mWidth, urbanTemp.mHeight);
        urbanTrans.resize(urbanTemp.mWidth, urbanTemp.mHeight);

        for (y = 0; y < urbanTemp.mHeight; ++y)
            for (x = 0; x < urbanTemp.mWidth; ++x)
            {
                urban(x, y) = urbanTemp.kernelN(x, y, URBAN_DENSE_KERN_SIZE, sUrbanDenseSpreaderKernel);
                //			urban(x,y) 		= urbanTemp(x,y);
                double local = urbanTemp.kernelN(x, y, URBAN_RADIAL_KERN_SIZE, sUrbanRadialSpreaderKernel);
                urbanRadial(x, y) = local;
                radial_max = max(local, radial_max);
            }
    }

    if (radial_max > 0.0)
        urbanRadial *= (1.0 / radial_max);

    for (y = 0; y < urban.mHeight; ++y)
        for (x = 0; x < urban.mWidth; ++x)
        {
            urban(x, y) = max(0.0f, min(1.0f, urban(x, y)));
            urbanRadial(x, y) = max(0.0f, min(1.0f, urbanRadial(x, y)));
        }

    if (inMap.number_of_halfedges() > 0)
        BuildRoadDensityDEM(inMap, urbanTrans);

    //	CalcPropertyValues(values, elevation_reduced, inMap);

    std::set<int> apts;

    FindAirports(Bbox2(landuse.mWest, landuse.mSouth, landuse.mEast, landuse.mNorth), ioAptIndex, apts);
    for (std::set<int>::iterator apt = apts.begin(); apt != apts.end(); ++apt)
        if (ioApts[*apt].kind_code == apt_airport)
            for (AptPavementVector::iterator rwy = ioApts[*apt].pavements.begin(); rwy != ioApts[*apt].pavements.end();
                 ++rwy)
                if (rwy->surf_code == apt_surf_asphalt || rwy->surf_code == apt_surf_concrete)
                {
                    POINT2 p = CGAL_midpoint(rwy->ends.source(), rwy->ends.target());
                    float e = urbanTrans.xy_nearest(CGAL2DOUBLE(p.x()), CGAL2DOUBLE(p.y()), x, y);
                    if (e != DEM_NO_DATA)
                        urbanTrans(x, y) = 1.0;
                }

    urbanTrans.filter_self(URBAN_TRANS_KERN_SIZE, sUrbanTransSpreaderKernel);

    for (y = 0; y < urbanTrans.mHeight; ++y)
        for (x = 0; x < urbanTrans.mWidth; ++x)
            urbanTrans(x, y) = max(0.0f, min(urbanTrans(x, y), 1.0f));

    for (y = 0; y < urbanSquare.mHeight; ++y)
        for (x = 0; x < urbanSquare.mWidth; ++x)
        {
            float e = urbanSquare.get(x, y);

            if (e == lu_globcover_URBAN_HIGH)
                e = 2.0;
            else if (e == lu_globcover_URBAN_TOWN)
                e = 2.0;
            else if (e == lu_globcover_URBAN_LOW)
                e = 2.0;
            else if (e == lu_globcover_URBAN_MEDIUM)
                e = 2.0;

            else if (e == lu_globcover_URBAN_SQUARE_TOWN)
                e = 1.0;
            else if (e == lu_globcover_URBAN_SQUARE_LOW)
                e = 1.0;
            else if (e == lu_globcover_URBAN_SQUARE_MEDIUM)
                e = 1.0;
            else if (e == lu_globcover_URBAN_SQUARE_HIGH)
                e = 1.0;

            else if (e == lu_globcover_URBAN_CROP_TOWN)
                e = 2.0;
            else if (e == lu_globcover_URBAN_SQUARE_CROP_TOWN)
                e = 1.0;
            else if (e == lu_globcover_INDUSTRY_SQUARE)
                e = 1.0;
            else if (e == lu_globcover_INDUSTRY)
                e = 2.0;
            else
                e = DEM_NO_DATA;
            urbanSquare(x, y) = e;
        }

    SpreadDEMValues(urbanSquare);
    if (urbanSquare.get(0, 0) == DEM_NO_DATA)
        urbanSquare = 1.0;

        /********************************************************************************************************
         * CALCULATE VEGETATION DENSITY
         ********************************************************************************************************/

#if 0
	if (inProg) inProg(0, 1, "Calculating Derived Raster Data", 0.5);

	for (y = 0; y < vegetation.mHeight; ++y)
	for (x = 0; x < vegetation.mWidth ; ++x)
	{
		lon = vegetation.x_to_lon(x);
		lat = vegetation.y_to_lat(y);

		float our_slope = slope.value_linear(lon, lat);
		float our_slopeh = slopeHeading.value_linear(lon, lat);
		if (lat > 0)
		{
			vegetation(x,y) = 0.5 - 0.25 * our_slope - 0.25 * our_slopeh;
		} else {
			vegetation(x,y) = 0.5 - 0.25 * our_slope + 0.25 * our_slopeh;
		}
	}
#endif

#if 0
	/********************************************************************************************************
	 * CALCULATE VEGETATION AND TERRAIN FROM PHENONMEA
	 ********************************************************************************************************/

	// Pass 1 - extract land use values and apply some limits to phenomena.

	for (y = 0; y < landuseBig.mHeight;++y)
	for (x = 0; x < landuseBig.mWidth; ++x)
	{
		int lu = landuseBig(x,y);
		LandUseInfoTable::iterator it = gLandUseInfo.find(lu);
		if (it == gLandUseInfo.end())
		{
			phenomTerrain(x,y) = NO_VALUE;
			phenom2d(x,y) = NO_VALUE;
			phenom3d(x,y) = NO_VALUE;
			density2d(x,y) = NO_VALUE;
			density3d(x,y) = NO_VALUE;
			// TODO: biomass, urban density
		} else {
			phenomTerrain(x,y) = it->second.terrain_type;
			phenom2d(x,y) = it->second.veg2d_type;
			phenom3d(x,y) = it->second.veg3d_type;

			float	biomass_here = biomass.value_linear(landuseBig.x_to_lon(x), landuseBig.y_to_lat(y));
			biomass_here /= 2500.0;
			if (biomass_here > 1.0) biomass_here = 1.0;
			if (biomass_here < 0.0) biomass_here = 0.0;

			if (it->second.veg2d_density == -1.0)
				density2d(x,y) = biomass_here;
			else
				density2d(x,y) = biomass_here * 0.5 + it->second.veg2d_density * 0.5;

			if (it->second.veg3d_density != -1)
				density3d(x,y) = it->second.veg3d_density * 0.5 + biomass_here;
			else
				density3d(x,y) = biomass_here;

			// Phenom control!  If the max temp (ave temp plus half the range) is less than 10 degrees,
			// trees can't grow!
			float max_isotherm_here = temp.value_linear(landuseBig.x_to_lon(x), landuseBig.y_to_lat(y)) +
								0.5 * tempRange.value_linear(landuseBig.x_to_lon(x), landuseBig.y_to_lat(y));
			if (max_isotherm_here < 10.0)
				phenom3d(x,y) = NO_VALUE;

			// The snow line is a function of latitude and also moisture (more dry - gotta be even colder to work up
			// snow).  To give you an idea of what a total HACK I am - this calibration of moisture to rain fall is designed
			// to put the snow cap on Mt Rainier in the right place...it is probably ok - even somewhat rainy areas like NE
			// are going to have enough moisture to maintain snow at relatively low altitudes...we don't have to be a rain
			// forest to have snow cover.  (Besides, users like seeing snow-capped mountains - the sim can be run at any
			// month, but in winter the snow line will of course be lower.)
			float	sn = GetSnowLine(landuseBig.y_to_lat(y),rainfall.value_linear(landuseBig.x_to_lon(x), landuseBig.y_to_lat(y)) / 1200.0);
			// One sanity check - make sure the min temperature is no warmer than 5 degrees - anything more would indicate a
			// very hot area, one that would never form snow.  Because temperature is a function of std lapse rate, we know that
			// (1) our temp values are going to be reasonable even after interpolation, and (2) they're never going to get
			// that hot due to a DEM problem, because that would require a giant crater.
			float	min_isotherm_here = temp.value_linear(landuseBig.x_to_lon(x), landuseBig.y_to_lat(y)) -
								0.5 * tempRange.value_linear(landuseBig.x_to_lon(x), landuseBig.y_to_lat(y));
			if (elevation.value_linear(landuseBig.x_to_lon(x), landuseBig.y_to_lat(y)) > sn && min_isotherm_here < 5)
			{

				phenomTerrain(x,y) = phenom_Ice;
				phenom2d(x,y) = NO_VALUE;
			}
		}
	}

	float	smear2[5*5];
	CalculateFilter(5,smear2,demFilter_Spread,true);
	density3d.filter_self(5,smear2);
	density2d.filter_self(5,smear2);

	// Pass 2 - go through and spread a 2-d and terrain phenomenon everywhere, but clamp the 2-d vege
	// value so we don't actually change what we have!  One special case - for water, go as far as we
	// can to get a 2-d and terrain phenom, and mix 50-50.
	{
		int	search_radius = 10;	// Limit smearing.
		int	wide_radius = max(landuseBig.mWidth, landuseBig.mHeight);
		DEMGeo tempTerrain(phenomTerrain);
		DEMGeo temp2d(phenom2d);
		float tt, vt;
		for (y = 0; y < landuseBig.mHeight;++y)
		for (x = 0; x < landuseBig.mWidth; ++x)
		{
			tt = phenomTerrain(x,y);
			vt = phenom2d(x,y);
			if (vt == NO_VALUE && tt == NO_VALUE)
				fprintf(stderr, "WARNING: %d, %d has NO terrain type!!\n", x, y);

			if (tt == phenom_SeaWater || tt == phenom_FreshWater || tt == phenom_Water)
			{
				phenomTerrain(x,y) = NO_VALUE;
				phenom2d(x,y) = NO_VALUE;
			}
		}

		for (y = 0; y < landuseBig.mHeight;++y)
		for (x = 0; x < landuseBig.mWidth; ++x)
		{
			tt = phenomTerrain(x,y);
			vt = phenom2d(x,y);
			bool	use_wide = tt == NO_VALUE && vt == NO_VALUE;
			if (tt == NO_VALUE)
			{
				tempTerrain(x,y) = phenomTerrain.get_radial(x, y, use_wide ? wide_radius : search_radius, NO_VALUE);
				density2d(x,y) = 1.0;
			}
			if (vt == NO_VALUE)
			{
				temp2d(x,y) = phenom2d.get_radial(x, y, use_wide ? wide_radius : search_radius, NO_VALUE);
				density2d(x,y) = 0.0;
			}
			if (use_wide)
			{
				density2d(x,y) = 0.5;
			}
		}
		phenomTerrain.swap(tempTerrain);
		phenom2d.swap(temp2d);
	}

	// Pass 3 - go in and vary 2-d and 3-d vegetation density based on various parameters.
	for (y = 0; y < density2d.mHeight; ++y)
	for (x = 0; x < density2d.mWidth ; ++x)
	{
		bool	can_vary_2d = false;
		bool	can_vary_3d = false;
		for (int n = 0; n < 9; ++n)
		if (non_integral(density2d.get(x+local_deltas_x[n], y + local_deltas_y[n])))
		{
			can_vary_2d = true;
			break;
		}
		for (int n = 0; n < 9; ++n)
		if (non_integral(density3d.get(x+local_deltas_x[n], y + local_deltas_y[n])))
		{
			can_vary_3d = true;
			break;
		}

		if (!can_vary_2d && !can_vary_3d) continue;

		float	min_change = -0.1;
		float	max_change =  0.1;
		float	slope_factor = slope.get(x,y);
		min_change -= (slope_factor / 4.0);
		max_change -= (slope_factor / 4.0);
		float	heading_factor = slopeHeading.get(x,y);
		min_change += (heading_factor / 8.0);
		max_change += (heading_factor / 8.0);

		if (can_vary_2d)
		{
			float h = density2d.get(x,y);
			h += RandRange(min_change, max_change);
			if (h < 0.0) h = 0.0; if (h > 1.0) h = 1.0;
			density2d(x,y) = h;
		}
		if (can_vary_3d)
		{
			float h = density3d.get(x,y);
			h += RandRange(min_change, max_change);
			if (h < 0.0) h = 0.0; if (h > 1.0) h = 1.0;
			density3d(x,y) = h;
		}
	}

	/********************************************************************************************************
	 * CALCULATE ACTUAL X-PLANE TERRAIN TYPES
	 ********************************************************************************************************/

	// Go through and calculate terrain types
	for (y = 0 ;y < terrain.mHeight; ++y)
	for (x = 0; x < terrain.mWidth; ++x)
	{
		int	tt = phenomTerrain(x,y);
		switch(tt) {
		case phenom_Soil:	nudeColor(x,y) = nude_Brown;		break;
		case phenom_Rock:	nudeColor(x,y) = nude_Gray;			break;
		case phenom_Sand:	nudeColor(x,y) = nude_Yellow;		break;
		case NO_VALUE:		nudeColor(x,y) = NO_VALUE;			break;
		default:			nudeColor(x,y) = nude_Gray;			break;
		}
		int tc = nudeColor(x,y);
		int	vt = phenom2d(x,y);
		int c = climate(x,y);
		float max_slope = 0.0;
		int xrat = (slope.mWidth-1) / (terrain.mWidth-1);
		int yrat = (slope.mHeight-1) / (terrain.mHeight-1);
		for (int dy = 0; dy < yrat; ++dy)
		for (int dx = 0; dx < xrat; ++dx)
		{
			float the_slope = slope(x * xrat + dx, y * yrat + dy);
			if (the_slope > max_slope) max_slope = the_slope;
		}

		// Try 1 -- exact match
		terrain(x,y) = DEM_NO_DATA;
		int hashv = HashTerrainTypes(tt,tc,vt,c);
		std::pair<TerrainMappingTable::iterator,TerrainMappingTable::iterator>	range = gTerrainMapping.equal_range(hashv);
		for (TerrainMappingTable::iterator i = range.first; i != range.second; ++i)
		{
			if (i->second.slope_min <= max_slope &&
				i->second.slope_max >= max_slope &&
				(i->second.terrain_type == tt) &&
				(i->second.terrain_color == tc || i->second.terrain_color == NO_VALUE) &&
				(i->second.vege2d_type == vt) &&
				(i->second.climate == c || i->second.climate == NO_VALUE))
			{
				terrain(x,y) = i->second.terrain_file;
			}
		}

		// Huh...no match.  Probably we smeared out a landuseBig that can't be matched, e.g.
		// grass over urban.  Try just the base terrain.
		if (terrain(x,y) == DEM_NO_DATA && density2d(x,y) < 0.5)
		{
			hashv = HashTerrainTypes(tt,tc,NO_VALUE,NO_VALUE);
			range = gTerrainMapping.equal_range(hashv);
			for (TerrainMappingTable::iterator i = range.first; i != range.second; ++i)
			{
				if (i->second.slope_min <= max_slope &&
					i->second.slope_max >= max_slope &&
					(i->second.terrain_type == tt) &&
					(i->second.terrain_color == tc || i->second.terrain_color == NO_VALUE) &&
					(i->second.vege2d_type == NO_VALUE) &&
					(i->second.climate == NO_VALUE))
				{
					terrain(x,y) = i->second.terrain_file;
				}
			}
		}

		// Or the other way, try just the vege
		if (terrain(x,y) == DEM_NO_DATA && density2d(x,y) > 0.5)
		{
			hashv = HashTerrainTypes(NO_VALUE,NO_VALUE, vt, c);
			range = gTerrainMapping.equal_range(hashv);
			for (TerrainMappingTable::iterator i = range.first; i != range.second; ++i)
			{
				if (i->second.slope_min <= max_slope &&
					i->second.slope_max >= max_slope &&
					(i->second.terrain_type == NO_VALUE) &&
					(i->second.terrain_color == NO_VALUE) &&
					(i->second.vege2d_type == vt) &&
					(i->second.climate == c || i->second.climate == NO_VALUE))
				{
					terrain(x,y) = i->second.terrain_file;
				}
			}
		}
		if (terrain(x,y) == DEM_NO_DATA)
			fprintf(stderr,"ERROR: Could not find land use for: %s %s %s %s\n", FetchTokenString(tt), FetchTokenString(tc),FetchTokenString(vt),FetchTokenString(c));
	}
#endif

#if 0
	for (y = 0; y < landuse.mHeight;++y)
	for (x = 0; x < landuse.mWidth; ++x)
	{
		float e = landuse(x,y);
		if (e != lu_usgs_INLAND_WATER &&
			e != lu_usgs_SEA_WATER)
			landuse(x,y) = DEM_NO_DATA;
	}
	landuse.fill_nearest();
#endif

    if (inProg)
        inProg(0, 1, "Calculating Derived Raster Data", 1.0);

    for (y = 0; y < landuse.mHeight; ++y)
        for (x = 0; x < landuse.mWidth; ++x)
        {
            int l = landuse.get(x, y);
            float t = temp.get(temp.map_x_from(landuse, x), temp.map_y_from(landuse, y));
            float r = rainfall.get(rainfall.map_x_from(landuse, x), rainfall.map_y_from(landuse, y));

            int f = FindForest(l, t, r);

            if (f == NO_VALUE)
                f = DEM_NO_DATA;
            forests(x, y) = f;
        }

    forests.fill_nearest();

    ioDEMs[dem_UrbanDensity].swap(urban);
    //	ioDEMs[dem_TerrainPhenomena].swap(phenomTerrain);
    //	ioDEMs[dem_2dVegePhenomena ].swap(phenom2d);
    //	ioDEMs[dem_3dVegePhenomena ].swap(phenom3d);
    //	ioDEMs[dem_2dVegiDensity   ].swap(density2d);
    //	ioDEMs[dem_3dVegiDensity   ].swap(density3d);
    //	ioDEMs[dem_UrbanPropertyValue].swap(values);
    //	ioDEMs[dem_TerrainType	   ].swap(terrain);
    //	ioDEMs[dem_NudeColor	   ].swap(nudeColor);
    //	ioDEMs[dem_VegetationDensity].swap(vegetation);
    ioDEMs[dem_UrbanRadial].swap(urbanRadial);
    ioDEMs[dem_UrbanTransport].swap(urbanTrans);
    ioDEMs[dem_ForestType].swap(forests);

    /************************************************************************************************************************
     * WATER AND BATHYMETRY CALC
     ************************************************************************************************************************/

    DEMGeo water_surface(WATER_SURF_DIM, WATER_SURF_DIM);
    water_surface.mPost = 0;
    water_surface.copy_geo_from(elevation);
    water_surface = DEM_NO_DATA;

    map<float, int> histo[WATER_SURF_DIM][WATER_SURF_DIM];
    int total[WATER_SURF_DIM][WATER_SURF_DIM] = {0};
    std::set<Halfedge_handle> coast_edges;
    std::set<Face_handle> wet_faces;

    for (Pmwx::Face_handle f = inMap.faces_begin(); f != inMap.faces_end(); ++f)
        if (!f->is_unbounded())
            if (f->data().IsWater())
                wet_faces.insert(f);

    FindEdgesForFaceSet<Pmwx>(wet_faces, coast_edges);

    PolyRasterizer<double> raster;

    y = SetupRasterizerForDEM(coast_edges, elevation, raster);
    int x1, x2;
    raster.StartScanline(0);

    while (!raster.DoneScan())
    {
        while (raster.GetRange(x1, x2))
        {
            for (x = x1; x < x2; ++x)
            {
                float e = elevation(x, y);
                if (e != DEM_NO_DATA)
                {
                    double lon = elevation.x_to_lon(x);
                    double lat = elevation.y_to_lat(y);
                    int bucket_x = water_surface.lon_to_x(lon);
                    int bucket_y = water_surface.lat_to_y(lat);
                    //					debug_mesh_point(Point2(lon,lat),1,1,1);
                    histo[bucket_x][bucket_y][e]++;
                    ++total[bucket_x][bucket_y];
                }
            }
        }
        ++y;
        if (y >= elevation.mHeight)
            break;
        raster.AdvanceScanline(y);
    }

    for (y = 0; y < water_surface.mHeight; ++y)
        for (x = 0; x < water_surface.mWidth; ++x)
        {
            if (total[x][y])
            {
                //			for(map<float,int>::iterator h = msl_hysto[x][y].begin(); h != msl_hysto[x][y].end(); ++h)
                //				printf("%f: %d\n", h->first, h->second);
                int want = total[x][y] / 10;
                //			if(wet < (total /2)) want = 0;
                for (map<float, int>::iterator h = histo[x][y].begin(); h != histo[x][y].end(); ++h)
                    if (h->second > want)
                    {
                        water_surface(x, y) = h->first;
                        break;
                    }
                    else
                        want -= h->second;
            }
        }

    water_surface.fill_nearest();
    //	DEMGeo& bath_old(ioDEMs[dem_Bathymetry]);
    //
    //	DEMGeo	bath_new(water_surface);
    //	for(y = 0; y < bath_new.mHeight; ++y)
    //	for(x = 0; x < bath_new.mWidth ; ++x)
    //	{
    //		float surf = bath_new(x,y);
    //		float bath = bath_old.value_linear(bath_new.x_to_lon(x),bath_new.y_to_lat(y));
    //		float bath_constrained = fltlim(bath, surf - MAX_DEPTH, surf - MIN_DEPTH);
    //		bath_new(x,y) = bath_constrained;
    //	}
    //
    //	bath_old.swap(bath_new);

    ioDEMs[dem_Bathymetry].fill_nearest();
}

void CalcWaterSurface(DEMGeoMap& ioDEMs, double west, double south, double east, double north)
{
    const DEMGeo& raw = ioDEMs[dem_Elevation];
    DEMGeo& surf = ioDEMs[dem_Water_Surface];

    Assert(raw.mPost != 0);

    int x1 = raw.lon_to_x(west);
    int y1 = raw.lat_to_y(south);
    int x2 = raw.lon_to_x(east);
    int y2 = raw.lat_to_y(north);

    surf.resize(x2 - x1 + 1, y2 - y1 + 1);
    surf = DEM_NO_DATA;
    surf.mNorth = north;
    surf.mSouth = south;
    surf.mWest = west;
    surf.mEast = east;
    surf.mPost = 1;

    for (int y = y1; y <= y2; ++y)
        for (int x = x1; x <= x2; ++x)
        {
            int count = 0;
            const int rad = 2;
            const int max_samples = (rad * 2 + 1) * (rad * 2 + 1);
            float hbuf[max_samples];
            for (int dy = -rad; dy <= rad; ++dy)
                for (int dx = -rad; dx <= rad; ++dx)
                {
                    float h = raw.get(x + dx, y + dy);
                    if (h != DEM_NO_DATA)
                    {
                        hbuf[count++] = h;
                    }
                }
            DebugAssert(count <= max_samples);

            if (count > 0)
            {
                sort(hbuf, hbuf + count);

                int cut_front = count / 8;
                int cut_back = count / 3;
                int stop = count - cut_back;
                float h_total = 0.0f;

                DebugAssert((count - cut_front - cut_back) > 0);
                for (int c = cut_front; c < stop; ++c)
                {
                    h_total += hbuf[c];
                }
                surf.set(x - x1, y - y1, h_total / ((float)(count - cut_front - cut_back)));
            }
        }
}

void CalcWaterBathymetry(DEMGeoMap& ioDEMs)
{
    const DEMGeo& elev(ioDEMs[dem_Elevation]);
    const DEMGeo& surf(ioDEMs[dem_Water_Surface]);
    DEMGeo& bath(ioDEMs[dem_Bathymetry]);

    bath.resize(elev.mWidth, elev.mHeight);
    bath.copy_geo_from(elev);
    bath = 0;

    for (DEMGeo::address a = elev.address_begin(); a != elev.address_end(); ++a)
        if (elev[a] != DEM_NO_DATA)
            bath[a] = FLT_MAX;

    // https://mshgrid.com/2021/02/04/the-fast-sweeping-algorithm/
    int width = bath.mWidth;
    int height = bath.mHeight;

    const int NSweeps = 4;
    // sweep directions { start, end, step }
    const int dirX[NSweeps][3] = {{0, width - 1, 1}, {width - 1, 0, -1}, {width - 1, 0, -1}, {0, width - 1, 1}};
    const int dirY[NSweeps][3] = {{0, height - 1, 1}, {0, height - 1, 1}, {height - 1, 0, -1}, {height - 1, 0, -1}};

    float aa[2];
    double d_new, a, b;
    int s, ix, iy;
    const double h = 1.0, f = 1.0;

    for (int s = 0; s < NSweeps; s++)
    {
        for (int iy = dirY[s][0]; dirY[s][2] * iy <= dirY[s][1]; iy += dirY[s][2])
        {
            for (int ix = dirX[s][0]; dirX[s][2] * ix <= dirX[s][1]; ix += dirX[s][2])
            {
                float v = bath.get(ix, iy);
                if (v > 0.0)
                {
                    if (iy == 0)
                        aa[1] = min(v, bath.get(ix, iy + 1));
                    else if (iy == (height - 1))
                        aa[1] = min(v, bath.get(ix, iy - 1));
                    else
                        aa[1] = min(bath.get(ix, iy - 1), bath.get(ix, iy + 1));

                    if (ix == 0)
                        aa[0] = min(v, bath.get(ix + 1, iy));
                    else if (ix == (width - 1))
                        aa[0] = min(v, bath.get(ix - 1, iy));
                    else
                        aa[0] = min(bath.get(ix - 1, iy), bath.get(ix + 1, iy));

                    a = aa[0];
                    b = aa[1];
                    d_new = (fabs(a - b) < f * h ? (a + b + sqrt(2.0 * f * f * h * h - (a - b) * (a - b))) * 0.5
                                                 : std::fminf(a, b) + f * h);

                    if (d_new < v)
                        bath.set(ix, iy, d_new);
                }
            }
        }
    }

    for (DEMGeo::address a = elev.address_begin(); a != elev.address_end(); ++a)
    {
        if (elev[a] != DEM_NO_DATA)
        {
            float d = bath[a];
            bath[a] = surf[a] - min(50.0f, (1.0f + 4.0f * max(0.0f, (d - 1.0f))));
        }
        else
        {
            bath[a] = DEM_NO_DATA;
        }
    }
}

/*
void CalcWaterBathymetry(DEMGeoMap& ioDEMs)
{
    const DEMGeo& elev(ioDEMs[dem_Elevation]);
    const DEMGeo& surf(ioDEMs[dem_Water_Surface]);
          DEMGeo& bath(ioDEMs[dem_Bathymetry]);

    bath.resize(elev.mWidth,elev.mHeight);
    bath.copy_geo_from(elev);
    bath = DEM_NO_DATA;

        address_fifo wet_pixels(bath.mWidth * bath.mHeight);

    for(DEMGeo::address any = elev.address_begin(); any != elev.address_end(); ++any)
    if(elev[any] == DEM_NO_DATA)
    {
        DEMGeo::address n[8];
        int nc = elev.get_neighbors8(any,n);
        for(int i = 0; i < nc; ++i)
        {
            if(elev[n[i]] != DEM_NO_DATA)
                wet_pixels.push(n[i]);
        }
    }

    while(!wet_pixels.empty())
    {
        DEMGeo::address w = wet_pixels.pop();

        if(bath[w] != DEM_NO_DATA)
            continue;

        DEMGeo::address n[8];
        int nc = elev.get_neighbors8(w, n);

        float dmax = 1.0;
        for(int i = 0; i < nc; ++i)
        {
            DEMGeo::address p = n[i];

            if(elev[p] != DEM_NO_DATA)
            {
                float d = bath[p];
                if(d == DEM_NO_DATA)
                {
//					wet_pixels.push(p);
                }
                else
                    dmax = max(dmax,d+1.0f);
            }
        }
        bath[w] = dmax;
    }

    for(DEMGeo::address b = bath.address_begin(); b != bath.address_end(); ++b)
    {
        float h = bath[b];
        if(h != DEM_NO_DATA)
            bath[b] = surf[b] - h;
    }
}
*/

void CalcSlopeParams(DEMGeoMap& ioDEMs, bool force, ProgressFunc inProg)
{
    if (!force && ioDEMs.count(dem_Slope) > 0 && ioDEMs.count(dem_SlopeHeading) > 0)
        return;
    if (ioDEMs.count(dem_Elevation) == 0)
        return;

    DEMGeo& elev = ioDEMs[dem_Elevation];
    DEMGeo& slope = ioDEMs[dem_Slope];
    DEMGeo& slopeHeading = ioDEMs[dem_SlopeHeading];
    DEMGeo& relativeElev = ioDEMs[dem_RelativeElevation];
    DEMGeo& elevationRange = ioDEMs[dem_ElevationRange];

    int y, x, x0, x1;
    float e0, e1;

    // This fills in missing datapoints with a simple, fast, scanline fill.
    // this is needed to clean up raw SRTM data.
    for (y = 0; y < elev.mHeight; ++y)
    {
        x0 = 0;
        while (x0 < elev.mWidth)
        {
            while (x0 < elev.mWidth && elev(x0, y) != DEM_NO_DATA)
                ++x0;
            x1 = x0;
            while (x1 < elev.mWidth && elev(x1, y) == DEM_NO_DATA)
                ++x1;

            if (x0 < 0 && x1 >= elev.mWidth)
                printf("ERROR: MISSING SCANLINED %d from dem.\n", y);
            else if (x0 == 0)
            {
                e1 = elev(x1, y);
                for (x = x0; x < x1; ++x)
                    elev(x, y) = e1;
            }
            else if (x1 >= elev.mWidth)
            {
                e0 = elev(x0 - 1, y);
                for (x = x0; x < x1; ++x)
                    elev(x, y) = e0;
            }
            else
            {
                e0 = elev(x0 - 1, y);
                e1 = elev(x1, y);
                for (x = x0; x < x1; ++x)
                {
                    float rat = ((float)x - x0 + 1) / ((float)(x1 - x0 + 1));
                    elev(x, y) = e0 + rat * (e1 - e0);
                }
            }

            x0 = x1;
        }
    }

    DEMGeo elev_not_insane(elev);
    while (elev_not_insane.mWidth > 1201 || elev_not_insane.mHeight > 1201)
        elev_not_insane.derez(2);

    DEMGeo elev2(elev);
    while (elev2.mWidth > 1200 && elev2.mHeight > 1200)
    {
        elev2.derez(2);
    }

    slope.resize(elev_not_insane.mWidth, elev_not_insane.mHeight);
    slopeHeading.resize(elev_not_insane.mWidth, elev_not_insane.mHeight);
    relativeElev.resize(elev2.mWidth, elev2.mHeight);
    elevationRange.resize(elev2.mWidth, elev2.mHeight);
    elevationRange.mNorth = relativeElev.mNorth = slope.mNorth = slopeHeading.mNorth = elev.mNorth;
    elevationRange.mSouth = relativeElev.mSouth = slope.mSouth = slopeHeading.mSouth = elev.mSouth;
    elevationRange.mEast = relativeElev.mEast = slope.mEast = slopeHeading.mEast = elev.mEast;
    elevationRange.mWest = relativeElev.mWest = slope.mWest = slopeHeading.mWest = elev.mWest;

    elev_not_insane.calc_slope(slope, slopeHeading, inProg);

    {
        DEMGeo mins, maxs;
        DEMGeo_ReduceMinMaxN(elev2, mins, maxs, 8);

        for (y = 0; y < elev2.mHeight; ++y)
            for (x = 0; x < elev2.mWidth; ++x)
            {
                e0 = mins.value_linear(elev2.x_to_lon(x), elev2.y_to_lat(y));
                e1 = maxs.value_linear(elev2.x_to_lon(x), elev2.y_to_lat(y));
                elevationRange(x, y) = e1 - e0;

                if (e0 == e1)
                    relativeElev(x, y) = 0.0;
                else
                    relativeElev(x, y) = min(1.0f, max(0.0f, (elev2(x, y) - e0) / (e1 - e0)));
            }
        if (inProg)
            inProg(1, 2, "Calculating local min/max", 1.0);
    }

#if 0
	{
		vector<DEMGeo>		minCache, maxCache;
		int levels = log2((double) elev.mWidth) / log2((double) 2.0);
		if (levels > 4) levels = 4;
		DEMGeo_BuildMinMax(elev, minCache, maxCache, levels);


		for (y = 0; y < elev.mHeight; ++y)
		for (x = 0; x < elev.mWidth ; ++x)
		{
			if (x == 0 && inProg)
				inProg(1, 2, "Calculating local min/max", (float) y / (float) elev.mHeight);
			elevationRange(x,y) = DEMGeo_LocalMinMaxWithCache(elev, minCache, maxCache,
				x - gDemPrefs.local_range, y - gDemPrefs.local_range, x + gDemPrefs.local_range, y + gDemPrefs.local_range,
				e0, e1);

			if (e0 == e1)
				relativeElev(x,y) = 0.0;
			else
				relativeElev(x,y) = min(1.0, max(0.0, (elev(x,y) - e0) / (e1 - e0)));
		}
		if (inProg) inProg(1, 2, "Calculating local min/max", 1.0);
	}
#endif
}

#pragma mark -

static void FFTSplit(const DEMGeo& inSrc, DEMGeo& equiv, DEMGeo& reduc, int n)
{
    reduc.copy_geo_from(inSrc);
    equiv.copy_geo_from(inSrc);
    reduc.mPost = inSrc.mPost;
    equiv.mPost = inSrc.mPost;
    equiv.resize(inSrc.mWidth, inSrc.mHeight);
    reduc.resize(inSrc.mWidth, inSrc.mHeight);

    int x, y;
    float et, e;

    DEMGeo temp(inSrc);
    temp.derez(n);
    for (y = 0; y < equiv.mHeight; ++y)
        for (x = 0; x < equiv.mWidth; ++x)
        {
            et = temp.value_linear(inSrc.x_to_lon(x), inSrc.y_to_lat(y));
            if (et != DEM_NO_DATA)
            {
                reduc(x, y) = et;
                e = inSrc(x, y);
                if (e != DEM_NO_DATA)
                    equiv(x, y) = e - et;
                else
                    equiv(x, y) = DEM_NO_DATA;
            }
            else
            {
                reduc(x, y) = DEM_NO_DATA;
                equiv(x, y) = DEM_NO_DATA;
            }
        }
}

void DEMMakeFFT(const DEMGeo& inDEM, vector<DEMGeo>& outFFT)
{
    DEMGeo equiv, reduc;
    DEMGeo cur(inDEM);

    int n = 2;

    while (n < inDEM.mWidth && n < inDEM.mHeight)
    {
        FFTSplit(cur, equiv, reduc, n);
        outFFT.push_back(DEMGeo());
        equiv.swap(outFFT.back());
        cur.swap(reduc);
        n *= 2;
    }
    outFFT.push_back(DEMGeo());
    cur.swap(outFFT.back());
}

void FFTMakeDEM(const vector<DEMGeo>& inFFT, DEMGeo& outDEM)
{
    for (int y = 0; y < outDEM.mHeight; ++y)
        for (int x = 0; x < outDEM.mWidth; ++x)
        {
            float e = DEM_NO_DATA;
            for (int n = 0; n < inFFT.size(); ++n)
            {
                float ex = inFFT[n](x, y);
                e = ADD_NODATA(e, ex);
            }

            outDEM(x, y) = e;
        }
}

int DEMMakeHistogram(const DEMGeo& inDEM, map<float, int>& histo, int x1, int y1, int x2, int y2)
{
    int ctr = 0, x, y;
    histo.clear();
    for (y = y1; y < y2; ++y)
        for (x = x1; x < x2; ++x)
        {
            float h = inDEM.get(x, y);
            if (h != DEM_NO_DATA)
            {
                histo[h]++;
                ctr++;
            }
        }
    return ctr;
}

float HistogramGetPercentile(const map<float, int>& histo, int total_samples, float percentile)
{
    int ctr = 0;
    for (map<float, int>::const_iterator iter = histo.begin(); iter != histo.end(); ++iter)
    {
        ctr += iter->second;
        if ((float)ctr / (float)total_samples >= percentile)
            return iter->first;
    }
    return DEM_NO_DATA;
}

void DEMMakeDifferential(const DEMGeo& src, DEMGeo& dst)
{
    dst.resize(src.mWidth, src.mHeight);
    dst.copy_geo_from(src);
    for (int y = 0; y < src.mHeight; ++y)
        for (int x = 0; x < src.mWidth; ++x)
        {
            int e = src.get(x, y);
            if (e != DEM_NO_DATA)
            {
                float en[8];
                en[0] = src.get(x - 1, y - 1);
                en[1] = src.get(x - 1, y);
                en[2] = src.get(x - 1, y + 1);
                en[3] = src.get(x, y + 1);
                en[4] = src.get(x + 1, y + 1);
                en[5] = src.get(x + 1, y);
                en[6] = src.get(x + 1, y - 1);
                en[7] = src.get(x, y - 1);
                float dif = 0;
                for (int k = 0; k < 8; ++k)
                    if (en[k] != DEM_NO_DATA)
                        dif = max(dif, fabsf(en[k] - e));
                dst(x, y) = dif;
            }
            else
                dst(x, y) = 0.0;
        }
}

void MakeTiles(const DEMGeo& inDEM, std::list<DEMGeo>& outTiles)
{
    for (int y = inDEM.mSouth; y < inDEM.mNorth; ++y)
        for (int x = inDEM.mWest; x < inDEM.mEast; ++x)
        {
            int x1 = inDEM.lon_to_x(x);
            int x2 = inDEM.lon_to_x(x + 1);
            int y1 = inDEM.lat_to_y(y);
            int y2 = inDEM.lat_to_y(y + 1);

            bool has_data = false;
            for (int yy = y1; yy <= y2; ++yy)
                for (int xx = x1; xx <= x2; ++xx)
                    if (inDEM.get(xx, yy) != DEM_NO_DATA)
                    {
                        has_data = true;
                        break;
                    }

            if (has_data)
            {
                outTiles.push_back(DEMGeo());
                inDEM.subset(outTiles.back(), x1, y1, x2, y2);
            }
        }
}

#pragma mark -

void DifferenceDEM(const DEMGeo& bottom, const DEMGeo& top, DEMGeo& diff)
{
    DebugAssert(bottom.mWidth == top.mWidth);
    DebugAssert(bottom.mHeight == top.mHeight);
    diff.resize(bottom.mWidth, top.mWidth);
    diff.copy_geo_from(bottom);
    diff.mPost = bottom.mPost;

    for (int y = 0; y < bottom.mHeight; ++y)
        for (int x = 0; x < bottom.mWidth; ++x)
        {
            float eb = bottom.get(x, y);
            float et = top.get(x, y);
            if (eb == DEM_NO_DATA || et == DEM_NO_DATA)
                diff(x, y) = 0;
            else
                diff(x, y) = et - eb;
        }
}

static void make_gaussian_kernel(float k[], int width, double sigma)
{
    for (int w = -width; w <= width; ++w)
    {
        double x = w;
        double f = (1.0 / sqrt(2.0 * M_PI * sigma * sigma)) * exp(-(x * x) / (2.0 * sigma * sigma));
        *k++ = f;
    }
}

static void normalize_kernel(float k[], int w)
{
    int s = w * 2 + 1;
    float sum = 0.0f;
    for (int i = 0; i < s; ++i)
        sum += k[i];
    if (sum != 0.0f)
    {
        sum = 1.0f / sum;
        for (int i = 0; i < s; ++i)
            k[i] *= sum;
    }
}

static float sample_kernel_h(const DEMGeo& src, int x, int y, float k[], int width)
{
    float s = 0.0f;
    float wt = 0.0f;
    for (int w = -width; w <= width; ++w)
    {
        float e = src.get(x + w, y);
        if (e != DEM_NO_DATA)
        {
            wt += *k;
            s += e * *k;
        }
        ++k;
    }
    if (wt == 0.0f)
        return DEM_NO_DATA;
    return s / wt;
}

static float sample_kernel_v(const DEMGeo& src, int x, int y, float k[], int width)
{
    float s = 0.0f;
    float wt = 0.0f;
    for (int w = -width; w <= width; ++w)
    {
        float e = src.get(x, y + w);
        if (e != DEM_NO_DATA)
        {
            wt += *k;
            s += e * *k;
        }
        ++k;
    }
    if (wt == 0.0f)
        return DEM_NO_DATA;
    return s / wt;
}

static void copy_kernel_h(const DEMGeo& src, DEMGeo& dst, float k[], int width)
{
    for (int y = 0; y < src.mHeight; ++y)
        for (int x = 0; x < src.mWidth; ++x)
            dst(x, y) = sample_kernel_h(src, x, y, k, width);
}

static void copy_kernel_v(const DEMGeo& src, DEMGeo& dst, float k[], int width)
{
    for (int y = 0; y < src.mHeight; ++y)
        for (int x = 0; x < src.mWidth; ++x)
            dst(x, y) = sample_kernel_v(src, x, y, k, width);
}

void GaussianBlurDEM(DEMGeo& dem, float sigma)
{
    // Technically the gaussian filter NEVER drops to zero...in practice, it's too expensive to run a filter the size of
    // the DEM. (Note this would _not_ be true if we used an FFT, but..whatever.)  So...pick a filter size that captures
    // 3 sigmas...error is less than 0.3%.

#define SIGMAS_NEEDED 3.0f

    int width = ceilf(sigma * SIGMAS_NEEDED);

    DEMGeo temp(dem.mWidth, dem.mHeight);
    vector<float> k(width * 2 + 1);
    make_gaussian_kernel(&*k.begin(), width, sigma);
    normalize_kernel(&*k.begin(), width);
    copy_kernel_v(dem, temp, &*k.begin(), width);
    copy_kernel_h(temp, dem, &*k.begin(), width);
}

// Line integral of the DEM over the points x1,y1 to x2,y2.  Over-sample by over_sample_ratio (should
// usually be higher than 1.4.
float IntegLine(const DEMGeo& dem, double x1, double y1, double x2, double y2, int over_sample_ratio)
{
    double dx = x2 - x1;
    double dy = y2 - y1;

    double len = sqrt(dx * dx + dy * dy);
    int samples = intmax2(1, (round(len * (double)over_sample_ratio)));
    float t = 0.0;
    for (int s = 0; s < samples; ++s)
    {
        double x = double_interp(0, x1, samples - 1, x2, s);
        double y = double_interp(0, y1, samples - 1, y2, s);
        t += dem.get(round(x), round(y));
    }
    return t * len / ((double)samples);
}

#pragma mark -

struct sort_pixel_by_height
{
    sort_pixel_by_height(const DEMGeo& d) : d_(d)
    {
    }
    const DEMGeo& d_;
    bool operator()(const DEMGeo::address lhs, const DEMGeo::address rhs) const
    {
        return d_[lhs] < d_[rhs];
    }
};

// This code is directly based on "Watersheds in Digital Spaces: An Efficient Algorithm Based on Immersion Simulations"
// by Luc Vincent and Pierre Soille from their 1991 paper.
void Watershed(DEMGeo& input, DEMGeo& output, vector<DEMGeo::address>* out_watersheds)
{
#define MASK -2.0f
#define WSHED DEM_NO_DATA
#define INIT -1.0f
#define FICT -1

    output.clear_from(input, INIT);

    float current_label = -1.0f;
    float current_dist;

    DEMGeo dist;
    dist.clear_from(input, 0.0f);

    address_fifo fifo(input.mWidth * input.mHeight + 2);

    vector<DEMGeo::address> all_pixels;
    all_pixels.reserve(input.mWidth * input.mHeight);
    for (DEMGeo::address i = input.address_begin(); i != input.address_end(); ++i)
        all_pixels.push_back(i);

    sort(all_pixels.begin(), all_pixels.end(), sort_pixel_by_height(input));

    DEMGeo::neighbor_iterator<4> n;
    vector<DEMGeo::address>::iterator hi = all_pixels.begin(), p;
    while (hi != all_pixels.end())
    {
        // printf("Processing altitude %f\n", input[*hi]);
        //  Part 1
        p = hi;
        do
        {
            output[*p] = MASK;
            for (n = output.neighbor_begin<4>(*p); n != output.neighbor_end<4>(*p); ++n)
            {
                if (output[*n] == WSHED || output[*n] >= 0)
                {
                    dist[*p] = 1.0f;
                    fifo.push(*p);
                }
            }

            ++p;
        } while (p != all_pixels.end() && input[*p] == input[*hi]);

        // Part 2
        fifo.push(FICT);
        current_dist = 1.0f;
        while (1)
        {
            // printf("Spreading batch of %d pixels.\n", fifo.size());
            DEMGeo::address p = fifo.pop();
            if (p == FICT)
            {
                if (fifo.empty())
                    break;
                else
                {
                    fifo.push(FICT);
                    ++current_dist;
                    p = fifo.pop();
                    DebugAssert(p != FICT);
                }
            }

            for (n = input.neighbor_begin<4>(p); n != input.neighbor_end<4>(p); ++n)
            {
                if (dist[*n] < current_dist && (output[*n] == WSHED || output[*n] >= 0))
                {
                    if (output[*n] >= 0)
                    {
                        if (output[p] == MASK || output[p] == WSHED)
                            output[p] = output[*n];
                        else if (output[p] != output[*n])
                            output[p] = WSHED;
                    }
                    else if (output[p] == MASK)
                        output[p] = WSHED;
                }
                else if (output[*n] == MASK && dist[*n] == 0.0f)
                {
                    dist[*n] = current_dist + 1.0f;
                    fifo.push(*n);
                }
            }
        }

        // Part 3
        p = hi;
        do
        {
            dist[*p] = 0.0f;

            if (output[*p] == MASK)
            {
                current_label++;
                // printf("Adding watershed: %f.\n", current_label);
                output[*p] = current_label;
                if (out_watersheds)
                {
                    out_watersheds->push_back(*p);
                    DebugAssert(out_watersheds->size() == current_label + 1);
                    DebugAssert((*out_watersheds)[current_label] == *p);
                    DebugAssert(output[*p] == current_label);
                }
                fifo.push(*p);
                while (!fifo.empty())
                {
                    DEMGeo::address pp = fifo.pop();
                    for (n = input.neighbor_begin<4>(pp); n != input.neighbor_end<4>(pp); ++n)
                        if (output[*n] == MASK)
                        {
                            fifo.push(*n);
                            output[*n] = current_label;
                        }
                }
            }

            ++p;

        } while (p != all_pixels.end() && input[*p] == input[*hi]);

        // printf("Finished %f, %d pixels.\n", input[*hi], p - hi);
        hi = p;
    }

    // Part 4 - made up by me: go "fix" all of the watershed pixels.  The paper eludes that 'thick' watersheds
    // need handling - in our case, we do a breadth-first search using the FIFO to spill any arbitrary watershed into
    // neighbors.  Good enough for our purposes I think.
    for (DEMGeo::address p = input.address_begin(); p != input.address_end(); ++p)
        if (output[p] == WSHED)
            fifo.push(p);

    while (!fifo.empty())
    {
        DEMGeo::address p = fifo.pop();

        if (output[p] != WSHED)
            continue;

        float l = WSHED;
        for (n = input.neighbor_begin<4>(p); n != input.neighbor_end<4>(p); ++n)
            if (output[*n] != WSHED)
            {
                l = output[*n];
                break;
            }
        if (l != WSHED)
        {
            output[p] = l;
            for (n = input.neighbor_begin<4>(p); n != input.neighbor_end<4>(p); ++n)
                if (output[*n] == WSHED)
                    fifo.push(*n);
        }
    }
}

void VerifySheds(const DEMGeo& ws, vector<DEMGeo::address>& seeds)
{
    std::set<float> found;
    for (DEMGeo::const_iterator i = ws.begin(); i != ws.end(); ++i)
        found.insert(*i);

    for (int id = 0; id < seeds.size(); ++id)
    {
        DebugAssert(found.count(id) > 0);
        DebugAssert(ws[seeds[id]] == id);
    }
}

void NeighborHisto(const DEMGeo& input, DEMGeo& output, int semi)
{
    output.clear_from(input);

    for (int y = 0; y < input.mHeight; ++y)
        for (int x = 0; x < input.mWidth; ++x)
        {
            float v = input.get(x, y);
            int c = 0;
            for (int dy = y - semi; dy <= y + semi; ++dy)
                for (int dx = x - semi; dx <= x + semi; ++dx)
                    if (input.get(dx, dy) != v)
                        ++c;

            output(x, y) = c;
        }
}

void FindWatersheds(DEMGeo& ws, vector<DEMGeo::address>& out_sheds)
{
    for (DEMGeo::address a = ws.address_begin(); a != ws.address_end(); ++a)
    {
        int ws_id = ws[a];
        if (ws_id >= out_sheds.size())
        {
            out_sheds.resize(ws_id + 1, DEM_NO_DATA);
        }
        out_sheds[ws_id] = a;
    }
}

static void flood_fill_shed(DEMGeo& ws, DEMGeo::address seed, float old_id, float new_id, int shed_size)
{
    DebugAssert(old_id != new_id);
    DebugAssert(ws.valid(seed));
    address_fifo fifo(shed_size);
    fifo.push(seed);
#if DEV
    int changed = 0;
#endif
    while (!fifo.empty())
    {
        DEMGeo::address p = fifo.pop();
        DebugAssert(ws[p] == old_id || ws[p] == new_id);
        if (ws[p] == old_id)
        {
#if DEV
            ++changed;
            DebugAssert(changed <= shed_size);
#endif
            ws[p] = new_id;
            for (DEMGeo::neighbor_iterator<4> n = ws.neighbor_begin<4>(p); n != ws.neighbor_end<4>(p); ++n)
                if (ws[*n] == old_id)
                    fifo.push(*n);
        }
    }
    DebugAssert(shed_size == changed);
}

static float find_best_neighbor(DEMGeo& ws, DEMGeo::address seed, int old_id, int new_id, int max_size)
{
    DebugAssert(old_id != new_id);

    DebugAssert(max_size > 0);
    DebugAssert(ws.valid(seed));
    DebugAssert(ws[seed] == old_id);
    map<float, int> neighbor_count;
    address_fifo fifo(max_size);
    fifo.push(seed);
#if DEV
    int filled = 0;
#endif
    while (!fifo.empty())
    {
        DEMGeo::address p = fifo.pop();
        DebugAssert(ws[p] == old_id || ws[p] == new_id);
        if (ws[p] == old_id)
        {
            ws[p] = new_id;
#if DEV
            ++filled;
            DebugAssert(filled <= max_size);
#endif
            for (DEMGeo::neighbor_iterator<4> n = ws.neighbor_begin<4>(p); n != ws.neighbor_end<4>(p); ++n)
                if (ws[*n] == old_id)
                {
                    fifo.push(*n);
                }
                else if (ws[*n] != new_id)
                {
                    float nid = ws[*n];
                    neighbor_count[nid]++;
                }
        }
    }
    DebugAssert(filled == max_size);
    if (neighbor_count.empty())
        return -1.0;
    map<float, int>::iterator n, best;
    n = best = neighbor_count.begin();
    ++n;
    while (n != neighbor_count.end())
    {
        if (n->second > best->second)
            best = n;
        ++n;
    }
    return best->first;
}

void MergeMMU(DEMGeo& ws, vector<DEMGeo::address>& io_sheds, int min_mmu_size)
{
    vector<int> ws_size_table;
    ws_size_table.resize(io_sheds.size(), 0);
    DEMGeo::address a;
    for (a = ws.address_begin(); a != ws.address_end(); ++a)
        ws_size_table[ws[a]]++;

    multimap<int, int> ws_size_q;

    int ws_id;
    int q_size;

    for (ws_id = 0; ws_id < ws_size_table.size(); ++ws_id)
        if (ws_size_table[ws_id] < min_mmu_size && ws_size_table[ws_id] > 0)
            ws_size_q.insert(multimap<int, int>::value_type(ws_size_table[ws_id], ws_id));

    while (!ws_size_q.empty())
    {
        ws_id = ws_size_q.begin()->second;
        q_size = ws_size_q.begin()->first;

        ws_size_q.erase(ws_size_q.begin());

        if (q_size != ws_size_table[ws_id])
        {
            if (ws_size_table[ws_id] < min_mmu_size && ws_size_table[ws_id] > 0)
                ws_size_q.insert(multimap<int, int>::value_type(ws_size_table[ws_id], ws_id));
        }
        else
        {
            int n_id = find_best_neighbor(ws, io_sheds[ws_id], ws_id, -1.0, ws_size_table[ws_id]);
            DebugAssert(n_id >= 0);
            int old_shed_size = ws_size_table[ws_id];
            ws_size_table[n_id] += old_shed_size;
            ws_size_table[ws_id] = 0;
            flood_fill_shed(ws, io_sheds[ws_id], -1.0, n_id, old_shed_size);
            io_sheds[ws_id] = -1;
        }
    }
}

static void SetWatershedToDominant(DEMGeo& underlying, DEMGeo& ws, DEMGeo::address seed, address_fifo& fifo)
{
    DebugAssert(seed != -1);
    DebugAssert(fifo.empty());
    DEMGeo::address p;
    DEMGeo::neighbor_iterator<4> n;
    map<float, int> histo;

    fifo.push(seed);
    float my_id = ws[seed];
    DebugAssert(my_id != -1);

    while (!fifo.empty())
    {
        p = fifo.pop();
        if (ws[p] == my_id)
        {
            ws[p] = -1;
            histo[underlying[p]]++;
            for (n = ws.neighbor_begin<4>(p); n != ws.neighbor_end<4>(p); ++n)
                if (ws[*n] == my_id)
                    fifo.push(*n);
        }
    }

    DebugAssert(!histo.empty());
    map<float, int>::iterator best, i;
    best = i = histo.begin();
    ++n;
    while (i != histo.end())
    {
        if (i->second > best->second)
            best = i;
        ++i;
    }

    float best_lu = best->first;

    fifo.push(seed);
    while (!fifo.empty())
    {
        p = fifo.pop();
        if (ws[p] == -1)
        {
            ws[p] = my_id;
            underlying[p] = best_lu;
            for (n = ws.neighbor_begin<4>(p); n != ws.neighbor_end<4>(p); ++n)
                if (ws[*n] == -1)
                    fifo.push(*n);
        }
    }
    DebugAssert(fifo.empty());
}

void SetWatershedsToDominant(DEMGeo& underlying, DEMGeo& ws, const vector<DEMGeo::address>& io_sheds)
{
    address_fifo fifo(underlying.mWidth * underlying.mHeight);
    for (vector<DEMGeo::address>::const_iterator a = io_sheds.begin(); a != io_sheds.end(); ++a)
        if (*a != -1)
            SetWatershedToDominant(underlying, ws, *a, fifo);
}
