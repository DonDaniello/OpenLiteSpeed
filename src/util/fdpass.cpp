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
#include <util/fdpass.h>

#include <stddef.h>
#include <sys/types.h>
#include <sys/socket.h> 
#include <sys/uio.h>

#ifndef __CMSG_ALIGN
#define __CMSG_ALIGN(p) (((u_int)(p) + sizeof(int) - 1) &~(sizeof(int) - 1))
#endif

/* Length of the contents of a control message of length len */
#ifndef CMSG_LEN
#define CMSG_LEN(len)   (__CMSG_ALIGN(sizeof(struct cmsghdr)) + (len))
#endif

/* Length of the space taken up by a padded control message of
length len */
#ifndef CMSG_SPACE
#define CMSG_SPACE(len) (__CMSG_ALIGN(sizeof(struct cmsghdr)) + __CMSG_ALIGN(len))
#endif

FDPass::FDPass(){
}
FDPass::~FDPass(){
}


int FDPass::read_fd(int fd, void *ptr, int nbytes, int *recvfd)
{
    struct msghdr   msg;
    struct iovec    iov[1];
    int             n;

#if (!defined(sun) && !defined(__sun)) || defined(_XPG4_2) || defined(_KERNEL)
    union {
      struct cmsghdr    cm;
      char                control[__CMSG_ALIGN(sizeof(struct cmsghdr)) + __CMSG_ALIGN(sizeof(int))];
    } control_un;
    struct cmsghdr    *cmptr;

    *recvfd = -1;
    msg.msg_control = control_un.control;
    msg.msg_controllen = sizeof(control_un.control);
#else
    int             newfd;
    msg.msg_accrights = (caddr_t) &newfd;
    msg.msg_accrightslen = sizeof(int);
#endif

    msg.msg_name = NULL;
    msg.msg_namelen = 0;

    iov[0].iov_base = (char *)ptr;
    iov[0].iov_len = nbytes;
    msg.msg_iov = iov;
    msg.msg_iovlen = 1;

    if ( (n = recvmsg(fd, &msg, 0)) <= 0)
        return(n);

#if (!defined(sun) && !defined(__sun)) || defined(_XPG4_2) || defined(_KERNEL)
    if ( (cmptr = CMSG_FIRSTHDR(&msg)) != NULL &&
        cmptr->cmsg_len == CMSG_LEN(sizeof(int))) {
        if (cmptr->cmsg_level != SOL_SOCKET)
            return -1;
        if (cmptr->cmsg_type != SCM_RIGHTS)
            return -2;
        *recvfd = *((int *) CMSG_DATA(cmptr));
    } else
        *recvfd = -1;        /* descriptor was not passed */
#else
 /* *INDENT-OFF* */
    if (msg.msg_accrightslen == sizeof(int))
        *recvfd = newfd;
    else
        *recvfd = -1;        /* descriptor was not passed */
/* *INDENT-ON* */
#endif

    return(n);
}
/* end read_fd */

int FDPass::write_fd(int fd, void *ptr, int nbytes, int sendfd)
{
    struct msghdr    msg;
    struct iovec    iov[1];

#if (!defined(sun) && !defined(__sun)) || defined(_XPG4_2) || defined(_KERNEL)
    union {
      struct cmsghdr    cm;
      char                control[__CMSG_ALIGN(sizeof(struct cmsghdr)) + __CMSG_ALIGN(sizeof(int))];
    } control_un;
    struct cmsghdr    *cmptr;

    msg.msg_control = control_un.control;
    msg.msg_controllen = sizeof(control_un.control);

    cmptr = CMSG_FIRSTHDR(&msg);
    cmptr->cmsg_len = CMSG_LEN(sizeof(int));
    cmptr->cmsg_level = SOL_SOCKET;
    cmptr->cmsg_type = SCM_RIGHTS;
    *((int *) CMSG_DATA(cmptr)) = sendfd;
#else
    msg.msg_accrights = (caddr_t) &sendfd;
    msg.msg_accrightslen = sizeof(int);
#endif

    msg.msg_name = NULL;
    msg.msg_namelen = 0;

    iov[0].iov_base = (char *)ptr;
    iov[0].iov_len = nbytes;
    msg.msg_iov = iov;
    msg.msg_iovlen = 1;

    return(sendmsg(fd, &msg, 0));
}

/* end write_fd */


#if defined(__FreeBSD__)
# include <sys/param.h>
#endif


int
FDPass::send_fd(int sock, int fdSend, void *ptr, int nbytes )
{
    struct msghdr msghdr;
    struct iovec msg_iov;
    struct cmsghdr *cmsg;
    char buffer[ sizeof( struct cmsghdr) + sizeof(int) ];

    msg_iov.iov_base = (char*)ptr;
    msg_iov.iov_len = nbytes;
    msghdr.msg_name = NULL;
    msghdr.msg_namelen = 0;
    msghdr.msg_iov = &msg_iov;
    msghdr.msg_iovlen = 1;
    msghdr.msg_flags = 0;
    msghdr.msg_control = buffer;
    msghdr.msg_controllen = sizeof( buffer );
    cmsg = CMSG_FIRSTHDR(&msghdr);
    cmsg->cmsg_len = msghdr.msg_controllen;
    cmsg->cmsg_level = SOL_SOCKET;
    cmsg->cmsg_type = SCM_RIGHTS;
    *((int *)CMSG_DATA(cmsg)) = fdSend;
    return(sendmsg(sock, &msghdr, 0) >= 0 ? 0 : -1);
}

int
FDPass::recv_fd(int sock, void * ptr, int nbytes )
{
    struct msghdr msghdr;
    struct iovec msg_iov;
    struct cmsghdr *cmsg;
    char buffer[ sizeof( struct cmsghdr) + sizeof(int) ];

    msg_iov.iov_base = (char*)ptr;
    msg_iov.iov_len = nbytes;
    msghdr.msg_name = NULL;
    msghdr.msg_namelen = 0;
    msghdr.msg_iov = &msg_iov;
    msghdr.msg_iovlen = 1;
    msghdr.msg_flags = 0;
    msghdr.msg_control = buffer;
    msghdr.msg_controllen = sizeof( buffer );
    cmsg = CMSG_FIRSTHDR(&msghdr);
    cmsg->cmsg_len = msghdr.msg_controllen;
    cmsg->cmsg_level = SOL_SOCKET;
    cmsg->cmsg_type = SCM_RIGHTS;
    *((int *)CMSG_DATA(cmsg)) = -1;
    
    if(recvmsg(sock, &msghdr, 0) < 0)
        return(-1);
    return *((int *)CMSG_DATA(cmsg));
}

#include <stdio.h>
#include <unistd.h>
int test_fdpass()
{
    int fd = dup( 1 );
    int intercommfds[2];
    int ret;
    socketpair( AF_UNIX, SOCK_STREAM, 0, intercommfds );
    ret = fork();
    if ( ret == 0 )
    {
        //child
        int n = 10;
        close( intercommfds[0] );
        //FDPass::send_fd( intercommfds[1], fd, &n, sizeof( int )  );
        FDPass::write_fd( intercommfds[1], &n, sizeof( int ), fd  );
        _exit( 0 );
    }
    else
    {
        close( intercommfds[1] );
        close( fd );
        int n = 0;
        int fd1;
        int ret = FDPass::read_fd( intercommfds[0], &n, sizeof( int ), &fd1 );
        printf( "recv fd: %d, n: %d\n", fd1, n );
        if ( fd1 != -1 )
            return 0;
    }
    return -1;
}

