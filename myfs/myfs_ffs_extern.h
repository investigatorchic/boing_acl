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
 *	@(#)ffs_extern.h	8.6 (Berkeley) 3/30/95
 * $FreeBSD: src/sys/ufs/ffs/ffs_extern.h,v 1.78.2.1.2.1 2009/10/25 01:10:29 kensmith Exp $
 */

#ifndef _MYFS_FFS_EXTERN_H
#define	_MYFS_FFS_EXTERN_H

struct buf;
struct myfs_cg;
struct fid;
struct myfs_fs;
struct myfs_inode;
struct malloc_type;
struct mount;
struct thread;
struct sockaddr;
struct statfs;
struct ucred;
struct vnode;
struct vop_fsync_args;
struct vop_reallocblks_args;

int	myfs_ffs_alloc(struct myfs_inode *, myfs_ufs2_daddr_t, myfs_ufs2_daddr_t, int, int,
	    struct ucred *, myfs_ufs2_daddr_t *);
int	myfs_ffs_balloc_ufs1(struct vnode *a_vp, off_t a_startoffset, int a_size,
            struct ucred *a_cred, int a_flags, struct buf **a_bpp);
int	myfs_ffs_balloc_ufs2(struct vnode *a_vp, off_t a_startoffset, int a_size,
            struct ucred *a_cred, int a_flags, struct buf **a_bpp);
int	myfs_ffs_blkatoff(struct vnode *, off_t, char **, struct buf **);
void	myfs_ffs_blkfree(struct myfs_ufsmount *, struct myfs_fs *, struct vnode *,
	    myfs_ufs2_daddr_t, long, ino_t);
myfs_ufs2_daddr_t myfs_ffs_blkpref_ufs1(struct myfs_inode *, myfs_ufs_lbn_t, int, myfs_ufs1_daddr_t *);
myfs_ufs2_daddr_t myfs_ffs_blkpref_ufs2(struct myfs_inode *, myfs_ufs_lbn_t, int, myfs_ufs2_daddr_t *);
int	myfs_ffs_checkfreefile(struct myfs_fs *, struct vnode *, ino_t);
void	myfs_ffs_clrblock(struct myfs_fs *, u_char *, myfs_ufs1_daddr_t);
void	myfs_ffs_bdflush(struct bufobj *, struct buf *);
int	myfs_ffs_copyonwrite(struct vnode *, struct buf *);
int	myfs_ffs_flushfiles(struct mount *, int, struct thread *);
void	myfs_ffs_fragacct(struct myfs_fs *, int, int32_t [], int);
int	myfs_ffs_freefile(struct myfs_ufsmount *, struct myfs_fs *, struct vnode *, ino_t,
	    int);
int	myfs_ffs_isblock(struct myfs_fs *, u_char *, myfs_ufs1_daddr_t);
void	myfs_ffs_load_inode(struct buf *, struct myfs_inode *, struct myfs_fs *, ino_t);
int	myfs_ffs_mountroot(void);
int	myfs_ffs_reallocblks(struct vop_reallocblks_args *);
int	myfs_ffs_realloccg(struct myfs_inode *, myfs_ufs2_daddr_t, myfs_ufs2_daddr_t,
	    myfs_ufs2_daddr_t, int, int, int, struct ucred *, struct buf **);
int	myfs_ffs_sbupdate(struct myfs_ufsmount *, int, int);
void	myfs_ffs_setblock(struct myfs_fs *, u_char *, myfs_ufs1_daddr_t);
int	myfs_ffs_snapblkfree(struct myfs_fs *, struct vnode *, myfs_ufs2_daddr_t, long, ino_t);
void	myfs_ffs_snapremove(struct vnode *vp);
int	myfs_ffs_snapshot(struct mount *mp, char *snapfile);
void	myfs_ffs_snapshot_mount(struct mount *mp);
void	myfs_ffs_snapshot_unmount(struct mount *mp);
void	myfs_process_deferred_inactive(struct mount *mp);
int	myfs_ffs_syncvnode(struct vnode *vp, int waitfor);
int	myfs_ffs_truncate(struct vnode *, off_t, int, struct ucred *, struct thread *);
int	myfs_ffs_update(struct vnode *, int);
int	myfs_ffs_valloc(struct vnode *, int, struct ucred *, struct vnode **);

int	myfs_ffs_vfree(struct vnode *, ino_t, int);
vfs_vget_t myfs_ffs_vget;
int	myfs_ffs_vgetf(struct mount *, ino_t, int, struct vnode **, int);

#define	MYFS_FFSV_FORCEINSMQ	0x0001

extern struct vop_vector myfs_ffs_vnodeops1;
extern struct vop_vector myfs_ffs_fifoops1;
extern struct vop_vector myfs_ffs_vnodeops2;
extern struct vop_vector myfs_ffs_fifoops2;

/*
 * Soft update function prototypes.
 */

int	myfs_softdep_check_suspend(struct mount *, struct vnode *,
	  int, int, int, int);
void	myfs_softdep_get_depcounts(struct mount *, int *, int *);
void	myfs_softdep_initialize(void);
void	myfs_softdep_uninitialize(void);
int	myfs_softdep_mount(struct vnode *, struct mount *, struct myfs_fs *,
	    struct ucred *);
int	myfs_softdep_flushworklist(struct mount *, int *, struct thread *);
int	myfs_softdep_flushfiles(struct mount *, int, struct thread *);
void	myfs_softdep_update_inodeblock(struct myfs_inode *, struct buf *, int);
void	myfs_softdep_load_inodeblock(struct myfs_inode *);
void	myfs_softdep_freefile(struct vnode *, ino_t, int);
int	myfs_softdep_request_cleanup(struct myfs_fs *, struct vnode *);
void	myfs_softdep_setup_freeblocks(struct myfs_inode *, off_t, int);
void	myfs_softdep_setup_inomapdep(struct buf *, struct myfs_inode *, ino_t);
void	myfs_softdep_setup_blkmapdep(struct buf *, struct mount *, myfs_ufs2_daddr_t);
void	myfs_softdep_setup_allocdirect(struct myfs_inode *, myfs_ufs_lbn_t, myfs_ufs2_daddr_t,
	    myfs_ufs2_daddr_t, long, long, struct buf *);
void	myfs_softdep_setup_allocext(struct myfs_inode *, myfs_ufs_lbn_t, myfs_ufs2_daddr_t,
	    myfs_ufs2_daddr_t, long, long, struct buf *);
void	myfs_softdep_setup_allocindir_meta(struct buf *, struct myfs_inode *,
	    struct buf *, int, myfs_ufs2_daddr_t);
void	myfs_softdep_setup_allocindir_page(struct myfs_inode *, myfs_ufs_lbn_t,
	    struct buf *, int, myfs_ufs2_daddr_t, myfs_ufs2_daddr_t, struct buf *);
void	myfs_softdep_fsync_mountdev(struct vnode *);
int	myfs_softdep_sync_metadata(struct vnode *);
int     myfs_softdep_fsync(struct vnode *);

int	myfs_ffs_rdonly(struct myfs_inode *);

#endif /* !_MYFS_FFS_EXTERN_H */
