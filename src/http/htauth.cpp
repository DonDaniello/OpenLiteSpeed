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
#include "htauth.h"
#include <http/authuser.h>
#include <util/autobuf.h>
#include <http/httpstatuscode.h>
#include <http/userdir.h>

#include <util/base64.h>
#include <util/pool.h>
#include <util/stringlist.h>
#include <util/stringtool.h>

#include <assert.h>
#include <openssl/md5.h>

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <util/ssnprintf.h>


HTAuth::HTAuth()
    : m_pName( NULL )
    , m_authHeader( NULL )
    , m_authHeaderLen( 0 )
    , m_pUserDir( NULL )
    , m_iAuthType( AUTH_DEFAULT )
{
}

HTAuth::HTAuth( const char * pRealm )
    : m_pName( NULL )
    , m_authHeader( NULL )
    , m_authHeaderLen( 0 )
    , m_pUserDir( NULL )
    , m_iAuthType( AUTH_DEFAULT )
{}

HTAuth::~HTAuth()
{
    if ( m_pName )
        g_pool.deallocate2( m_pName );
    if ( m_authHeader )
        g_pool.deallocate2( m_authHeader );
}

void HTAuth::setName( const char * pName )
{
    m_pName = (char *)g_pool.dupstr( pName );
    buildWWWAuthHeader( pName );
}



int HTAuth::buildWWWAuthHeader( const char * pName )
{
    if ( m_iAuthType & AUTH_DIGEST )
    {
        m_authHeader = (char *)g_pool.dupstr( pName );
        return 0;
    }
    else if ( m_iAuthType & AUTH_BASIC )
    {
        int len = strlen( pName ) + 40;

        m_authHeader = (char *)g_pool.reallocate2( m_authHeader, len );
        if ( m_authHeader )
        {
            m_authHeaderLen = safe_snprintf( m_authHeader, len,
                    "WWW-Authenticate: Basic realm=\"%s\"\r\n", pName );
            return 0;
        }
    }
    return -1;
}

int HTAuth::addWWWAuthHeader( AutoBuf &buf ) const
{
    if ( m_iAuthType & AUTH_BASIC )
    {
        buf.append( m_authHeader, m_authHeaderLen);
        return 0;
    }
    else if ( m_iAuthType & AUTH_DIGEST )
    {
        if ( buf.available() < 256 )
            if ( buf.grow( 256 ) == -1 )
                return -1;
        //FIXME: add digest header
        int iBufLen = safe_snprintf( buf.end(), buf.available(),
                    "WWW-Authenticate: Digest realm=\"%s\" nonce=\"%lu\"\r\n",
                    m_authHeader, time(NULL) );
        buf.used( iBufLen );
        return 0;
    }
    return -1;
}

#define MAX_PASSWD_LEN      128
#define MAX_BASIC_AUTH_LEN  256
#define MAX_DIGEST_AUTH_LEN  4096


int HTAuth::basicAuth( HttpConnection * pConn, const char * pAuthorization, int size,
                        char * pAuthUser, int bufLen,
                        const AuthRequired * pRequired ) const
{

    char buf[MAX_BASIC_AUTH_LEN];
    char * pUser = buf;
    int ret = Base64::decode( pAuthorization, size, pUser );
    if ( ret == -1 )
        return SC_401;
    while( isspace( *pUser ) )
        ++pUser;
    if ( *pUser == ':' )
        return SC_401;
    char * passReq = strchr(pUser+1, ':');
    if ( passReq == NULL)
        return SC_401;
    char * p = passReq++;
    while( isspace( p[-1] ) )
        --p;
    *p = 0;
    int userLen = p - pUser;
    memccpy( pAuthUser, pUser, 0, bufLen - 1 );
    *(pAuthUser + bufLen - 1) = 0;
    
    while( isspace( *passReq ) )
        ++passReq;
    p = (char *)buf + ret;
    if ( passReq >= p )
        return SC_401;
    while( isspace( p[-1] ) )
        --p;
    *p = 0;
    ret = m_pUserDir->authenticate( pConn, pUser, userLen, passReq, ENCRYPT_PLAIN,
                        pRequired );
    return ret;
}

//enum
//{
//    DIGEST_USERNAME,
//    DIGEST_REALM,
//    DIGEST_NONCE,
//    DIGEST_URI,
//    DIGEST_QOP,
//    DIGEST_NC,
//    DIGEST_CNONCE,
//    DIGEST_RESPONSE,
//    DIGEST_OPAQUE,
//    DIGEST_ALGORITHM,
//};

int HTAuth::digestAuth( HttpConnection * pConn, const char * pAuthorization,
                        int size, char * pAuthUser, int bufLen,
                        const AuthRequired * pRequired ) const
{
    const char *    username = NULL;
    int             username_len;
    const char *    realm = NULL;
    int             realm_len;
    const char *    nonce = NULL;
    int             nonce_len;
    const char *    requri = NULL;
    int             requri_len;
    const char *    resp_digest = NULL;
    int             resp_digest_len;
    const char * p = pAuthorization;
    const char * pEnd = p + size;
    const char * pNameEnd;
    const char * pNameBegin;
    const char * pValueBegin;
    const char * pValueEnd;
    while( p < pEnd )
    {
        pNameEnd = strchr( p, '=' );
        if ( !pNameEnd )
            break;
        pNameBegin = p;
        p = pNameEnd + 1;
        while( isspace( pNameEnd[-1] ) )
            --pNameEnd;
        
        while( isspace( *p ) )
            ++p;
        pValueBegin = p;
        if ( *p == '"' )
        {
            ++pValueBegin;
            pValueEnd = strchr( p, '"' );
            if (!pValueEnd )
                break;
            p = pValueEnd + 1;
            while( isspace( *p ) || (*p == ','))
                ++p;
        }
        else
        {
            pValueEnd = strchr( p, ',' );
            if ( pValueEnd )
            {
                p = pValueEnd + 1;
                while( isspace( *p ))
                    ++p;
            }
            else
            {
                p = pValueEnd = pEnd;
            }
        }
        if ( strncasecmp( pNameBegin, "username", 8 ) == 0 )
        {
            username = pValueBegin;
            username_len = pValueEnd - pValueBegin;
        }
        else if ( strncasecmp( pNameBegin, "realm", 5 ) == 0 )
        {
            realm = pValueBegin;
            realm_len = pValueEnd - pValueBegin;
        }
        else if ( strncasecmp( pNameBegin, "nonce", 5 ) == 0 )
        {
            nonce = pValueBegin;
            nonce_len = pValueEnd - pValueBegin;
            
        }
        else if ( strncasecmp( pNameBegin, "uri", 3 ) == 0 )
        {
            requri = pValueBegin;
            requri_len = pValueEnd - pValueBegin;
        }
        else if ( strncasecmp( pNameBegin, "response", 8 ) == 0 )
        {
            resp_digest = pValueBegin;
            resp_digest_len = pValueEnd - pValueBegin;
        }
        else if ( strncasecmp( pNameBegin, "nc", 2 ) == 0 )
        {
            
        }
    }
    if (( username )&&( username_len < bufLen ))
    {
        memccpy( pAuthUser, username, 0, username_len );
        *(pAuthUser + username_len) = 0;

    }
    if (( !username )||( !realm )||( !nonce )||( !requri )||( !resp_digest ))
    {
        return SC_401;
    }
//    const AuthUser * pUserData = getUser( pAuthUser, username_len );
//    if ( pUserData )
//    {
//        const char * pPasswd = pUserData->getPasswd();
//        if ( pPasswd )
//        {
//            char achMethod[24];
//            unsigned char achReqHash[16];
//            char achReqHashAsc[33];
//            int methodLen;
//            MD5_CTX md5_request;
//            MD5_Init( &md5_request );
//            MD5_Update( &md5_request, achMethod, methodLen );
//            MD5_Update( &md5_request, requri, requri_len );
//            MD5_Final( achReqHash, &md5_request );
//            StringTool::hex_to_str( (const char *)achReqHash, 16, achReqHashAsc );
//            //fix me
//
//
//        }
//    }
    return SC_401;
}

int HTAuth::authenticate( HttpConnection * pConn, const char * pAuthHeader,
       int authHeaderLen, char * pAuthUser, int userBufLen,
       const AuthRequired * pRequired ) const
{
    if ( strncasecmp( pAuthHeader, "Basic ", 6 ) == 0 )
    {
        pAuthHeader += 6;
        authHeaderLen -= 6;
        if (!( m_iAuthType & AUTH_BASIC )||( authHeaderLen > MAX_BASIC_AUTH_LEN ))
        {
            return SC_401;
        }
        return basicAuth( pConn, pAuthHeader,
                    authHeaderLen, pAuthUser,
                    AUTH_USER_SIZE - 1, pRequired );
    }
    else if ( strncasecmp( pAuthHeader, "Digest ", 7 ) == 0 )
    {
        pAuthHeader += 7;
        authHeaderLen -= 7;
        if ( authHeaderLen > MAX_DIGEST_AUTH_LEN )
        {
            return SC_401;
        }
        return digestAuth( pConn, pAuthHeader,
                    authHeaderLen, pAuthUser,
                    AUTH_USER_SIZE - 1, pRequired );
    }
    return SC_401;
    
}


