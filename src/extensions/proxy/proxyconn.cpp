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
#include "proxyconn.h"
#include <extensions/extworker.h>
#include <http/httpconnection.h>
#include <http/httpextconnector.h>
#include <http/httpdefs.h>
#include <http/httpglobals.h>
#include <http/httplog.h>
#include <http/httpreq.h>
#include <http/httpresourcemanager.h>
#include <http/chunkinputstream.h>

#include <sys/socket.h>

static char s_achForwardHttps[] = "X-Forwarded-Proto: https\r\n";

ProxyConn::ProxyConn()
{
    strcpy( m_extraHeader, "Accept-Encoding: gzip\r\nX-Forwarded-For: " );
    memset( &m_iTotalPending, 0,
            ((char *)(&m_pChunkIS + 1)) - (char *)&m_iTotalPending );
}

ProxyConn::~ProxyConn()
{
}

void ProxyConn::init( int fd, Multiplexer* pMplx )
{
    EdStream::init( fd, pMplx, POLLIN|POLLOUT|POLLHUP|POLLERR );
    reset();

    m_lReqBeginTime = time( NULL );
    
    //Increase the number of successful request to avoid max connections reduction.
    incReqProcessed();
}

int ProxyConn::doWrite()
{
    if ( getConnector() )
    {
        int state = getConnector()->getState();
        if ((!state)||( state & (HEC_FWD_REQ_HEADER | HEC_FWD_REQ_BODY) ))
        {
            int ret = getConnector()->extOutputReady();
            if ( getState() == ABORT )
            {
                if ( getConnector() )
                {
                    incReqProcessed();
                    getConnector()->endResponse( 0, 0 );
                }
            }
            return ret;
        }
    }
    if ( m_iTotalPending > 0 )
        return flush();
    else
        suspendWrite();
    return 0;
}


int ProxyConn::sendReqHeader()
{
    m_iovec.clear();
    HttpConnection * pConn = getConnector()->getHttpConn();
    HttpReq * pReq = pConn->getReq();
    //remove the trailing "\r\n" before adding our headers
    const char * pBegin = pReq->getOrgReqLine();
    m_iTotalPending = pReq->getHttpHeaderLen();
    int headerLen = 17;
    char * pExtraHeader = &m_extraHeader[23];
    const char * pForward = pReq->getHeader( HttpHeader::H_X_FORWARDED_FOR );
    int len;
    if ( *pForward != '\0' )
    {
        len = pReq->getHeaderLen( HttpHeader::H_X_FORWARDED_FOR );
        if ( len > 160 )
            len = 160;
        memmove( &pExtraHeader[headerLen], pForward, len );
        headerLen += len;
        pExtraHeader[headerLen++] = ',';
        
    }
    //add "X-Forwarded-For" header
    memmove( &pExtraHeader[headerLen], pConn->getPeerAddrString(),
            pConn->getPeerAddrStrLen() );
    headerLen += pConn->getPeerAddrStrLen();
    pExtraHeader[headerLen++] = '\r';
    pExtraHeader[headerLen++] = '\n';
    
#if 0       //always set "Accept-Encoding" header to "gzip"
    char * pAE = ( char *)pReq->getHeader( HttpHeader::H_ACC_ENCODING );
    if ( *pAE )
    {
        int len = pReq->getHeaderLen( HttpHeader::H_ACC_ENCODING );
        if ( len > 4 )
        {
            memmove( pAE, "gzip", 4 );
            memset( pAE + 4, ' ', len - 4 );
        }
    }
    else
    {
        pExtraHeader = m_extraHeader;
        headerLen += 23;
    }        
#endif

    if ( *(pBegin + --m_iTotalPending - 1 ) == '\r' )
        --m_iTotalPending;
    if ( *pForward )
    {
        if (( pBegin + m_iTotalPending ) -
            ( pForward + pReq->getHeaderLen( HttpHeader::H_X_FORWARDED_FOR )) == 2 )
        {
            const char * p = pForward -= 16;
            while( *(p - 1) != '\n' )
                --p;
            m_iTotalPending = p - pBegin;
        }
    }
    m_iovec.append( pBegin, m_iTotalPending );
    
    if ( pConn->isSSL() )
    {
        m_iovec.append( s_achForwardHttps, sizeof( s_achForwardHttps ) - 1 );
        m_iTotalPending += sizeof( s_achForwardHttps ) - 1;
    }

    //if ( headerLen > 0 )
    {
        pExtraHeader[headerLen++] = '\r';
        pExtraHeader[headerLen++] = '\n';
        m_iovec.append( pExtraHeader, headerLen );
        m_iTotalPending += headerLen;
    }
    m_iReqHeaderSize = m_iTotalPending;
    m_iReqBodySize = pReq->getContentFinished();
    setInProcess( 1 );
    return 1;
}

int  ProxyConn::sendReqBody( const char * pBuf, int size )
{
    int ret;
    if ( m_iTotalPending > 0 )
    {
        m_iovec.append(pBuf, size );
        int total = m_iTotalPending + size;
        ret = writev( m_iovec, total );
        m_iovec.pop_back(1);
        if ( ret > 0 )
        {
            m_iReqTotalSent += ret;
            if ( ret >= total )
            {
                m_iTotalPending = 0;
                m_iovec.clear();
                return size;
            }
            if ( ret >= m_iTotalPending )
            {
                ret -= m_iTotalPending;
                m_iovec.clear();
                m_iTotalPending = 0;
                return ret;
            }
            else
            {
                m_iovec.finish( ret );
                m_iTotalPending -= ret;
                return 0;
            }
        }
    }
    else
    {
        ret = write( pBuf, size );
        if ( ret > 0 )
            m_iReqTotalSent += ret;
    }
    return ret;
}

void ProxyConn::abort()
{
    setState( ABORT );
    //::shutdown( getfd(), SHUT_RDWR );
}

void ProxyConn::reset()
{
    m_iovec.clear();
    if ( m_pChunkIS )
        HttpGlobals::getResManager()->recycle( m_pChunkIS );
    memset( &m_iTotalPending, 0,
            ((char *)(&m_pChunkIS + 1)) - (char *)&m_iTotalPending );
}


int  ProxyConn::begin()
{
    return 1;
}

int  ProxyConn::beginReqBody()
{
    return 1;
}


int ProxyConn::read( char * pBuf , int size )
{
    int len = m_pBufEnd - m_pBufBegin;
    if ( len > 0 )
    {
        if ( len > size )
            len = size;
        memmove( pBuf, m_pBufBegin, len );
        m_pBufBegin += len;
        if ( len >= size )
            return len;
        pBuf += len;
        size -= len;
    }
    int ret = ExtConn::read( pBuf, size );
    if ( D_ENABLED( DL_LESS ) )
        LOG_D(( getLogger(), "[%s] read Response %d bytes",
            getLogId(), ret ));
    if ( ret > 0 )
    {
        m_iRespRecv += ret;
        //::write( 1, pBuf, ret );
        len += ret;
        return len;
    }
    else if ( len )
        return len;
    return ret;
}

int ProxyConn::readv( struct iovec *vector, size_t count )
{
    int len;
    int total = 0;
    const struct iovec *pEnd = vector + count;
    while ( (len = m_pBufEnd - m_pBufBegin) > 0 )
    {
        if (vector == pEnd )
            return total;
        if ( len > (int)vector->iov_len )
            len = vector->iov_len;
        memmove( vector->iov_base, m_pBufBegin, len );
        m_pBufBegin += len;
        total += len;
        if ( len == (int)vector->iov_len )
            ++vector;
        else
        {
            vector->iov_base = (char *)vector->iov_base + len;
            vector->iov_len -= len;
            break;
        }
    }
    int ret = ExtConn::readv( vector, pEnd - vector );
    if ( D_ENABLED( DL_LESS ) )
        LOG_D(( getLogger(), "[%s] read Response %d bytes",
            getLogId(), ret ));
    if ( ret > 0 )
    {
//        int left = ret;
//        const struct iovec* pVec = vector;
//        while( left > 0 )
//        {
//            int writeLen = pVec->iov_len;
//            if ( writeLen > left )
//                writeLen = left;
//            ::write( 1, pVec->iov_base, writeLen );
//            ++pVec;
//            left -= writeLen;
//        }
        m_iRespRecv += ret;
        total += ret;
        return total;
    }
    else if ( total )
        return total;
    return ret;
}


int ProxyConn::doRead()
{
    if ( D_ENABLED( DL_LESS ) )
        LOG_D(( getLogger(), "[%s] ProxyConn::doRead()\n", getLogId() ));
    int ret = processResp();
    if ( getState() == ABORT )
    {
        if ( getConnector() )
        {
            incReqProcessed();
            getConnector()->endResponse( 0, 0 );
        }
    }
    return ret;
}

int ProxyConn::processResp()
{
    register HttpExtConnector * pHEC = getConnector();
    if ( !pHEC )
    {
        errno = ECONNRESET;
        return -1;
    }
    int len = 0;
    int ret = 0;
    int &respState = pHEC->getRespState();
    if ( !(respState & 0xff) )
    {
        const char * pBuf = HttpGlobals::g_achBuf;
        len = ExtConn::read( (char *)pBuf, 1460 );
        
        if ( len > 0 )
        {
            int copy = len;
            if ( m_iRespHeaderRecv + copy > 4095 )
                copy = 4095 - m_iRespHeaderRecv;
            //memmove( &m_achRespBuf[ m_iRespHeaderRecv ], pBuf, copy );
            m_iRespHeaderRecv += copy;
            m_iRespRecv += len;
            if ( D_ENABLED( DL_LESS ) )
                LOG_D(( getLogger(), "[%s] read Response %d bytes",
                    getLogId(), len ));
            //debug code
            //::write( 1, pBuf, len );
            
            ret = pHEC->parseHeader( pBuf, len, 1 );
            switch( ret )
            {
            case -2:
                LOG_WARN(( getLogger(), "[%s] Invalid Http response header, retry!",
                      getLogId() ));
                //debug code
                //::write( 1, pBuf, len );
                errno = ECONNRESET;
            case -1:
                return -1;
            }            
        }
        else
            return len;
        if ( respState & 0xff )
        {
            //debug code
            //::write( 1, HttpGlobals::g_achBuf, pBuf - HttpGlobals::g_achBuf );
            HttpReq * pReq = pHEC->getHttpConn()->getReq();
            if ( pReq->noRespBody() )
            {
                incReqProcessed();
                if ( len > 0 )
                    abort();
                else if ( respState & HEC_RESP_CONN_CLOSE )
                    setState( CLOSING );
                else if ( getState() == ABORT )
                {
                    setState( PROCESSING );
                }

                setInProcess( 0 );
                pHEC->endResponse( 0, 0 );
                return 0;
            }

            m_iRespBodySize = pHEC->getHttpConn()->getResp()->getContentLen();
            if ( D_ENABLED( DL_LESS ) )
                LOG_D(( getLogger(), "[%s] Response body size of proxy reply is %d",
                    getLogId(), m_iRespBodySize ));
            if ( m_iRespBodySize == -1 )
            {
                setupChunkIS();
            }
            m_pBufBegin = pBuf;
            m_pBufEnd = pBuf + len;
            if ( D_ENABLED( DL_MEDIUM ) )
                LOG_D(( getLogger(), "[%s] process Response body %d bytes",
                    getLogId(), len ));
            return readRespBody();
        }
    }
    else
        return readRespBody();
    return 0;
}


int ProxyConn::readRespBody()
{
    register HttpExtConnector * pHEC = getConnector();
    int ret = 0;
    size_t bufLen;
    if ( !pHEC )
    {
        return -1;
    }
    if ( m_pChunkIS )
    {
        while( getState() != ABORT )
        {
            char * pBuf = pHEC->getRespBuf( bufLen );
            if ( !pBuf )
            {
                return -1;
            }
            ret = m_pChunkIS->read( pBuf, bufLen );
            if ( ret >= 0 )
            {
                if ( ret > 0 )
                {
                    m_lLastRespRecvTime = time( NULL );
                    m_iRespBodyRecv += ret;
                    int ret1 = pHEC->respBodyRecv( pBuf, ret );
                    if ( ret1 )
                        return ret1;
                    if ( ret > 1024 )
                        pHEC->flushResp();
                    
                }
                if ( m_pChunkIS->eos() )
                {
                    ret = 0;
                    break;
                }
                if ( ret < (int)bufLen)
                {
                    pHEC->flushResp();
                    return 0;
                }
            }
            else
            {
                if ((errno == ECONNRESET )&&( getConnector() ))
                    break;
                return -1;
            }
        }
    }
    else
    {
        while(( getState() != ABORT )&&( m_iRespBodySize - m_iRespBodyRecv > 0 ))
        {
            char * pBuf = pHEC->getRespBuf( bufLen );
            if ( !pBuf )
            {
                return -1;
            }
            int toRead = m_iRespBodySize - m_iRespBodyRecv;
            if ( toRead > (int)bufLen )
                toRead = bufLen ;
            ret = read( pBuf, toRead );
            if ( ret > 0 )
            {
                m_iRespBodyRecv += ret;
                pHEC->respBodyRecv( pBuf, ret );
                if ( ret > 1024 )
                    pHEC->flushResp();
                //if ( ret1 )
                //    return ret1;
                if ( m_iRespBodySize - m_iRespBodyRecv <= 0 )
                    break;
                if ( ret < (int)toRead)
                {
                    pHEC->flushResp();
                    return 0;
                }
            }
            else
            {
                if ( ret )
                {
                    if ((errno == ECONNRESET )&&( getConnector() ))
                        break;
                }
                return ret;
            }
        }
    }
    incReqProcessed();
    if ( pHEC->getRespState() & HEC_RESP_CONN_CLOSE )
    {
        setState( CLOSING );
    }

    setInProcess( 0 );
    pHEC->endResponse( 0, 0 );
    return ret;
    
}

void ProxyConn::setupChunkIS()
{
    assert ( m_pChunkIS == NULL );
    m_pChunkIS = HttpGlobals::getResManager()->getChunkInputStream();
    m_pChunkIS->setStream( this );
    m_pChunkIS->open();
}


int ProxyConn::doError( int err)
{
    if ( D_ENABLED( DL_LESS ) )
        LOG_D(( getLogger(), "[%s] ProxyConn::doError()", getLogId() ));
    if ( getConnector())
    {
        int state = getConnector()->getState();
        if ( !(state & (HEC_FWD_RESP_BODY | HEC_ABORT_REQUEST
                        | HEC_ERROR|HEC_COMPLETE) ))
        {
            if ( D_ENABLED( DL_LESS ) )
                LOG_D(( getLogger(), "[%s] Proxy Peer closed connection, "
                            "try another connection!", getLogId() ));
            connError( err );
            return 0;
        }
        if ( !(state & HEC_COMPLETE) )
            getConnector()->endResponse( SC_500, -1 );
    }
    return 0;
}


int ProxyConn::addRequest( ExtRequest * pReq )
{
    assert( pReq );
    setConnector( (HttpExtConnector *)pReq );
    reset();
    m_lReqBeginTime = time( NULL );
    return 0;
}

ExtRequest* ProxyConn::getReq() const
{
    return getConnector();
}


int ProxyConn::removeRequest( ExtRequest * pReq )
{
    if ( getConnector() )
    {
        getConnector()->setProcessor( NULL );
        setConnector( NULL );
    }
    return 0;
}


int  ProxyConn::endOfReqBody()
{
    if ( m_iTotalPending )
    {
        int ret = flush();
        if ( ret )
            return ret;
    }
    suspendWrite();
    m_lReqSentTime = time( NULL );
    return 0;
}


int  ProxyConn::flush()
{
    if ( m_iTotalPending )
    {
        int ret = writev( m_iovec, m_iTotalPending );
        if ( ret >= m_iTotalPending )
        {
            ret -= m_iTotalPending;
            m_iTotalPending = 0;
            m_iovec.clear();
        }
        else
        {
            if ( ret > 0 )
            {
                m_iTotalPending -= ret;
                m_iovec.finish( ret );
                return 1;
            }
            return -1;
        }
    }
    return 0;
}

void ProxyConn::finishRecvBuf()
{
    //doRead();
}

void ProxyConn::cleanUp()
{
    setConnector( NULL );
    reset();
    recycle();
}

void ProxyConn::onTimer()
{
//    if (!( getEvents() & POLLIN ))
//    {
//        LOG_WARN(( getLogger(), "[%s] Oops! POLLIN is turned off for this proxy connection,"
//                    " turn it on, this should never happen!!!!", getLogId() ));
//        continueRead();
//    }
//    if (( m_iTotalPending > 0 )&& !( getEvents() & POLLOUT ))
//    {
//        LOG_WARN(( getLogger(), "[%s] Oops! POLLOUT is turned off while there is pending data,"
//                    " turn it on, this should never happen!!!!", getLogId() ));
//        continueWrite();
//    }
    if ( m_lLastRespRecvTime )
    {
        long tm = time( NULL );
        long delta = tm - m_lLastRespRecvTime;
        if (( delta > getWorker()->getTimeout() )&&( m_iRespBodyRecv ))
        {
            if ( m_pChunkIS )
                LOG_INFO(( getLogger(), "[%s] Timeout, partial chunk encoded body received,"
                    " received: %d, chunk len: %d, remain: %d!",
                    getLogId(), m_iRespBodyRecv, m_pChunkIS->getChunkLen(),
                    m_pChunkIS->getChunkRemain() ));
            else
                LOG_INFO((getLogger(), "[%s] Timeout, partial response body received,"
                    " body len: %d, received: %d!",
                    getLogId(), m_iRespBodySize, m_iRespBodyRecv ));
            setState( ABORT );
            getConnector()->endResponse( 0, 0 );;
            return;
        }
        else if (( m_pChunkIS )&&(!m_pChunkIS->getChunkLen())&&( delta > 1 ))
        {
            if (( getConnector() ))
            {
                if ( D_ENABLED( DL_LESS ) )
                    LOG_D(( getLogger(), "[%s] Missing trailing CRLF in Chunked Encoding,"
                                    " remain: %d!", getLogId(), m_pChunkIS->getChunkRemain() ));
//                const char * p = m_pChunkIS->getLastBytes();
//                LOG_INFO(( getLogger(),
//                        "[%s] Last 8 bytes are: %#x %#x %#x %#x %#x %#x %#x %#x",
//                         getLogId(), p[0], p[1], p[2], p[3], p[4], p[5], p[6], p[7] ));
//                HttpReq * pReq = getConnector()->getHttpConn()->getReq();
//                pReq->dumpHeader();
                
                setState( CLOSING );
                getConnector()->endResponse( 0, 0 );
                return;
            }
        }
    }
        
    ExtConn::onTimer();
}


bool ProxyConn::wantRead()
{
    return false;
}

bool ProxyConn::wantWrite()
{
    return false;
}

int  ProxyConn::readResp( char * pBuf, int size )
{
    return 0;
}

void ProxyConn::dump()
{
    LOG_INFO(( getLogger(), "[%s] Proxy connection state: %d, watching event: %d, "
                "Request header:%d, body:%d, sent:%d, "
                "Response header: %d, total: %d bytes received in %ld seconds,"
                "Total processing time: %ld.",
                getLogId(), getState(), getEvents(), m_iReqHeaderSize,
                m_iReqBodySize, m_iReqTotalSent, m_iRespHeaderRecv, m_iRespRecv,
                (m_lReqSentTime)?time(NULL) - m_lReqSentTime : 0,
                time(NULL) - m_lReqBeginTime ));
//    if ( m_iRespHeaderRecv > 0 )
//    {
//        m_achRespBuf[ m_iRespHeaderRecv ] = 0;
//        LOG_INFO(( getLogger(), "[%s] Response Header Received: \n%s", getLogId(),
//            m_achRespBuf ));
//    }
}



