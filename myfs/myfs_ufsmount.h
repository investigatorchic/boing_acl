/*-
 * Copyright (c) 1982, 1986, 1989, 1993
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
 *	@(#)ufsmount.h	8.6 (Berkeley) 3/30/95
 * $FreeBSD: src/sys/ufs/ufs/ufsmount.h,v 1.39.2.1.2.1 2009/10/25 01:10:29 kensmith Exp $
 */

#ifndef _MYFS_UFS_UFSMOUNT_H_
#define _MYFS_UFS_UFSMOUNT_H_

#include <sys/buf.h>	/* XXX For struct workhead. */

/*
 * Arguments to mount MYFS-based filesystems
 */
struct myfs_ufs_args {
	char	*fspec;			/* block special device to mount */
	struct	export_args export;	/* network export information */
};

#ifdef _KERNEL

#ifdef MALLOC_DECLARE
MALLOC_DECLARE(M_MYFSMNT);
#endif

struct buf;
struct myfs_inode;
struct nameidata;
struct timeval;
struct ucred;
struct uio;
struct vnode;
struct myfs_ufs_extattr_per_mount;

/* This structure describes the MYFS specific mount structure data. */
struct myfs_ufsmount {
	struct	mount *um_mountp;		/* filesystem vfs structure */
	struct	cdev *um_dev;			/* device mounted */
	struct	g_consumer *um_cp;
	struct	bufobj *um_bo;			/* Buffer cache object */
	struct	vnode *um_devvp;		/* block device mounted vnode */
	u_long	um_fstype;			/* type of filesystem */
	struct	myfs_fs *um_fs;			/* pointer to superblock */
	struct	myfs_ufs_extattr_per_mount um_extattr;	/* extended attrs */
	u_long	um_nindir;			/* indirect ptrs per block */
	u_long	um_bptrtodb;			/* indir ptr to disk block */
	u_long	um_seqinc;			/* inc between seq blocks */
	struct	mtx um_lock;			/* Protects ufsmount & fs */
	long	um_numindirdeps;		/* outstanding indirdeps */
	struct	workhead softdep_workitem_pending; /* softdep work queue */
	struct	worklist *softdep_worklist_tail; /* Tail pointer for above */
	int	softdep_on_worklist;		/* Items on the worklist */
	int	softdep_on_worklist_inprogress;	/* Busy items on worklist */
	int	softdep_deps;			/* Total dependency count */
	int	softdep_accdeps;		/* accumulated dep count */
	int	softdep_req;			/* Wakeup when deps hits 0. */
	struct	vnode *um_quotas[MYFS_MAXQUOTAS];	/* pointer to quota files */
	struct	ucred *um_cred[MYFS_MAXQUOTAS];	/* quota file access cred */
	time_t	um_btime[MYFS_MAXQUOTAS];		/* block quota time limit */
	time_t	um_itime[MYFS_MAXQUOTAS];		/* inode quota time limit */
	char	um_qflags[MYFS_MAXQUOTAS];		/* quota specific flags */
	int64_t	um_savedmaxfilesize;		/* XXX - limit maxfilesize */
	int	(*um_balloc)(struct vnode *, off_t, int, struct ucred *, int, struct buf **);
	int	(*um_blkatoff)(struct vnode *, off_t, char **, struct buf **);
	int	(*um_truncate)(struct vnode *, off_t, int, struct ucred *, struct thread *);
	int	(*um_update)(struct vnode *, int);
	int	(*um_valloc)(struct vnode *, int, struct ucred *, struct vnode **);
	int	(*um_vfree)(struct vnode *, ino_t, int);
	void	(*um_ifree)(struct myfs_ufsmount *, struct myfs_inode *);
	int	(*um_rdonly)(struct myfs_inode *);
};

#define MYFS_BALLOC(aa, bb, cc, dd, ee, ff) VFSTOMYFS((aa)->v_mount)->um_balloc(aa, bb, cc, dd, ee, ff)
#define MYFS_BLKATOFF(aa, bb, cc, dd) VFSTOMYFS((aa)->v_mount)->um_blkatoff(aa, bb, cc, dd)
#define MYFS_TRUNCATE(aa, bb, cc, dd, ee) VFSTOMYFS((aa)->v_mount)->um_truncate(aa, bb, cc, dd, ee)
#define MYFS_UPDATE(aa, bb) VFSTOMYFS((aa)->v_mount)->um_update(aa, bb)
#define MYFS_VALLOC(aa, bb, cc, dd) VFSTOMYFS((aa)->v_mount)->um_valloc(aa, bb, cc, dd)
#define MYFS_VFREE(aa, bb, cc) VFSTOMYFS((aa)->v_mount)->um_vfree(aa, bb, cc)
#define MYFS_IFREE(aa, bb) ((aa)->um_ifree(aa, bb))
#define	MYFS_RDONLY(aa) ((aa)->i_ump->um_rdonly(aa))

#define	MYFS_LOCK(aa)	mtx_lock(&(aa)->um_lock)
#define	MYFS_UNLOCK(aa)	mtx_unlock(&(aa)->um_lock)
#define	MYFS_MTX(aa)	(&(aa)->um_lock)

/*
 * Filesystem types
 */
#define MYFS1	1
#define MYFS2	2

/*
 * Flags describing the state of quotas.
 */
#define	QTF_OPENING	0x01			/* Q_QUOTAON in progress */
#define	QTF_CLOSING	0x02			/* Q_QUOTAOFF in progress */

/* Convert mount ptr to ufsmount ptr. */
#define VFSTOMYFS(mp)	((struct myfs_ufsmount *)((mp)->mnt_data))
#define	MYFSTOVFS(ump)	(ump)->um_mountp

/*
 * Macros to access filesystem parameters in the ufsmount structure.
 * Used by myfs_ufs_bmap.
 */
#define MYFS_MNINDIR(ump)			((ump)->um_nindir)
#define	blkptrtodb(ump, b)		((b) << (ump)->um_bptrtodb)
#define	is_sequential(ump, a, b)	((b) == (a) + ump->um_seqinc)
#endif /* _KERNEL */

#endif
