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
#include "gsockaddr.h"
#include <assert.h>
#include <netdb.h>
#include <stdio.h>
#include <strings.h>
#include <sys/types.h>

#include <util/pool.h>
#include <util/ssnprintf.h>

GSockAddr::GSockAddr( const GSockAddr& rhs )
{
    ::memset( this, 0, sizeof( GSockAddr ) );
    *this = rhs;
}

GSockAddr& GSockAddr::operator=( const struct sockaddr & rhs )
{
    if (( !m_pSockAddr )||( m_pSockAddr->sa_family != rhs.sa_family ))
        allocate( rhs.sa_family );
    memmove( m_pSockAddr, &rhs, m_len );
    return *this;
}

static int sockAddrLen( int family )
{
    int len = 128;
    switch( family )
    {
    case AF_INET:
        len = 16;
        break;
    case AF_INET6:
        len = sizeof( struct sockaddr_in6 );
        break;
    case AF_UNIX:
        len = sizeof( struct sockaddr_un );
        break;
    }
    return len;    
}




int GSockAddr::allocate( int family )
{
    if ( m_pSockAddr )
        g_pool.deallocate( m_pSockAddr, m_len );
    m_len = sockAddrLen( family );
    m_pSockAddr = ( struct sockaddr *)g_pool.allocate( m_len );
    if ( m_pSockAddr )
    {
        bzero( m_pSockAddr, m_len );
        m_pSockAddr->sa_family = family;
        //m_pSockAddr->sa_len = m_len;
        return 0;
    }
    else
        return -1;
}

void GSockAddr::release()
{   if ( m_pSockAddr )
        g_pool.deallocate( m_pSockAddr, m_len );
}

/**
  * @param pURL  the destination of the connection, format of the string is:
  *              TCP connection "192.168.0.1:800", "TCP://192.168.0.1:800"
  *              UDP  "UDP:192.168.0.1:800"
  *              Unix Domain Stream Socket "UDS:///tmp/foo.bar"
  *              Unix Doamin Digram Socket "UDD:///tmp/foo.bar"
  */
const char * parseURL( const char * pURL, int* domain )
{
    if ( pURL == NULL )
        return NULL;
    if ( strncmp( pURL + 3, "://", 3 ) != 0 )
    {
        if (( strncasecmp( pURL, "TCP6://", 7 ) == 0 )||
            ( strncasecmp( pURL, "UDP6://", 7 ) == 0 ))
        {
            *domain = AF_INET6;
            pURL += 7;
        }
        else  if ( *pURL != '[' )
            *domain = AF_INET;
        else    
            *domain = AF_INET6;
    }
    else
    {
        if (( strncasecmp( pURL, "TCP:", 4 ) == 0 )||
            ( strncasecmp( pURL, "UDP:", 4 ) == 0 ))
        {
            pURL += 6;
            if ( *pURL != '[' )
                *domain = AF_INET;
            else    
                *domain = AF_INET6;
        }
        else if (( strncasecmp( pURL, "UDS:", 4 ) == 0 )||
                 ( strncasecmp( pURL, "UDD:", 4 ) == 0 ))
        {
            *domain = AF_UNIX;
            pURL += 5;
        }
        else 
            return NULL;
    }
    return pURL;
}


int GSockAddr::set( const char * pURL, int tag )
{
    int domain;
    const char * p = parseURL( pURL, &domain );
    if ( !p )
        return -1;
    return set( domain, p, tag );
}

int GSockAddr::set( int family, const char * pURL, int tag )
{
    char *p;
    int  port = 0;
    int  gotAddr = 1;
    char achDest[128];
    
    if ( allocate( family ) )
        return -1;
    if ( family == AF_UNIX )
    {
        memccpy( m_un->sun_path, pURL, 0, sizeof(m_un->sun_path) );
        return 0;
    }
    
    memccpy( achDest, pURL, 0, sizeof( achDest ) -1 );
    achDest[ sizeof( achDest ) - 1 ] = 0;
    p = strrchr(  achDest, ':' );
    if ( p == NULL )
    {
        if ( !(tag & ADDR_ONLY) )   
            return -1;
    }
    else
    {
        *p++ = 0;
        port = atoi( p );
        if (( port <= 0 )||( port > 655535 ))
            return -1;
    }
    p = achDest;    
    switch( family )
    {
    case AF_INET:
        if (( !strcmp( achDest, "*"))||( !strcmp( p, "ANY" ) )||( !strcasecmp( achDest, "ALL" ) ))
        {
            if ( tag & NO_ANY != 0 )
                m_v4->sin_addr.s_addr = htonl( INADDR_LOOPBACK );
            else
                m_v4->sin_addr.s_addr = htonl(INADDR_ANY);
        } 
        else if (!strcasecmp( achDest, "localhost" ) )
            m_v4->sin_addr.s_addr = htonl( INADDR_LOOPBACK );
        else
        {
            m_v4->sin_addr.s_addr = inet_addr( achDest );
            if ( m_v4->sin_addr.s_addr == INADDR_BROADCAST)
            {
                if ( tag & DO_NSLOOKUP == 0 )
                    return -1;
                gotAddr = 0;
/*              struct hostent * hep;
                hep = gethostbyname(achDest);
                if ((!hep) || (hep->h_addrtype != AF_INET || !hep->h_addr_list[0]))
                {
                  return -1;
                }
                if (hep->h_addr_list[1]) {
                  //fprintf(stderr, "Host %s has multiple addresses ---\n", host);
                  //fprintf(stderr, "you must choose one explicitly!!!\n");
                  return -1;
                }
                m_v4->sin_addr.s_addr = ((struct in_addr *)(hep->h_addr))->s_addr;*/
            }
        }
        break;
    case AF_INET6:
        if ( *achDest != '[' )
            return -1;
        p = strchr( achDest, ']' );
        if ( !p )
            return -1;
        *p = 0;
        p = &achDest[1];
        if (( !strcmp( p, "*"))||( !strcmp( p, "ANY" ) )||( !strcmp( p, "ALL" ) ))
        {
            if ( tag & NO_ANY != 0 )
                strcpy( p, "::1" );
            else
                strcpy( p, "::" );
        } else if (!strcasecmp( p, "localhost" ) )
            strcpy( p, "::1" );
        gotAddr = 0;
        break;
    }
    if ( 0 == gotAddr )
    {
        struct addrinfo *res, hints;
        
        memset(&hints, 0, sizeof(hints));
        
        hints.ai_family   = family;
        hints.ai_socktype = SOCK_STREAM;
        hints.ai_protocol = IPPROTO_TCP;
        
        if ( getaddrinfo(p, NULL, &hints, &res) ) 
        {
            return -1;
        }
        
        memcpy(m_pSockAddr, res->ai_addr, res->ai_addrlen);
        freeaddrinfo(res);
    }
    m_v4->sin_port = htons( port );
    return 0;
}

int GSockAddr::parseAddr( const char * pString )
{
    int family = AF_INET;
    if ( *pString == '[' )
    {
        family = AF_INET6;
    }
    else if ( *pString == '/' )
    {
        family = AF_UNIX;
    }
    return set( family, pString, ADDR_ONLY );
}


/** return the address in string format. */
const char * GSockAddr::ntop( const struct sockaddr * pAddr, char * pBuf, int len )
{
    if ( !pBuf )
        return NULL;
    switch( pAddr->sa_family )
    {
    case AF_INET:
        return inet_ntop( pAddr->sa_family,
                &((const struct sockaddr_in *)pAddr)->sin_addr,
                pBuf, len );
    case AF_INET6:
        return inet_ntop( pAddr->sa_family,
                &((const struct sockaddr_in6 *)pAddr)->sin6_addr,
                pBuf, len );
    case AF_UNIX:
        memccpy( pBuf, ((const struct sockaddr_un *)pAddr)->sun_path, 0, len - 1 );
        *( pBuf + len - 1 ) = 0;
        return pBuf;
    default:
        return NULL;
    }

}

const char * GSockAddr::toString( char * pBuf, int len ) const
{
    if ( toAddrString( pBuf, len ) == NULL )
        return NULL;
    if ( m_pSockAddr->sa_family != AF_UNIX )
    {
        int used = strlen( pBuf );
        safe_snprintf( pBuf + used, len - used, ":%d", getPort() );
    }
    return pBuf;
}


void GSockAddr::set( const in_addr_t addr, const in_port_t port )
{
    if (( !m_pSockAddr )||( m_pSockAddr->sa_family != AF_INET ))
        allocate( AF_INET );
    m_v4->sin_addr.s_addr = addr;
    m_v4->sin_port = htons( port );
}

void GSockAddr::set( const in6_addr* addr, const in_port_t port,
                    uint32_t flowinfo)
{
    if (( !m_pSockAddr )||( m_pSockAddr->sa_family != AF_INET6 ))
        allocate( AF_INET6 );
    memmove( &(m_v6->sin6_addr), addr, sizeof( in6_addr ) );
    m_v6->sin6_flowinfo = flowinfo;
}

int GSockAddr::getPort() const
{
    return getPort( m_pSockAddr );
}

int GSockAddr::getPort( const sockaddr * pAddr ) 
{
    switch( pAddr->sa_family )
    {
    case AF_INET:
        return ntohs(((struct sockaddr_in  *)pAddr)->sin_port);
    case AF_INET6:
        return ntohs(((struct sockaddr_in6 *)pAddr)->sin6_port);
    default:
        return 0;
    }
}

void GSockAddr::setPort( short port) 
{
    switch( m_pSockAddr->sa_family )
    {
    case AF_INET:
    case AF_INET6:
        m_v4->sin_port = htons( port );
        break;
    }
}


GSockAddr& GSockAddr::operator=( const in_addr_t addr )
{
    if (( !m_pSockAddr )||( m_pSockAddr->sa_family != AF_INET ))
        allocate( AF_INET );
    m_v4->sin_addr.s_addr = addr;
    return *this;
}



