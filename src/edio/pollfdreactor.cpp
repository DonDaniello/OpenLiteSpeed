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
#include "pollfdreactor.h"
#include "eventreactor.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/poll.h>
#include <assert.h>


PollfdReactor::PollfdReactor( )
    : m_pfds( NULL )
    , m_pReactors( NULL )
    , m_pEnd( NULL )
    , m_pStoreEnd( NULL )
    , m_iFirstRecycled( 65535 )
    , m_priHandler( NULL )
{
}

PollfdReactor::~PollfdReactor()
{
    deallocate();
}

/** No descriptions */
int PollfdReactor::allocate( int capacity )
{
    EventReactor **clients= (EventReactor **) realloc(m_pReactors,
                        capacity * sizeof(EventReactor *));
    if (!clients)
        return -1;
    m_pReactors = clients;
    struct pollfd *pfds = (struct pollfd *) realloc(m_pfds,
                        capacity * sizeof(struct pollfd));
    if (!pfds)
        return -1;
    m_pCur = pfds + ( m_pCur - m_pfds );
    m_pEnd = pfds + ( m_pEnd - m_pfds );
    m_pfds = pfds;
    m_pStoreEnd = m_pfds + capacity;
    memset( m_pEnd, 0, sizeof( struct pollfd) * (m_pStoreEnd - m_pEnd ) );
    EventReactor ** pEnd = m_pReactors + ( m_pEnd - m_pfds );
    while( clients < pEnd )
        (*clients++)->setPollfd( pfds++ );
    memset( pEnd, 0, sizeof( EventReactor *) * ( m_pStoreEnd - m_pEnd ) );
    pfds = m_pEnd;
    while( pfds < m_pStoreEnd )
        (pfds++)->fd = -1;
    return 0;
}

/** No descriptions */
int PollfdReactor::deallocate()
{
    m_pStoreEnd = NULL;
    m_pEnd = NULL;
    if ( m_pReactors )
    {
        free(m_pReactors);
        m_pReactors = NULL;
    }
    if ( m_pfds )
    {
        free(m_pfds);
        m_pfds = NULL;
    }
    return 0;
}

int PollfdReactor::grow()
{
    int n = (m_pStoreEnd - m_pfds) * 2;
    if ( n == 0 )
        n = DEFAULT_CAPACITY;
    return allocate( n );
}


int PollfdReactor::remove( EventReactor *pHandler )
{
    struct pollfd * pRm = pHandler->getPollfd();
    int fd = pHandler->getfd();
    pHandler->setPollfd( NULL );
    //assert( pRm == m_pCur );
    if (( pRm >= m_pfds )&&( pRm < m_pEnd)&&(fd == pRm->fd))
    {
        if ( pRm->revents )
        {
            pRm->revents = 0;
            --m_iEvents;
        }
        pRm->fd = -1;
        m_pReactors[pRm - m_pfds] = NULL;

        if ( pRm == m_pEnd - 1)
        {
            --m_pEnd;
            while( m_pEnd[-1].fd == -1 )
                --m_pEnd;
        }
        else
        {
            pRm->events = m_iFirstRecycled;
            m_iFirstRecycled = pRm - m_pfds;
        }
        return 0;
    }
    return -1;
}



