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
#ifndef HTTPRESOURCEMANAGER_H
#define HTTPRESOURCEMANAGER_H



#include <util/objpool.h>
  
class ChunkInputStream;
class ChunkOutputStream;
class MMapVMemBuf;
class VMemBuf;
class GzipBuf;

typedef ObjPool<ChunkInputStream>       ChunkInputStreamPool;
typedef ObjPool<ChunkOutputStream>      ChunkOutputStreamPool;
typedef ObjPool<MMapVMemBuf>            VMemBufPool;
typedef ObjPool<GzipBuf>                GzipBufPool;
typedef ObjPool<GzipBuf>                GunzipBufPool;

class HttpResourceManager
{
    ChunkInputStreamPool    m_poolChunkInputStream;
    ChunkOutputStreamPool   m_poolChunkOutputStream;
    VMemBufPool             m_poolVMemBuf;
    GzipBufPool             m_poolGzipBuf;
    GunzipBufPool           m_poolGunzipBuf;
    
public:
    HttpResourceManager();
    ~HttpResourceManager();

    GzipBuf * getGzipBuf()
    {   return m_poolGzipBuf.get();         }

    void recycle( GzipBuf * pBuf )
    {   m_poolGzipBuf.recycle( pBuf );      }
    
    GzipBuf * getGunzipBuf()
    {   return m_poolGunzipBuf.get();         }  
    void recycleGunzip( GzipBuf * pBuf )
    {   m_poolGunzipBuf.recycle( pBuf );      }    
    
    MMapVMemBuf * getVMemBuf()
    {   return m_poolVMemBuf.get();         }

    int getVMemBufPoolSize()
    {   return m_poolVMemBuf.getPoolSize();         }
    int getVMemBufPoolCapacity()
    {   return m_poolVMemBuf.getPoolCapacity();     }

    void recycle( VMemBuf * pBuf )
    {   m_poolVMemBuf.recycle((MMapVMemBuf*) pBuf );    }

    ChunkInputStream* getChunkInputStream()
    {   return m_poolChunkInputStream.get();    }
    
    void recycle( ChunkInputStream* pStream )
    {   m_poolChunkInputStream.recycle( pStream );  }
    
    ChunkOutputStream* getChunkOutputStream()
    {   return m_poolChunkOutputStream.get();   }
    
    void recycle( ChunkOutputStream* pStream )
    {   m_poolChunkOutputStream.recycle( pStream ); }

    void releaseAll();
    
    void onTimer();
};

#endif
