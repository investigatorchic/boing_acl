/*-
 * Copyright (c) 1991, 1993, 1994
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
 *	@(#)ufs_vfsops.c	8.8 (Berkeley) 5/20/95
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD: src/sys/ufs/ufs/ufs_vfsops.c,v 1.52.2.1.2.1 2009/10/25 01:10:29 kensmith Exp $");

#include "opt_quota.h"
#include "opt_ufs.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/lock.h>
#include <sys/malloc.h>
#include <sys/mount.h>
#include <sys/proc.h>
#include <sys/socket.h>
#include <sys/vnode.h>

#include <local/myfs/myfs_extattr.h>
#include <local/myfs/myfs_quota.h>
#include <local/myfs/myfs_inode.h>
#include <local/myfs/myfs_ufsmount.h>
#include <local/myfs/myfs_ufs_extern.h>
#ifdef MYFS_DIRHASH
#include <local/myfs/myfs_dir.h>
#include <local/myfs/myfs_dirhash.h>
#endif

MALLOC_DEFINE(M_MYFSMNT, "ufs_mount", "UFS mount structure");

/*
 * Return the root of a filesystem.
 */
int
myfs_ufs_root(mp, flags, vpp)
	struct mount *mp;
	int flags;
	struct vnode **vpp;
{
	struct vnode *nvp;
	int error;

	error = VFS_VGET(mp, (ino_t)MYFS_ROOTINO, flags, &nvp);
	if (error)
		return (error);
	*vpp = nvp;
	return (0);
}

/*
 * Do operations associated with quotas
 */
int
myfs_ufs_quotactl(mp, cmds, id, arg)
	struct mount *mp;
	int cmds;
	uid_t id;
	void *arg;
{
#ifndef QUOTA
	return (EOPNOTSUPP);
#else
	struct thread *td;
	int cmd, type, error;

	td = curthread;
	cmd = cmds >> SUBCMDSHIFT;
	type = cmds & SUBCMDMASK;
	if (id == -1) {
		switch (type) {

		case MYFS_USRQUOTA:
			id = td->td_ucred->cr_ruid;
			break;

		case MYFS_GRPQUOTA:
			id = td->td_ucred->cr_rgid;
			break;

		default:
			return (EINVAL);
		}
	}
	if ((u_int)type >= MYFS_MAXQUOTAS)
		return (EINVAL);

	switch (cmd) {
	case Q_QUOTAON:
		error = myfs_quotaon(td, mp, type, arg);
		break;

	case Q_QUOTAOFF:
		error = myfs_quotaoff(td, mp, type);
		break;

	case Q_SETQUOTA:
		error = myfs_setquota(td, mp, id, type, arg);
		break;

	case Q_SETUSE:
		error = myfs_setuse(td, mp, id, type, arg);
		break;

	case Q_GETQUOTA:
		error = myfs_getquota(td, mp, id, type, arg);
		break;

	case Q_SYNC:
		error = myfs_qsync(mp);
		break;

	default:
		error = EINVAL;
		break;
	}
	return (error);
#endif
}

/*
 * Initial MYFS filesystems, done only once.
 */
int
myfs_ufs_init(vfsp)
	struct vfsconf *vfsp;
{

#ifdef QUOTA
	myfs_dqinit();
#endif
#ifdef MYFS_DIRHASH
	myfs_ufsdirhash_init();
#endif
	return (0);
}

/*
 * Uninitialise MYFS filesystems, done before module unload.
 */
int
myfs_ufs_uninit(vfsp)
	struct vfsconf *vfsp;
{

#ifdef QUOTA
	myfs_dquninit();
#endif
#ifdef MYFS_DIRHASH
	myfs_ufsdirhash_uninit();
#endif
	return (0);
}

/*
 * This is the generic part of fhtovp called after the underlying
 * filesystem has validated the file handle.
 *
 * Call the VFS_CHECKEXP beforehand to verify access.
 */
int
myfs_ufs_fhtovp(mp, ufhp, vpp)
	struct mount *mp;
	struct myfs_ufid *ufhp;
	struct vnode **vpp;
{
	struct myfs_inode *ip;
	struct vnode *nvp;
	int error;

	error = VFS_VGET(mp, ufhp->ufid_ino, LK_EXCLUSIVE, &nvp);
	if (error) {
		*vpp = NULLVP;
		return (error);
	}
	ip = MYFS_VTOI(nvp);
	if (ip->i_mode == 0 || ip->i_gen != ufhp->ufid_gen ||
	    ip->i_effnlink <= 0) {
		vput(nvp);
		*vpp = NULLVP;
		return (ESTALE);
	}
	*vpp = nvp;
	vnode_create_vobject(*vpp, MYFS_DIP(ip, i_size), curthread);
	return (0);
}
