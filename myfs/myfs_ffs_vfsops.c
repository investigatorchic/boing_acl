/*-
 * Copyright (c) 1989, 1991, 1993, 1994
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
 *	@(#)ffs_vfsops.c	8.31 (Berkeley) 5/20/95
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD: src/sys/ufs/ffs/ffs_vfsops.c,v 1.366.2.2.2.1 2009/10/25 01:10:29 kensmith Exp $");

#include "opt_quota.h"
#include "opt_ufs.h"
#include "opt_ffs.h"
#include "opt_ddb.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/namei.h>
#include <sys/priv.h>
#include <sys/proc.h>
#include <sys/kernel.h>
#include <sys/vnode.h>
#include <sys/mount.h>
#include <sys/bio.h>
#include <sys/buf.h>
#include <sys/conf.h>
#include <sys/fcntl.h>
#include <sys/malloc.h>
#include <sys/mutex.h>

#include <security/mac/mac_framework.h>

#include <local/myfs/myfs_extattr.h>
#include <local/myfs/myfs_gjournal.h>
#include <local/myfs/myfs_quota.h>
#include <local/myfs/myfs_ufsmount.h>
#include <local/myfs/myfs_inode.h>
#include <local/myfs/myfs_ufs_extern.h>

#include <local/myfs/myfs_fs.h>
#include <local/myfs/myfs_ffs_extern.h>

#include <vm/vm.h>
#include <vm/uma.h>
#include <vm/vm_page.h>

#include <geom/geom.h>
#include <geom/geom_vfs.h>

#include <ddb/ddb.h>

static uma_zone_t uma_inode, uma_ufs1, uma_ufs2;

static int	ffs_reload(struct mount *, struct thread *);
static int	myfs_ffs_mountfs(struct vnode *, struct mount *, struct thread *);
static void	ffs_oldfscompat_read(struct myfs_fs *, struct myfs_ufsmount *,
		    myfs_ufs2_daddr_t);
static void	ffs_oldfscompat_write(struct myfs_fs *, struct myfs_ufsmount *);
static void	ffs_ifree(struct myfs_ufsmount *ump, struct myfs_inode *ip);
static vfs_init_t myfs_ffs_init;
static vfs_uninit_t myfs_ffs_uninit;
static vfs_extattrctl_t myfs_ffs_extattrctl;
static vfs_cmount_t myfs_ffs_cmount;
static vfs_unmount_t myfs_ffs_unmount;
static vfs_mount_t myfs_ffs_mount;
static vfs_statfs_t myfs_ffs_statfs;
static vfs_fhtovp_t myfs_ffs_fhtovp;
static vfs_sync_t myfs_ffs_sync;

static struct vfsops myfs_ufs_vfsops = {
	.vfs_extattrctl =	myfs_ffs_extattrctl,
	.vfs_fhtovp =		myfs_ffs_fhtovp,
	.vfs_init =		myfs_ffs_init,
	.vfs_mount =		myfs_ffs_mount,
	.vfs_cmount =		myfs_ffs_cmount,
	.vfs_quotactl =		myfs_ufs_quotactl,
	.vfs_root =		myfs_ufs_root,
	.vfs_statfs =		myfs_ffs_statfs,
	.vfs_sync =		myfs_ffs_sync,
	.vfs_uninit =		myfs_ffs_uninit,
	.vfs_unmount =		myfs_ffs_unmount,
	.vfs_vget =		myfs_ffs_vget,
	.vfs_susp_clean =	myfs_process_deferred_inactive,
};

VFS_SET(myfs_ufs_vfsops, myfs, 0);
MODULE_VERSION(myfs, 1);

static b_strategy_t myfs_ffs_geom_strategy;
static b_write_t myfs_ffs_bufwrite;

static struct buf_ops ffs_ops = {
	.bop_name =	"MYFS",
	.bop_write =	myfs_ffs_bufwrite,
	.bop_strategy =	myfs_ffs_geom_strategy,
	.bop_sync =	bufsync,
	.bop_bdflush =	bufbdflush,
};

static const char *myfs_opts[] = { "acls", "async", "noatime", "noclusterr",
    "noclusterw", "noexec", "export", "force", "from", "multilabel", 
    "snapshot", "nosuid", "suiddir", "nosymfollow", "sync",
    "union", NULL };

static int
myfs_ffs_mount(struct mount *mp)
{
	struct vnode *devvp;
	struct thread *td;
	struct myfs_ufsmount *ump = 0;
	struct myfs_fs *fs;
	int error, flags;
	u_int mntorflags, mntandnotflags;
	accmode_t accmode;
	struct nameidata ndp;
	char *fspec;

	td = curthread;
	if (vfs_filteropt(mp->mnt_optnew, myfs_opts))
		return (EINVAL);
	if (uma_inode == NULL) {
		uma_inode = uma_zcreate("MYFS inode",
		    sizeof(struct myfs_inode), NULL, NULL, NULL, NULL,
		    UMA_ALIGN_PTR, 0);
		uma_ufs1 = uma_zcreate("MYFS1 dinode",
		    sizeof(struct myfs_ufs1_dinode), NULL, NULL, NULL, NULL,
		    UMA_ALIGN_PTR, 0);
		uma_ufs2 = uma_zcreate("MYFS2 dinode",
		    sizeof(struct myfs_ufs2_dinode), NULL, NULL, NULL, NULL,
		    UMA_ALIGN_PTR, 0);
	}

	fspec = vfs_getopts(mp->mnt_optnew, "from", &error);
	if (error)
		return (error);

	mntorflags = 0;
	mntandnotflags = 0;
	if (vfs_getopt(mp->mnt_optnew, "acls", NULL, NULL) == 0)
		mntorflags |= MNT_ACLS;

	if (vfs_getopt(mp->mnt_optnew, "snapshot", NULL, NULL) == 0) {
		mntorflags |= MNT_SNAPSHOT;
		/*
		 * Once we have set the MNT_SNAPSHOT flag, do not
		 * persist "snapshot" in the options list.
		 */
		vfs_deleteopt(mp->mnt_optnew, "snapshot");
		vfs_deleteopt(mp->mnt_opt, "snapshot");
	}

	MNT_ILOCK(mp);
	mp->mnt_flag = (mp->mnt_flag | mntorflags) & ~mntandnotflags;
	MNT_IUNLOCK(mp);
	/*
	 * If updating, check whether changing from read-only to
	 * read/write; if there is no device name, that's all we do.
	 */
	if (mp->mnt_flag & MNT_UPDATE) {
		ump = VFSTOMYFS(mp);
		fs = ump->um_fs;
		devvp = ump->um_devvp;
		if (fs->fs_ronly == 0 &&
		    vfs_flagopt(mp->mnt_optnew, "ro", NULL, 0)) {
			/*
			 * Flush any dirty data and suspend filesystem.
			 */
			if ((error = vn_start_write(NULL, &mp, V_WAIT)) != 0)
				return (error);
			for (;;) {
				vn_finished_write(mp);
				if ((error = vfs_write_suspend(mp)) != 0)
					return (error);
				MNT_ILOCK(mp);
				if (mp->mnt_kern_flag & MNTK_SUSPENDED) {
					/*
					 * Allow the secondary writes
					 * to proceed.
					 */
					mp->mnt_kern_flag &= ~(MNTK_SUSPENDED |
					    MNTK_SUSPEND2);
					wakeup(&mp->mnt_flag);
					MNT_IUNLOCK(mp);
					/*
					 * Allow the curthread to
					 * ignore the suspension to
					 * synchronize on-disk state.
					 */
					td->td_pflags |= TDP_IGNSUSP;
					break;
				}
				MNT_IUNLOCK(mp);
				vn_start_write(NULL, &mp, V_WAIT);
			}
			/*
			 * Check for and optionally get rid of files open
			 * for writing.
			 */
			flags = WRITECLOSE;
			if (mp->mnt_flag & MNT_FORCE)
				flags |= FORCECLOSE;
			if (mp->mnt_flag & MNT_SOFTDEP) {
				error = myfs_softdep_flushfiles(mp, flags, td);
			} else {
				error = myfs_ffs_flushfiles(mp, flags, td);
			}
			if (error) {
				vfs_write_resume(mp);
				return (error);
			}
			if (fs->fs_pendingblocks != 0 ||
			    fs->fs_pendinginodes != 0) {
				printf("%s: %s: blocks %jd files %d\n",
				    fs->fs_fsmnt, "update error",
				    (intmax_t)fs->fs_pendingblocks,
				    fs->fs_pendinginodes);
				fs->fs_pendingblocks = 0;
				fs->fs_pendinginodes = 0;
			}
			if ((fs->fs_flags & (MYFS_FS_UNCLEAN | MYFS_FS_NEEDSFSCK)) == 0)
				fs->fs_clean = 1;
			if ((error = myfs_ffs_sbupdate(ump, MNT_WAIT, 0)) != 0) {
				fs->fs_ronly = 0;
				fs->fs_clean = 0;
				vfs_write_resume(mp);
				return (error);
			}
			DROP_GIANT();
			g_topology_lock();
			g_access(ump->um_cp, 0, -1, 0);
			g_topology_unlock();
			PICKUP_GIANT();
			fs->fs_ronly = 1;
			MNT_ILOCK(mp);
			mp->mnt_flag |= MNT_RDONLY;
			MNT_IUNLOCK(mp);
			/*
			 * Allow the writers to note that filesystem
			 * is ro now.
			 */
			vfs_write_resume(mp);
		}
		if ((mp->mnt_flag & MNT_RELOAD) &&
		    (error = ffs_reload(mp, td)) != 0)
			return (error);
		if (fs->fs_ronly &&
		    !vfs_flagopt(mp->mnt_optnew, "ro", NULL, 0)) {
			/*
			 * If upgrade to read-write by non-root, then verify
			 * that user has necessary permissions on the device.
			 */
			vn_lock(devvp, LK_EXCLUSIVE | LK_RETRY);
			error = VOP_ACCESS(devvp, VREAD | VWRITE,
			    td->td_ucred, td);
			if (error)
				error = priv_check(td, PRIV_VFS_MOUNT_PERM);
			if (error) {
				VOP_UNLOCK(devvp, 0);
				return (error);
			}
			VOP_UNLOCK(devvp, 0);
			fs->fs_flags &= ~MYFS_FS_UNCLEAN;
			if (fs->fs_clean == 0) {
				fs->fs_flags |= MYFS_FS_UNCLEAN;
				if ((mp->mnt_flag & MNT_FORCE) ||
				    ((fs->fs_flags & MYFS_FS_NEEDSFSCK) == 0 &&
				     (fs->fs_flags & MYFS_FS_DOSOFTDEP))) {
					printf("WARNING: %s was not %s\n",
					   fs->fs_fsmnt, "properly dismounted");
				} else {
					printf(
"WARNING: R/W mount of %s denied.  Filesystem is not clean - run fsck\n",
					    fs->fs_fsmnt);
					return (EPERM);
				}
			}
			DROP_GIANT();
			g_topology_lock();
			/*
			 * If we're the root device, we may not have an E count
			 * yet, get it now.
			 */
			if (ump->um_cp->ace == 0)
				error = g_access(ump->um_cp, 0, 1, 1);
			else
				error = g_access(ump->um_cp, 0, 1, 0);
			g_topology_unlock();
			PICKUP_GIANT();
			if (error)
				return (error);
			if ((error = vn_start_write(NULL, &mp, V_WAIT)) != 0)
				return (error);
			fs->fs_ronly = 0;
			MNT_ILOCK(mp);
			mp->mnt_flag &= ~MNT_RDONLY;
			MNT_IUNLOCK(mp);
			fs->fs_clean = 0;
			if ((error = myfs_ffs_sbupdate(ump, MNT_WAIT, 0)) != 0) {
				vn_finished_write(mp);
				return (error);
			}
			/* check to see if we need to start softdep */
			if ((fs->fs_flags & MYFS_FS_DOSOFTDEP) &&
			    (error = myfs_softdep_mount(devvp, mp, fs, td->td_ucred))){
				vn_finished_write(mp);
				return (error);
			}
			if (fs->fs_snapinum[0] != 0)
				myfs_ffs_snapshot_mount(mp);
			vn_finished_write(mp);
		}
		/*
		 * Soft updates is incompatible with "async",
		 * so if we are doing softupdates stop the user
		 * from setting the async flag in an update.
		 * Softdep_mount() clears it in an initial mount 
		 * or ro->rw remount.
		 */
		if (mp->mnt_flag & MNT_SOFTDEP) {
			/* XXX: Reset too late ? */
			MNT_ILOCK(mp);
			mp->mnt_flag &= ~MNT_ASYNC;
			MNT_IUNLOCK(mp);
		}
		/*
		 * Keep MNT_ACLS flag if it is stored in superblock.
		 */
		if ((fs->fs_flags & MYFS_FS_ACLS) != 0) {
			/* XXX: Set too late ? */
			MNT_ILOCK(mp);
			mp->mnt_flag |= MNT_ACLS;
			MNT_IUNLOCK(mp);
		}

		/*
		 * If this is a snapshot request, take the snapshot.
		 */
		if (mp->mnt_flag & MNT_SNAPSHOT)
			return (myfs_ffs_snapshot(mp, fspec));
	}

	/*
	 * Not an update, or updating the name: look up the name
	 * and verify that it refers to a sensible disk device.
	 */
	NDINIT(&ndp, LOOKUP, FOLLOW | LOCKLEAF, UIO_SYSSPACE, fspec, td);
	if ((error = namei(&ndp)) != 0)
		return (error);
	NDFREE(&ndp, NDF_ONLY_PNBUF);
	devvp = ndp.ni_vp;
	if (!vn_isdisk(devvp, &error)) {
		vput(devvp);
		return (error);
	}

	/*
	 * If mount by non-root, then verify that user has necessary
	 * permissions on the device.
	 */
	accmode = VREAD;
	if ((mp->mnt_flag & MNT_RDONLY) == 0)
		accmode |= VWRITE;
	error = VOP_ACCESS(devvp, accmode, td->td_ucred, td);
	if (error)
		error = priv_check(td, PRIV_VFS_MOUNT_PERM);
	if (error) {
		vput(devvp);
		return (error);
	}

	if (mp->mnt_flag & MNT_UPDATE) {
		/*
		 * Update only
		 *
		 * If it's not the same vnode, or at least the same device
		 * then it's not correct.
		 */

		if (devvp->v_rdev != ump->um_devvp->v_rdev)
			error = EINVAL;	/* needs translation */
		vput(devvp);
		if (error)
			return (error);
	} else {
		/*
		 * New mount
		 *
		 * We need the name for the mount point (also used for
		 * "last mounted on") copied in. If an error occurs,
		 * the mount point is discarded by the upper level code.
		 * Note that vfs_mount() populates f_mntonname for us.
		 */
		if ((error = myfs_ffs_mountfs(devvp, mp, td)) != 0) {
			vrele(devvp);
			return (error);
		}
	}
	vfs_mountedfrom(mp, fspec);
	return (0);
}

/*
 * Compatibility with old mount system call.
 */

static int
myfs_ffs_cmount(struct mntarg *ma, void *data, uint64_t flags)
{
	struct myfs_ufs_args args;
	int error;

	if (data == NULL)
		return (EINVAL);
	error = copyin(data, &args, sizeof args);
	if (error)
		return (error);

	ma = mount_argsu(ma, "from", args.fspec, MAXPATHLEN);
	ma = mount_arg(ma, "export", &args.export, sizeof args.export);
	error = kernel_mount(ma, flags);

	return (error);
}

/*
 * Reload all incore data for a filesystem (used after running fsck on
 * the root filesystem and finding things to fix). The filesystem must
 * be mounted read-only.
 *
 * Things to do to update the mount:
 *	1) invalidate all cached meta-data.
 *	2) re-read superblock from disk.
 *	3) re-read summary information from disk.
 *	4) invalidate all inactive vnodes.
 *	5) invalidate all cached file data.
 *	6) re-read inode data for all active vnodes.
 */
static int
ffs_reload(struct mount *mp, struct thread *td)
{
	struct vnode *vp, *mvp, *devvp;
	struct myfs_inode *ip;
	void *space;
	struct buf *bp;
	struct myfs_fs *fs, *newfs;
	struct myfs_ufsmount *ump;
	myfs_ufs2_daddr_t sblockloc;
	int i, blks, size, error;
	int32_t *lp;

	if ((mp->mnt_flag & MNT_RDONLY) == 0)
		return (EINVAL);
	ump = VFSTOMYFS(mp);
	/*
	 * Step 1: invalidate all cached meta-data.
	 */
	devvp = VFSTOMYFS(mp)->um_devvp;
	vn_lock(devvp, LK_EXCLUSIVE | LK_RETRY);
	if (vinvalbuf(devvp, 0, 0, 0) != 0)
		panic("ffs_reload: dirty1");
	VOP_UNLOCK(devvp, 0);

	/*
	 * Step 2: re-read superblock from disk.
	 */
	fs = VFSTOMYFS(mp)->um_fs;
	if ((error = bread(devvp, btodb(fs->fs_sblockloc), fs->fs_sbsize,
	    NOCRED, &bp)) != 0)
		return (error);
	newfs = (struct myfs_fs *)bp->b_data;
	if ((newfs->fs_magic != MYFS_FS_UFS1_MAGIC &&
	     newfs->fs_magic != MYFS_FS_UFS2_MAGIC) ||
	    newfs->fs_bsize > MAXBSIZE ||
	    newfs->fs_bsize < sizeof(struct myfs_fs)) {
			brelse(bp);
			return (EIO);		/* XXX needs translation */
	}
	/*
	 * Copy pointer fields back into superblock before copying in	XXX
	 * new superblock. These should really be in the ufsmount.	XXX
	 * Note that important parameters (eg fs_ncg) are unchanged.
	 */
	newfs->fs_csp = fs->fs_csp;
	newfs->fs_maxcluster = fs->fs_maxcluster;
	newfs->fs_contigdirs = fs->fs_contigdirs;
	newfs->fs_active = fs->fs_active;
	/* The file system is still read-only. */
	newfs->fs_ronly = 1;
	sblockloc = fs->fs_sblockloc;
	bcopy(newfs, fs, (u_int)fs->fs_sbsize);
	brelse(bp);
	mp->mnt_maxsymlinklen = fs->fs_maxsymlinklen;
	ffs_oldfscompat_read(fs, VFSTOMYFS(mp), sblockloc);
	MYFS_LOCK(ump);
	if (fs->fs_pendingblocks != 0 || fs->fs_pendinginodes != 0) {
		printf("%s: reload pending error: blocks %jd files %d\n",
		    fs->fs_fsmnt, (intmax_t)fs->fs_pendingblocks,
		    fs->fs_pendinginodes);
		fs->fs_pendingblocks = 0;
		fs->fs_pendinginodes = 0;
	}
	MYFS_UNLOCK(ump);

	/*
	 * Step 3: re-read summary information from disk.
	 */
	blks = howmany(fs->fs_cssize, fs->fs_fsize);
	space = fs->fs_csp;
	for (i = 0; i < blks; i += fs->fs_frag) {
		size = fs->fs_bsize;
		if (i + fs->fs_frag > blks)
			size = (blks - i) * fs->fs_fsize;
		error = bread(devvp, myfs_fsbtodb(fs, fs->fs_csaddr + i), size,
		    NOCRED, &bp);
		if (error)
			return (error);
		bcopy(bp->b_data, space, (u_int)size);
		space = (char *)space + size;
		brelse(bp);
	}
	/*
	 * We no longer know anything about clusters per cylinder group.
	 */
	if (fs->fs_contigsumsize > 0) {
		lp = fs->fs_maxcluster;
		for (i = 0; i < fs->fs_ncg; i++)
			*lp++ = fs->fs_contigsumsize;
	}

loop:
	MNT_ILOCK(mp);
	MNT_VNODE_FOREACH(vp, mp, mvp) {
		VI_LOCK(vp);
		if (vp->v_iflag & VI_DOOMED) {
			VI_UNLOCK(vp);
			continue;
		}
		MNT_IUNLOCK(mp);
		/*
		 * Step 4: invalidate all cached file data.
		 */
		if (vget(vp, LK_EXCLUSIVE | LK_INTERLOCK, td)) {
			MNT_VNODE_FOREACH_ABORT(mp, mvp);
			goto loop;
		}
		if (vinvalbuf(vp, 0, 0, 0))
			panic("ffs_reload: dirty2");
		/*
		 * Step 5: re-read inode data for all active vnodes.
		 */
		ip = MYFS_VTOI(vp);
		error =
		    bread(devvp, myfs_fsbtodb(fs, myfs_ino_to_fsba(fs, ip->i_number)),
		    (int)fs->fs_bsize, NOCRED, &bp);
		if (error) {
			VOP_UNLOCK(vp, 0);
			vrele(vp);
			MNT_VNODE_FOREACH_ABORT(mp, mvp);
			return (error);
		}
		myfs_ffs_load_inode(bp, ip, fs, ip->i_number);
		ip->i_effnlink = ip->i_nlink;
		brelse(bp);
		VOP_UNLOCK(vp, 0);
		vrele(vp);
		MNT_ILOCK(mp);
	}
	MNT_IUNLOCK(mp);
	return (0);
}

/*
 * Possible superblock locations ordered from most to least likely.
 */
static int sblock_try[] = MYFS_SBLOCKSEARCH;

/*
 * Common code for mount and mountroot
 */
static int
myfs_ffs_mountfs(devvp, mp, td)
	struct vnode *devvp;
	struct mount *mp;
	struct thread *td;
{
	struct myfs_ufsmount *ump;
	struct buf *bp;
	struct myfs_fs *fs;
	struct cdev *dev;
	void *space;
	myfs_ufs2_daddr_t sblockloc;
	int error, i, blks, size, ronly;
	int32_t *lp;
	struct ucred *cred;
	struct g_consumer *cp;
	struct mount *nmp;

	bp = NULL;
	ump = NULL;
	cred = td ? td->td_ucred : NOCRED;
	ronly = (mp->mnt_flag & MNT_RDONLY) != 0;

	dev = devvp->v_rdev;
	dev_ref(dev);
	DROP_GIANT();
	g_topology_lock();
	error = g_vfs_open(devvp, &cp, "ffs", ronly ? 0 : 1);

	/*
	 * If we are a root mount, drop the E flag so fsck can do its magic.
	 * We will pick it up again when we remount R/W.
	 */
	if (error == 0 && ronly && (mp->mnt_flag & MNT_ROOTFS))
		error = g_access(cp, 0, 0, -1);
	g_topology_unlock();
	PICKUP_GIANT();
	VOP_UNLOCK(devvp, 0);
	if (error)
		goto out;
	if (devvp->v_rdev->si_iosize_max != 0)
		mp->mnt_iosize_max = devvp->v_rdev->si_iosize_max;
	if (mp->mnt_iosize_max > MAXPHYS)
		mp->mnt_iosize_max = MAXPHYS;

	devvp->v_bufobj.bo_private = cp;
	devvp->v_bufobj.bo_ops = &ffs_ops;

	fs = NULL;
	sblockloc = 0;
	/*
	 * Try reading the superblock in each of its possible locations.
	 */
	for (i = 0; sblock_try[i] != -1; i++) {
		if ((MYFS_SBLOCKSIZE % cp->provider->sectorsize) != 0) {
			error = EINVAL;
			vfs_mount_error(mp,
			    "Invalid sectorsize %d for superblock size %d",
			    cp->provider->sectorsize, MYFS_SBLOCKSIZE);
			goto out;
		}
		if ((error = bread(devvp, btodb(sblock_try[i]), MYFS_SBLOCKSIZE,
		    cred, &bp)) != 0)
			goto out;
		fs = (struct myfs_fs *)bp->b_data;
		sblockloc = sblock_try[i];
		if ((fs->fs_magic == MYFS_FS_UFS1_MAGIC ||
		     (fs->fs_magic == MYFS_FS_UFS2_MAGIC &&
		      (fs->fs_sblockloc == sblockloc ||
		       (fs->fs_old_flags & MYFS_FS_FLAGS_UPDATED) == 0))) &&
		    fs->fs_bsize <= MAXBSIZE &&
		    fs->fs_bsize >= sizeof(struct myfs_fs))
			break;
		brelse(bp);
		bp = NULL;
	}
	if (sblock_try[i] == -1) {
		error = EINVAL;		/* XXX needs translation */
		goto out;
	}
	fs->fs_fmod = 0;
	fs->fs_flags &= ~MYFS_FS_INDEXDIRS;	/* no support for directory indicies */
	fs->fs_flags &= ~MYFS_FS_UNCLEAN;
	if (fs->fs_clean == 0) {
		fs->fs_flags |= MYFS_FS_UNCLEAN;
		if (ronly || (mp->mnt_flag & MNT_FORCE) ||
		    ((fs->fs_flags & MYFS_FS_NEEDSFSCK) == 0 &&
		     (fs->fs_flags & MYFS_FS_DOSOFTDEP))) {
			printf(
"WARNING: %s was not properly dismounted\n",
			    fs->fs_fsmnt);
		} else {
			printf(
"WARNING: R/W mount of %s denied.  Filesystem is not clean - run fsck\n",
			    fs->fs_fsmnt);
			error = EPERM;
			goto out;
		}
		if ((fs->fs_pendingblocks != 0 || fs->fs_pendinginodes != 0) &&
		    (mp->mnt_flag & MNT_FORCE)) {
			printf("%s: lost blocks %jd files %d\n", fs->fs_fsmnt,
			    (intmax_t)fs->fs_pendingblocks,
			    fs->fs_pendinginodes);
			fs->fs_pendingblocks = 0;
			fs->fs_pendinginodes = 0;
		}
	}
	if (fs->fs_pendingblocks != 0 || fs->fs_pendinginodes != 0) {
		printf("%s: mount pending error: blocks %jd files %d\n",
		    fs->fs_fsmnt, (intmax_t)fs->fs_pendingblocks,
		    fs->fs_pendinginodes);
		fs->fs_pendingblocks = 0;
		fs->fs_pendinginodes = 0;
	}
	if ((fs->fs_flags & MYFS_FS_GJOURNAL) != 0) {
#ifdef MYFS_GJOURNAL
		/*
		 * Get journal provider name.
		 */
		size = 1024;
		mp->mnt_gjprovider = malloc(size, M_MYFSMNT, M_WAITOK);
		if (g_io_getattr("GJOURNAL::provider", cp, &size,
		    mp->mnt_gjprovider) == 0) {
			mp->mnt_gjprovider = realloc(mp->mnt_gjprovider, size,
			    M_MYFSMNT, M_WAITOK);
			MNT_ILOCK(mp);
			mp->mnt_flag |= MNT_GJOURNAL;
			MNT_IUNLOCK(mp);
		} else {
			printf(
"WARNING: %s: GJOURNAL flag on fs but no gjournal provider below\n",
			    mp->mnt_stat.f_mntonname);
			free(mp->mnt_gjprovider, M_MYFSMNT);
			mp->mnt_gjprovider = NULL;
		}
#else
		printf(
"WARNING: %s: GJOURNAL flag on fs but no MYFS_GJOURNAL support\n",
		    mp->mnt_stat.f_mntonname);
#endif
	} else {
		mp->mnt_gjprovider = NULL;
	}
	ump = malloc(sizeof *ump, M_MYFSMNT, M_WAITOK | M_ZERO);
	ump->um_cp = cp;
	ump->um_bo = &devvp->v_bufobj;
	ump->um_fs = malloc((u_long)fs->fs_sbsize, M_MYFSMNT, M_WAITOK);
	if (fs->fs_magic == MYFS_FS_UFS1_MAGIC) {
		ump->um_fstype = MYFS1;
		ump->um_balloc = myfs_ffs_balloc_ufs1;
	} else {
		ump->um_fstype = MYFS2;
		ump->um_balloc = myfs_ffs_balloc_ufs2;
	}
	ump->um_blkatoff = myfs_ffs_blkatoff;
	ump->um_truncate = myfs_ffs_truncate;
	ump->um_update = myfs_ffs_update;
	ump->um_valloc = myfs_ffs_valloc;
	ump->um_vfree = myfs_ffs_vfree;
	ump->um_ifree = ffs_ifree;
	ump->um_rdonly = myfs_ffs_rdonly;
	mtx_init(MYFS_MTX(ump), "MYFS", "MYFS Lock", MTX_DEF);
	bcopy(bp->b_data, ump->um_fs, (u_int)fs->fs_sbsize);
	if (fs->fs_sbsize < MYFS_SBLOCKSIZE)
		bp->b_flags |= B_INVAL | B_NOCACHE;
	brelse(bp);
	bp = NULL;
	fs = ump->um_fs;
	ffs_oldfscompat_read(fs, ump, sblockloc);
	fs->fs_ronly = ronly;
	size = fs->fs_cssize;
	blks = howmany(size, fs->fs_fsize);
	if (fs->fs_contigsumsize > 0)
		size += fs->fs_ncg * sizeof(int32_t);
	size += fs->fs_ncg * sizeof(u_int8_t);
	space = malloc((u_long)size, M_MYFSMNT, M_WAITOK);
	fs->fs_csp = space;
	for (i = 0; i < blks; i += fs->fs_frag) {
		size = fs->fs_bsize;
		if (i + fs->fs_frag > blks)
			size = (blks - i) * fs->fs_fsize;
		if ((error = bread(devvp, myfs_fsbtodb(fs, fs->fs_csaddr + i), size,
		    cred, &bp)) != 0) {
			free(fs->fs_csp, M_MYFSMNT);
			goto out;
		}
		bcopy(bp->b_data, space, (u_int)size);
		space = (char *)space + size;
		brelse(bp);
		bp = NULL;
	}
	if (fs->fs_contigsumsize > 0) {
		fs->fs_maxcluster = lp = space;
		for (i = 0; i < fs->fs_ncg; i++)
			*lp++ = fs->fs_contigsumsize;
		space = lp;
	}
	size = fs->fs_ncg * sizeof(u_int8_t);
	fs->fs_contigdirs = (u_int8_t *)space;
	bzero(fs->fs_contigdirs, size);
	fs->fs_active = NULL;
	mp->mnt_data = ump;
	mp->mnt_stat.f_fsid.val[0] = fs->fs_id[0];
	mp->mnt_stat.f_fsid.val[1] = fs->fs_id[1];
	nmp = NULL;
	if (fs->fs_id[0] == 0 || fs->fs_id[1] == 0 || 
	    (nmp = vfs_getvfs(&mp->mnt_stat.f_fsid))) {
		if (nmp)
			vfs_rel(nmp);
		vfs_getnewfsid(mp);
	}
	mp->mnt_maxsymlinklen = fs->fs_maxsymlinklen;
	MNT_ILOCK(mp);
	mp->mnt_flag |= MNT_LOCAL;
	MNT_IUNLOCK(mp);
	if ((fs->fs_flags & MYFS_FS_MULTILABEL) != 0) {
#ifdef MAC
		MNT_ILOCK(mp);
		mp->mnt_flag |= MNT_MULTILABEL;
		MNT_IUNLOCK(mp);
#else
		printf(
"WARNING: %s: multilabel flag on fs but no MAC support\n",
		    mp->mnt_stat.f_mntonname);
#endif
	}
	if ((fs->fs_flags & MYFS_FS_ACLS) != 0) {
#ifdef MYFS_ACL
		MNT_ILOCK(mp);
		mp->mnt_flag |= MNT_ACLS;
		MNT_IUNLOCK(mp);
#else
		printf(
"WARNING: %s: ACLs flag on fs but no ACLs support\n",
		    mp->mnt_stat.f_mntonname);
#endif
	}
	ump->um_mountp = mp;
	ump->um_dev = dev;
	ump->um_devvp = devvp;
	ump->um_nindir = fs->fs_nindir;
	ump->um_bptrtodb = fs->fs_fsbtodb;
	ump->um_seqinc = fs->fs_frag;
	for (i = 0; i < MYFS_MAXQUOTAS; i++)
		ump->um_quotas[i] = NULLVP;
#ifdef MYFS_EXTATTR
	myfs_ufs_extattr_uepm_init(&ump->um_extattr);
#endif
	/*
	 * Set FS local "last mounted on" information (NULL pad)
	 */
	bzero(fs->fs_fsmnt, MYFS_MAXMNTLEN);
	strlcpy(fs->fs_fsmnt, mp->mnt_stat.f_mntonname, MYFS_MAXMNTLEN);

	if( mp->mnt_flag & MNT_ROOTFS) {
		/*
		 * Root mount; update timestamp in mount structure.
		 * this will be used by the common root mount code
		 * to update the system clock.
		 */
		mp->mnt_time = fs->fs_time;
	}

	if (ronly == 0) {
		if ((fs->fs_flags & MYFS_FS_DOSOFTDEP) &&
		    (error = myfs_softdep_mount(devvp, mp, fs, cred)) != 0) {
			free(fs->fs_csp, M_MYFSMNT);
			goto out;
		}
		if (fs->fs_snapinum[0] != 0)
			myfs_ffs_snapshot_mount(mp);
		fs->fs_fmod = 1;
		fs->fs_clean = 0;
		(void) myfs_ffs_sbupdate(ump, MNT_WAIT, 0);
	}
	/*
	 * Initialize filesystem stat information in mount struct.
	 */
	MNT_ILOCK(mp);
	mp->mnt_kern_flag |= MNTK_MPSAFE | MNTK_LOOKUP_SHARED |
	    MNTK_EXTENDED_SHARED;
	MNT_IUNLOCK(mp);
#ifdef MYFS_EXTATTR
#ifdef MYFS_EXTATTR_AUTOSTART
	/*
	 *
	 * Auto-starting does the following:
	 *	- check for /.attribute in the fs, and extattr_start if so
	 *	- for each file in .attribute, enable that file with
	 * 	  an attribute of the same name.
	 * Not clear how to report errors -- probably eat them.
	 * This would all happen while the filesystem was busy/not
	 * available, so would effectively be "atomic".
	 */
	mp->mnt_stat.f_iosize = fs->fs_bsize;
	(void) myfs_ufs_extattr_autostart(mp, td);
#endif /* !MYFS_EXTATTR_AUTOSTART */
#endif /* !MYFS_EXTATTR */
	return (0);
out:
	if (bp)
		brelse(bp);
	if (cp != NULL) {
		DROP_GIANT();
		g_topology_lock();
		g_vfs_close(cp);
		g_topology_unlock();
		PICKUP_GIANT();
	}
	if (ump) {
		mtx_destroy(MYFS_MTX(ump));
		if (mp->mnt_gjprovider != NULL) {
			free(mp->mnt_gjprovider, M_MYFSMNT);
			mp->mnt_gjprovider = NULL;
		}
		free(ump->um_fs, M_MYFSMNT);
		free(ump, M_MYFSMNT);
		mp->mnt_data = NULL;
	}
	dev_rel(dev);
	return (error);
}

#include <sys/sysctl.h>
static int myfs_bigcgs = 0;
SYSCTL_INT(_debug, OID_AUTO, myfs_bigcgs, CTLFLAG_RW, &myfs_bigcgs, 0, "");

/*
 * Sanity checks for loading old filesystem superblocks.
 * See ffs_oldfscompat_write below for unwound actions.
 *
 * XXX - Parts get retired eventually.
 * Unfortunately new bits get added.
 */
static void
ffs_oldfscompat_read(fs, ump, sblockloc)
	struct myfs_fs *fs;
	struct myfs_ufsmount *ump;
	myfs_ufs2_daddr_t sblockloc;
{
	off_t maxfilesize;

	/*
	 * If not yet done, update fs_flags location and value of fs_sblockloc.
	 */
	if ((fs->fs_old_flags & MYFS_FS_FLAGS_UPDATED) == 0) {
		fs->fs_flags = fs->fs_old_flags;
		fs->fs_old_flags |= MYFS_FS_FLAGS_UPDATED;
		fs->fs_sblockloc = sblockloc;
	}
	/*
	 * If not yet done, update MYFS1 superblock with new wider fields.
	 */
	if (fs->fs_magic == MYFS_FS_UFS1_MAGIC && fs->fs_maxbsize != fs->fs_bsize) {
		fs->fs_maxbsize = fs->fs_bsize;
		fs->fs_time = fs->fs_old_time;
		fs->fs_size = fs->fs_old_size;
		fs->fs_dsize = fs->fs_old_dsize;
		fs->fs_csaddr = fs->fs_old_csaddr;
		fs->fs_cstotal.cs_ndir = fs->fs_old_cstotal.cs_ndir;
		fs->fs_cstotal.cs_nbfree = fs->fs_old_cstotal.cs_nbfree;
		fs->fs_cstotal.cs_nifree = fs->fs_old_cstotal.cs_nifree;
		fs->fs_cstotal.cs_nffree = fs->fs_old_cstotal.cs_nffree;
	}
	if (fs->fs_magic == MYFS_FS_UFS1_MAGIC &&
	    fs->fs_old_inodefmt < MYFS_FS_44INODEFMT) {
		fs->fs_maxfilesize = ((uint64_t)1 << 31) - 1;
		fs->fs_qbmask = ~fs->fs_bmask;
		fs->fs_qfmask = ~fs->fs_fmask;
	}
	if (fs->fs_magic == MYFS_FS_UFS1_MAGIC) {
		ump->um_savedmaxfilesize = fs->fs_maxfilesize;
		maxfilesize = (uint64_t)0x80000000 * fs->fs_bsize - 1;
		if (fs->fs_maxfilesize > maxfilesize)
			fs->fs_maxfilesize = maxfilesize;
	}
	/* Compatibility for old filesystems */
	if (fs->fs_avgfilesize <= 0)
		fs->fs_avgfilesize = MYFS_AVFILESIZ;
	if (fs->fs_avgfpdir <= 0)
		fs->fs_avgfpdir = MYFS_AFPDIR;
	if (myfs_bigcgs) {
		fs->fs_save_cgsize = fs->fs_cgsize;
		fs->fs_cgsize = fs->fs_bsize;
	}
}

/*
 * Unwinding superblock updates for old filesystems.
 * See ffs_oldfscompat_read above for details.
 *
 * XXX - Parts get retired eventually.
 * Unfortunately new bits get added.
 */
static void
ffs_oldfscompat_write(fs, ump)
	struct myfs_fs *fs;
	struct myfs_ufsmount *ump;
{

	/*
	 * Copy back MYFS2 updated fields that MYFS1 inspects.
	 */
	if (fs->fs_magic == MYFS_FS_UFS1_MAGIC) {
		fs->fs_old_time = fs->fs_time;
		fs->fs_old_cstotal.cs_ndir = fs->fs_cstotal.cs_ndir;
		fs->fs_old_cstotal.cs_nbfree = fs->fs_cstotal.cs_nbfree;
		fs->fs_old_cstotal.cs_nifree = fs->fs_cstotal.cs_nifree;
		fs->fs_old_cstotal.cs_nffree = fs->fs_cstotal.cs_nffree;
		fs->fs_maxfilesize = ump->um_savedmaxfilesize;
	}
	if (myfs_bigcgs) {
		fs->fs_cgsize = fs->fs_save_cgsize;
		fs->fs_save_cgsize = 0;
	}
}

/*
 * unmount system call
 */
static int
myfs_ffs_unmount(mp, mntflags)
	struct mount *mp;
	int mntflags;
{
	struct thread *td;
	struct myfs_ufsmount *ump = VFSTOMYFS(mp);
	struct myfs_fs *fs;
	int error, flags, susp;
#ifdef MYFS_EXTATTR
	int e_restart;
#endif

	flags = 0;
	td = curthread;
	fs = ump->um_fs;
	if (mntflags & MNT_FORCE) {
		flags |= FORCECLOSE;
		susp = fs->fs_ronly != 0;
	} else
		susp = 0;
#ifdef MYFS_EXTATTR
	if ((error = myfs_ufs_extattr_stop(mp, td))) {
		if (error != EOPNOTSUPP)
			printf("myfs_ffs_unmount: myfs_ufs_extattr_stop returned %d\n",
			    error);
		e_restart = 0;
	} else {
		myfs_ufs_extattr_uepm_destroy(&ump->um_extattr);
		e_restart = 1;
	}
#endif
	if (susp) {
		/*
		 * dounmount already called vn_start_write().
		 */
		for (;;) {
			vn_finished_write(mp);
			if ((error = vfs_write_suspend(mp)) != 0)
				return (error);
			MNT_ILOCK(mp);
			if (mp->mnt_kern_flag & MNTK_SUSPENDED) {
				mp->mnt_kern_flag &= ~(MNTK_SUSPENDED |
				    MNTK_SUSPEND2);
				wakeup(&mp->mnt_flag);
				MNT_IUNLOCK(mp);
				td->td_pflags |= TDP_IGNSUSP;
				break;
			}
			MNT_IUNLOCK(mp);
			vn_start_write(NULL, &mp, V_WAIT);
		}
	}
	if (mp->mnt_flag & MNT_SOFTDEP)
		error = myfs_softdep_flushfiles(mp, flags, td);
	else
		error = myfs_ffs_flushfiles(mp, flags, td);
	if (error != 0 && error != ENXIO)
		goto fail;

	MYFS_LOCK(ump);
	if (fs->fs_pendingblocks != 0 || fs->fs_pendinginodes != 0) {
		printf("%s: unmount pending error: blocks %jd files %d\n",
		    fs->fs_fsmnt, (intmax_t)fs->fs_pendingblocks,
		    fs->fs_pendinginodes);
		fs->fs_pendingblocks = 0;
		fs->fs_pendinginodes = 0;
	}
	MYFS_UNLOCK(ump);
	if (fs->fs_ronly == 0) {
		fs->fs_clean = fs->fs_flags & (MYFS_FS_UNCLEAN|MYFS_FS_NEEDSFSCK) ? 0 : 1;
		error = myfs_ffs_sbupdate(ump, MNT_WAIT, 0);
		if (error && error != ENXIO) {
			fs->fs_clean = 0;
			goto fail;
		}
	}
	if (susp) {
		vfs_write_resume(mp);
		vn_start_write(NULL, &mp, V_WAIT);
	}
	DROP_GIANT();
	g_topology_lock();
	g_vfs_close(ump->um_cp);
	g_topology_unlock();
	PICKUP_GIANT();
	vrele(ump->um_devvp);
	dev_rel(ump->um_dev);
	mtx_destroy(MYFS_MTX(ump));
	if (mp->mnt_gjprovider != NULL) {
		free(mp->mnt_gjprovider, M_MYFSMNT);
		mp->mnt_gjprovider = NULL;
	}
	free(fs->fs_csp, M_MYFSMNT);
	free(fs, M_MYFSMNT);
	free(ump, M_MYFSMNT);
	mp->mnt_data = NULL;
	MNT_ILOCK(mp);
	mp->mnt_flag &= ~MNT_LOCAL;
	MNT_IUNLOCK(mp);
	return (error);

fail:
	if (susp) {
		vfs_write_resume(mp);
		vn_start_write(NULL, &mp, V_WAIT);
	}
#ifdef MYFS_EXTATTR
	if (e_restart) {
		myfs_ufs_extattr_uepm_init(&ump->um_extattr);
#ifdef MYFS_EXTATTR_AUTOSTART
		(void) myfs_ufs_extattr_autostart(mp, td);
#endif
	}
#endif

	return (error);
}

/*
 * Flush out all the files in a filesystem.
 */
int
myfs_ffs_flushfiles(mp, flags, td)
	struct mount *mp;
	int flags;
	struct thread *td;
{
	struct myfs_ufsmount *ump;
	int error;

	ump = VFSTOMYFS(mp);
#ifdef QUOTA
	if (mp->mnt_flag & MNT_QUOTA) {
		int i;
		error = vflush(mp, 0, SKIPSYSTEM|flags, td);
		if (error)
			return (error);
		for (i = 0; i < MYFS_MAXQUOTAS; i++) {
			myfs_quotaoff(td, mp, i);
		}
		/*
		 * Here we fall through to vflush again to ensure
		 * that we have gotten rid of all the system vnodes.
		 */
	}
#endif
	ASSERT_VOP_LOCKED(ump->um_devvp, "myfs_ffs_flushfiles");
	if (ump->um_devvp->v_vflag & VV_COPYONWRITE) {
		if ((error = vflush(mp, 0, SKIPSYSTEM | flags, td)) != 0)
			return (error);
		myfs_ffs_snapshot_unmount(mp);
		flags |= FORCECLOSE;
		/*
		 * Here we fall through to vflush again to ensure
		 * that we have gotten rid of all the system vnodes.
		 */
	}
        /*
	 * Flush all the files.
	 */
	if ((error = vflush(mp, 0, flags, td)) != 0)
		return (error);
	/*
	 * Flush filesystem metadata.
	 */
	vn_lock(ump->um_devvp, LK_EXCLUSIVE | LK_RETRY);
	error = VOP_FSYNC(ump->um_devvp, MNT_WAIT, td);
	VOP_UNLOCK(ump->um_devvp, 0);
	return (error);
}

/*
 * Get filesystem statistics.
 */
static int
myfs_ffs_statfs(mp, sbp)
	struct mount *mp;
	struct statfs *sbp;
{
	struct myfs_ufsmount *ump;
	struct myfs_fs *fs;

	ump = VFSTOMYFS(mp);
	fs = ump->um_fs;
	if (fs->fs_magic != MYFS_FS_UFS1_MAGIC && fs->fs_magic != MYFS_FS_UFS2_MAGIC)
		panic("myfs_ffs_statfs");
	sbp->f_version = STATFS_VERSION;
	sbp->f_bsize = fs->fs_fsize;
	sbp->f_iosize = fs->fs_bsize;
	sbp->f_blocks = fs->fs_dsize;
	MYFS_LOCK(ump);
	sbp->f_bfree = fs->fs_cstotal.cs_nbfree * fs->fs_frag +
	    fs->fs_cstotal.cs_nffree + myfs_dbtofsb(fs, fs->fs_pendingblocks);
	sbp->f_bavail = myfs_freespace(fs, fs->fs_minfree) +
	    myfs_dbtofsb(fs, fs->fs_pendingblocks);
	sbp->f_files =  fs->fs_ncg * fs->fs_ipg - MYFS_ROOTINO;
	sbp->f_ffree = fs->fs_cstotal.cs_nifree + fs->fs_pendinginodes;
	MYFS_UNLOCK(ump);
	sbp->f_namemax = NAME_MAX;
	return (0);
}

/*
 * Go through the disk queues to initiate sandbagged IO;
 * go through the inodes to write those that have been modified;
 * initiate the writing of the super block if it has been modified.
 *
 * Note: we are always called with the filesystem marked `MPBUSY'.
 */
static int
myfs_ffs_sync(mp, waitfor)
	struct mount *mp;
	int waitfor;
{
	struct vnode *mvp, *vp, *devvp;
	struct thread *td;
	struct myfs_inode *ip;
	struct myfs_ufsmount *ump = VFSTOMYFS(mp);
	struct myfs_fs *fs;
	int error, count, wait, lockreq, allerror = 0;
	int suspend;
	int suspended;
	int secondary_writes;
	int secondary_accwrites;
	int softdep_deps;
	int softdep_accdeps;
	struct bufobj *bo;

	td = curthread;
	fs = ump->um_fs;
	if (fs->fs_fmod != 0 && fs->fs_ronly != 0) {		/* XXX */
		printf("fs = %s\n", fs->fs_fsmnt);
		panic("myfs_ffs_sync: rofs mod");
	}
	/*
	 * Write back each (modified) inode.
	 */
	wait = 0;
	suspend = 0;
	suspended = 0;
	lockreq = LK_EXCLUSIVE | LK_NOWAIT;
	if (waitfor == MNT_SUSPEND) {
		suspend = 1;
		waitfor = MNT_WAIT;
	}
	if (waitfor == MNT_WAIT) {
		wait = 1;
		lockreq = LK_EXCLUSIVE;
	}
	lockreq |= LK_INTERLOCK | LK_SLEEPFAIL;
	MNT_ILOCK(mp);
loop:
	/* Grab snapshot of secondary write counts */
	secondary_writes = mp->mnt_secondary_writes;
	secondary_accwrites = mp->mnt_secondary_accwrites;

	/* Grab snapshot of softdep dependency counts */
	MNT_IUNLOCK(mp);
	myfs_softdep_get_depcounts(mp, &softdep_deps, &softdep_accdeps);
	MNT_ILOCK(mp);

	MNT_VNODE_FOREACH(vp, mp, mvp) {
		/*
		 * Depend on the mntvnode_slock to keep things stable enough
		 * for a quick test.  Since there might be hundreds of
		 * thousands of vnodes, we cannot afford even a subroutine
		 * call unless there's a good chance that we have work to do.
		 */
		VI_LOCK(vp);
		if (vp->v_iflag & VI_DOOMED) {
			VI_UNLOCK(vp);
			continue;
		}
		ip = MYFS_VTOI(vp);
		if (vp->v_type == VNON || ((ip->i_flag &
		    (IN_ACCESS | IN_CHANGE | IN_MODIFIED | IN_UPDATE)) == 0 &&
		    vp->v_bufobj.bo_dirty.bv_cnt == 0)) {
			VI_UNLOCK(vp);
			continue;
		}
		MNT_IUNLOCK(mp);
		if ((error = vget(vp, lockreq, td)) != 0) {
			MNT_ILOCK(mp);
			if (error == ENOENT || error == ENOLCK) {
				MNT_VNODE_FOREACH_ABORT_ILOCKED(mp, mvp);
				goto loop;
			}
			continue;
		}
		if ((error = myfs_ffs_syncvnode(vp, waitfor)) != 0)
			allerror = error;
		vput(vp);
		MNT_ILOCK(mp);
	}
	MNT_IUNLOCK(mp);
	/*
	 * Force stale filesystem control information to be flushed.
	 */
	if (waitfor == MNT_WAIT) {
		if ((error = myfs_softdep_flushworklist(ump->um_mountp, &count, td)))
			allerror = error;
		/* Flushed work items may create new vnodes to clean */
		if (allerror == 0 && count) {
			MNT_ILOCK(mp);
			goto loop;
		}
	}
#ifdef QUOTA
	myfs_qsync(mp);
#endif
	devvp = ump->um_devvp;
	bo = &devvp->v_bufobj;
	BO_LOCK(bo);
	if (waitfor != MNT_LAZY &&
	    (bo->bo_numoutput > 0 || bo->bo_dirty.bv_cnt > 0)) {
		BO_UNLOCK(bo);
		vn_lock(devvp, LK_EXCLUSIVE | LK_RETRY);
		if ((error = VOP_FSYNC(devvp, waitfor, td)) != 0)
			allerror = error;
		VOP_UNLOCK(devvp, 0);
		if (allerror == 0 && waitfor == MNT_WAIT) {
			MNT_ILOCK(mp);
			goto loop;
		}
	} else if (suspend != 0) {
		if (myfs_softdep_check_suspend(mp,
					  devvp,
					  softdep_deps,
					  softdep_accdeps,
					  secondary_writes,
					  secondary_accwrites) != 0)
			goto loop;	/* More work needed */
		mtx_assert(MNT_MTX(mp), MA_OWNED);
		mp->mnt_kern_flag |= MNTK_SUSPEND2 | MNTK_SUSPENDED;
		MNT_IUNLOCK(mp);
		suspended = 1;
	} else
		BO_UNLOCK(bo);
	/*
	 * Write back modified superblock.
	 */
	if (fs->fs_fmod != 0 &&
	    (error = myfs_ffs_sbupdate(ump, waitfor, suspended)) != 0)
		allerror = error;
	return (allerror);
}

int
myfs_ffs_vget(mp, ino, flags, vpp)
	struct mount *mp;
	ino_t ino;
	int flags;
	struct vnode **vpp;
{
	return (myfs_ffs_vgetf(mp, ino, flags, vpp, 0));
}

int
myfs_ffs_vgetf(mp, ino, flags, vpp, ffs_flags)
	struct mount *mp;
	ino_t ino;
	int flags;
	struct vnode **vpp;
	int ffs_flags;
{
	struct myfs_fs *fs;
	struct myfs_inode *ip;
	struct myfs_ufsmount *ump;
	struct buf *bp;
	struct vnode *vp;
	struct cdev *dev;
	int error;

	error = vfs_hash_get(mp, ino, flags, curthread, vpp, NULL, NULL);
	if (error || *vpp != NULL)
		return (error);

	/*
	 * We must promote to an exclusive lock for vnode creation.  This
	 * can happen if lookup is passed LOCKSHARED.
 	 */
	if ((flags & LK_TYPE_MASK) == LK_SHARED) {
		flags &= ~LK_TYPE_MASK;
		flags |= LK_EXCLUSIVE;
	}

	/*
	 * We do not lock vnode creation as it is believed to be too
	 * expensive for such rare case as simultaneous creation of vnode
	 * for same ino by different processes. We just allow them to race
	 * and check later to decide who wins. Let the race begin!
	 */

	ump = VFSTOMYFS(mp);
	dev = ump->um_dev;
	fs = ump->um_fs;

	/*
	 * If this malloc() is performed after the getnewvnode()
	 * it might block, leaving a vnode with a NULL v_data to be
	 * found by myfs_ffs_sync() if a sync happens to fire right then,
	 * which will cause a panic because myfs_ffs_sync() blindly
	 * dereferences vp->v_data (as well it should).
	 */
	ip = uma_zalloc(uma_inode, M_WAITOK | M_ZERO);

	/* Allocate a new vnode/inode. */
	if (fs->fs_magic == MYFS_FS_UFS1_MAGIC)
		error = getnewvnode("ufs", mp, &myfs_ffs_vnodeops1, &vp);
	else
		error = getnewvnode("ufs", mp, &myfs_ffs_vnodeops2, &vp);
	if (error) {
		*vpp = NULL;
		uma_zfree(uma_inode, ip);
		return (error);
	}
	/*
	 * MYFS supports recursive locking.
	 */
	VN_LOCK_AREC(vp);
	vp->v_data = ip;
	vp->v_bufobj.bo_bsize = fs->fs_bsize;
	ip->i_vnode = vp;
	ip->i_ump = ump;
	ip->i_fs = fs;
	ip->i_dev = dev;
	ip->i_number = ino;
	ip->i_ea_refs = 0;
#ifdef QUOTA
	{
		int i;
		for (i = 0; i < MYFS_MAXQUOTAS; i++)
			ip->i_dquot[i] = MYFS_NODQUOT;
	}
#endif

	lockmgr(vp->v_vnlock, LK_EXCLUSIVE, NULL);
	if (ffs_flags & MYFS_FFSV_FORCEINSMQ)
		vp->v_vflag |= VV_FORCEINSMQ;
	error = insmntque(vp, mp);
	if (error != 0) {
		uma_zfree(uma_inode, ip);
		*vpp = NULL;
		return (error);
	}
	vp->v_vflag &= ~VV_FORCEINSMQ;
	error = vfs_hash_insert(vp, ino, flags, curthread, vpp, NULL, NULL);
	if (error || *vpp != NULL)
		return (error);

	/* Read in the disk contents for the inode, copy into the inode. */
	error = bread(ump->um_devvp, myfs_fsbtodb(fs, myfs_ino_to_fsba(fs, ino)),
	    (int)fs->fs_bsize, NOCRED, &bp);
	if (error) {
		/*
		 * The inode does not contain anything useful, so it would
		 * be misleading to leave it on its hash chain. With mode
		 * still zero, it will be unlinked and returned to the free
		 * list by vput().
		 */
		brelse(bp);
		vput(vp);
		*vpp = NULL;
		return (error);
	}
	if (ip->i_ump->um_fstype == MYFS1)
		ip->i_din1 = uma_zalloc(uma_ufs1, M_WAITOK);
	else
		ip->i_din2 = uma_zalloc(uma_ufs2, M_WAITOK);
	myfs_ffs_load_inode(bp, ip, fs, ino);
	if (MYFS_DOINGSOFTDEP(vp))
		myfs_softdep_load_inodeblock(ip);
	else
		ip->i_effnlink = ip->i_nlink;
	bqrelse(bp);

	/*
	 * Initialize the vnode from the inode, check for aliases.
	 * Note that the underlying vnode may have changed.
	 */
	if (ip->i_ump->um_fstype == MYFS1)
		error = myfs_ufs_vinit(mp, &myfs_ffs_fifoops1, &vp);
	else
		error = myfs_ufs_vinit(mp, &myfs_ffs_fifoops2, &vp);
	if (error) {
		vput(vp);
		*vpp = NULL;
		return (error);
	}

	/*
	 * Finish inode initialization.
	 */
	if (vp->v_type != VFIFO) {
		/* MYFS supports shared locking for all files except fifos. */
		VN_LOCK_ASHARE(vp);
	}

	/*
	 * Set up a generation number for this inode if it does not
	 * already have one. This should only happen on old filesystems.
	 */
	if (ip->i_gen == 0) {
		ip->i_gen = arc4random() / 2 + 1;
		if ((vp->v_mount->mnt_flag & MNT_RDONLY) == 0) {
			ip->i_flag |= IN_MODIFIED;
			MYFS_DIP_SET(ip, i_gen, ip->i_gen);
		}
	}
	/*
	 * Ensure that uid and gid are correct. This is a temporary
	 * fix until fsck has been changed to do the update.
	 */
	if (fs->fs_magic == MYFS_FS_UFS1_MAGIC &&		/* XXX */
	    fs->fs_old_inodefmt < MYFS_FS_44INODEFMT) {	/* XXX */
		ip->i_uid = ip->i_din1->di_ouid;	/* XXX */
		ip->i_gid = ip->i_din1->di_ogid;	/* XXX */
	}						/* XXX */

#ifdef MAC
	if ((mp->mnt_flag & MNT_MULTILABEL) && ip->i_mode) {
		/*
		 * If this vnode is already allocated, and we're running
		 * multi-label, attempt to perform a label association
		 * from the extended attributes on the inode.
		 */
		error = mac_vnode_associate_extattr(mp, vp);
		if (error) {
			/* myfs_ufs_inactive will release ip->i_devvp ref. */
			vput(vp);
			*vpp = NULL;
			return (error);
		}
	}
#endif

	*vpp = vp;
	return (0);
}

/*
 * File handle to vnode
 *
 * Have to be really careful about stale file handles:
 * - check that the inode number is valid
 * - call myfs_ffs_vget() to get the locked inode
 * - check for an unallocated inode (i_mode == 0)
 * - check that the given client host has export rights and return
 *   those rights via. exflagsp and credanonp
 */
static int
myfs_ffs_fhtovp(mp, fhp, flags, vpp)
	struct mount *mp;
	struct fid *fhp;
	int flags;
	struct vnode **vpp;
{
	struct myfs_ufid *ufhp;
	struct myfs_fs *fs;

	ufhp = (struct myfs_ufid *)fhp;
	fs = VFSTOMYFS(mp)->um_fs;
	if (ufhp->ufid_ino < MYFS_ROOTINO ||
	    ufhp->ufid_ino >= fs->fs_ncg * fs->fs_ipg)
		return (ESTALE);
	return (myfs_ufs_fhtovp(mp, ufhp, vpp));
}

/*
 * Initialize the filesystem.
 */
static int
myfs_ffs_init(vfsp)
	struct vfsconf *vfsp;
{

	myfs_softdep_initialize();
	return (myfs_ufs_init(vfsp));
}

/*
 * Undo the work of myfs_ffs_init().
 */
static int
myfs_ffs_uninit(vfsp)
	struct vfsconf *vfsp;
{
	int ret;

	ret = myfs_ufs_uninit(vfsp);
	myfs_softdep_uninitialize();
	return (ret);
}

/*
 * Write a superblock and associated information back to disk.
 */
int
myfs_ffs_sbupdate(mp, waitfor, suspended)
	struct myfs_ufsmount *mp;
	int waitfor;
	int suspended;
{
	struct myfs_fs *fs = mp->um_fs;
	struct buf *sbbp;
	struct buf *bp;
	int blks;
	void *space;
	int i, size, error, allerror = 0;

	if (fs->fs_ronly == 1 &&
	    (mp->um_mountp->mnt_flag & (MNT_RDONLY | MNT_UPDATE)) != 
	    (MNT_RDONLY | MNT_UPDATE))
		panic("myfs_ffs_sbupdate: write read-only filesystem");
	/*
	 * We use the superblock's buf to serialize calls to myfs_ffs_sbupdate().
	 */
	sbbp = getblk(mp->um_devvp, btodb(fs->fs_sblockloc), (int)fs->fs_sbsize,
	    0, 0, 0);
	/*
	 * First write back the summary information.
	 */
	blks = howmany(fs->fs_cssize, fs->fs_fsize);
	space = fs->fs_csp;
	for (i = 0; i < blks; i += fs->fs_frag) {
		size = fs->fs_bsize;
		if (i + fs->fs_frag > blks)
			size = (blks - i) * fs->fs_fsize;
		bp = getblk(mp->um_devvp, myfs_fsbtodb(fs, fs->fs_csaddr + i),
		    size, 0, 0, 0);
		bcopy(space, bp->b_data, (u_int)size);
		space = (char *)space + size;
		if (suspended)
			bp->b_flags |= B_VALIDSUSPWRT;
		if (waitfor != MNT_WAIT)
			bawrite(bp);
		else if ((error = bwrite(bp)) != 0)
			allerror = error;
	}
	/*
	 * Now write back the superblock itself. If any errors occurred
	 * up to this point, then fail so that the superblock avoids
	 * being written out as clean.
	 */
	if (allerror) {
		brelse(sbbp);
		return (allerror);
	}
	bp = sbbp;
	if (fs->fs_magic == MYFS_FS_UFS1_MAGIC && fs->fs_sblockloc != MYFS_SBLOCK_UFS1 &&
	    (fs->fs_flags & MYFS_FS_FLAGS_UPDATED) == 0) {
		printf("%s: correcting fs_sblockloc from %jd to %d\n",
		    fs->fs_fsmnt, fs->fs_sblockloc, MYFS_SBLOCK_UFS1);
		fs->fs_sblockloc = MYFS_SBLOCK_UFS1;
	}
	if (fs->fs_magic == MYFS_FS_UFS2_MAGIC && fs->fs_sblockloc != MYFS_SBLOCK_UFS2 &&
	    (fs->fs_flags & MYFS_FS_FLAGS_UPDATED) == 0) {
		printf("%s: correcting fs_sblockloc from %jd to %d\n",
		    fs->fs_fsmnt, fs->fs_sblockloc, MYFS_SBLOCK_UFS2);
		fs->fs_sblockloc = MYFS_SBLOCK_UFS2;
	}
	fs->fs_fmod = 0;
	fs->fs_time = time_second;
	bcopy((caddr_t)fs, bp->b_data, (u_int)fs->fs_sbsize);
	ffs_oldfscompat_write((struct myfs_fs *)bp->b_data, mp);
	if (suspended)
		bp->b_flags |= B_VALIDSUSPWRT;
	if (waitfor != MNT_WAIT)
		bawrite(bp);
	else if ((error = bwrite(bp)) != 0)
		allerror = error;
	return (allerror);
}

static int
myfs_ffs_extattrctl(struct mount *mp, int cmd, struct vnode *filename_vp,
	int attrnamespace, const char *attrname)
{

#ifdef MYFS_EXTATTR
	return (myfs_ufs_extattrctl(mp, cmd, filename_vp, attrnamespace,
	    attrname));
#else
	return (vfs_stdextattrctl(mp, cmd, filename_vp, attrnamespace,
	    attrname));
#endif
}

static void
ffs_ifree(struct myfs_ufsmount *ump, struct myfs_inode *ip)
{

	if (ump->um_fstype == MYFS1 && ip->i_din1 != NULL)
		uma_zfree(uma_ufs1, ip->i_din1);
	else if (ip->i_din2 != NULL)
		uma_zfree(uma_ufs2, ip->i_din2);
	uma_zfree(uma_inode, ip);
}

static int myfs_dobkgrdwrite = 1;
SYSCTL_INT(_debug, OID_AUTO, myfs_dobkgrdwrite, CTLFLAG_RW, &myfs_dobkgrdwrite, 0,
    "Do background writes (honoring the BV_BKGRDWRITE flag)?");

/*
 * Complete a background write started from bwrite.
 */
static void
ffs_backgroundwritedone(struct buf *bp)
{
	struct bufobj *bufobj;
	struct buf *origbp;

	/*
	 * Find the original buffer that we are writing.
	 */
	bufobj = bp->b_bufobj;
	BO_LOCK(bufobj);
	if ((origbp = gbincore(bp->b_bufobj, bp->b_lblkno)) == NULL)
		panic("backgroundwritedone: lost buffer");
	/* Grab an extra reference to be dropped by the bufdone() below. */
	bufobj_wrefl(bufobj);
	BO_UNLOCK(bufobj);
	/*
	 * Process dependencies then return any unfinished ones.
	 */
	if (!LIST_EMPTY(&bp->b_dep))
		buf_complete(bp);
	/*
	 * This buffer is marked B_NOCACHE so when it is released
	 * by biodone it will be tossed.
	 */
	bp->b_flags |= B_NOCACHE;
	bp->b_flags &= ~B_CACHE;
	bufdone(bp);
	BO_LOCK(bufobj);
	/*
	 * Clear the BV_BKGRDINPROG flag in the original buffer
	 * and awaken it if it is waiting for the write to complete.
	 * If BV_BKGRDINPROG is not set in the original buffer it must
	 * have been released and re-instantiated - which is not legal.
	 */
	KASSERT((origbp->b_vflags & BV_BKGRDINPROG),
	    ("backgroundwritedone: lost buffer2"));
	origbp->b_vflags &= ~BV_BKGRDINPROG;
	if (origbp->b_vflags & BV_BKGRDWAIT) {
		origbp->b_vflags &= ~BV_BKGRDWAIT;
		wakeup(&origbp->b_xflags);
	}
	BO_UNLOCK(bufobj);
}


/*
 * Write, release buffer on completion.  (Done by iodone
 * if async).  Do not bother writing anything if the buffer
 * is invalid.
 *
 * Note that we set B_CACHE here, indicating that buffer is
 * fully valid and thus cacheable.  This is true even of NFS
 * now so we set it generally.  This could be set either here 
 * or in biodone() since the I/O is synchronous.  We put it
 * here.
 */
static int
myfs_ffs_bufwrite(struct buf *bp)
{
	int oldflags, s;
	struct buf *newbp;

	CTR3(KTR_BUF, "bufwrite(%p) vp %p flags %X", bp, bp->b_vp, bp->b_flags);
	if (bp->b_flags & B_INVAL) {
		brelse(bp);
		return (0);
	}

	oldflags = bp->b_flags;

	if (!BUF_ISLOCKED(bp))
		panic("bufwrite: buffer is not busy???");
	s = splbio();
	/*
	 * If a background write is already in progress, delay
	 * writing this block if it is asynchronous. Otherwise
	 * wait for the background write to complete.
	 */
	BO_LOCK(bp->b_bufobj);
	if (bp->b_vflags & BV_BKGRDINPROG) {
		if (bp->b_flags & B_ASYNC) {
			BO_UNLOCK(bp->b_bufobj);
			splx(s);
			bdwrite(bp);
			return (0);
		}
		bp->b_vflags |= BV_BKGRDWAIT;
		msleep(&bp->b_xflags, BO_MTX(bp->b_bufobj), PRIBIO, "bwrbg", 0);
		if (bp->b_vflags & BV_BKGRDINPROG)
			panic("bufwrite: still writing");
	}
	BO_UNLOCK(bp->b_bufobj);

	/* Mark the buffer clean */
	bundirty(bp);

	/*
	 * If this buffer is marked for background writing and we
	 * do not have to wait for it, make a copy and write the
	 * copy so as to leave this buffer ready for further use.
	 *
	 * This optimization eats a lot of memory.  If we have a page
	 * or buffer shortfall we can't do it.
	 */
	if (myfs_dobkgrdwrite && (bp->b_xflags & BX_BKGRDWRITE) && 
	    (bp->b_flags & B_ASYNC) &&
	    !vm_page_count_severe() &&
	    !buf_dirty_count_severe()) {
		KASSERT(bp->b_iodone == NULL,
		    ("bufwrite: needs chained iodone (%p)", bp->b_iodone));

		/* get a new block */
		newbp = geteblk(bp->b_bufsize, GB_NOWAIT_BD);
		if (newbp == NULL)
			goto normal_write;

		/*
		 * set it to be identical to the old block.  We have to
		 * set b_lblkno and BKGRDMARKER before calling bgetvp()
		 * to avoid confusing the splay tree and gbincore().
		 */
		memcpy(newbp->b_data, bp->b_data, bp->b_bufsize);
		newbp->b_lblkno = bp->b_lblkno;
		newbp->b_xflags |= BX_BKGRDMARKER;
		BO_LOCK(bp->b_bufobj);
		bp->b_vflags |= BV_BKGRDINPROG;
		bgetvp(bp->b_vp, newbp);
		BO_UNLOCK(bp->b_bufobj);
		newbp->b_bufobj = &bp->b_vp->v_bufobj;
		newbp->b_blkno = bp->b_blkno;
		newbp->b_offset = bp->b_offset;
		newbp->b_iodone = ffs_backgroundwritedone;
		newbp->b_flags |= B_ASYNC;
		newbp->b_flags &= ~B_INVAL;

		/*
		 * Initiate write on the copy, release the original to
		 * the B_LOCKED queue so that it cannot go away until
		 * the background write completes. If not locked it could go
		 * away and then be reconstituted while it was being written.
		 * If the reconstituted buffer were written, we could end up
		 * with two background copies being written at the same time.
		 */
		bqrelse(bp);
		bp = newbp;
	}

	/* Let the normal bufwrite do the rest for us */
normal_write:
	return (bufwrite(bp));
}


static void
myfs_ffs_geom_strategy(struct bufobj *bo, struct buf *bp)
{
	struct vnode *vp;
	int error;
	struct buf *tbp;

	vp = bo->__bo_vnode;
	if (bp->b_iocmd == BIO_WRITE) {
		if ((bp->b_flags & B_VALIDSUSPWRT) == 0 &&
		    bp->b_vp != NULL && bp->b_vp->v_mount != NULL &&
		    (bp->b_vp->v_mount->mnt_kern_flag & MNTK_SUSPENDED) != 0)
			panic("myfs_ffs_geom_strategy: bad I/O");
		bp->b_flags &= ~B_VALIDSUSPWRT;
		if ((vp->v_vflag & VV_COPYONWRITE) &&
		    vp->v_rdev->si_snapdata != NULL) {
			if ((bp->b_flags & B_CLUSTER) != 0) {
				runningbufwakeup(bp);
				TAILQ_FOREACH(tbp, &bp->b_cluster.cluster_head,
					      b_cluster.cluster_entry) {
					error = myfs_ffs_copyonwrite(vp, tbp);
					if (error != 0 &&
					    error != EOPNOTSUPP) {
						bp->b_error = error;
						bp->b_ioflags |= BIO_ERROR;
						bufdone(bp);
						return;
					}
				}
				bp->b_runningbufspace = bp->b_bufsize;
				atomic_add_long(&runningbufspace,
					       bp->b_runningbufspace);
			} else {
				error = myfs_ffs_copyonwrite(vp, bp);
				if (error != 0 && error != EOPNOTSUPP) {
					bp->b_error = error;
					bp->b_ioflags |= BIO_ERROR;
					bufdone(bp);
					return;
				}
			}
		}
#ifdef SOFTUPDATES
		if ((bp->b_flags & B_CLUSTER) != 0) {
			TAILQ_FOREACH(tbp, &bp->b_cluster.cluster_head,
				      b_cluster.cluster_entry) {
				if (!LIST_EMPTY(&tbp->b_dep))
					buf_start(tbp);
			}
		} else {
			if (!LIST_EMPTY(&bp->b_dep))
				buf_start(bp);
		}

#endif
	}
	g_vfs_strategy(bo, bp);
}

#ifdef	DDB

static void
db_print_ffs(struct myfs_ufsmount *ump)
{
	db_printf("mp %p %s devvp %p fs %p su_wl %d su_wl_in %d su_deps %d "
		  "su_req %d\n",
	    ump->um_mountp, ump->um_mountp->mnt_stat.f_mntonname,
	    ump->um_devvp, ump->um_fs, ump->softdep_on_worklist,
	    ump->softdep_on_worklist_inprogress, ump->softdep_deps,
	    ump->softdep_req);
}

DB_SHOW_COMMAND(ffs, db_show_ffs)
{
	struct mount *mp;
	struct myfs_ufsmount *ump;

	if (have_addr) {
		ump = VFSTOMYFS((struct mount *)addr);
		db_print_ffs(ump);
		return;
	}

	TAILQ_FOREACH(mp, &mountlist, mnt_list) {
		if (!strcmp(mp->mnt_stat.f_fstypename, ufs_vfsconf.vfc_name))
			db_print_ffs(VFSTOMYFS(mp));
	}
}

#endif	/* DDB */
