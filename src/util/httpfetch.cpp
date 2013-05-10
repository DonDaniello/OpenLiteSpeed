/*****************************************************************************
*    Open LiteSpeed is an open source HTTP server.                           *
*    Copyright (C) 2013  LiteSpeed Technologies, Inc.                        *
*                                                                            *
*    This program is free software: you can redistribute it and/or modify    *
*    it under the terms of the GNU General Public License as published by    *
*    the Free Software Foundation, either version 3 of the License, or       *
*    (at your option) any later version.                                     *
*                                                                            *
*    This program is distributed in the hope that it will be useful,         *
*    but WITHOUT ANY WARRANTY; without even the implied warranty of          *
*    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the            *
*    GNU General Public License for more details.                            *
*                                                                            *
*    You should have received a copy of the GNU General Public License       *
*    along with this program. If not, see http://www.gnu.org/licenses/.      *
*****************************************************************************/
#include "httpfetch.h"

#include <socket/coresocket.h>
#include <util/vmembuf.h>
#include <util/ni_fio.h>

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <util/ssnprintf.h>

HttpFetch::HttpFetch()
    : m_fdHttp( -1 )
    , m_pBuf( NULL )
    , m_statusCode( 0 )
    , m_pReqBuf( NULL )
    , m_reqBufLen( 0 )
    , m_reqSent( 0 )
    , m_reqHeaderLen( 0 )
    , m_pReqBody( NULL )
    , m_reqBodyLen( 0 )
    , m_connTimeout( 20 )
    , m_pRespContentType( NULL )
    , m_pProxyServerAddr( NULL )
    , m_callback( NULL )
{
}

HttpFetch::~HttpFetch()
{
    if ( m_fdHttp != -1 )
        close( m_fdHttp );
    if ( m_pBuf )
        delete m_pBuf;
    if ( m_pReqBuf )
        free( m_pReqBuf );
    if ( m_pRespContentType )
        free( m_pRespContentType );
    if ( m_pProxyServerAddr )
        free( m_pProxyServerAddr );
}

void HttpFetch::releaseResult()
{
    if ( m_pBuf )
    {
        m_pBuf->close();
        delete m_pBuf;
        m_pBuf = NULL;
    }
    if ( m_pRespContentType )
    {
        free( m_pRespContentType );
        m_pRespContentType = NULL;
    }
}

void HttpFetch::reset()
{
    if ( m_fdHttp != -1 )
    {
        close( m_fdHttp );
        m_fdHttp = -1;
    }
    if ( m_pReqBuf )
    {
        free( m_pReqBuf );
        m_pReqBuf = NULL;
    }
    releaseResult();
    m_statusCode    = 0;
    m_reqBufLen     = 0;
    m_reqSent       = 0;
    m_reqHeaderLen  = 0;
    m_reqBodyLen    = 0;
}

int HttpFetch::allocateBuf( const char * pSaveFile )
{
    int ret;
    if ( m_pBuf != NULL )
    {
        delete m_pBuf;
    }
    m_pBuf = new VMemBuf();
    if ( !m_pBuf )
        return -1;
    if ( pSaveFile )
        ret = m_pBuf->set( pSaveFile, 8192 );
    else
        ret = m_pBuf->set( VMBUF_ANON_MAP, 8192 );
    return ret;
    
}

int HttpFetch::startReq( const char * pURL, int nonblock, 
            const char * pBody, int bodyLen, const char * pSaveFile,
            const char * pContentType, const char * addrServer )
{
    if ( m_fdHttp != -1 )
        return -1;
    m_pReqBody = pBody;
    if ( m_pReqBody )
    {
        m_reqBodyLen = bodyLen;
        if ( buildReq( "POST", pURL, pContentType ) == -1 )
            return -1;
    }
    else
    {
        m_reqBodyLen = 0;
        if ( buildReq( "GET", pURL ) == -1 )
            return -1;
    }
    if ( allocateBuf( pSaveFile ) == -1 )
        return -1;
    return startProcessReq( nonblock, addrServer );
}

int HttpFetch::buildReq( const char * pMethod, const char * pURL, const char * pContentType )
{
    int len = strlen( pURL ) + 200;
    char * pReqBuf;
    const char * pURI;
    if (( strncasecmp( pURL, "http://", 7 ) != 0 )&&
        ( strncasecmp( pURL, "https://", 8 ) != 0 ))
        return -1;
    if ( pContentType == NULL )
        pContentType = "application/x-www-form-urlencoded";
    const char * p = pURL + 7;
    if (*( pURL + 4 ) != ':' )
        ++p;
    pURI = strchr( p, '/' );
    if ( !pURI )
        return -1;
    if ( pURI - p > 250 )
        return -1;
    memcpy( m_achHost, p, pURI - p );
    m_achHost[ pURI - p ] = 0;
    if ( pURI - p == 0 )
        return -1;
    if ( m_reqBufLen < len )
    {
        pReqBuf = ( char *)realloc( m_pReqBuf, len );
        if ( pReqBuf == NULL )
            return -1;
        m_reqBufLen = len;
        m_pReqBuf = pReqBuf;
    }
    m_reqHeaderLen = safe_snprintf( m_pReqBuf, len,
                    "%s %s HTTP/1.0\r\n"
                    "Host: %s\r\n"
                    "Connection: Close\r\n",
                    pMethod, pURI, m_achHost );
    if ( m_reqBodyLen > 0 )
    {
        m_reqHeaderLen += safe_snprintf( m_pReqBuf + m_reqHeaderLen,
                            len - m_reqHeaderLen,
                            "Content-Type: %s\r\n"
                            "Content-Length: %d\r\n",
                            pContentType,
                            m_reqBodyLen );
    }
    strcpy( m_pReqBuf + m_reqHeaderLen, "\r\n" );
    if ( strchr( m_achHost, ':' ) == NULL )
        strcat( m_achHost, ":80" );
    m_reqHeaderLen += 2;
    m_reqSent = 0;
    return 0;
    
}

int HttpFetch::startProcessReq( int nonblock, const char * pAddr)
{
    m_reqState = 0;
    if ( m_fdHttp != -1 )
        close( m_fdHttp );
    int iFLTag = 0;
        iFLTag = O_NONBLOCK;
    if ( m_pProxyServerAddr )
        pAddr = m_pProxyServerAddr;
    if ( !pAddr )
        pAddr = m_achHost;
    int ret = CoreSocket::connect( pAddr, iFLTag, &m_fdHttp, 1 );
    if ( m_fdHttp == -1 )
        return -1;
    ::fcntl( m_fdHttp, F_SETFD, FD_CLOEXEC );
    if ( !nonblock )
    {
        fd_set          readfds;
        struct timeval  timeout;
        FD_ZERO( &readfds );
        FD_SET( m_fdHttp, &readfds );
        timeout.tv_sec = m_connTimeout; timeout.tv_usec = 0;
        if ((ret = select(m_fdHttp+1, &readfds, &readfds, NULL, &timeout)) != 1 )
        {
            close( m_fdHttp );
            m_fdHttp = -1;
            return -1;
        }
        else
        {
            int error;
            socklen_t len = sizeof( int );
            ret = getsockopt( m_fdHttp, SOL_SOCKET, SO_ERROR, &error, &len );
            if (( ret == -1 )||( error != 0 ))
            {
                if ( ret != -1 )
                    errno = error;
                endReq( -1 );
                return -1;
            }
        }
        int val = fcntl( m_fdHttp, F_GETFL, 0 );
        fcntl( m_fdHttp, F_SETFL, val &(~O_NONBLOCK) );
    }
    if ( ret == 0 )
    {
        m_reqState = 2; //connected, sending request header
        sendReq();
    }
    else
        m_reqState = 1; //connecting
    return 0;
}

short HttpFetch::getPollEvent() const
{
    if ( m_reqState <= 3 )
        return POLLOUT;
    else
        return POLLIN;
}

int HttpFetch::sendReq()
{
    int error;
    int ret = 0;
    switch( m_reqState )
    {
    case 1: //connecting
        {
            socklen_t len = sizeof( int );
            ret = getsockopt( m_fdHttp, SOL_SOCKET, SO_ERROR, &error, &len );
        }
        if (( ret == -1 )||( error != 0 ))
        {
            if ( ret != -1 )
                errno = error;
            endReq( -1 );
            return -1;
        }
        m_reqState = 2;
        //fall through
    case 2:
        ret = ::nio_write( m_fdHttp, m_pReqBuf + m_reqSent,
                        m_reqHeaderLen - m_reqSent );
        //write( 1, m_pReqBuf + m_reqSent, m_reqHeaderLen - m_reqSent );
        if ( ret > 0 )
            m_reqSent += ret;
        else if ( ret == -1 )
        {
            if ( errno != EAGAIN )
            {
                endReq( -1 );
                return -1;
            }
        }
        if ( m_reqSent >= m_reqHeaderLen )
        {
            m_reqState = 3;
            m_reqSent = 0;
        }
        else
            break;
        //fall through
    case 3: //send request body
        if ( m_reqBodyLen )
        {
            if ( m_reqSent < m_reqBodyLen )
            {
                ret = ::nio_write( m_fdHttp, m_pReqBody + m_reqSent,
                        m_reqBodyLen - m_reqSent );
                //write( 1, m_pReqBody + m_reqSent, m_reqBodyLen - m_reqSent );
                if ( ret == -1 )
                { 
                    if ( errno != EAGAIN )
                    {
                        endReq( -1 );
                        return -1;
                    }
                    return -1;
                }
                if ( ret > 0 )
                {
                    m_reqSent += ret;
                }
                if ( m_reqSent < m_reqBodyLen )
                    break;
            }   
        }
        m_respHeaderBufLen = 0;
        m_reqState = 4;
    }
    if (( ret == -1 )&&( errno != EAGAIN ))
        return -1;
    return 0;
}

int HttpFetch::getLine( char * &p, char * pEnd,
                    char * &pLineBegin, char * &pLineEnd )
{
    int ret = 0;
    pLineBegin = NULL;
    pLineEnd = ( char *)memchr( p, '\n', pEnd - p );
    if ( !pLineEnd )
    {
        if ( pEnd - p < 1024 - m_respHeaderBufLen )
        {
            memmove( &m_achResHeaderBuf[m_respHeaderBufLen], p, pEnd - p );
            m_respHeaderBufLen += pEnd - p;
        }
        else
        {
            ret = -1;
        }
        p = pEnd;
    }
    else
    {
        if ( !m_respHeaderBufLen )
        {
            pLineBegin = p;
            p = pLineEnd + 1;
        }
        else
        {
            if ( pLineEnd - p < 1024 - m_respHeaderBufLen )
            {
                pLineBegin = p;
                memmove( &m_achResHeaderBuf[m_respHeaderBufLen], p, pLineEnd - p );
                pLineEnd = &m_achResHeaderBuf[m_respHeaderBufLen + pLineEnd - p ];
                p = pLineEnd + 1;
                m_respHeaderBufLen = 0;

            }
            else
            {
                ret = -1;
                p = pEnd;
            }
        }
        if ( *(pLineEnd-1) == '\r' )
            --pLineEnd;
        *pLineEnd = 0;
    }
    return ret;
}


int HttpFetch::recvResp()
{
    int ret = 0;
    int len;
    char *p;
    char *pEnd;
    char *pLineBegin;
    char *pLineEnd;
    char achBuf[8192];
    while( m_statusCode != -1 )
    {
        fd_set          readfds;
        struct timeval  timeout;
        FD_ZERO( &readfds );
        FD_SET( m_fdHttp, &readfds );
        timeout.tv_sec = m_connTimeout; timeout.tv_usec = 0;
        if ((ret = select(m_fdHttp+1, &readfds, &readfds, NULL, &timeout)) != 1 )
        {
            endReq( -1 );
            return -1;
        }
        ret = ::nio_read( m_fdHttp, achBuf, 8192 );
        if ( ret == 0 )
        {
            if ( m_respBodyLen == -1 )
                endReq( 0 );
            else
                endReq( -1 );
        }
        
        if ( ret <= 0 )
            break;
        p = achBuf;
        pEnd = p + ret;
        while( p < pEnd )
        {
            switch( m_reqState )
            {
            case 4: //waiting response status line
                if ( getLine( p, pEnd, pLineBegin, pLineEnd ) )
                    endReq( -1 );
                if ( pLineBegin )
                {
                    if ( memcmp( pLineBegin, "HTTP/1.", 7 ) != 0 )
                        return endReq( -1 );
                    pLineBegin += 9;
                    if ( !isdigit(*pLineBegin)||!isdigit(*(pLineBegin+1))||
                         !isdigit(*(pLineBegin + 2 )))
                         return endReq( -1 );
                    m_statusCode = (*pLineBegin     - '0')*100 +
                                   (*(pLineBegin+1) - '0')* 10 +
                                    *(pLineBegin+2) - '0';
                    m_reqState = 5;
                    m_respBodyLen = -1;
                }
                else
                    break;
                //fall through
            case 5: //waiting response header
                if ( getLine( p, pEnd, pLineBegin, pLineEnd ) )
                    endReq( -1 );
                if ( pLineBegin )
                {
                    if ( strncasecmp( pLineBegin, "Content-Length:", 15 ) == 0 )
                    {
                        m_respBodyLen = strtol(pLineBegin + 15, (char **)NULL, 10);
                    }
                    else if ( strncasecmp( pLineBegin, "Content-Type:", 13 ) == 0 )
                    {
                        pLineBegin += 13;
                        while((pLineBegin < pLineEnd) && isspace( *pLineBegin ) )
                            ++pLineBegin;   
                        if ( pLineBegin < pLineEnd )
                        {
                            if ( m_pRespContentType )
                                free( m_pRespContentType );
                            m_pRespContentType = strdup( pLineBegin );
                        }
                    }
                    else if ( pLineBegin == pLineEnd )
                    {
                        m_reqState = 6;
                        m_respBodyRead = 0;
                        if (( m_respBodyLen == 0 )||( m_respBodyLen < -1 ))
                            return endReq( 0 );

                    }

                }
                break;
            case 6: //waiting response body
                if ( (len = m_pBuf->write( p, pEnd - p )) == -1 )
                {
                    return endReq( -1 );
                }
                if ((m_respBodyLen>0)&&( m_pBuf->writeBufSize() >= m_respBodyLen ))
                {
                    return endReq( 0 );
                }
                p += len;
                break;
            }
        }
    }
    if (( ret == -1 )&&( errno != EAGAIN ))
        return -1;
    return 0;
}


int HttpFetch::endReq( int res )
{
    if ( m_pBuf )
    {
        if ( res == 0 )
        {
            m_reqState = 7;
            if ( m_pBuf->getfd() != -1 )
            {
                m_pBuf->exactSize();
            }
        }
//        m_pBuf->close();
    }
    if ( res != 0 )
    {
        m_reqState = 8;
        m_statusCode = res;
    }
    if ( m_fdHttp != -1 )
    {
        close( m_fdHttp );
        m_fdHttp = -1;
    }
    if ( m_callback != NULL )
        (*m_callback)( m_callbackArg, this );
    return 0;
}

int HttpFetch::cancel()
{
    return endReq( -1 );   
}

int HttpFetch::processEvents( short revent )
{
    if ( revent & POLLOUT )
        sendReq();
    if ( revent & POLLIN )
        recvResp();
    if ( revent & POLLERR )
        endReq( -1 );
    return 0;
}

int HttpFetch::process()
{
    while(( m_fdHttp != -1 )&&(m_reqState < 4 ))
    {
        sendReq();
    }
    while(( m_fdHttp != -1 )&&( m_reqState < 7 ))
    {
        recvResp();
    }
    return (m_reqState == 7)? 0 : -1;
}

void HttpFetch::setProxyServerAddr( const char * pAddr )
{
    if ( m_pProxyServerAddr )
    {
        free( m_pProxyServerAddr );
        m_pProxyServerAddr = NULL;
    }
    if ( pAddr )
        m_pProxyServerAddr = strdup( pAddr );
}



