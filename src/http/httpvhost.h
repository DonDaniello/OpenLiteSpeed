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
#ifndef HTTPVHOST_H
#define HTTPVHOST_H



#include <http/contexttree.h>
#include <http/expiresctrl.h>
#include <http/httpcontext.h>
#include <http/httplogsource.h>
#include <http/reqstats.h>
#include <http/rewriterulelist.h>
#include <http/throttlecontrol.h>

#include <log4cxx/nsdefs.h>

#include <util/hashstringmap.h>
#include <util/refcounter.h>
#include <util/stringlist.h>

#include <sys/types.h>

BEGIN_LOG4CXX_NS
class Logger;
class Appender;
END_LOG4CXX_NS

#define LS_NEVER_FOLLOW     0
#define LS_ALWAYS_FOLLOW    1
#define LS_FOLLOW_OWNER     2
#define VH_SYM_CTRL         3
#define VH_ENABLE           4
#define VH_SERVER_ENABLE    8
#define VH_ENABLE_SCRIPT    16
//#define VH_CONTEXT_AC       32
#define VH_RESTRAINED       64
#define VH_ACC_LOG          128
#define VH_GZIP             256


#define DEFAULT_ADMIN_SERVER_NAME   "_AdminVHost"


class HttpConnection;
class HttpContext;
class AccessControl;
class AccessCache;
class Awstats;
class FcgiApp;
class UserDir;
class StringList;
class AccessLog;
class HttpHandler;
class HotlinkCtrl;
class HttpMime;
class ModUserdir;
class RewriteRuleList;
class RewriteMapList;
class SSITagConfig;
class SSLContext;

class RealmMap : public HashStringMap< UserDir* >
{
    typedef HashStringMap< UserDir* > _shmap;
public:
    explicit RealmMap( int initSize = 20 );
    ~RealmMap();
    const UserDir* find( const char * pScript ) const;
    UserDir* get( const char * pFile, const char * pGroup );
};


class HttpVHost : public RefCounter, public HttpLogSource
{
private:
    ReqStats            m_reqStats;
    AccessLog         * m_pAccessLog;
    LOG4CXX_NS::Logger* m_pLogger;
    LOG4CXX_NS::Appender * m_pBytesLog;

    ThrottleLimits      m_throttle;

    int16_t             m_iMaxKeepAliveRequests;
    int16_t             m_iSmartKeepAlive;

    int                 m_iFeatures;
    
    AccessCache       * m_pAccessCache;
    ContextTree         m_contexts;
    HttpContext         m_rootContext;
    AutoStr2            m_sVhRoot;
    HotlinkCtrl       * m_pHotlinkCtrl;
    RealmMap            m_realmMap;
    StringList          m_matchNameList;

    Awstats           * m_pAwstats;
    
    AutoStr2            m_sName;
    AutoStr2            m_sAdminEmails;
    AutoStr2            m_sAutoIndexURI;
    int                 m_iMappingRef;

    uid_t               m_uid;
    gid_t               m_gid;
    char                m_iRewriteLogLevel;
    char                m_iGlobalMatchContext;
    int                 m_iDummy2;
    AutoStr2            m_sChroot;
    RewriteRuleList     m_rewriteRules;    
    RewriteMapList    * m_pRewriteMaps;
    SSLContext        * m_pSSLCtx;
    SSITagConfig      * m_pSSITagConfig;
    HttpVHost( const HttpVHost& rhs );
    void operator=( const HttpVHost& rhs );
public:
    explicit HttpVHost( const char * pHostName );
    ~HttpVHost();

    int setDocRoot( const char * psRoot );
    const AutoStr2 * getDocRoot() const
    {   return m_rootContext.getRoot();     }

    const char * getName() const            {   return m_sName.c_str();     }
    void updateName( const char * p )       {   m_sName.setStr( p );        }

    void setVhRoot( const char* psVhRoot )  {   m_sVhRoot = psVhRoot;       }
    const AutoStr2 * getVhRoot() const      {   return &m_sVhRoot;          }
                    
//     const StringList* getIndexFileList() const
//     {   return &m_indexFileList;  }
//     StringList* getIndexFileList()          {   return &m_indexFileList;    }
//     void setIndexFileList(StringList * p)   {   m_rootContext.setIndexFileList( p );    }

    ReqStats * getReqStats()                {   return &m_reqStats;         }

        
//    int  setCustomErrUrls(int statusCode, const char* url)
//    {   return m_rootContext.setCustomErrUrls( statusCode, url );  }
//    const AutoStr2 * getErrDocUrl( int statusCode ) const
//    {   return m_rootContext.getErrDocUrl( statusCode );    }

    const char * getAccessLogPath() const;
    LOG4CXX_NS::Logger * getLogger() const  {   return m_pLogger;       }

    AccessControl * getAccessCtrl();
    AccessCache * getAccessCache() const    {   return m_pAccessCache;  }

    HotlinkCtrl * getHotlinkCtrl() const    {   return m_pHotlinkCtrl;  }
    void setHotlinkCtrl( HotlinkCtrl * p )  {   m_pHotlinkCtrl = p;     }
        
    UserDir * getRealm( const char * pRealm );
    const UserDir * getRealm(const char * pRealm ) const;
    void onTimer();
    void onTimer30Secs();

    //const char * getAdminEmails() const;
    const AutoStr2 * getAdminEmails() const {   return &m_sAdminEmails;  }
    void setAdminEmails( const char * pEmails);

    int addContext( HttpContext* pContext )
    {   return m_contexts.add( pContext );          }

    const HttpContext* bestMatch( const char * pURI) const
    {   return m_contexts.bestMatch( pURI );        }

    const HttpContext* matchLocation( const char * pURI, int regex = 0 ) const;

    HttpContext* getContext( const char * pURI, int regex = 0 ) const;

    ContextTree* getContextTree()
    {   return &m_contexts;     }

    virtual void setLogLevel( const char * pLevel );
    virtual int  setAccessLogFile( const char * pFileName, int pipe );
    virtual int  setErrorLogFile( const char * pFileName );
    virtual void setErrorLogRollingSize( int size );
    
    virtual AccessLog * getAccessLog() const  {   return m_pAccessLog;    }

    void offsetChroot( const char * pChroot, int len );
    
    void setFeature( int bit, int enable )
    {   m_iFeatures = (m_iFeatures & (~bit) ) |((enable)? bit : 0);     }
    
    int  isEnabled() const  {   return m_iFeatures & VH_ENABLE;         }
    void enable( int enable)
    {   setFeature( VH_ENABLE, (m_iFeatures & VH_SERVER_ENABLE) ? enable : 0 );  }

    void serverEnabled( int enable)     {   setFeature( VH_SERVER_ENABLE, enable ); }
    
    void enableScript( int enable )     {   setFeature( VH_ENABLE_SCRIPT, enable ); }
    int  isScriptEnabled() const        {   return m_iFeatures & VH_ENABLE_SCRIPT;  }

    void followSymLink( int follow )
    {   m_iFeatures = (m_iFeatures & (~VH_SYM_CTRL) ) | (follow & VH_SYM_CTRL); }
    int  followSymLink() const          {   return m_iFeatures & VH_SYM_CTRL;   }

    void enableAccessCtrl();

    void restrained( int enable )       {   setFeature(VH_RESTRAINED, enable);  }
    int  restrained() const             {   return m_iFeatures & VH_RESTRAINED; }

    void enableAccessLog( int enable )  {   setFeature(VH_ACC_LOG, enable); }
    int  enableAccessLog() const        {   return m_iFeatures & VH_ACC_LOG;}

    void enableGzip( int enable )       {   setFeature(VH_GZIP, enable);    }
    int  enableGzip() const             {   return m_iFeatures & VH_GZIP;   }
        
    ExpiresCtrl& getExpires()           {   return m_rootContext.getExpires();  }
    const ExpiresCtrl& getExpires() const
    {   return m_rootContext.getExpires();           }

    HttpMime * getMIME()                {   return m_rootContext.getMIME();     }
    const HttpMime * getMIME() const    {   return m_rootContext.getMIME();     }

    HttpContext & getRootContext()              {   return m_rootContext;   }
    const HttpContext& getRootContext() const   {   return m_rootContext;   }
    
    void  logAccess( HttpConnection * pConn ) const;

    const AutoStr2 * addMatchName( const char * pName )
    {
        const AutoStr2 * ret = m_matchNameList.find( pName );
        if ( !ret )
            return m_matchNameList.add( pName );
        else
            return ret;
    }
    UserDir *getFileUserDir( const char * pName, 
                   const char * pFile, const char * pGroup );

    void contextInherit()
    {   m_contexts.contextInherit();     }

    void setUid( uid_t uid )    {   m_uid = uid;    }
    uid_t getUid() const        {   return m_uid;   }

    void setGid( gid_t gid )    {   m_gid = gid;    }
    gid_t getGid() const        {   return m_gid;   }

    void incMappingRef()        {   ++m_iMappingRef;        }
    void decMappingRef()        {   --m_iMappingRef;        }
    int getMappingRef() const   {   return m_iMappingRef;   }

    void setChroot( const char * pRoot );
    const AutoStr2 * getChroot() const  {   return &m_sChroot;      }

    void setUidMode( int a )    {   m_rootContext.setUidMode( a );      }
    void setChrootMode( int a ) {   m_rootContext.setChrootMode( a );   }

    void setMaxKAReqs( int a )  {   m_iMaxKeepAliveRequests = a;        }
    short getMaxKAReqs() const  {   return m_iMaxKeepAliveRequests;     }

    void setSmartKA( int a )    {   m_iSmartKeepAlive = a;              }
    short getSmartKA() const    {   return m_iSmartKeepAlive;           }

    ThrottleLimits * getThrottleLimits()    {   return &m_throttle;     }
    const ThrottleLimits * getThrottleLimits() const
    {   return &m_throttle;         }
    
    char getRewriteLogLevel() const     {   return m_iRewriteLogLevel;  }
    void setRewriteLogLevel( int l )    {   m_iRewriteLogLevel = l;     }

    const RewriteRuleList * getRewriteRules() const
    {   return &m_rewriteRules;     }
    const RewriteMapList * getRewriteMaps() const
    {   return m_pRewriteMaps;      }

    void addRewriteMap( const char * pName, const char * pLocation );
    void addRewriteRule( char * pRules );

    void updateUGid( const char * pLogId, const char * pPath );

    const AutoStr2 * getAutoIndexURI() const
    {   return &m_sAutoIndexURI;    }

    void setAutoIndexURI( const char * pURI )
    {   m_sAutoIndexURI.setStr( pURI );     }

    HttpContext * addContext( const char * pUri, int type,
                const char * pLocation, const char * pHandler, int allowBrowse );
    HttpContext * setContext( HttpContext * pContext, const char * pUri,
                int type, const char * pLocation, const char * pHandler,
                int allowBrowse, int match = 0 );
    HttpContext * addContext( int match, const char * pUri, int type,
        const char * pLocation, const char * pHandler, int allowBrowse );
    void setAwstats( Awstats * pAwstats )
    {
        m_pAwstats = pAwstats;
    }
    
    void logBytes( long bytes );
    void setBytesLogFilePath( const char * pPath, long rollingSize );
    int  BytesLogEnabled() const     {   return m_pBytesLog != NULL; }


    
    char isGlobalMatchContext() const           {   return m_iGlobalMatchContext;   }
    void setGlobalMatchContext( char global )   {   m_iGlobalMatchContext = global; }
    
    const char * getVhName( int &len ) const
    {
        len = m_sName.len();
        return m_sName.c_str();
    }  
    void setSSITagConfig( SSITagConfig * pConfig )
    {   m_pSSITagConfig = pConfig;  }

    SSITagConfig * getSSITagConfig() const
    {   return m_pSSITagConfig;     }   
    void setSSLContext( SSLContext * pCtx );

    SSLContext * getSSLContext() const
    {   return m_pSSLCtx;           }    
};


#endif
