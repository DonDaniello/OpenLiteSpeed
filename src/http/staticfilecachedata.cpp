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
#include <http/staticfilecachedata.h>
#include <http/datetime.h>
#include <http/httpheader.h>
#include <http/httplog.h>
#include <http/httpmime.h>
#include <http/httpreq.h>
#include <http/httpstatuscode.h>
#include <ssi/ssiscript.h>

#include <util/ni_fio.h>

#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <unistd.h>
#include <util/ssnprintf.h>

static size_t   s_iMaxInMemCacheSize = 4096;
static size_t   s_iMaxMMapCacheSize = 256 * 1024;



static size_t   s_iMaxTotalInMemCache = DEFAULT_TOTAL_INMEM_CACHE;

static size_t   s_iMaxTotalMMAPCache  = DEFAULT_TOTAL_MMAP_CACHE;
static size_t   s_iCurTotalInMemCache = 0;
static size_t   s_iCurTotalMMAPCache  = 0;

static int      s_iAutoUpdateStaticGzip = 0;
static int      s_iGzipCompressLevel    = 6;
static int      s_iMaxFileSize          = 1024 * 1024;
static int      s_iMinFileSize          = 300;

void FileCacheDataEx::setTotalInMemCacheSize( size_t max)
{
    s_iMaxTotalInMemCache = max;
}

void FileCacheDataEx::setTotalMMapCacheSize( size_t max)
{
    s_iMaxTotalMMAPCache = max;
}


void FileCacheDataEx::setMaxInMemCacheSize( size_t max)
{
    //FIXME: use page size
    if ( max <= 16384 )
        s_iMaxInMemCacheSize = max;
}

void FileCacheDataEx::setMaxMMapCacheSize( size_t max)
{
    s_iMaxMMapCacheSize = max;
}

static int openFile( const char * pPath, int& fd )
{
    fd = nio_open( pPath, O_RDONLY, 0 );
    if ( fd == -1 )
    {
        int err = errno;
        LOG_INFO(( "Failed to open file [%s], error: %s", pPath,
                strerror( err ) ));
        switch( err )
        {
        case EACCES:
            return SC_403;
        case EMFILE:
        case ENFILE:
            return SC_503;
        default:
            return SC_500;
        }
    }
    return 0;
}

FileCacheDataEx::FileCacheDataEx()
    : m_fd( -1 )
{
    memset( &m_iStatus, 0,
            (char *)(&m_pCache + 1) - (char *)&m_iStatus );
}

FileCacheDataEx::~FileCacheDataEx()
{
    release();
    //deallocateCache();
}


void FileCacheDataEx::setFileStat( const struct stat &st )
{
    m_lSize     = st.st_size;
    m_lastMod   = st.st_mtime;
    m_inode     = st.st_ino;
}

int  FileCacheDataEx::allocateCache( size_t size )
{
    assert( m_pCache == NULL );
    int newSize = (( size + 128) >> 7 ) << 7;
    if ( size + s_iCurTotalInMemCache > s_iMaxTotalInMemCache )
        return ENOMEM;
    m_pCache = ( char *)malloc( newSize );
    if ( !m_pCache )
    {
        return ENOMEM;
    }
    setStatus( CACHED );
    s_iCurTotalInMemCache += size;
    return 0;
}

void FileCacheDataEx::release()
{
    switch( getStatus() )
    {
    case MMAPED:
        if ( m_pCache )
        {
            if ( D_ENABLED( DL_MORE ))
                LOG_D(( "[MMAP] Release mapped data at %p", m_pCache ));
            munmap( m_pCache, m_lSize );
            s_iCurTotalMMAPCache -= m_lSize;
        }
        break;
    case CACHED:
        if ( m_pCache )
        {
            s_iCurTotalInMemCache -= m_lSize;
            free( m_pCache );
        }
        break;
    default:
        if ( m_fd != -1 )
        {
            close( m_fd );
            m_fd = -1;
        }
    }
    memset( &m_iStatus, 0,
            (char *)(&m_pCache + 1) - (char *)&m_iStatus );
}

void FileCacheDataEx::closefd()
{
    close( m_fd );
    m_fd = -1;
}


int FileCacheDataEx::readyData(const char * pPath)
{
    int fd;
    int ret = openFile( pPath, fd );
    if ( ret )
        return ret;
    if ( (size_t)m_lSize < s_iMaxInMemCacheSize )
    {
        ret = allocateCache( m_lSize );
        if ( ret == 0 )
        {
            ret = nio_read( fd, m_pCache, m_lSize );
            if ( ret == m_lSize )
            {
                close( fd );
                return 0;
            }
            else
            {
                release();
            }
        }
    }
    else if (( (size_t)m_lSize < s_iMaxMMapCacheSize )
            &&((size_t)m_lSize + s_iCurTotalMMAPCache < s_iMaxTotalMMAPCache ))
    {
        m_pCache = (char *)mmap( 0, m_lSize, PROT_READ,
                MAP_PRIVATE, fd, 0 );
        s_iCurTotalMMAPCache += m_lSize;
        if ( D_ENABLED( DL_MORE ))
            LOG_D(( "[MMAP] Map %p to file:%s", m_pCache, pPath ));
        if ( m_pCache == MAP_FAILED )
        {
            m_pCache = 0;
        }
        else
        {
            setStatus( MMAPED );
            close( fd );
            return 0;
        }
    }
    setfd( fd );
    fcntl( fd, F_SETFD, FD_CLOEXEC );
    return 0;
}

const char * FileCacheDataEx::getCacheData(
    off_t offset, int& wanted, char *pBuf, long len )
{
    if ( isCached() )
    {
        if ( offset > m_lSize )
        {
            wanted = 0;
            return pBuf;
        }
        if ( wanted > m_lSize - offset )
            wanted = m_lSize - offset;
        return m_pCache + offset;
    }
    else
    {
        assert( m_fd != -1 );
        off_t off = nio_lseek( m_fd, offset, SEEK_SET );
/*        if ( D_ENABLED( DL_MORE ))
            LOG_D(( "lseek() return %d", (int)off ));*/
        if ( off == offset )
        {
            wanted = nio_read( m_fd, pBuf, len );
        }
        else
        {
            wanted = -1;
        }
        return pBuf;
    }

}





StaticFileCacheData::StaticFileCacheData()
{
    memset( &m_pMimeType, 0,
            (char *)(&m_pGziped + 1) - (char *)&m_pMimeType );
}

StaticFileCacheData::~StaticFileCacheData()
{
    if ( m_pGziped )
        delete m_pGziped;
    if ( m_pSSIScript )
        delete m_pSSIScript;    
}



int StaticFileCacheData::testMod( HttpReq * pReq )
{
    const char * pNonMatch;
    if ( pReq->isHeaderSet( HttpHeader::H_IF_NO_MATCH ) )
    {
        pNonMatch = pReq->getHeader( HttpHeader::H_IF_NO_MATCH );
        int len = pReq->getHeaderLen( HttpHeader::H_IF_NO_MATCH );
        if ( *pNonMatch == 'W' )
        {
            len -= 2;
            pNonMatch += 2;
        }
        if ((( m_iETagLen == len)
            &&( memcmp( pNonMatch, m_pETag, m_iETagLen ) == 0 ))
            ||( *pNonMatch == '*' ))
            return SC_304;
    }
    else
    {
        if ( pReq->isHeaderSet( HttpHeader::H_IF_MODIFIED_SINCE ))
        {
            pNonMatch = pReq->getHeader( HttpHeader::H_IF_MODIFIED_SINCE );
            long IMS = DateTime::parseHttpTime( pNonMatch );
            if ( IMS >= m_fileData.getLastMod() )
                return SC_304;
        }
    }
    return 0;
}

int StaticFileCacheData::testIfRange( const char * pMatch, int len )
{
    if (( *(pMatch+1) == '/' )||( m_fileData.getLastMod() == DateTime::s_curTime ))
    {
        return SC_412;
    }
    if ( *pMatch == '"' )
    {
        if (( m_iETagLen != len)
            ||( memcmp( pMatch, m_pETag, m_iETagLen ) != 0 ))
            return SC_412;
    }
    else
    {
        long IUMS = DateTime::parseHttpTime( pMatch );
        if ( IUMS < m_fileData.getLastMod() )
             return SC_412;
    }
    return 0;
}

int StaticFileCacheData::testUnMod( HttpReq * pReq )
{
    if ( m_fileData.getLastMod() == DateTime::s_curTime )
    {
        return SC_412;
    }
    const char * pMatch;
    if ( pReq->isHeaderSet( HttpHeader::H_IF_MATCH ))
    {
        pMatch = pReq->getHeader( HttpHeader::H_IF_MATCH );
        if ( *pMatch != '*' )
        {
            int len = pReq->getHeaderLen( HttpHeader::H_IF_MATCH );
            if ( *pMatch == 'W' )
            {
                return SC_412;
            }
            if (( m_iETagLen != len)
                ||( memcmp( pMatch, m_pETag, m_iETagLen ) != 0 ))
                return SC_412;
        }
        return 0;
    }
    if ( pReq->isHeaderSet( HttpHeader::H_IF_UNMOD_SINCE ) )
    {
        pMatch = pReq->getHeader( HttpHeader::H_IF_UNMOD_SINCE );
        long IMS = DateTime::parseHttpTime( pMatch );
        if ( IMS < m_fileData.getLastMod() )
            return SC_412;
    }
    return 0;
}


int StaticFileCacheData::buildFixedHeaders( int etag )
{
    int size = 6 + 30 + 17 + RFC_1123_TIME_LEN
            + 20 + m_pMimeType->getMIME()->len() + 10 ;
    const char * pCharset;
    if ( m_pCharset && HttpMime::needCharset(m_pMimeType->getMIME()->c_str()) )
    {
        pCharset = m_pCharset->c_str();
        size += m_pCharset->len();
    }
    else
        pCharset = "";
    if ( !m_sHeaders.resizeBuf( size ) )
        return SC_500;
    
    char * pEnd = m_sHeaders.buf() + size;
    char *p = m_sHeaders.buf();
    m_iFileETag = etag;
    
    memcpy( p, "ETag: ", 6 );
    m_pETag = p + 6;
    m_iETagLen = safe_snprintf( m_pETag, pEnd - m_pETag,
            "\"%lx-%lx-%lx\"",
            (unsigned long)m_fileData.getFileSize(),
            m_fileData.getLastMod(),
            (long)m_fileData.getINode());
    p = m_pETag + m_iETagLen;
    memcpy( p, "\r\nLast-Modified: ", 17 );
    p += 17;
    DateTime::getRFCTime( m_fileData.getLastMod(), p );
    p += RFC_1123_TIME_LEN;

    p += safe_snprintf( p, pEnd - p ,
            "\r\nContent-Type: %s%s\r\n",
             m_pMimeType->getMIME()->c_str(), pCharset );
            
    m_sHeaders.setLen( p - m_sHeaders.buf() );
    m_iValidateHeaderLen = 6 + 17 + 2 + m_iETagLen + RFC_1123_TIME_LEN ;
    return 0;
}

int  FileCacheDataEx::buildCLHeader( bool gziped )
{
    int size = 40;
    //if ( gziped )
    //    size += 24;
    if ( !m_sCLHeader.resizeBuf( size ) )
        return SC_500;
    char * p = m_sCLHeader.buf();
//    if ( gziped )
//    {
//        p += safe_snprintf( p, size,
//            "Content-Encoding: gzip\r\n"
//            "Content-Length: %ld\r\n", getFileSize() );
//    }
//    else
    {
        if ( sizeof( off_t) == 8 )
        {
            p += safe_snprintf( p, size,
                "Content-Length: %lld\r\n", (long long )getFileSize() );
        }
        else
        {
            p += safe_snprintf( p, size,
                "Content-Length: %ld\r\n", (long)getFileSize() );
        }
    }
    m_sCLHeader.setLen( p - m_sCLHeader.buf() );
    return 0;
}


int StaticFileCacheData::build( const AutoStr2 &path, const struct stat& fileStat , int etag)
{
    m_fileData.setFileStat( fileStat );
    char * pReal = m_real.resizeBuf( path.len() + 6 );
    if ( !pReal )
        return -1;
    strcpy( pReal, path.c_str() );
    m_real.setLen( path.len() );
    memmove( pReal + path.len() + 1, "lsz\0\0", 5 );
    //int ret = buildFixedHeaders();
    int ret = buildFixedHeaders(etag);
    if ( ret )
        return ret;
    ret = m_fileData.buildCLHeader( false );
    return ret;
//    if ( m_pMimeType->getExpires()->compressable() )
//    {
//        buildGziped();
//    }
//    return 0;
}

//FileCacheDataEx* StaticFileCacheData::buildGziped()
//{
//    struct stat state;
//    if (( ::stat( pFileName, &state ) != -1 )
//        &&( state.st_mtime >= m_fileData.getLastMod() ))
//    {
//        if ( state.st_size < m_fileData.getFileSize() )
//        {
//            newGzipCache( state.st_size );
//        }
//    }
//    else
//    {
//        //IMPROVE: create the gziped file on the fly
//    }
//    *p = 0;
//    return m_pGziped;
//
//}

#include <sys/time.h>
#include <sys/resource.h>

static int createLockFile( const char * pReal, char * p )
{
    *p = 'l';       // filename "*.lszl"
    struct stat st;
    int ret = nio_stat( pReal, &st );
    if ( ret != -1 )  //compression in progress
    {
        *p = 0;
        return -1;
    }
    ret = ::open( pReal, O_WRONLY | O_CREAT | O_EXCL, 0600 );
    *p = 0;
    if ( ret == -1)
    {
        return -1;
    }
    close( ret );
    return 0;    
}

static void removeLockFile( const char * pReal, char *p )
{
    *p = 'l';       // filename "*.lszl"
    unlink( pReal );
    *p = 0;
}

int StaticFileCacheData::tryCreateGziped()
{
    if ( !s_iAutoUpdateStaticGzip )
        return -1;
    off_t size = m_fileData.getFileSize();
    if (( size > s_iMaxFileSize )||( size < s_iMinFileSize ))
        return -1;
    {
        int n = m_real.len();
        struct stat st;
        char achBuf[4096];
        memmove( achBuf, m_real.c_str(), n );
        memmove( &achBuf[n], ".gz\0", 4 );
        if ( nio_stat( achBuf, &st ) == 0 )
        {
            if (( st.st_mtime > getLastMod() )&&( st.st_size < getFileSize() ))
            {
                rename( achBuf, m_real.c_str() );
                return 0;
            }
        }
    }
    char *p = m_real.buf() + m_real.len() + 4;
    if ( createLockFile( m_real.buf(), p ) )
        return -1;
    if ( size < (off_t)s_iMaxMMapCacheSize )
    {
        
        struct timeval begin;
        gettimeofday( &begin, NULL );
        long ret = compressFile();
        if (( ret != -1 )&&( D_ENABLED( DL_MORE )))
        {
            struct timeval end;
            gettimeofday( &end, NULL );
            if ( D_ENABLED( DL_MORE ))
            {
                LOG_D(( "[PROFILE] %s: Compressed from %ld to %ld bytes "
                        "in %ld microseconds", m_real.c_str(),
                        size, ret, ( end.tv_sec - begin.tv_sec ) * 1000000 +
                        end.tv_usec - begin.tv_usec  ));
            }
        }
        removeLockFile( m_real.buf(), p );
        return ret;
        
    }
    else
    {
        //IMPROVE: move this to a standalone process,
        //          fork() is too expensive.
            
        if ( D_ENABLED( DL_MORE ))
        {
            LOG_D(( "To compressed file %s in another process.",
                    m_real.c_str() ));
        }
        int forkResult;
        forkResult = fork();
        if( forkResult )  //error or parent process
            return -1;

        //child process
        setpriority( PRIO_PROCESS, 0, 5 );

        long ret = compressFile();
        removeLockFile( m_real.buf(), p );
        if ( ret == -1 )
        {
            LOG_WARN(( "Failed to compress file %s!", m_real.c_str() ));
            
        }
        exit(1);
    }
}



#include <util/gzipbuf.h>
#include <util/vmembuf.h>
int StaticFileCacheData::compressFile()
{
    int ret;
    GzipBuf gzBuf;
    if ( 0 != gzBuf.init( GzipBuf::GZIP_DEFLATE, s_iGzipCompressLevel ) )
        return -1;
    VMemBuf gzFile;
    char * pFileName = m_real.buf();
    char * p = pFileName + m_real.len();
    register char ch = *p;
    if (( !m_fileData.isCached() &&
        ( m_fileData.getfd() == -1 )))
    {
        *p = 0;
        if ( m_fileData.readyData( pFileName ) != 0 )
            return -1;
    }
        
    *p = '.';
    ret= gzFile.set( pFileName , -1 );
    *p = ch;
    if ( ret )
        return ret;
    gzBuf.setCompressCache( &gzFile );
    if ( gzBuf.beginStream() )
        return -1;
    off_t offset = 0;
    int len;
    int wanted;
    const char * pData;
    char achBuf[8192];
    while( true )
    {
        wanted = getFileSize() - offset;
        if ( wanted <= 0 )
            break;
        if ( wanted < 8192 )
            len = wanted;
        else
            len = 8192;
        pData = m_fileData.getCacheData( offset, wanted, achBuf, len );
        if ( wanted <= 0 )
            return -1;
        if ( gzBuf.write( pData, wanted ) )
            return -1;
        offset += wanted;
        
    }    
    if ( 0 == gzBuf.endStream() )
    {
        int size;
        if ( gzFile.exactSize( &size ) == 0 )
        {
            gzFile.close();
            return size;
        }
    }
    return -1;
}


int StaticFileCacheData::buildGzipCache( const struct stat &st)
{
    FileCacheDataEx * pData = m_pGziped;
    if ( !pData )
    {
        pData = new FileCacheDataEx();
        if ( !pData )
            return -1;
    }
    else
        pData->release();

    pData->setFileStat( st );
    if ( pData->buildCLHeader( true ))
    {
        m_pGziped = NULL;
        delete pData;
        return -1;
    }
    else
        m_pGziped = pData;
    return 0;
}



int StaticFileCacheData::readyGziped()
{
    time_t tm = time( NULL );
    if ( tm == getLastMod() )
        return -1;
    if ( tm != m_tmLastCheckGzip )
    {
        struct stat st;
        m_tmLastCheckGzip = tm;
        int ret = nio_stat( m_real.c_str(), &st );
        if (( ret == -1 )||( st.st_mtime < getLastMod() )) 
        {
            if (( !m_pGziped )||( m_pGziped->getRef() == 0 ))
            {
                if ( ret != -1 )
                    unlink( m_real.c_str() );
                ret = tryCreateGziped();
                if ( ret == -1 )
                {
                    delete m_pGziped;
                    m_pGziped = NULL;
                    return -1;
                }
                else
                {
                    ret = nio_stat( m_real.c_str(), &st );
                    if ( ret )
                        return -1;
                }
            }
            else
                return -1;
        }
        if (( !m_pGziped )||( m_pGziped->isDirty( st ) ))
            buildGzipCache( st );        
    }
    if ( m_pGziped )
    {
        if (( m_pGziped->isCached() ||
            ( m_pGziped->getfd() != -1 )))
            return 0;
        return m_pGziped->readyData( m_real.c_str() );
    }
    return -1;
}

int StaticFileCacheData::readyCacheData(
            FileCacheDataEx *&pECache, char compress )
{
    char * pFileName = m_real.buf();
    int ret;
    if (( compress )&&(m_pMimeType->getExpires()->compressable() ))
    {
        char * p = pFileName + m_real.len();
        *p = '.';
        ret = readyGziped();
        *p = 0;
        if ( ret == 0 )
        {
            pECache = m_pGziped;
            return 0;
        }
    }
    pECache = &m_fileData;
    if (( m_fileData.isCached() ||
        ( m_fileData.getfd() != -1 )))
        return 0;
    return m_fileData.readyData( pFileName );
}

int StaticFileCacheData::release()
{
    m_fileData.release();
    if ( m_pGziped )
        m_pGziped->release();
    return 0;
}

void StaticFileCacheData::setUpdateStaticGzipFile( int enable, int level,
                                    size_t min, size_t max )
{
    s_iAutoUpdateStaticGzip = enable;
    s_iGzipCompressLevel = level;
    s_iMaxFileSize      = max;
    s_iMinFileSize      = min;
}


