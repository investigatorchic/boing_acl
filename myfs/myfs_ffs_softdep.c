/*-
 * Copyright 1998, 2000 Marshall Kirk McKusick. All Rights Reserved.
 *
 * The soft updates code is derived from the appendix of a University
 * of Michigan technical report (Gregory R. Ganger and Yale N. Patt,
 * "Soft Updates: A Solution to the Metadata Update Problem in File
 * Systems", CSE-TR-254-95, August 1995).
 *
 * Further information about soft updates can be obtained from:
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
 *	from: @(#)ffs_softdep.c	9.59 (McKusick) 6/21/00
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD: src/sys/ufs/ffs/ffs_softdep.c,v 1.234.2.3.2.1 2009/10/25 01:10:29 kensmith Exp $");

#include "opt_ffs.h"
#include "opt_ddb.h"

/*
 * For now we want the safety net that the DEBUG flag provides.
 */
#ifndef DEBUG
#define DEBUG
#endif

#include <sys/param.h>
#include <sys/kernel.h>
#include <sys/systm.h>
#include <sys/bio.h>
#include <sys/buf.h>
#include <sys/kdb.h>
#include <sys/kthread.h>
#include <sys/lock.h>
#include <sys/malloc.h>
#include <sys/mount.h>
#include <sys/mutex.h>
#include <sys/proc.h>
#include <sys/stat.h>
#include <sys/sysctl.h>
#include <sys/syslog.h>
#include <sys/vnode.h>
#include <sys/conf.h>
#include <local/myfs/myfs_dir.h>
#include <local/myfs/myfs_extattr.h>
#include <local/myfs/myfs_quota.h>
#include <local/myfs/myfs_inode.h>
#include <local/myfs/myfs_ufsmount.h>
#include <local/myfs/myfs_fs.h>
#include <local/myfs/myfs_softdep.h>
#include <local/myfs/myfs_ffs_extern.h>
#include <local/myfs/myfs_ufs_extern.h>

#include <vm/vm.h>

#include <ddb/ddb.h>

int
myfs_softdep_flushfiles(oldmnt, flags, td)
	struct mount *oldmnt;
	int flags;
	struct thread *td;
{

	panic("myfs_softdep_flushfiles called");
}

int
myfs_softdep_mount(devvp, mp, fs, cred)
	struct vnode *devvp;
	struct mount *mp;
	struct myfs_fs *fs;
	struct ucred *cred;
{

	return (0);
}

void 
myfs_softdep_initialize()
{

	return;
}

void
myfs_softdep_uninitialize()
{

	return;
}

void
myfs_softdep_setup_inomapdep(bp, ip, newinum)
	struct buf *bp;
	struct myfs_inode *ip;
	ino_t newinum;
{

	panic("myfs_softdep_setup_inomapdep called");
}

void
myfs_softdep_setup_blkmapdep(bp, mp, newblkno)
	struct buf *bp;
	struct mount *mp;
	myfs_ufs2_daddr_t newblkno;
{

	panic("myfs_softdep_setup_blkmapdep called");
}

void 
myfs_softdep_setup_allocdirect(ip, lbn, newblkno, oldblkno, newsize, oldsize, bp)
	struct myfs_inode *ip;
	myfs_ufs_lbn_t lbn;
	myfs_ufs2_daddr_t newblkno;
	myfs_ufs2_daddr_t oldblkno;
	long newsize;
	long oldsize;
	struct buf *bp;
{
	
	panic("myfs_softdep_setup_allocdirect called");
}

void 
myfs_softdep_setup_allocext(ip, lbn, newblkno, oldblkno, newsize, oldsize, bp)
	struct myfs_inode *ip;
	myfs_ufs_lbn_t lbn;
	myfs_ufs2_daddr_t newblkno;
	myfs_ufs2_daddr_t oldblkno;
	long newsize;
	long oldsize;
	struct buf *bp;
{
	
	panic("myfs_softdep_setup_allocext called");
}

void
myfs_softdep_setup_allocindir_page(ip, lbn, bp, ptrno, newblkno, oldblkno, nbp)
	struct myfs_inode *ip;
	myfs_ufs_lbn_t lbn;
	struct buf *bp;
	int ptrno;
	myfs_ufs2_daddr_t newblkno;
	myfs_ufs2_daddr_t oldblkno;
	struct buf *nbp;
{

	panic("myfs_softdep_setup_allocindir_page called");
}

void
myfs_softdep_setup_allocindir_meta(nbp, ip, bp, ptrno, newblkno)
	struct buf *nbp;
	struct myfs_inode *ip;
	struct buf *bp;
	int ptrno;
	myfs_ufs2_daddr_t newblkno;
{

	panic("myfs_softdep_setup_allocindir_meta called");
}

void
myfs_softdep_setup_freeblocks(ip, length, flags)
	struct myfs_inode *ip;
	off_t length;
	int flags;
{
	
	panic("myfs_softdep_setup_freeblocks called");
}

void
myfs_softdep_freefile(pvp, ino, mode)
		struct vnode *pvp;
		ino_t ino;
		int mode;
{

	panic("myfs_softdep_freefile called");
}

int 
myfs_softdep_setup_directory_add(bp, dp, diroffset, newinum, newdirbp, isnewblk)
	struct buf *bp;
	struct myfs_inode *dp;
	off_t diroffset;
	ino_t newinum;
	struct buf *newdirbp;
	int isnewblk;
{

	panic("myfs_softdep_setup_directory_add called");
}

void 
myfs_softdep_change_directoryentry_offset(dp, base, oldloc, newloc, entrysize)
	struct myfs_inode *dp;
	caddr_t base;
	caddr_t oldloc;
	caddr_t newloc;
	int entrysize;
{

	panic("myfs_softdep_change_directoryentry_offset called");
}

void 
myfs_softdep_setup_remove(bp, dp, ip, isrmdir)
	struct buf *bp;
	struct myfs_inode *dp;
	struct myfs_inode *ip;
	int isrmdir;
{
	
	panic("myfs_softdep_setup_remove called");
}

void 
myfs_softdep_setup_directory_change(bp, dp, ip, newinum, isrmdir)
	struct buf *bp;
	struct myfs_inode *dp;
	struct myfs_inode *ip;
	ino_t newinum;
	int isrmdir;
{

	panic("myfs_softdep_setup_directory_change called");
}

void
myfs_softdep_change_linkcnt(ip)
	struct myfs_inode *ip;
{

	panic("myfs_softdep_change_linkcnt called");
}

void 
myfs_softdep_load_inodeblock(ip)
	struct myfs_inode *ip;
{

	panic("myfs_softdep_load_inodeblock called");
}

void 
myfs_softdep_update_inodeblock(ip, bp, waitfor)
	struct myfs_inode *ip;
	struct buf *bp;
	int waitfor;
{

	panic("myfs_softdep_update_inodeblock called");
}

int
myfs_softdep_fsync(vp)
	struct vnode *vp;	/* the "in_core" copy of the inode */
{

	return (0);
}

void
myfs_softdep_fsync_mountdev(vp)
	struct vnode *vp;
{

	return;
}

int
myfs_softdep_flushworklist(oldmnt, countp, td)
	struct mount *oldmnt;
	int *countp;
	struct thread *td;
{

	*countp = 0;
	return (0);
}

int
myfs_softdep_sync_metadata(struct vnode *vp)
{

	return (0);
}

int
myfs_softdep_slowdown(vp)
	struct vnode *vp;
{

	panic("myfs_softdep_slowdown called");
}

void
myfs_softdep_releasefile(ip)
	struct myfs_inode *ip;	/* inode with the zero effective link count */
{

	panic("myfs_softdep_releasefile called");
}

int
myfs_softdep_request_cleanup(fs, vp)
	struct myfs_fs *fs;
	struct vnode *vp;
{

	return (0);
}

int
myfs_softdep_check_suspend(struct mount *mp,
		      struct vnode *devvp,
		      int softdep_deps,
		      int softdep_accdeps,
		      int secondary_writes,
		      int secondary_accwrites)
{
	struct bufobj *bo;
	int error;
	
	(void) softdep_deps,
	(void) softdep_accdeps;

	bo = &devvp->v_bufobj;
	ASSERT_BO_LOCKED(bo);

	MNT_ILOCK(mp);
	while (mp->mnt_secondary_writes != 0) {
		BO_UNLOCK(bo);
		msleep(&mp->mnt_secondary_writes, MNT_MTX(mp),
		    (PUSER - 1) | PDROP, "secwr", 0);
		BO_LOCK(bo);
		MNT_ILOCK(mp);
	}

	/*
	 * Reasons for needing more work before suspend:
	 * - Dirty buffers on devvp.
	 * - Secondary writes occurred after start of vnode sync loop
	 */
	error = 0;
	if (bo->bo_numoutput > 0 ||
	    bo->bo_dirty.bv_cnt > 0 ||
	    secondary_writes != 0 ||
	    mp->mnt_secondary_writes != 0 ||
	    secondary_accwrites != mp->mnt_secondary_accwrites)
		error = EAGAIN;
	BO_UNLOCK(bo);
	return (error);
}

void
myfs_softdep_get_depcounts(struct mount *mp,
		      int *softdepactivep,
		      int *softdepactiveaccp)
{
	(void) mp;
	*softdepactivep = 0;
	*softdepactiveaccp = 0;
}

