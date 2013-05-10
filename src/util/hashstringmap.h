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
#ifndef HASHSTRINGMAP_H
#define HASHSTRINGMAP_H



#include <util/ghash.h>
#include <util/autostr.h>

template< class T >
class HashStringMap
    : public GHash
{
public:
    class iterator
    {
        GHash::iterator m_iter;
    public:
        iterator() : m_iter( NULL )
        {}

        iterator( GHash::iterator iter ) : m_iter( iter )
        {}
        iterator( GHash::const_iterator iter )
            : m_iter( (GHash::iterator)iter )
        {}

        iterator( const iterator& rhs ) : m_iter( rhs.m_iter )
        {}

        const char * first() const
        {  return (const char *)( m_iter->first() );   }

        T second() const
        {   return (T)( m_iter->second() );   }

        operator GHash::iterator ()
        {   return m_iter;  }

    };
    typedef iterator const_iterator;
    
    HashStringMap( int initsize = 29, 
                GHash::hash_fn hf = GHash::hash_string,
                GHash::val_comp vc = GHash::comp_string )
        : GHash( initsize, hf, vc )
        {};
    ~HashStringMap() {};

    iterator insert( const char * pKey, const T& val )
    {
        return GHash::insert( pKey, val );
    }

    iterator update( const char * pKey, const T& val )
    {
        return GHash::update( pKey, val );
    }

    iterator remove( const char * pKey )
    {
        iterator iter = GHash::find( pKey );
        if ( iter != end() )
        {
            GHash::erase( iter );
        }
        return iter;
    }

    static int deleteObj( GHash::iterator iter )
    {
        delete (T)(iter->second());
        return 0;
    }

    void release_objects()
    {
        GHash::for_each( begin(), end(), deleteObj );
        GHash::clear();
    }

};

class StrStr
{
public:
    AutoStr str1;
    AutoStr str2;
};

class StrStrHashMap : public HashStringMap<StrStr *>
{
public:
    StrStrHashMap( int initsize = 29, GHash::hash_fn hf= GHash::hash_string,
                    GHash::val_comp vc = GHash::comp_string)
        : HashStringMap<StrStr *>( initsize, hf, vc )
        {};
    ~StrStrHashMap() {  release_objects();   };
    iterator insert_update( const char * pKey, const char * pValue )
    {
        iterator iter = find( pKey );
        if (iter != end() )
        {
            iter.second()->str2.setStr( pValue );
            return iter;
        }
        else
        {
            StrStr * pStr = new StrStr();
            pStr->str1.setStr( pKey );
            pStr->str2.setStr( pValue );
            return insert( pStr->str1.c_str(), pStr );
        }
    }


};


#endif
