/*-
 * Copyright (c) 1991, 1993, 1994
 *	The Regents of the University of California.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 4. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 *	@(#)ufs_extern.h	8.10 (Berkeley) 5/14/95
 * $FreeBSD: src/sys/ufs/ufs/ufs_extern.h,v 1.57.2.1.2.1 2009/10/25 01:10:29 kensmith Exp $
 */

#ifndef _MYFS_UFS_EXTERN_H_
#define	_MYFS_UFS_EXTERN_H_

struct componentname;
struct myfs_direct;
struct myfs_indir;
struct myfs_inode;
struct mount;
struct thread;
struct sockaddr;
struct ucred;
struct myfs_ufid;
struct vfsconf;
struct vnode;
struct vop_bmap_args;
struct vop_cachedlookup_args;
struct vop_generic_args;
struct vop_inactive_args;
struct vop_reclaim_args;

extern struct vop_vector myfs_ufs_fifoops;
extern struct vop_vector myfs_ufs_vnodeops;

int	 myfs_ufs_bmap(struct vop_bmap_args *);
int	 myfs_ufs_bmaparray(struct vnode *, myfs_ufs2_daddr_t, myfs_ufs2_daddr_t *,
	    struct buf *, int *, int *);
int	 myfs_ufs_fhtovp(struct mount *, struct myfs_ufid *, struct vnode **);
int	 myfs_ufs_checkpath(ino_t, struct myfs_inode *, struct ucred *);
void	 myfs_ufs_dirbad(struct myfs_inode *, myfs_doff_t, char *);
int	 myfs_ufs_dirbadentry(struct vnode *, struct myfs_direct *, int);
int	 myfs_ufs_dirempty(struct myfs_inode *, ino_t, struct ucred *);
void	 myfs_ufs_makedirentry(struct myfs_inode *, struct componentname *,
	    struct myfs_direct *);
int	 myfs_ufs_direnter(struct vnode *, struct vnode *, struct myfs_direct *,
	    struct componentname *, struct buf *);
int	 myfs_ufs_dirremove(struct vnode *, struct myfs_inode *, int, int);
int	 myfs_ufs_dirrewrite(struct myfs_inode *, struct myfs_inode *, ino_t, int, int);
int	 myfs_ufs_getlbns(struct vnode *, myfs_ufs2_daddr_t, struct myfs_indir *, int *);
int	 myfs_ufs_inactive(struct vop_inactive_args *);
int	 myfs_ufs_init(struct vfsconf *);
void	 myfs_ufs_itimes(struct vnode *vp);
int	 myfs_ufs_lookup(struct vop_cachedlookup_args *);
int	 myfs_ufs_readdir(struct vop_readdir_args *);
int	 myfs_ufs_reclaim(struct vop_reclaim_args *);
void	 myfs_ffs_snapgone(struct myfs_inode *);
vfs_root_t myfs_ufs_root;
int	 myfs_ufs_uninit(struct vfsconf *);
int	 myfs_ufs_vinit(struct mount *, struct vop_vector *, struct vnode **);

/*
 * Soft update function prototypes.
 */
int	myfs_softdep_setup_directory_add(struct buf *, struct myfs_inode *, off_t,
	    ino_t, struct buf *, int);
void	myfs_softdep_change_directoryentry_offset(struct myfs_inode *, caddr_t,
	    caddr_t, caddr_t, int);
void	myfs_softdep_setup_remove(struct buf *,struct myfs_inode *, struct myfs_inode *, int);
void	myfs_softdep_setup_directory_change(struct buf *, struct myfs_inode *,
	    struct myfs_inode *, ino_t, int);
void	myfs_softdep_change_linkcnt(struct myfs_inode *);
void	myfs_softdep_releasefile(struct myfs_inode *);
int	myfs_softdep_slowdown(struct vnode *);

/*
 * Flags to low-level allocation routines.  The low 16-bits are reserved
 * for IO_ flags from vnode.h.
 *
 * Note: The general vfs code typically limits the sequential heuristic
 * count to 127.  See sequential_heuristic() in kern/vfs_vnops.c
 */
#define MYFS_BA_CLRBUF	0x00010000	/* Clear invalid areas of buffer. */
#define MYFS_BA_METAONLY	0x00020000	/* Return indirect block buffer. */
#define MYFS_BA_SEQMASK	0x7F000000	/* Bits holding seq heuristic. */
#define MYFS_BA_SEQSHIFT	24
#define MYFS_BA_SEQMAX	0x7F

#endif /* !_MYFS_UFS_EXTERN_H_ */
