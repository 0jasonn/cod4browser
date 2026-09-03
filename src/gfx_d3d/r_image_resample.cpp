#include "r_image_resample.h"
#include <qcommon/qcommon_math.h>
#include <universal/assertive.h>

static void R_UpsamplePixelData(
    int oldSize,
    int newSize,
    int stride,
    int bytesPerPixel,
    uint8_t *src,
    uint8_t *dst)
{
    int backwardWeight; // [esp+90h] [ebp-18h]
    int nextSample; // [esp+94h] [ebp-14h]
    float colorScale; // [esp+98h] [ebp-10h]
    uint8_t *currSrc; // [esp+9Ch] [ebp-Ch]
    int column; // [esp+A0h] [ebp-8h]
    int forwardWeight; // [esp+A4h] [ebp-4h]
    uint8_t *dsta; // [esp+C4h] [ebp+1Ch]

    iassert( newSize > oldSize );
    nextSample = bytesPerPixel * stride;
    currSrc = &src[bytesPerPixel * stride * (oldSize - 1)];
    dsta = &dst[bytesPerPixel * stride * (newSize - 1)];
    currSrc[bytesPerPixel * stride] = *currSrc;
    currSrc[nextSample + 1] = currSrc[1];
    currSrc[nextSample + 2] = currSrc[2];
    forwardWeight = newSize - oldSize;
    backwardWeight = oldSize + newSize;
    colorScale = 0.5 / (double)newSize;
    for (column = newSize - 1; column >= 0; --column)
    {
        if (currSrc < src)
        {
            dsta[0] = SnapFloatToInt(colorScale * (float)((forwardWeight + backwardWeight) * src[0]));
            dsta[1] = SnapFloatToInt(colorScale * (float)((forwardWeight + backwardWeight) * src[1]));
            dsta[2] = SnapFloatToInt(colorScale * (float)((forwardWeight + backwardWeight) * src[2]));
        }
        else
        {
            dsta[0] = SnapFloatToInt(colorScale * (float)(forwardWeight * currSrc[nextSample] + backwardWeight * currSrc[0]));
            dsta[1] = SnapFloatToInt(colorScale * (float)(forwardWeight * currSrc[nextSample + 1] + backwardWeight * currSrc[1]));
            dsta[2] = SnapFloatToInt(colorScale * (float)(forwardWeight * currSrc[nextSample + 2] + backwardWeight * currSrc[2]));
        }
        dsta -= nextSample;
        backwardWeight += 2 * oldSize;
        forwardWeight -= 2 * oldSize;
        if (forwardWeight < 0)
        {
            backwardWeight -= 2 * newSize;
            forwardWeight += 2 * newSize;
            currSrc -= nextSample;
        }
    }
}

static void R_DownsamplePixelData(
    int oldSize,
    int newSize,
    int stride,
    int bytesPerPixel,
    uint8_t *src,
    uint8_t *dst)
{
    int nextSample; // [esp+3Ch] [ebp-1Ch]
    float colorScale; // [esp+40h] [ebp-18h]
    int residual; // [esp+44h] [ebp-14h]
    int column; // [esp+48h] [ebp-10h]
    int color; // [esp+4Ch] [ebp-Ch]
    int color_4; // [esp+50h] [ebp-8h]
    int color_4a; // [esp+50h] [ebp-8h]
    int color_8; // [esp+54h] [ebp-4h]
    int color_8a; // [esp+54h] [ebp-4h]

    iassert( newSize < oldSize );
    colorScale = 1.0 / (double)oldSize;
    nextSample = bytesPerPixel * stride;
    residual = newSize;
    for (column = 0; column < newSize; ++column)
    {
        iassert( residual > 0 );
        color = residual * *src;
        color_4 = residual * src[1];
        color_8 = residual * src[2];
        src += nextSample;
        while (newSize + residual - oldSize <= 0)
        {
            color += newSize * *src;
            color_4 += newSize * src[1];
            color_8 += newSize * src[2];
            residual += newSize;
            src += nextSample;
        }
        residual = newSize + residual - oldSize;
        // At an exact sample boundary (including the last pixel), src may
        // be one past the image. A zero weight must not dereference it.
        const int weight = newSize - residual;
        color_4a = color_4 + (weight ? src[1] * weight : 0);
        color_8a = color_8 + (weight ? src[2] * weight : 0);
        dst[0] = SnapFloatToInt(colorScale * (float)(color + (weight ? *src * weight : 0)));
        dst[1] = SnapFloatToInt(colorScale * (float)color_4a);
        dst[2] = SnapFloatToInt(colorScale * (float)color_8a);
        dst += nextSample;
    }
}

void R_ResampleImage(
    int oldWidth,
    int oldHeight,
    int newWidth,
    int newHeight,
    int bytesPerPixel,
    uint8_t *data)
{
    uint8_t *src; // [esp+0h] [ebp-10h]
    uint8_t *srca; // [esp+0h] [ebp-10h]
    uint8_t *srcb; // [esp+0h] [ebp-10h]
    uint8_t *srcc; // [esp+0h] [ebp-10h]
    int row; // [esp+4h] [ebp-Ch]
    int rowa; // [esp+4h] [ebp-Ch]
    uint8_t *dst; // [esp+8h] [ebp-8h]
    uint8_t *dsta; // [esp+8h] [ebp-8h]
    uint8_t *dstb; // [esp+8h] [ebp-8h]
    uint8_t *dstc; // [esp+8h] [ebp-8h]
    int col; // [esp+Ch] [ebp-4h]
    int cola; // [esp+Ch] [ebp-4h]

    if (oldWidth <= newWidth)
    {
        if (oldWidth < newWidth)
        {
            srca = &data[bytesPerPixel * oldWidth * (oldHeight - 1)];
            dsta = &data[bytesPerPixel * newWidth * (oldHeight - 1)];
            for (rowa = oldHeight - 1; rowa >= 0; --rowa)
            {
                R_UpsamplePixelData(oldWidth, newWidth, 1, bytesPerPixel, srca, dsta);
                srca -= bytesPerPixel * oldWidth;
                dsta -= bytesPerPixel * newWidth;
            }
        }
    }
    else
    {
        src = data;
        dst = data;
        for (row = 0; row < oldHeight; ++row)
        {
            R_DownsamplePixelData(oldWidth, newWidth, 1, bytesPerPixel, src, dst);
            src += bytesPerPixel * oldWidth;
            dst += bytesPerPixel * newWidth;
        }
    }
    if (oldHeight <= newHeight)
    {
        if (oldHeight < newHeight)
        {
            srcc = &data[bytesPerPixel * (newWidth - 1)];
            dstc = srcc;
            for (cola = newWidth - 1; cola >= 0; --cola)
            {
                R_UpsamplePixelData(oldHeight, newHeight, newWidth, bytesPerPixel, srcc, dstc);
                srcc -= bytesPerPixel;
                dstc -= bytesPerPixel;
            }
        }
    }
    else
    {
        srcb = data;
        dstb = data;
        for (col = 0; col < newWidth; ++col)
        {
            R_DownsamplePixelData(oldHeight, newHeight, newWidth, bytesPerPixel, srcb, dstb);
            srcb += bytesPerPixel;
            dstb += bytesPerPixel;
        }
    }
}

