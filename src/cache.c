/*
    cache.c -- Http request route caching 

    Caching operates as both a handler and an output filter. If acceptable cached content is found, the 
    cacheHandler will serve it instead of the normal handler. If no content is acceptable and caching is enabled
    for the request, the cacheFilter will capture and save the response.

    Copyright (c) All Rights Reserved. See copyright notice at the bottom of the file.
 */

/********************************* Includes ***********************************/

#include    "http.h"

/********************************** Forwards **********************************/

static bool fetchCachedResponse(HttpConn *conn);
static HttpCache *lookupCacheControl(HttpConn *conn);
static char *makeCacheKey(HttpConn *conn);
static void manageHttpCache(HttpCache *cache, int flags);
static int matchCacheFilter(HttpConn *conn, HttpRoute *route, int dir);
static int matchCacheHandler(HttpConn *conn, HttpRoute *route, int dir);
static void outgoingCacheFilterService(HttpQueue *q);
static void processCacheHandler(HttpQueue *q);
static void saveCachedResponse(HttpConn *conn);

/************************************ Code ************************************/

int httpOpenCacheHandler(Http *http)
{
    HttpStage     *handler, *filter;

    /*
        Create the cache handler to serve cached content 
     */
    if ((handler = httpCreateHandler(http, "cacheHandler", HTTP_STAGE_ALL, NULL)) == 0) {
        return MPR_ERR_CANT_CREATE;
    }
    http->cacheHandler = handler;
    handler->match = matchCacheHandler;
    handler->process = processCacheHandler;

    /*
        Create the cache filter to capture and cache response content
     */
    if ((filter = httpCreateHandler(http, "cacheFilter", HTTP_STAGE_ALL, NULL)) == 0) {
        return MPR_ERR_CANT_CREATE;
    }
    http->cacheFilter = filter;
    filter->match = matchCacheFilter;
    filter->outgoingService = outgoingCacheFilterService;
    return 0;
}


/*
    See if there is acceptable cached content to serve
 */
static int matchCacheHandler(HttpConn *conn, HttpRoute *route, int dir)
{
    mprAssert(route->caching);

    if ((conn->tx->cacheControl = lookupCacheControl(conn)) == 0) {
        return HTTP_ROUTE_REJECT;
    }
    if (!fetchCachedResponse(conn)) {
        /* 
            Side-effect: may have setup tx->cacheBuffer to cause the cacheFilter to match and catpure output 
         */
        return HTTP_ROUTE_REJECT;
    }
    /* 
        There is acceptable cached content - so use the cacheHandler 
     */
    return HTTP_ROUTE_OK;
}


static void processCacheHandler(HttpQueue *q) 
{
    HttpConn    *conn;
    HttpTx      *tx;

    conn = q->conn;
    tx = conn->tx;
    if (tx->cachedContent) {
        mprLog(3, "cacheHandler: write cached content for '%s'", conn->rx->uri);
        httpWriteString(q, tx->cachedContent);
    }
    httpFinalize(conn);
}


static int matchCacheFilter(HttpConn *conn, HttpRoute *route, int dir)
{
    if ((dir & HTTP_STAGE_TX) && conn->tx->cacheBuffer) {
        mprLog(3, "cacheFilter: Cache response content for '%s'", conn->rx->uri);
        return HTTP_ROUTE_OK;
    }
    return HTTP_ROUTE_REJECT;
}


static void outgoingCacheFilterService(HttpQueue *q)
{
    HttpPacket  *packet;
    HttpConn    *conn;
    HttpTx      *tx;

    conn = q->conn;
    tx = conn->tx;
    mprAssert(tx->cacheBuffer);

    for (packet = httpGetPacket(q); packet; packet = httpGetPacket(q)) {
        if (!httpWillNextQueueAcceptPacket(q, packet)) {
            httpPutBackPacket(q, packet);
            return;
        }
        if (packet->flags & HTTP_PACKET_DATA) {
            mprPutBlockToBuf(tx->cacheBuffer, mprGetBufStart(packet->content), mprGetBufLength(packet->content));

        } else if (packet->flags & HTTP_PACKET_END) {
            saveCachedResponse(conn);
        }
        httpPutPacketToNext(q, packet);
    }
}


/*
    Find a qualifying cache control entry. Any configured uri,method,extension,type must match.
 */
static HttpCache *lookupCacheControl(HttpConn *conn)
{
    HttpRx      *rx;
    HttpTx      *tx;
    HttpCache   *cache;
    cchar       *mimeType, *ukey;
    int         next;

    rx = conn->rx;
    tx = conn->tx;

    /*
        Find first qualifying cache control entry. Any configured uri,method,extension,type must match.
     */
    for (next = 0; (cache = mprGetNextItem(rx->route->caching, &next)) != 0; ) {
        if (cache->uris) {
            if (cache->flags & HTTP_CACHE_IGNORE_PARAMS) {
                ukey = rx->pathInfo;
            } else {
                ukey = sfmt("%s?%s", rx->pathInfo, httpGetParamsString(conn));
            }
            if (!mprLookupKey(cache->uris, ukey)) {
                continue;
            }
        }
        if (cache->methods && !mprLookupKey(cache->methods, rx->method)) {
            continue;
        }
        if (cache->extensions && !mprLookupKey(cache->extensions, tx->ext)) {
            continue;
        }
        if (cache->types) {
            if ((mimeType = (char*) mprLookupMime(conn->host->mimeTypes, tx->ext)) != 0) {
                if (!mprLookupKey(cache->types, mimeType)) {
                    continue;
                }
            }
        }
        /* All match */
        break;
    }
    return cache;
}


/*
    See if there is acceptable cached content for this request. If so, return true.
    Will setup tx->cacheBuffer as a side-effect if the output should be captured and cached.
 */
static bool fetchCachedResponse(HttpConn *conn)
{
    HttpRx      *rx;
    HttpTx      *tx;
    HttpRoute   *route;
    MprTime     modified, when;
    HttpCache   *cache;
    cchar       *value, *key, *tag;
    int         status, cacheOk, canUseClientCache;

    rx = conn->rx;
    tx = conn->tx;
    route = rx->route;
    cache = tx->cacheControl;
    mprAssert(cache);

    if (cache->flags & HTTP_CACHE_CLIENT) {
        if (!mprLookupKey(tx->headers, "Cache-Control")) {
            if ((value = mprLookupKey(conn->tx->headers, "Cache-Control")) != 0) {
                if (strstr(value, "max-age") == 0) {
                    httpAppendHeader(conn, "Cache-Control", "max-age=%d", cache->lifespan / MPR_TICKS_PER_SEC);
                }
            } else {
                httpAddHeader(conn, "Cache-Control", "max-age=%d", cache->lifespan / MPR_TICKS_PER_SEC);
            }
#if UNUSED && KEEP
            {
                /* Old HTTP/1.0 clients don't understand Cache-Control */
                struct tm   tm;
                mprDecodeUniversalTime(&tm, conn->http->now + (expires * MPR_TICKS_PER_SEC));
                httpAddHeader(conn, "Expires", "%s", mprFormatTime(MPR_HTTP_DATE, &tm));
            }
#endif
        }
        return 0;
    }
    if (!(cache->flags & HTTP_CACHE_MANUAL)) {
        /*
            Transparent caching. Manual caching must manually call httpWriteCached()
         */
        key = makeCacheKey(conn);
        if ((value = httpGetHeader(conn, "Cache-Control")) != 0 && 
                (scontains(value, "max-age=0", -1) == 0 || scontains(value, "no-cache", -1) == 0)) {
            mprLog(3, "Client reload. Cache-control header '%s' rejects use of cached content.", value);

        } else if ((tx->cachedContent = mprReadCache(conn->host->cache, key, &modified, 0)) != 0) {
            /*
                See if a NotModified response can be served. This is much faster than sending the response.
                Observe headers:
                    If-None-Match: "ec18d-54-4d706a63"
                    If-Modified-Since: Fri, 04 Mar 2011 04:28:19 GMT
                Set status to OK when content must be transmitted.
             */
            cacheOk = 1;
            canUseClientCache = 0;
            tag = mprGetMD5(key);
            if ((value = httpGetHeader(conn, "If-None-Match")) != 0) {
                canUseClientCache = 1;
                if (scmp(value, tag) != 0) {
                    cacheOk = 0;
                }
            }
            if (cacheOk && (value = httpGetHeader(conn, "If-Modified-Since")) != 0) {
                canUseClientCache = 1;
                mprParseTime(&when, value, 0, 0);
                if (modified > when) {
                    cacheOk = 0;
                }
            }
            mprLog(3, "cacheHandler: Use cached content for %s, status %d", key, status);
            status = (canUseClientCache && cacheOk) ? HTTP_CODE_NOT_MODIFIED : HTTP_CODE_OK;
            httpSetStatus(conn, status);
            httpSetHeader(conn, "Etag", mprGetMD5(key));
            httpSetHeader(conn, "Last-Modified", mprFormatUniversalTime(MPR_HTTP_DATE, modified));
            return 1;
        }
        mprLog(3, "cacheHandler: No cached content for %s", key);
    }
    /*
        Must run the real handler, so create a capture buffer.
     */
    conn->tx->cacheBuffer = mprCreateBuf(-1, -1);
    return 0;
}


static void saveCachedResponse(HttpConn *conn)
{
    HttpTx      *tx;
    MprBuf      *buf;
    MprTime     modified;

    tx = conn->tx;

    mprAssert(conn->finalized && tx->cacheBuffer);
    buf = tx->cacheBuffer;
    mprAddNullToBuf(buf);
    tx->cacheBuffer = 0;
    /* 
        Truncate modified time to get a 1 sec resolution. This is the resolution for If-Modified headers.  
     */
    modified = mprGetTime() / MPR_TICKS_PER_SEC * MPR_TICKS_PER_SEC;
    mprWriteCache(conn->host->cache, makeCacheKey(conn), mprGetBufStart(buf), modified, tx->cacheControl->lifespan, 0, 0);
}


//  MOB -- test
ssize httpWriteCached(HttpConn *conn)
{
    HttpRoute   *route;
    MprTime     modified;
    cchar       *key, *content;

    //  MOB - debug and verify this
    route = conn->rx->route;
    key = makeCacheKey(conn);
    if ((content = mprReadCache(conn->host->cache, key, &modified, 0)) == 0) {
        mprLog(3, "No cached data for ", key);
        return 0;
    }
    mprLog(5, "Used cached ", key);
    httpSetHeader(conn, "Etag", mprGetMD5(key));
    httpSetHeader(conn, "Last-Modified", mprFormatUniversalTime(MPR_HTTP_DATE, modified));
    conn->tx->cacheBuffer = 0;
    httpWriteString(conn->writeq, content);
    httpFinalize(conn);
    return slen(content);
}


//  MOB -- test
int httpUpdateCache(HttpConn *conn, cchar *data)
{
    HttpCache   *cache;

    if ((cache = lookupCacheControl(conn)) == 0) {
        return MPR_ERR_CANT_FIND;
    }
    mprWriteCache(conn->host->cache, makeCacheKey(conn), data, 0, cache->lifespan, 0, 0);
    return 0;
}


/*
    Add cache configuration to the route. This can be called multiple times.
    Uris, extensions and methods may optionally provide a space or comma separated list of items.
    If URI is NULL or "*", cache all URIs for this route. Otherwise, cache only the given URIs. 
    The URIs may contain an ordered set of request parameters. For example: "/user/show?name=john&posts=true"
    Note: the URI should not include the route prefix (scriptName)
    The extensions should not contain ".". The methods may contain "*" for all methods.
 */
void httpAddCache(HttpRoute *route, cchar *methods, cchar *uris, cchar *extensions, cchar *types, MprTime lifespan, 
        int flags)
{
    HttpCache   *cache;
    char        *cp, *item, *tok;

    cache = 0;
    if (!route->caching) {
        httpAddRouteHandler(route, "cacheHandler", "");
        httpAddRouteFilter(route, "cacheFilter", "", HTTP_STAGE_TX);
        route->caching = mprCreateList(0, 0);

    } else if (flags & HTTP_CACHE_RESET) {
        route->caching = mprCreateList(0, 0);

    } else if (route->parent && route->caching == route->parent->caching) {
        route->caching = mprCloneList(route->parent->caching);
    }
    if ((cache = mprAllocObj(HttpCache, manageHttpCache)) == 0) {
        return;
    }
    if (extensions) {
        cache->extensions = mprCreateHash(0, 0);
        for (item = stok(sclone(extensions), " \t,", &tok); item; item = stok(0, " \t,", &tok)) {
            mprAddKey(cache->extensions, item, cache);
        }
    } else if (types) {
        cache->types = mprCreateHash(0, 0);
        for (item = stok(sclone(types), " \t,", &tok); item; item = stok(0, " \t,", &tok)) {
            mprAddKey(cache->types, item, cache);
        }
    }
    if (methods) {
        cache->methods = mprCreateHash(0, MPR_HASH_CASELESS);
        for (item = stok(sclone(methods), " \t,", &tok); item; item = stok(0, " \t,", &tok)) {
            if (smatch(item, "*")) {
                methods = 0;
            } else {
                mprAddKey(cache->methods, item, cache);
            }
        }
    }
    if (uris) {
        cache->uris = mprCreateHash(0, 0);
        for (item = stok(sclone(uris), " \t,", &tok); item; item = stok(0, " \t,", &tok)) {
            if (flags & HTTP_CACHE_IGNORE_PARAMS) {
                if ((cp = schr(item, '?')) != 0) {
                    mprError("URI has params and ignore parms requested");
                    *cp = '\0';
                }
            } else {
                /*
                    For usability, auto-add ?prefix=ROUTE_NAME
                 */
                if (!scontains(item, sfmt("prefix=%s", route->name), -1)) {
                    if (!schr(item, '?')) {
                        item = sfmt("%s?prefix=%s", item, route->name); 
                    }
                }
            }
            mprAddKey(cache->uris, item, cache);
        }
    }
    cache->lifespan = lifespan;
    cache->flags = flags;
    mprAddItem(route->caching, cache);

    mprLog(3, "Caching route %s for methods %s, URIs %s, extensions %s, types %s, lifespan %d", 
        route->name,
        (methods) ? methods: "*",
        (uris) ? uris: "*",
        (extensions) ? extensions: "*",
        (types) ? types: "*",
        cache->lifespan / MPR_TICKS_PER_SEC);
}


static void manageHttpCache(HttpCache *cache, int flags)
{
    if (flags & MPR_MANAGE_MARK) {
        mprMark(cache->uris);
        mprMark(cache->extensions);
        mprMark(cache->methods);
        mprMark(cache->types);
    }
}


static char *makeCacheKey(HttpConn *conn)
{
    HttpRx      *rx;
    HttpRoute   *route;

    rx = conn->rx;
    route = rx->route;
    if (conn->tx->cacheControl->flags & HTTP_CACHE_IGNORE_PARAMS) {
        return sfmt("http::response-%s", rx->pathInfo);
    } else {
        return sfmt("http::response-%s?%s", rx->pathInfo, httpGetParamsString(conn));
    }
}


/*
    @copy   default
    
    Copyright (c) Embedthis Software LLC, 2003-2011. All Rights Reserved.
    Copyright (c) Michael O'Brien, 1993-2011. All Rights Reserved.
    
    This software is distributed under commercial and open source licenses.
    You may use the GPL open source license described below or you may acquire 
    a commercial license from Embedthis Software. You agree to be fully bound 
    by the terms of either license. Consult the LICENSE.TXT distributed with 
    this software for full details.
    
    This software is open source; you can redistribute it and/or modify it 
    under the terms of the GNU General Public License as published by the 
    Free Software Foundation; either version 2 of the License, or (at your 
    option) any later version. See the GNU General Public License for more 
    details at: http://embedthis.com/downloads/gplLicense.html
    
    This program is distributed WITHOUT ANY WARRANTY; without even the 
    implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. 
    
    This GPL license does NOT permit incorporating this software into 
    proprietary programs. If you are unable to comply with the GPL, you must
    acquire a commercial license to use this software. Commercial licenses 
    for this software and support services are available from Embedthis 
    Software at http://embedthis.com 
    
    Local variables:
    tab-width: 4
    c-basic-offset: 4
    End:
    vim: sw=4 ts=4 expandtab

    @end
 */