/*-
 * Copyright (c) 1991, 1993, 1995
 *	The Regents of the University of California.  All rights reserved.
 * (c) UNIX System Laboratories, Inc.
 * All or some portions of this file are derived from material licensed
 * to the University of California by American Telephone and Telegraph
 * Co. or Unix System Laboratories, Inc. and are reproduced herein with
 * the permission of UNIX System Laboratories, Inc.
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
 *	@(#)ufs_inode.c	8.9 (Berkeley) 5/14/95
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD: src/sys/ufs/ufs/ufs_inode.c,v 1.70.2.1.2.1 2009/10/25 01:10:29 kensmith Exp $");

#include "opt_quota.h"
#include "opt_ufs.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/vnode.h>
#include <sys/lock.h>
#include <sys/mount.h>
#include <sys/malloc.h>
#include <sys/mutex.h>

#include <local/myfs/myfs_extattr.h>
#include <local/myfs/myfs_quota.h>
#include <local/myfs/myfs_inode.h>
#include <local/myfs/myfs_ufsmount.h>
#include <local/myfs/myfs_ufs_extern.h>
#ifdef MYFS_DIRHASH
#include <local/myfs/myfs_dir.h>
#include <local/myfs/myfs_dirhash.h>
#endif
#ifdef MYFS_GJOURNAL
#include <local/myfs/myfs_gjournal.h>
#endif

/*
 * Last reference to an inode.  If necessary, write or delete it.
 */
int
myfs_ufs_inactive(ap)
	struct vop_inactive_args /* {
		struct vnode *a_vp;
		struct thread *a_td;
	} */ *ap;
{
	struct vnode *vp = ap->a_vp;
	struct myfs_inode *ip = MYFS_VTOI(vp);
	struct thread *td = ap->a_td;
	mode_t mode;
	int error = 0;
	struct mount *mp;

	mp = NULL;
	/*
	 * Ignore inodes related to stale file handles.
	 */
	if (ip->i_mode == 0)
		goto out;
#ifdef MYFS_GJOURNAL
	myfs_ufs_gjournal_close(vp);
#endif
	if ((ip->i_effnlink == 0 && MYFS_DOINGSOFTDEP(vp)) ||
	    (ip->i_nlink <= 0 && !MYFS_RDONLY(ip))) {
	loop:
		if (vn_start_secondary_write(vp, &mp, V_NOWAIT) != 0) {
			/* Cannot delete file while file system is suspended */
			if ((vp->v_iflag & VI_DOOMED) != 0) {
				/* Cannot return before file is deleted */
				(void) vn_start_secondary_write(vp, &mp,
								V_WAIT);
			} else {
				MNT_ILOCK(mp);
				if ((mp->mnt_kern_flag &
				     (MNTK_SUSPEND2 | MNTK_SUSPENDED)) == 0) {
					MNT_IUNLOCK(mp);
					goto loop;
				}
				/*
				 * Fail to inactivate vnode now and
				 * let myfs_ffs_snapshot() clean up after
				 * it has resumed the file system.
				 */
				VI_LOCK(vp);
				vp->v_iflag |= VI_OWEINACT;
				VI_UNLOCK(vp);
				MNT_IUNLOCK(mp);
				return (0);
			}
		}
	}
	if (ip->i_effnlink == 0 && MYFS_DOINGSOFTDEP(vp))
		myfs_softdep_releasefile(ip);
	if (ip->i_nlink <= 0 && !MYFS_RDONLY(ip)) {
#ifdef QUOTA
		if (!myfs_getinoquota(ip))
			(void)myfs_chkiq(ip, -1, NOCRED, FORCE);
#endif
#ifdef MYFS_EXTATTR
		myfs_ufs_extattr_vnode_inactive(vp, td);
#endif
		error = MYFS_TRUNCATE(vp, (off_t)0, IO_EXT | IO_NORMAL,
		    NOCRED, td);
		/*
		 * Setting the mode to zero needs to wait for the inode
		 * to be written just as does a change to the link count.
		 * So, rather than creating a new entry point to do the
		 * same thing, we just use myfs_softdep_change_linkcnt().
		 */
		MYFS_DIP_SET(ip, i_rdev, 0);
		mode = ip->i_mode;
		ip->i_mode = 0;
		MYFS_DIP_SET(ip, i_mode, 0);
		ip->i_flag |= IN_CHANGE | IN_UPDATE;
		if (MYFS_DOINGSOFTDEP(vp))
			myfs_softdep_change_linkcnt(ip);
		MYFS_VFREE(vp, ip->i_number, mode);
	}
	if (ip->i_flag & (IN_ACCESS | IN_CHANGE | IN_MODIFIED | IN_UPDATE)) {
		if ((ip->i_flag & (IN_CHANGE | IN_UPDATE | IN_MODIFIED)) == 0 &&
		    mp == NULL &&
		    vn_start_secondary_write(vp, &mp, V_NOWAIT)) {
			mp = NULL;
			ip->i_flag &= ~IN_ACCESS;
		} else {
			if (mp == NULL)
				(void) vn_start_secondary_write(vp, &mp,
								V_WAIT);
			MYFS_UPDATE(vp, 0);
		}
	}
out:
	/*
	 * If we are done with the inode, reclaim it
	 * so that it can be reused immediately.
	 */
	if (ip->i_mode == 0)
		vrecycle(vp, td);
	if (mp != NULL)
		vn_finished_secondary_write(mp);
	return (error);
}

/*
 * Reclaim an inode so that it can be used for other purposes.
 */
int
myfs_ufs_reclaim(ap)
	struct vop_reclaim_args /* {
		struct vnode *a_vp;
		struct thread *a_td;
	} */ *ap;
{
	struct vnode *vp = ap->a_vp;
	struct myfs_inode *ip = MYFS_VTOI(vp);
	struct myfs_ufsmount *ump = ip->i_ump;
#ifdef QUOTA
	int i;
#endif

	/*
	 * Destroy the vm object and flush associated pages.
	 */
	vnode_destroy_vobject(vp);
	if (ip->i_flag & IN_LAZYMOD)
		ip->i_flag |= IN_MODIFIED;
	MYFS_UPDATE(vp, 0);
	/*
	 * Remove the inode from its hash chain.
	 */
	vfs_hash_remove(vp);
	/*
	 * Purge old data structures associated with the inode.
	 */
#ifdef QUOTA
	for (i = 0; i < MYFS_MAXQUOTAS; i++) {
		if (ip->i_dquot[i] != MYFS_NODQUOT) {
			myfs_dqrele(vp, ip->i_dquot[i]);
			ip->i_dquot[i] = MYFS_NODQUOT;
		}
	}
#endif
#ifdef MYFS_DIRHASH
	if (ip->i_dirhash != NULL)
		myfs_ufsdirhash_free(ip);
#endif
	/*
	 * Lock the clearing of v_data so ffs_lock() can inspect it
	 * prior to obtaining the lock.
	 */
	VI_LOCK(vp);
	vp->v_data = 0;
	VI_UNLOCK(vp);
	MYFS_IFREE(ump, ip);
	return (0);
}
