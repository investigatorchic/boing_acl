/*-
 * Copyright 2000 Marshall Kirk McKusick. All Rights Reserved.
 *
 * Further information about snapshots can be obtained from:
 *
 *	Marshall Kirk McKusick		http://www.mckusick.com/softdep/
 *	1614 Oxford Street		mckusick@mckusick.com
 *	Berkeley, CA 94709-1608		+1-510-843-9542
 *	USA
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY MARSHALL KIRK MCKUSICK ``AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED.  IN NO EVENT SHALL MARSHALL KIRK MCKUSICK BE LIABLE FOR
 * ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 *	@(#)ffs_snapshot.c	8.11 (McKusick) 7/23/00
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD: src/sys/ufs/ffs/ffs_snapshot.c,v 1.150.2.1.2.1 2009/10/25 01:10:29 kensmith Exp $");

#include "opt_quota.h"

#include <sys/param.h>
#include <sys/kernel.h>
#include <sys/systm.h>
#include <sys/conf.h>
#include <sys/bio.h>
#include <sys/buf.h>
#include <sys/fcntl.h>
#include <sys/proc.h>
#include <sys/namei.h>
#include <sys/sched.h>
#include <sys/stat.h>
#include <sys/malloc.h>
#include <sys/mount.h>
#include <sys/resource.h>
#include <sys/resourcevar.h>
#include <sys/vnode.h>

#include <geom/geom.h>

#include <local/myfs/myfs_extattr.h>
#include <local/myfs/myfs_quota.h>
#include <local/myfs/myfs_ufsmount.h>
#include <local/myfs/myfs_inode.h>
#include <local/myfs/myfs_ufs_extern.h>

#include <local/myfs/myfs_fs.h>
#include <local/myfs/myfs_ffs_extern.h>

#define KERNCRED thread0.td_ucred
#define DEBUG 1

#include "opt_ffs.h"

int
myfs_ffs_snapshot(mp, snapfile)
	struct mount *mp;
	char *snapfile;
{
	return (EINVAL);
}

int
myfs_ffs_snapblkfree(fs, devvp, bno, size, inum)
	struct myfs_fs *fs;
	struct vnode *devvp;
	myfs_ufs2_daddr_t bno;
	long size;
	ino_t inum;
{
	return (EINVAL);
}

void
myfs_ffs_snapremove(vp)
	struct vnode *vp;
{
}

void
myfs_ffs_snapshot_mount(mp)
	struct mount *mp;
{
}

void
myfs_ffs_snapshot_unmount(mp)
	struct mount *mp;
{
}

void
myfs_ffs_snapgone(ip)
	struct myfs_inode *ip;
{
}

int
myfs_ffs_copyonwrite(devvp, bp)
	struct vnode *devvp;
	struct buf *bp;
{
	return (EINVAL);
}

/*
 * Process file deletes that were deferred by myfs_ufs_inactive() due to
 * the file system being suspended. Transfer IN_LAZYACCESS into
 * IN_MODIFIED for vnodes that were accessed during suspension.
 */
void
myfs_process_deferred_inactive(struct mount *mp)
{
	struct vnode *vp, *mvp;
	struct myfs_inode *ip;
	struct thread *td;
	int error;

	td = curthread;
	(void) vn_start_secondary_write(NULL, &mp, V_WAIT);
	MNT_ILOCK(mp);
 loop:
	MNT_VNODE_FOREACH(vp, mp, mvp) {
		VI_LOCK(vp);
		/*
		 * IN_LAZYACCESS is checked here without holding any
		 * vnode lock, but this flag is set only while holding
		 * vnode interlock.
		 */
		if (vp->v_type == VNON || (vp->v_iflag & VI_DOOMED) != 0 ||
		    ((MYFS_VTOI(vp)->i_flag & IN_LAZYACCESS) == 0 &&
			((vp->v_iflag & VI_OWEINACT) == 0 ||
			vp->v_usecount > 0))) {
			VI_UNLOCK(vp);
			continue;
		}
		MNT_IUNLOCK(mp);
		vholdl(vp);
		error = vn_lock(vp, LK_EXCLUSIVE | LK_INTERLOCK);
		if (error != 0) {
			vdrop(vp);
			MNT_ILOCK(mp);
			if (error == ENOENT)
				continue;	/* vnode recycled */
			MNT_VNODE_FOREACH_ABORT_ILOCKED(mp, mvp);
			goto loop;
		}
		ip = MYFS_VTOI(vp);
		if ((ip->i_flag & IN_LAZYACCESS) != 0) {
			ip->i_flag &= ~IN_LAZYACCESS;
			ip->i_flag |= IN_MODIFIED;
		}
		VI_LOCK(vp);
		if ((vp->v_iflag & VI_OWEINACT) == 0 || vp->v_usecount > 0) {
			VI_UNLOCK(vp);
			VOP_UNLOCK(vp, 0);
			vdrop(vp);
			MNT_ILOCK(mp);
			continue;
		}
		
		VNASSERT((vp->v_iflag & VI_DOINGINACT) == 0, vp,
			 ("myfs_process_deferred_inactive: "
			  "recursed on VI_DOINGINACT"));
		vp->v_iflag |= VI_DOINGINACT;
		vp->v_iflag &= ~VI_OWEINACT;
		VI_UNLOCK(vp);
		(void) VOP_INACTIVE(vp, td);
		VI_LOCK(vp);
		VNASSERT(vp->v_iflag & VI_DOINGINACT, vp,
			 ("myfs_process_deferred_inactive: lost VI_DOINGINACT"));
		VNASSERT((vp->v_iflag & VI_OWEINACT) == 0, vp,
			 ("myfs_process_deferred_inactive: got VI_OWEINACT"));
		vp->v_iflag &= ~VI_DOINGINACT;
		VI_UNLOCK(vp);
		VOP_UNLOCK(vp, 0);
		vdrop(vp);
		MNT_ILOCK(mp);
	}
	MNT_IUNLOCK(mp);
	vn_finished_secondary_write(mp);
}
