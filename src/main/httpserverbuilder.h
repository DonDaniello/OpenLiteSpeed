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
#ifndef HTTPSERVERBUILDER_H
#define HTTPSERVERBUILDER_H


#include <util/autostr.h>

#include <limits.h>
#include <stddef.h>
#include <sys/types.h>

class AccessControl;
class CgidWorker;
class Env;
class LocalWorker;
class ExtWorkerConfig;
class LocalWorkerConfig;
class HashDataCache;
class HttpContext;
class HttpHandler;
class HttpLogSource;
class HttpListener;
class HttpMime;
class HttpServer;
class HttpVHost;
class RLimits;
class ScriptHandlerMap;
class SSLContext;
class XmlNode;
class XmlNodeList;

//#ifdef PATH_MAX
//    #define MAX_PATH_LEN          PATH_MAX
//#else
//    #define MAX_PATH_LEN          4096
//#endif
#define MAX_PATH_LEN                512


class HttpServerBuilder {
private:

    static XmlNode* parseFile(const char* configFilePath, const char* rootTag);
    int getRootPath( const char *&pRoot, const char *&pFile );
    int getAbsoluteFile(char* dest, const char* file);
    int getAbsolutePath(char* dest, const char* path);
    int getLogFilePath( char * pBuf, const XmlNode * pNode );
    int getValidFile(char * dest, const char * file, const char * desc );
    int getValidPath(char * dest, const char * path, const char * desc );
    int getValidChrootPath(char * dest, const char * path, const char * desc );
    int expandDomainNames( const char *pDomainNames, 
                    char * achDomains, int len, char dilemma = ',' );
    int expandVariable( const char * pValue, char * pBuf, int bufLen,
                        int allVariable = 0 );
    char * getExpandedTag( const XmlNode * pNode,
                    const char * pName, char *pBuf, int bufLen );

    // virtual host
    int configChroot();
    int initGroups();
    void enableCoreDump();
    int startListeners();
    int initMultiplexer();
    
    int configVHBasics(HttpVHost* pVHost, const XmlNode* pVhConfNode);
    int configVHContextList( HttpVHost* pVHost, const XmlNode* pVhConfNode );
    int configVHScriptHandler( HttpVHost* pVHost, const XmlNode* pVhConfNode );
    int configVHSecurity( HttpVHost* pVHost, const XmlNode* pVhConfNode );
    int configVHIndexFile( HttpVHost* pVHost, const XmlNode* pVhConfNode );
    void configVHChrootMode( HttpVHost * pVHost, const XmlNode * pNode );

    const HttpHandler * getHandler( const HttpVHost * pVHost,
                        const char * pType, const char * pHandler );
    const HttpHandler * getHandler( const HttpVHost * pVHost, const XmlNode * pNode );
    int setContextExtAuthorizer( HttpVHost * pVHost, HttpContext * pContext,
                        const XmlNode * pContextNode );
    
    //1 is for old XML format and 2 is for plain text conf
    int configScriptHandler1( HttpVHost * pVHost, const XmlNodeList* pList,
                             HttpMime * pHttpMime );
    int configScriptHandler2( HttpVHost * pVHost, const XmlNodeList* pList,
                             HttpMime * pHttpMime );

    int configContextRewriteRule( HttpVHost *pVHost,
                        HttpContext * pContext, char * pRule );
    int configContextRewriteRule( HttpVHost *pVHost,
                        HttpContext * pContext, const XmlNode *pRewriteNode);
    
    int addLoadBalancer(  const XmlNode* pNode, const HttpVHost * pVHost );
    
    int configRailsRunner( char * pRunnerCmd, int cmdLen, const char * pRubyBin );
    int loadRailsDefault( const XmlNode * pNode );
    HttpContext * configRailsContext( const XmlNode * pNode,
            HttpVHost * pVHost, const char * contextUri, const char* appPath );
    HttpContext * configRailsContext(
            HttpVHost * pVHost, const char * contextUri,
            const char* appPath, int maxConns, const char * pRailsEnv,
            int maxIdle, const Env * pEnv, const char * pRubyPath );

    
    int initAwstats( HttpVHost * pVHost, const XmlNode *pNode );
    int initRewrite( HttpVHost *pVHost, const XmlNode * pNode );

    int checkDeniedSubDirs( HttpVHost * pVHost,
            const char * pUri, const char *pLocation );
    HttpContext * configContext( HttpVHost * pVHost, const char * pUri, int type,
                const char * pLocation, const char * pHandler,
                int allowBrowse );
    int configContext( HttpVHost * pVHost, const XmlNode* pContextNode );
    int configContextMime( HttpContext * pContext,
                    const XmlNode * pContextNode );
    int configCustomErrorPages( HttpContext* pContext, const XmlNode* pNode );
    int configVhost( HttpVHost* pVHost, const XmlNode* pVhRoot);
    
    HttpVHost * configVHost( const XmlNode* pNode, const char * pName,
                            const char * pDoamin, const char * pAliases,
                            const char * pVhRoot,
                            const XmlNode * pConfigNode );
    HttpVHost * configVHost( XmlNode* pNode );
    int initVHosts(  const XmlNode* pRoot );

    int configVHTemplate( const XmlNode* pNode );
    int initVHTemplates( const XmlNode* pRoot );

    
    int initRealmList( HttpVHost * pVHost, const XmlNode * pRoot );
    int configRealm( HttpVHost * pVHost,
        const char * pName, const XmlNode * pRealmNode );
    int initHotlinkCtrl( HttpVHost *pVHost, const XmlNode * pNode );

    int setAuthCache( const XmlNode * pNode, HashDataCache* pAuth );

    int addListener(  const XmlNode* pNode, int isAdmin );
    int initListeners(  const XmlNode* pRoot, int isAdmin );

    int initVirtualHostMapping(
        HttpListener* pListner, const XmlNode* pNode , const char * pVHostName);
    int addVirtualHostMapping(
        HttpListener* pListner, const char *value , const char * pVHostName);
    
    //This function just used for old version XML format
    int addVirtualHostMapping(
        HttpListener* pListner, const XmlNode* pNode , const char * pVHostName);
    
    int initListenerVHostMap(  const XmlNode* pNode , const char * pVHostName);
    void initExtAppConfig( const XmlNode * pNode, ExtWorkerConfig * pConfig, int autoStart );
    int addExtProcessor(  const XmlNode* pNode, const HttpVHost* pVHost );
    int initExtProcessors(  const XmlNode* pRoot, const HttpVHost * pVHost );

    int initTuning(  const XmlNode* pRoot );

    int initAccessLog( HttpLogSource& logSource, const XmlNode* pRoot,
                      int setDebugLevel );
    int initAccessLog( HttpLogSource& server, const XmlNode* pNode,
                      long * pRollingSize );
    int initErrorLog2( HttpLogSource& server, const XmlNode* pNode,
                        int setDebugLevel );
    int initErrorLog( HttpLogSource& source, const XmlNode * pRoot,
                        int setDebugLevel );

    int initMime(  const XmlNode* pRoot );
    int configCgid( CgidWorker * pWorker, const XmlNode * pNode1 );
    int initSecurity(  const XmlNode* pRoot );
    int initAccessDeniedDir( const XmlNode* pNode );
    int initServerBasic2(  const XmlNode* pRoot );
    int reconfigVHost( const char * pVHostName, HttpVHost * &pVHost );

    int denyAccessFiles( HttpVHost * pVHost, const char * pFile, int regex );
    
    int offsetChroot( char * dest, const char *path );
    struct passwd * getUGid( const char * pUser, const char * pGroup,
                                                gid_t &gid );
    
    HttpServer    * m_pServer;
    HttpVHost     * m_pCurVHost;
    AutoStr2        m_sVhName;
    char            m_achVhRoot[MAX_PATH_LEN];
    AutoStr2        m_vhDomain;
    AutoStr2        m_vhAliases;
    XmlNode       * m_pRoot;

    AutoStr         m_sServerName;
    AutoStr         m_sServerRoot;
    AutoStr         m_sConfigFilePath;
    AutoStr         m_sPlainconfPath;
    AutoStr         m_sAdminEmails;
    AutoStr2        m_sChroot;
    AutoStr2        m_sIPv4;
    AutoStr         m_sAutoIndexURI;
    AutoStr         m_sDefaultLogFormat;
    
    int             m_iCrashGuard;
    int             m_enableCoreDump;
    int             m_iEnableExpire;
    Env           * m_pAdminEnv;
    RLimits       * m_pRLimits;
    gid_t           m_pri_gid;
    AutoStr         m_sUser;
    AutoStr         m_sGroup;
    //DeniedDir       m_deniedDir;
    AutoStr         m_gdbPath;
    AutoStr         m_sRailsRunner;
    int             m_iRailsEnv;
    LocalWorkerConfig * m_railsDefault;
    int             m_iSuPHPMaxConn;
   
public:
    HttpServerBuilder(HttpServer * pServer )
        : m_pServer( pServer )
        , m_pCurVHost( NULL )
        , m_vhDomain( "" )
        , m_vhAliases( "" )
        , m_pRoot(NULL)
        , m_sAutoIndexURI( "/_autoindex/default.php" )
        , m_iCrashGuard( 2 )
        , m_enableCoreDump( 0 )
        , m_pAdminEnv( NULL)
        , m_pRLimits( NULL )
        , m_pri_gid( 0 )
        , m_sUser( "nobody" )
        , m_sGroup( "nobody" )
        , m_iRailsEnv( 1 )
        , m_sPlainconfPath ( "" )
        {};

    ~HttpServerBuilder();
    int getAbsolute( char * dest, const char * path, int pathOnly );
    void releaseConfigXmlTree();
    int initServer( int reconfig = 0 );
    int loadConfigFile( const char * pConfigFile = NULL);
    int loadPlainConfigFile();
    int configServerBasics( int reconfig = 0);
    int configServer( int reconfig = 0);
    void reconfigVHost( char * pVHostName );
    int reconfigVHostMappings( const char * pVHostName );
    HttpContext * importWebApp( HttpVHost * pVHost, const char * contextUri,
                        const char* appPath, const char * pWorkerName,
                        int allowBrowse );
    int initAdmin(  const XmlNode * pNode );
    int loadAdminConfig( XmlNode* pRoot );

    void setConfigFilePath(const char * pConfig)
    {   m_sConfigFilePath = pConfig;      }
    void setPlainConfigFilePath(const char * pConfig)
    {   m_sPlainconfPath = pConfig;      }
    
    const char * getServerRoot() const      {   return m_sServerRoot.c_str();   }
    void setServerRoot( const char * pRoot );

    const char * getAdminEmails() const     {   return m_sAdminEmails.c_str();  }
    void setAdminEmails( const char * pEmails)
    {   m_sAdminEmails = pEmails;   }

    void setUser( const char * pUser )      { m_sUser = pUser;  }
    void setGroup( const char * pGroup )    { m_sGroup = pGroup;}
    void setServerName( const char * pServerName )    { m_sServerName = pServerName;}

    const char * getUser() const        {   return m_sUser.c_str();         }
    const char * getGroup() const       {   return m_sGroup.c_str();        }
    const char * getServerName() const  {   return m_sServerName.c_str();   }
    const char * getChroot() const      {   return m_sChroot.c_str();       }
    const AutoStr2 * getIPv4() const    {   return &m_sIPv4;        }
    
    int  getCrashGuard() const          {   return m_iCrashGuard;   }
    void setCrashGuard( int guard )     {   m_iCrashGuard = guard;  }

    int changeUserChroot();

    const char * getGDBPath() const     {   return m_gdbPath.c_str();   }
    Env * getAdminEnv() const           {   return m_pAdminEnv;         }
    SSLContext * newSSLContext( const XmlNode* pNode );

    LocalWorker * configRailsApp(
            HttpVHost * pVHost, const char * pAppName, const char* appPath,
            int maxConns, const char * pRailsEnv, int maxIdle, const Env * pEnv,
            int runOnStart, const char * pRubyPath = NULL );
    HttpContext * addRailsContext( HttpVHost * pVHost, const char * pURI, const char * pLocation,
                    LocalWorker * pApp );

#ifdef RUN_TEST    
    int testDomainMod();
#endif

};

extern HttpContext * addRailsContext( HttpVHost * pVHost, const char * pURI, const char * pLocation,
                    LocalWorker * pApp );

#endif
