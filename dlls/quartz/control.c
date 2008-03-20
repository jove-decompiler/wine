/*
 * Filter Seeking and Control Interfaces
 *
 * Copyright 2003 Robert Shearman
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA
 */
/* FIXME: critical sections */

#include "quartz_private.h"
#include "control_private.h"

#include "uuids.h"
#include "wine/debug.h"

#include <assert.h>

WINE_DEFAULT_DEBUG_CHANNEL(quartz);

HRESULT MediaSeekingImpl_Init(IBaseFilter *pUserData, CHANGEPROC fnChangeStop, CHANGEPROC fnChangeStart, CHANGEPROC fnChangeRate, MediaSeekingImpl * pSeeking, PCRITICAL_SECTION crit_sect)
{
    assert(fnChangeStop && fnChangeStart && fnChangeRate);

    pSeeking->refCount = 1;
    pSeeking->pUserData = pUserData;
    pSeeking->fnChangeRate = fnChangeRate;
    pSeeking->fnChangeStop = fnChangeStop;
    pSeeking->fnChangeStart = fnChangeStart;
    pSeeking->dwCapabilities = AM_SEEKING_CanSeekForwards |
        AM_SEEKING_CanSeekBackwards |
        AM_SEEKING_CanSeekAbsolute |
        AM_SEEKING_CanGetStopPos |
        AM_SEEKING_CanGetDuration;
    pSeeking->llStart = 0;
    pSeeking->llStop = ((ULONGLONG)0x80000000) << 32;
    pSeeking->llDuration = pSeeking->llStop - pSeeking->llStart;
    pSeeking->dRate = 1.0;
    pSeeking->timeformat = TIME_FORMAT_MEDIA_TIME;
    pSeeking->crst = crit_sect;

    return S_OK;
}


HRESULT WINAPI MediaSeekingImpl_GetCapabilities(IMediaSeeking * iface, DWORD * pCapabilities)
{
    MediaSeekingImpl *This = (MediaSeekingImpl *)iface;

    TRACE("(%p)\n", pCapabilities);

    *pCapabilities = This->dwCapabilities;

    return S_OK;
}

HRESULT WINAPI MediaSeekingImpl_CheckCapabilities(IMediaSeeking * iface, DWORD * pCapabilities)
{
    MediaSeekingImpl *This = (MediaSeekingImpl *)iface;
    HRESULT hr;
    DWORD dwCommonCaps;

    TRACE("(%p)\n", pCapabilities);

    dwCommonCaps = *pCapabilities & This->dwCapabilities;

    if (!dwCommonCaps)
        hr = E_FAIL;
    else
        hr = (*pCapabilities == dwCommonCaps) ?  S_OK : S_FALSE;
    *pCapabilities = dwCommonCaps;

    return hr;
}

HRESULT WINAPI MediaSeekingImpl_IsFormatSupported(IMediaSeeking * iface, const GUID * pFormat)
{
    TRACE("(%s)\n", qzdebugstr_guid(pFormat));

    return (IsEqualIID(pFormat, &TIME_FORMAT_MEDIA_TIME) ? S_OK : S_FALSE);
}

HRESULT WINAPI MediaSeekingImpl_QueryPreferredFormat(IMediaSeeking * iface, GUID * pFormat)
{
    TRACE("(%s)\n", qzdebugstr_guid(pFormat));

    *pFormat = TIME_FORMAT_MEDIA_TIME;
    return S_OK;
}

HRESULT WINAPI MediaSeekingImpl_GetTimeFormat(IMediaSeeking * iface, GUID * pFormat)
{
    MediaSeekingImpl *This = (MediaSeekingImpl *)iface;
    TRACE("(%s)\n", qzdebugstr_guid(pFormat));

    EnterCriticalSection(This->crst);
    *pFormat = This->timeformat;
    LeaveCriticalSection(This->crst);

    return S_OK;
}

HRESULT WINAPI MediaSeekingImpl_IsUsingTimeFormat(IMediaSeeking * iface, const GUID * pFormat)
{
    MediaSeekingImpl *This = (MediaSeekingImpl *)iface;
    HRESULT hr = S_OK;

    TRACE("(%s)\n", qzdebugstr_guid(pFormat));

    EnterCriticalSection(This->crst);
    if (!IsEqualIID(pFormat, &This->timeformat))
        hr = S_FALSE;
    LeaveCriticalSection(This->crst);

    return hr;
}

HRESULT WINAPI MediaSeekingImpl_GetDuration(IMediaSeeking * iface, LONGLONG * pDuration)
{
    MediaSeekingImpl *This = (MediaSeekingImpl *)iface;

    TRACE("(%p)\n", pDuration);

    EnterCriticalSection(This->crst);
    *pDuration = This->llDuration;
    LeaveCriticalSection(This->crst);

    return S_OK;
}

HRESULT WINAPI MediaSeekingImpl_GetStopPosition(IMediaSeeking * iface, LONGLONG * pStop)
{
    MediaSeekingImpl *This = (MediaSeekingImpl *)iface;

    TRACE("(%p)\n", pStop);

    EnterCriticalSection(This->crst);
    *pStop = This->llStop;
    LeaveCriticalSection(This->crst);

    return S_OK;
}

HRESULT WINAPI MediaSeekingImpl_GetCurrentPosition(IMediaSeeking * iface, LONGLONG * pCurrent)
{
    MediaSeekingImpl *This = (MediaSeekingImpl *)iface;

    TRACE("(%p)\n", pCurrent);

    EnterCriticalSection(This->crst);
    *pCurrent = This->llStart;
    LeaveCriticalSection(This->crst);

    return S_OK;
}

HRESULT WINAPI MediaSeekingImpl_ConvertTimeFormat(IMediaSeeking * iface, LONGLONG * pTarget, const GUID * pTargetFormat, LONGLONG Source, const GUID * pSourceFormat)
{
    if (IsEqualIID(pTargetFormat, &TIME_FORMAT_MEDIA_TIME) && IsEqualIID(pSourceFormat, &TIME_FORMAT_MEDIA_TIME))
    {
        *pTarget = Source;
        return S_OK;
    }
    /* FIXME: clear pTarget? */
    return E_INVALIDARG;
}

static inline LONGLONG Adjust(LONGLONG value, const LONGLONG * pModifier, DWORD dwFlags)
{
    switch (dwFlags & AM_SEEKING_PositioningBitsMask)
    {
    case AM_SEEKING_NoPositioning:
        return value;
    case AM_SEEKING_AbsolutePositioning:
        return *pModifier;
    case AM_SEEKING_RelativePositioning:
    case AM_SEEKING_IncrementalPositioning:
        return value + *pModifier;
    default:
        assert(FALSE);
        return 0;
    }
}

HRESULT WINAPI MediaSeekingImpl_SetPositions(IMediaSeeking * iface, LONGLONG * pCurrent, DWORD dwCurrentFlags, LONGLONG * pStop, DWORD dwStopFlags)
{
    MediaSeekingImpl *This = (MediaSeekingImpl *)iface;
    BOOL bChangeStart = FALSE, bChangeStop = FALSE;
    LONGLONG llNewStart, llNewStop;

    TRACE("(%p, %x, %p, %x)\n", pCurrent, dwCurrentFlags, pStop, dwStopFlags);

    EnterCriticalSection(This->crst);
    llNewStart = Adjust(This->llStart, pCurrent, dwCurrentFlags);
    llNewStop = Adjust(This->llStop, pStop, dwStopFlags);

    if (llNewStart != This->llStart)
        bChangeStart = TRUE;
    if (llNewStop != This->llStop)
        bChangeStop = TRUE;

    This->llStart = llNewStart;
    This->llStop = llNewStop;

    if (dwCurrentFlags & AM_SEEKING_ReturnTime)
        *pCurrent = llNewStart;
    if (dwStopFlags & AM_SEEKING_ReturnTime)
        *pStop = llNewStop;

    if (bChangeStart)
        This->fnChangeStart(This->pUserData);
    if (bChangeStop)
        This->fnChangeStop(This->pUserData);
    LeaveCriticalSection(This->crst);

    return S_OK;
}

HRESULT WINAPI MediaSeekingImpl_GetPositions(IMediaSeeking * iface, LONGLONG * pCurrent, LONGLONG * pStop)
{
    MediaSeekingImpl *This = (MediaSeekingImpl *)iface;

    TRACE("(%p, %p)\n", pCurrent, pStop);

    EnterCriticalSection(This->crst);
    *pCurrent = This->llStart;
    *pStop = This->llStop;
    LeaveCriticalSection(This->crst);

    return S_OK;
}

HRESULT WINAPI MediaSeekingImpl_GetAvailable(IMediaSeeking * iface, LONGLONG * pEarliest, LONGLONG * pLatest)
{
    MediaSeekingImpl *This = (MediaSeekingImpl *)iface;

    TRACE("(%p, %p)\n", pEarliest, pLatest);

    EnterCriticalSection(This->crst);
    *pEarliest = 0;
    *pLatest = This->llDuration;
    LeaveCriticalSection(This->crst);

    return S_OK;
}

HRESULT WINAPI MediaSeekingImpl_SetRate(IMediaSeeking * iface, double dRate)
{
    MediaSeekingImpl *This = (MediaSeekingImpl *)iface;
    BOOL bChangeRate = (dRate != This->dRate);
    HRESULT hr = S_OK;

    TRACE("(%e)\n", dRate);

    EnterCriticalSection(This->crst);
    This->dRate = dRate;
    if (bChangeRate)
        hr = This->fnChangeRate(This->pUserData);
    LeaveCriticalSection(This->crst);

    return hr;
}

HRESULT WINAPI MediaSeekingImpl_GetRate(IMediaSeeking * iface, double * dRate)
{
    MediaSeekingImpl *This = (MediaSeekingImpl *)iface;

    TRACE("(%p)\n", dRate);

    EnterCriticalSection(This->crst);
    *dRate = This->dRate;
    LeaveCriticalSection(This->crst);

    return S_OK;
}

HRESULT WINAPI MediaSeekingImpl_GetPreroll(IMediaSeeking * iface, LONGLONG * pPreroll)
{
    TRACE("(%p)\n", pPreroll);

    *pPreroll = 0;
    return S_OK;
}
