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
#ifndef FDPASS_H
#define FDPASS_H



class FDPass
{
public: 
	FDPass();
	~FDPass();
    static int read_fd(int fd, void *ptr, int nbytes, int *recvfd);
    static int write_fd(int fd, void *ptr, int nbytes, int sendfd);
    static int send_fd(int fd, int fdSend, void *ptr, int nbytes);
    static int recv_fd(int fd, void *ptr, int nbytes );
    
};

#endif
