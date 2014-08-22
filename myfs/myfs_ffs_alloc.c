/*-
 * Copyright (c) 2002 Networks Associates Technology, Inc.
 * All rights reserved.
 *
 * This software was developed for the FreeBSD Project by Marshall
 * Kirk McKusick and Network Associates Laboratories, the Security
 * Research Division of Network Associates, Inc. under DARPA/SPAWAR
 * contract N66001-01-C-8035 ("CBOSS"), as part of the DARPA CHATS
 * research program
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
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
 *	@(#)ffs_alloc.c	8.18 (Berkeley) 5/26/95
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD: src/sys/ufs/ffs/ffs_alloc.c,v 1.153.2.1.2.1 2009/10/25 01:10:29 kensmith Exp $");

#include "opt_quota.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/bio.h>
#include <sys/buf.h>
#include <sys/conf.h>
#include <sys/file.h>
#include <sys/filedesc.h>
#include <sys/priv.h>
#include <sys/proc.h>
#include <sys/vnode.h>
#include <sys/mount.h>
#include <sys/kernel.h>
#include <sys/sysctl.h>
#include <sys/syslog.h>

#include <local/myfs/myfs_extattr.h>
#include <local/myfs/myfs_quota.h>
#include <local/myfs/myfs_inode.h>
#include <local/myfs/myfs_ufs_extern.h>
#include <local/myfs/myfs_ufsmount.h>

#include <local/myfs/myfs_fs.h>
#include <local/myfs/myfs_ffs_extern.h>

typedef myfs_ufs2_daddr_t allocfcn_t(struct myfs_inode *ip, int cg, myfs_ufs2_daddr_t bpref,
				  int size);

static myfs_ufs2_daddr_t ffs_alloccg(struct myfs_inode *, int, myfs_ufs2_daddr_t, int);
static myfs_ufs2_daddr_t
	      ffs_alloccgblk(struct myfs_inode *, struct buf *, myfs_ufs2_daddr_t);
#ifdef INVARIANTS
static int	myfs_ffs_checkblk(struct myfs_inode *, myfs_ufs2_daddr_t, long);
#endif
static myfs_ufs2_daddr_t ffs_clusteralloc(struct myfs_inode *, int, myfs_ufs2_daddr_t, int);
static void	myfs_ffs_clusteracct(struct myfs_ufsmount *, struct myfs_fs *, struct myfs_cg *,
		    myfs_ufs1_daddr_t, int);
static ino_t	myfs_ffs_dirpref(struct myfs_inode *);
static myfs_ufs2_daddr_t ffs_fragextend(struct myfs_inode *, int, myfs_ufs2_daddr_t, int, int);
static void	ffs_fserr(struct myfs_fs *, ino_t, char *);
static myfs_ufs2_daddr_t	ffs_hashalloc
		(struct myfs_inode *, int, myfs_ufs2_daddr_t, int, allocfcn_t *);
static myfs_ufs2_daddr_t ffs_nodealloccg(struct myfs_inode *, int, myfs_ufs2_daddr_t, int);
static myfs_ufs1_daddr_t ffs_mapsearch(struct myfs_fs *, struct myfs_cg *, myfs_ufs2_daddr_t, int);
static int	myfs_ffs_reallocblks_ufs1(struct vop_reallocblks_args *);
static int	myfs_ffs_reallocblks_ufs2(struct vop_reallocblks_args *);

#define    CAP_FSCK                0x0000000000008000ULL

/*
 * Allocate a block in the filesystem.
 *
 * The size of the requested block is given, which must be some
 * multiple of fs_fsize and <= fs_bsize.
 * A preference may be optionally specified. If a preference is given
 * the following hierarchy is used to allocate a block:
 *   1) allocate the requested block.
 *   2) allocate a rotationally optimal block in the same cylinder.
 *   3) allocate a block in the same cylinder group.
 *   4) quadradically rehash into other cylinder groups, until an
 *      available block is located.
 * If no block preference is given the following hierarchy is used
 * to allocate a block:
 *   1) allocate a block in the cylinder group that contains the
 *      inode for the file.
 *   2) quadradically rehash into other cylinder groups, until an
 *      available block is located.
 */
int
myfs_ffs_alloc(ip, lbn, bpref, size, flags, cred, bnp)
	struct myfs_inode *ip;
	myfs_ufs2_daddr_t lbn, bpref;
	int size, flags;
	struct ucred *cred;
	myfs_ufs2_daddr_t *bnp;
{
	struct myfs_fs *fs;
	struct myfs_ufsmount *ump;
	myfs_ufs2_daddr_t bno;
	int cg, reclaimed;
	static struct timeval lastfail;
	static int curfail;
	int64_t delta;
#ifdef QUOTA
	int error;
#endif

	*bnp = 0;
	fs = ip->i_fs;
	ump = ip->i_ump;
	mtx_assert(MYFS_MTX(ump), MA_OWNED);
#ifdef INVARIANTS
	if ((u_int)size > fs->fs_bsize || myfs_fragoff(fs, size) != 0) {
		printf("dev = %s, bsize = %ld, size = %d, fs = %s\n",
		    devtoname(ip->i_dev), (long)fs->fs_bsize, size,
		    fs->fs_fsmnt);
		panic("ffs_alloc: bad size");
	}
	if (cred == NOCRED)
		panic("ffs_alloc: missing credential");
#endif /* INVARIANTS */
	reclaimed = 0;
retry:
#ifdef QUOTA
	MYFS_UNLOCK(ump);
	error = myfs_chkdq(ip, btodb(size), cred, 0);
	if (error)
		return (error);
	MYFS_LOCK(ump);
#endif
	if (size == fs->fs_bsize && fs->fs_cstotal.cs_nbfree == 0)
		goto nospace;
	if (priv_check_cred(cred, PRIV_VFS_BLOCKRESERVE, 0) &&
	    myfs_freespace(fs, fs->fs_minfree) - myfs_numfrags(fs, size) < 0)
		goto nospace;
	if (bpref >= fs->fs_size)
		bpref = 0;
	if (bpref == 0)
		cg = myfs_ino_to_cg(fs, ip->i_number);
	else
		cg = myfs_dtog(fs, bpref);
	bno = ffs_hashalloc(ip, cg, bpref, size, ffs_alloccg);
	if (bno > 0) {
		delta = btodb(size);
		if (ip->i_flag & IN_SPACECOUNTED) {
			MYFS_LOCK(ump);
			fs->fs_pendingblocks += delta;
			MYFS_UNLOCK(ump);
		}
		MYFS_DIP_SET(ip, i_blocks, MYFS_DIP(ip, i_blocks) + delta);
		if (flags & IO_EXT)
			ip->i_flag |= IN_CHANGE;
		else
			ip->i_flag |= IN_CHANGE | IN_UPDATE;
		*bnp = bno;
		return (0);
	}
nospace:
#ifdef QUOTA
	MYFS_UNLOCK(ump);
	/*
	 * Restore user's disk quota because allocation failed.
	 */
	(void) myfs_chkdq(ip, -btodb(size), cred, FORCE);
	MYFS_LOCK(ump);
#endif
	if (fs->fs_pendingblocks > 0 && reclaimed == 0) {
		reclaimed = 1;
		myfs_softdep_request_cleanup(fs, MYFS_ITOV(ip));
		goto retry;
	}
	MYFS_UNLOCK(ump);
	if (ppsratecheck(&lastfail, &curfail, 1)) {
		ffs_fserr(fs, ip->i_number, "filesystem full");
		uprintf("\n%s: write failed, filesystem is full\n",
		    fs->fs_fsmnt);
	}
	return (ENOSPC);
}

/*
 * Reallocate a fragment to a bigger size
 *
 * The number and size of the old block is given, and a preference
 * and new size is also specified. The allocator attempts to extend
 * the original block. Failing that, the regular block allocator is
 * invoked to get an appropriate block.
 */
int
myfs_ffs_realloccg(ip, lbprev, bprev, bpref, osize, nsize, flags, cred, bpp)
	struct myfs_inode *ip;
	myfs_ufs2_daddr_t lbprev;
	myfs_ufs2_daddr_t bprev;
	myfs_ufs2_daddr_t bpref;
	int osize, nsize, flags;
	struct ucred *cred;
	struct buf **bpp;
{
	struct vnode *vp;
	struct myfs_fs *fs;
	struct buf *bp;
	struct myfs_ufsmount *ump;
	int cg, request, error, reclaimed;
	myfs_ufs2_daddr_t bno;
	static struct timeval lastfail;
	static int curfail;
	int64_t delta;

	*bpp = 0;
	vp = MYFS_ITOV(ip);
	fs = ip->i_fs;
	bp = NULL;
	ump = ip->i_ump;
	mtx_assert(MYFS_MTX(ump), MA_OWNED);
#ifdef INVARIANTS
	if (vp->v_mount->mnt_kern_flag & MNTK_SUSPENDED)
		panic("myfs_ffs_realloccg: allocation on suspended filesystem");
	if ((u_int)osize > fs->fs_bsize || myfs_fragoff(fs, osize) != 0 ||
	    (u_int)nsize > fs->fs_bsize || myfs_fragoff(fs, nsize) != 0) {
		printf(
		"dev = %s, bsize = %ld, osize = %d, nsize = %d, fs = %s\n",
		    devtoname(ip->i_dev), (long)fs->fs_bsize, osize,
		    nsize, fs->fs_fsmnt);
		panic("myfs_ffs_realloccg: bad size");
	}
	if (cred == NOCRED)
		panic("myfs_ffs_realloccg: missing credential");
#endif /* INVARIANTS */
	reclaimed = 0;
retry:
	if (priv_check_cred(cred, PRIV_VFS_BLOCKRESERVE, 0) &&
	    myfs_freespace(fs, fs->fs_minfree) -  myfs_numfrags(fs, nsize - osize) < 0) {
		goto nospace;
	}
	if (bprev == 0) {
		printf("dev = %s, bsize = %ld, bprev = %jd, fs = %s\n",
		    devtoname(ip->i_dev), (long)fs->fs_bsize, (intmax_t)bprev,
		    fs->fs_fsmnt);
		panic("myfs_ffs_realloccg: bad bprev");
	}
	MYFS_UNLOCK(ump);
	/*
	 * Allocate the extra space in the buffer.
	 */
	error = bread(vp, lbprev, osize, NOCRED, &bp);
	if (error) {
		brelse(bp);
		return (error);
	}

	if (bp->b_blkno == bp->b_lblkno) {
		if (lbprev >= MYFS_NDADDR)
			panic("myfs_ffs_realloccg: lbprev out of range");
		bp->b_blkno = myfs_fsbtodb(fs, bprev);
	}

#ifdef QUOTA
	error = myfs_chkdq(ip, btodb(nsize - osize), cred, 0);
	if (error) {
		brelse(bp);
		return (error);
	}
#endif
	/*
	 * Check for extension in the existing location.
	 */
	cg = myfs_dtog(fs, bprev);
	MYFS_LOCK(ump);
	bno = ffs_fragextend(ip, cg, bprev, osize, nsize);
	if (bno) {
		if (bp->b_blkno != myfs_fsbtodb(fs, bno))
			panic("myfs_ffs_realloccg: bad blockno");
		delta = btodb(nsize - osize);
		if (ip->i_flag & IN_SPACECOUNTED) {
			MYFS_LOCK(ump);
			fs->fs_pendingblocks += delta;
			MYFS_UNLOCK(ump);
		}
		MYFS_DIP_SET(ip, i_blocks, MYFS_DIP(ip, i_blocks) + delta);
		if (flags & IO_EXT)
			ip->i_flag |= IN_CHANGE;
		else
			ip->i_flag |= IN_CHANGE | IN_UPDATE;
		allocbuf(bp, nsize);
		bp->b_flags |= B_DONE;
		bzero(bp->b_data + osize, nsize - osize);
		if ((bp->b_flags & (B_MALLOC | B_VMIO)) == B_VMIO)
			vfs_bio_set_valid(bp, osize, nsize - osize);
		*bpp = bp;
		return (0);
	}
	/*
	 * Allocate a new disk location.
	 */
	if (bpref >= fs->fs_size)
		bpref = 0;
	switch ((int)fs->fs_optim) {
	case MYFS_FS_OPTSPACE:
		/*
		 * Allocate an exact sized fragment. Although this makes
		 * best use of space, we will waste time relocating it if
		 * the file continues to grow. If the fragmentation is
		 * less than half of the minimum free reserve, we choose
		 * to begin optimizing for time.
		 */
		request = nsize;
		if (fs->fs_minfree <= 5 ||
		    fs->fs_cstotal.cs_nffree >
		    (off_t)fs->fs_dsize * fs->fs_minfree / (2 * 100))
			break;
		log(LOG_NOTICE, "%s: optimization changed from SPACE to TIME\n",
			fs->fs_fsmnt);
		fs->fs_optim = MYFS_FS_OPTTIME;
		break;
	case MYFS_FS_OPTTIME:
		/*
		 * At this point we have discovered a file that is trying to
		 * grow a small fragment to a larger fragment. To save time,
		 * we allocate a full sized block, then free the unused portion.
		 * If the file continues to grow, the `ffs_fragextend' call
		 * above will be able to grow it in place without further
		 * copying. If aberrant programs cause disk fragmentation to
		 * grow within 2% of the free reserve, we choose to begin
		 * optimizing for space.
		 */
		request = fs->fs_bsize;
		if (fs->fs_cstotal.cs_nffree <
		    (off_t)fs->fs_dsize * (fs->fs_minfree - 2) / 100)
			break;
		log(LOG_NOTICE, "%s: optimization changed from TIME to SPACE\n",
			fs->fs_fsmnt);
		fs->fs_optim = MYFS_FS_OPTSPACE;
		break;
	default:
		printf("dev = %s, optim = %ld, fs = %s\n",
		    devtoname(ip->i_dev), (long)fs->fs_optim, fs->fs_fsmnt);
		panic("myfs_ffs_realloccg: bad optim");
		/* NOTREACHED */
	}
	bno = ffs_hashalloc(ip, cg, bpref, request, ffs_alloccg);
	if (bno > 0) {
		bp->b_blkno = myfs_fsbtodb(fs, bno);
		if (!MYFS_DOINGSOFTDEP(vp))
			myfs_ffs_blkfree(ump, fs, ip->i_devvp, bprev, (long)osize,
			    ip->i_number);
		if (nsize < request)
			myfs_ffs_blkfree(ump, fs, ip->i_devvp,
			    bno + myfs_numfrags(fs, nsize),
			    (long)(request - nsize), ip->i_number);
		delta = btodb(nsize - osize);
		if (ip->i_flag & IN_SPACECOUNTED) {
			MYFS_LOCK(ump);
			fs->fs_pendingblocks += delta;
			MYFS_UNLOCK(ump);
		}
		MYFS_DIP_SET(ip, i_blocks, MYFS_DIP(ip, i_blocks) + delta);
		if (flags & IO_EXT)
			ip->i_flag |= IN_CHANGE;
		else
			ip->i_flag |= IN_CHANGE | IN_UPDATE;
		allocbuf(bp, nsize);
		bp->b_flags |= B_DONE;
		bzero(bp->b_data + osize, nsize - osize);
		if ((bp->b_flags & (B_MALLOC | B_VMIO)) == B_VMIO)
			vfs_bio_set_valid(bp, osize, nsize - osize);
		*bpp = bp;
		return (0);
	}
#ifdef QUOTA
	MYFS_UNLOCK(ump);
	/*
	 * Restore user's disk quota because allocation failed.
	 */
	(void) myfs_chkdq(ip, -btodb(nsize - osize), cred, FORCE);
	MYFS_LOCK(ump);
#endif
nospace:
	/*
	 * no space available
	 */
	if (fs->fs_pendingblocks > 0 && reclaimed == 0) {
		reclaimed = 1;
		myfs_softdep_request_cleanup(fs, vp);
		MYFS_UNLOCK(ump);
		if (bp)
			brelse(bp);
		MYFS_LOCK(ump);
		goto retry;
	}
	MYFS_UNLOCK(ump);
	if (bp)
		brelse(bp);
	if (ppsratecheck(&lastfail, &curfail, 1)) {
		ffs_fserr(fs, ip->i_number, "filesystem full");
		uprintf("\n%s: write failed, filesystem is full\n",
		    fs->fs_fsmnt);
	}
	return (ENOSPC);
}

/*
 * Reallocate a sequence of blocks into a contiguous sequence of blocks.
 *
 * The vnode and an array of buffer pointers for a range of sequential
 * logical blocks to be made contiguous is given. The allocator attempts
 * to find a range of sequential blocks starting as close as possible
 * from the end of the allocation for the logical block immediately
 * preceding the current range. If successful, the physical block numbers
 * in the buffer pointers and in the inode are changed to reflect the new
 * allocation. If unsuccessful, the allocation is left unchanged. The
 * success in doing the reallocation is returned. Note that the error
 * return is not reflected back to the user. Rather the previous block
 * allocation will be used.
 */

SYSCTL_NODE(_vfs, OID_AUTO, myfs, CTLFLAG_RW, 0, "MYFS filesystem");

static int myfs_doasyncfree = 1;
SYSCTL_INT(_vfs_myfs, OID_AUTO, myfs_doasyncfree, CTLFLAG_RW, &myfs_doasyncfree, 0, "");

static int myfs_doreallocblks = 1;
SYSCTL_INT(_vfs_myfs, OID_AUTO, myfs_doreallocblks, CTLFLAG_RW, &myfs_doreallocblks, 0, "");

#ifdef DEBUG
static volatile int prtrealloc = 0;
#endif

int
myfs_ffs_reallocblks(ap)
	struct vop_reallocblks_args /* {
		struct vnode *a_vp;
		struct cluster_save *a_buflist;
	} */ *ap;
{

	if (myfs_doreallocblks == 0)
		return (ENOSPC);
	if (MYFS_VTOI(ap->a_vp)->i_ump->um_fstype == MYFS1)
		return (myfs_ffs_reallocblks_ufs1(ap));
	return (myfs_ffs_reallocblks_ufs2(ap));
}
	
static int
myfs_ffs_reallocblks_ufs1(ap)
	struct vop_reallocblks_args /* {
		struct vnode *a_vp;
		struct cluster_save *a_buflist;
	} */ *ap;
{
	struct myfs_fs *fs;
	struct myfs_inode *ip;
	struct vnode *vp;
	struct buf *sbp, *ebp;
	myfs_ufs1_daddr_t *bap, *sbap, *ebap = 0;
	struct cluster_save *buflist;
	struct myfs_ufsmount *ump;
	myfs_ufs_lbn_t start_lbn, end_lbn;
	myfs_ufs1_daddr_t soff, newblk, blkno;
	myfs_ufs2_daddr_t pref;
	struct myfs_indir start_ap[MYFS_NIADDR + 1], end_ap[MYFS_NIADDR + 1], *idp;
	int i, len, start_lvl, end_lvl, ssize;

	vp = ap->a_vp;
	ip = MYFS_VTOI(vp);
	fs = ip->i_fs;
	ump = ip->i_ump;
	if (fs->fs_contigsumsize <= 0)
		return (ENOSPC);
	buflist = ap->a_buflist;
	len = buflist->bs_nchildren;
	start_lbn = buflist->bs_children[0]->b_lblkno;
	end_lbn = start_lbn + len - 1;
#ifdef INVARIANTS
	for (i = 0; i < len; i++)
		if (!myfs_ffs_checkblk(ip,
		   myfs_dbtofsb(fs, buflist->bs_children[i]->b_blkno), fs->fs_bsize))
			panic("myfs_ffs_reallocblks: unallocated block 1");
	for (i = 1; i < len; i++)
		if (buflist->bs_children[i]->b_lblkno != start_lbn + i)
			panic("myfs_ffs_reallocblks: non-logical cluster");
	blkno = buflist->bs_children[0]->b_blkno;
	ssize = myfs_fsbtodb(fs, fs->fs_frag);
	for (i = 1; i < len - 1; i++)
		if (buflist->bs_children[i]->b_blkno != blkno + (i * ssize))
			panic("myfs_ffs_reallocblks: non-physical cluster %d", i);
#endif
	/*
	 * If the latest allocation is in a new cylinder group, assume that
	 * the filesystem has decided to move and do not force it back to
	 * the previous cylinder group.
	 */
	if (myfs_dtog(fs, myfs_dbtofsb(fs, buflist->bs_children[0]->b_blkno)) !=
	    myfs_dtog(fs, myfs_dbtofsb(fs, buflist->bs_children[len - 1]->b_blkno)))
		return (ENOSPC);
	if (myfs_ufs_getlbns(vp, start_lbn, start_ap, &start_lvl) ||
	    myfs_ufs_getlbns(vp, end_lbn, end_ap, &end_lvl))
		return (ENOSPC);
	/*
	 * Get the starting offset and block map for the first block.
	 */
	if (start_lvl == 0) {
		sbap = &ip->i_din1->di_db[0];
		soff = start_lbn;
	} else {
		idp = &start_ap[start_lvl - 1];
		if (bread(vp, idp->in_lbn, (int)fs->fs_bsize, NOCRED, &sbp)) {
			brelse(sbp);
			return (ENOSPC);
		}
		sbap = (myfs_ufs1_daddr_t *)sbp->b_data;
		soff = idp->in_off;
	}
	/*
	 * If the block range spans two block maps, get the second map.
	 */
	if (end_lvl == 0 || (idp = &end_ap[end_lvl - 1])->in_off + 1 >= len) {
		ssize = len;
	} else {
#ifdef INVARIANTS
		if (start_lvl > 0 &&
		    start_ap[start_lvl - 1].in_lbn == idp->in_lbn)
			panic("ffs_reallocblk: start == end");
#endif
		ssize = len - (idp->in_off + 1);
		if (bread(vp, idp->in_lbn, (int)fs->fs_bsize, NOCRED, &ebp))
			goto fail;
		ebap = (myfs_ufs1_daddr_t *)ebp->b_data;
	}
	/*
	 * Find the preferred location for the cluster.
	 */
	MYFS_LOCK(ump);
	pref = myfs_ffs_blkpref_ufs1(ip, start_lbn, soff, sbap);
	/*
	 * Search the block map looking for an allocation of the desired size.
	 */
	if ((newblk = ffs_hashalloc(ip, myfs_dtog(fs, pref), pref,
	    len, ffs_clusteralloc)) == 0) {
		MYFS_UNLOCK(ump);
		goto fail;
	}
	/*
	 * We have found a new contiguous block.
	 *
	 * First we have to replace the old block pointers with the new
	 * block pointers in the inode and indirect blocks associated
	 * with the file.
	 */
#ifdef DEBUG
	if (prtrealloc)
		printf("realloc: ino %d, lbns %jd-%jd\n\told:", ip->i_number,
		    (intmax_t)start_lbn, (intmax_t)end_lbn);
#endif
	blkno = newblk;
	for (bap = &sbap[soff], i = 0; i < len; i++, blkno += fs->fs_frag) {
		if (i == ssize) {
			bap = ebap;
			soff = -i;
		}
#ifdef INVARIANTS
		if (!myfs_ffs_checkblk(ip,
		   myfs_dbtofsb(fs, buflist->bs_children[i]->b_blkno), fs->fs_bsize))
			panic("myfs_ffs_reallocblks: unallocated block 2");
		if (myfs_dbtofsb(fs, buflist->bs_children[i]->b_blkno) != *bap)
			panic("myfs_ffs_reallocblks: alloc mismatch");
#endif
#ifdef DEBUG
		if (prtrealloc)
			printf(" %d,", *bap);
#endif
		if (MYFS_DOINGSOFTDEP(vp)) {
			if (sbap == &ip->i_din1->di_db[0] && i < ssize)
				myfs_softdep_setup_allocdirect(ip, start_lbn + i,
				    blkno, *bap, fs->fs_bsize, fs->fs_bsize,
				    buflist->bs_children[i]);
			else
				myfs_softdep_setup_allocindir_page(ip, start_lbn + i,
				    i < ssize ? sbp : ebp, soff + i, blkno,
				    *bap, buflist->bs_children[i]);
		}
		*bap++ = blkno;
	}
	/*
	 * Next we must write out the modified inode and indirect blocks.
	 * For strict correctness, the writes should be synchronous since
	 * the old block values may have been written to disk. In practise
	 * they are almost never written, but if we are concerned about
	 * strict correctness, the `doasyncfree' flag should be set to zero.
	 *
	 * The test on `doasyncfree' should be changed to test a flag
	 * that shows whether the associated buffers and inodes have
	 * been written. The flag should be set when the cluster is
	 * started and cleared whenever the buffer or inode is flushed.
	 * We can then check below to see if it is set, and do the
	 * synchronous write only when it has been cleared.
	 */
	if (sbap != &ip->i_din1->di_db[0]) {
		if (myfs_doasyncfree)
			bdwrite(sbp);
		else
			bwrite(sbp);
	} else {
		ip->i_flag |= IN_CHANGE | IN_UPDATE;
		if (!myfs_doasyncfree)
			myfs_ffs_update(vp, 1);
	}
	if (ssize < len) {
		if (myfs_doasyncfree)
			bdwrite(ebp);
		else
			bwrite(ebp);
	}
	/*
	 * Last, free the old blocks and assign the new blocks to the buffers.
	 */
#ifdef DEBUG
	if (prtrealloc)
		printf("\n\tnew:");
#endif
	for (blkno = newblk, i = 0; i < len; i++, blkno += fs->fs_frag) {
		if (!MYFS_DOINGSOFTDEP(vp))
			myfs_ffs_blkfree(ump, fs, ip->i_devvp,
			    myfs_dbtofsb(fs, buflist->bs_children[i]->b_blkno),
			    fs->fs_bsize, ip->i_number);
		buflist->bs_children[i]->b_blkno = myfs_fsbtodb(fs, blkno);
#ifdef INVARIANTS
		if (!myfs_ffs_checkblk(ip,
		   myfs_dbtofsb(fs, buflist->bs_children[i]->b_blkno), fs->fs_bsize))
			panic("myfs_ffs_reallocblks: unallocated block 3");
#endif
#ifdef DEBUG
		if (prtrealloc)
			printf(" %d,", blkno);
#endif
	}
#ifdef DEBUG
	if (prtrealloc) {
		prtrealloc--;
		printf("\n");
	}
#endif
	return (0);

fail:
	if (ssize < len)
		brelse(ebp);
	if (sbap != &ip->i_din1->di_db[0])
		brelse(sbp);
	return (ENOSPC);
}

static int
myfs_ffs_reallocblks_ufs2(ap)
	struct vop_reallocblks_args /* {
		struct vnode *a_vp;
		struct cluster_save *a_buflist;
	} */ *ap;
{
	struct myfs_fs *fs;
	struct myfs_inode *ip;
	struct vnode *vp;
	struct buf *sbp, *ebp;
	myfs_ufs2_daddr_t *bap, *sbap, *ebap = 0;
	struct cluster_save *buflist;
	struct myfs_ufsmount *ump;
	myfs_ufs_lbn_t start_lbn, end_lbn;
	myfs_ufs2_daddr_t soff, newblk, blkno, pref;
	struct myfs_indir start_ap[MYFS_NIADDR + 1], end_ap[MYFS_NIADDR + 1], *idp;
	int i, len, start_lvl, end_lvl, ssize;

	vp = ap->a_vp;
	ip = MYFS_VTOI(vp);
	fs = ip->i_fs;
	ump = ip->i_ump;
	if (fs->fs_contigsumsize <= 0)
		return (ENOSPC);
	buflist = ap->a_buflist;
	len = buflist->bs_nchildren;
	start_lbn = buflist->bs_children[0]->b_lblkno;
	end_lbn = start_lbn + len - 1;
#ifdef INVARIANTS
	for (i = 0; i < len; i++)
		if (!myfs_ffs_checkblk(ip,
		   myfs_dbtofsb(fs, buflist->bs_children[i]->b_blkno), fs->fs_bsize))
			panic("myfs_ffs_reallocblks: unallocated block 1");
	for (i = 1; i < len; i++)
		if (buflist->bs_children[i]->b_lblkno != start_lbn + i)
			panic("myfs_ffs_reallocblks: non-logical cluster");
	blkno = buflist->bs_children[0]->b_blkno;
	ssize = myfs_fsbtodb(fs, fs->fs_frag);
	for (i = 1; i < len - 1; i++)
		if (buflist->bs_children[i]->b_blkno != blkno + (i * ssize))
			panic("myfs_ffs_reallocblks: non-physical cluster %d", i);
#endif
	/*
	 * If the latest allocation is in a new cylinder group, assume that
	 * the filesystem has decided to move and do not force it back to
	 * the previous cylinder group.
	 */
	if (myfs_dtog(fs, myfs_dbtofsb(fs, buflist->bs_children[0]->b_blkno)) !=
	    myfs_dtog(fs, myfs_dbtofsb(fs, buflist->bs_children[len - 1]->b_blkno)))
		return (ENOSPC);
	if (myfs_ufs_getlbns(vp, start_lbn, start_ap, &start_lvl) ||
	    myfs_ufs_getlbns(vp, end_lbn, end_ap, &end_lvl))
		return (ENOSPC);
	/*
	 * Get the starting offset and block map for the first block.
	 */
	if (start_lvl == 0) {
		sbap = &ip->i_din2->di_db[0];
		soff = start_lbn;
	} else {
		idp = &start_ap[start_lvl - 1];
		if (bread(vp, idp->in_lbn, (int)fs->fs_bsize, NOCRED, &sbp)) {
			brelse(sbp);
			return (ENOSPC);
		}
		sbap = (myfs_ufs2_daddr_t *)sbp->b_data;
		soff = idp->in_off;
	}
	/*
	 * If the block range spans two block maps, get the second map.
	 */
	if (end_lvl == 0 || (idp = &end_ap[end_lvl - 1])->in_off + 1 >= len) {
		ssize = len;
	} else {
#ifdef INVARIANTS
		if (start_lvl > 0 &&
		    start_ap[start_lvl - 1].in_lbn == idp->in_lbn)
			panic("ffs_reallocblk: start == end");
#endif
		ssize = len - (idp->in_off + 1);
		if (bread(vp, idp->in_lbn, (int)fs->fs_bsize, NOCRED, &ebp))
			goto fail;
		ebap = (myfs_ufs2_daddr_t *)ebp->b_data;
	}
	/*
	 * Find the preferred location for the cluster.
	 */
	MYFS_LOCK(ump);
	pref = myfs_ffs_blkpref_ufs2(ip, start_lbn, soff, sbap);
	/*
	 * Search the block map looking for an allocation of the desired size.
	 */
	if ((newblk = ffs_hashalloc(ip, myfs_dtog(fs, pref), pref,
	    len, ffs_clusteralloc)) == 0) {
		MYFS_UNLOCK(ump);
		goto fail;
	}
	/*
	 * We have found a new contiguous block.
	 *
	 * First we have to replace the old block pointers with the new
	 * block pointers in the inode and indirect blocks associated
	 * with the file.
	 */
#ifdef DEBUG
	if (prtrealloc)
		printf("realloc: ino %d, lbns %jd-%jd\n\told:", ip->i_number,
		    (intmax_t)start_lbn, (intmax_t)end_lbn);
#endif
	blkno = newblk;
	for (bap = &sbap[soff], i = 0; i < len; i++, blkno += fs->fs_frag) {
		if (i == ssize) {
			bap = ebap;
			soff = -i;
		}
#ifdef INVARIANTS
		if (!myfs_ffs_checkblk(ip,
		   myfs_dbtofsb(fs, buflist->bs_children[i]->b_blkno), fs->fs_bsize))
			panic("myfs_ffs_reallocblks: unallocated block 2");
		if (myfs_dbtofsb(fs, buflist->bs_children[i]->b_blkno) != *bap)
			panic("myfs_ffs_reallocblks: alloc mismatch");
#endif
#ifdef DEBUG
		if (prtrealloc)
			printf(" %jd,", (intmax_t)*bap);
#endif
		if (MYFS_DOINGSOFTDEP(vp)) {
			if (sbap == &ip->i_din2->di_db[0] && i < ssize)
				myfs_softdep_setup_allocdirect(ip, start_lbn + i,
				    blkno, *bap, fs->fs_bsize, fs->fs_bsize,
				    buflist->bs_children[i]);
			else
				myfs_softdep_setup_allocindir_page(ip, start_lbn + i,
				    i < ssize ? sbp : ebp, soff + i, blkno,
				    *bap, buflist->bs_children[i]);
		}
		*bap++ = blkno;
	}
	/*
	 * Next we must write out the modified inode and indirect blocks.
	 * For strict correctness, the writes should be synchronous since
	 * the old block values may have been written to disk. In practise
	 * they are almost never written, but if we are concerned about
	 * strict correctness, the `doasyncfree' flag should be set to zero.
	 *
	 * The test on `doasyncfree' should be changed to test a flag
	 * that shows whether the associated buffers and inodes have
	 * been written. The flag should be set when the cluster is
	 * started and cleared whenever the buffer or inode is flushed.
	 * We can then check below to see if it is set, and do the
	 * synchronous write only when it has been cleared.
	 */
	if (sbap != &ip->i_din2->di_db[0]) {
		if (myfs_doasyncfree)
			bdwrite(sbp);
		else
			bwrite(sbp);
	} else {
		ip->i_flag |= IN_CHANGE | IN_UPDATE;
		if (!myfs_doasyncfree)
			myfs_ffs_update(vp, 1);
	}
	if (ssize < len) {
		if (myfs_doasyncfree)
			bdwrite(ebp);
		else
			bwrite(ebp);
	}
	/*
	 * Last, free the old blocks and assign the new blocks to the buffers.
	 */
#ifdef DEBUG
	if (prtrealloc)
		printf("\n\tnew:");
#endif
	for (blkno = newblk, i = 0; i < len; i++, blkno += fs->fs_frag) {
		if (!MYFS_DOINGSOFTDEP(vp))
			myfs_ffs_blkfree(ump, fs, ip->i_devvp,
			    myfs_dbtofsb(fs, buflist->bs_children[i]->b_blkno),
			    fs->fs_bsize, ip->i_number);
		buflist->bs_children[i]->b_blkno = myfs_fsbtodb(fs, blkno);
#ifdef INVARIANTS
		if (!myfs_ffs_checkblk(ip,
		   myfs_dbtofsb(fs, buflist->bs_children[i]->b_blkno), fs->fs_bsize))
			panic("myfs_ffs_reallocblks: unallocated block 3");
#endif
#ifdef DEBUG
		if (prtrealloc)
			printf(" %jd,", (intmax_t)blkno);
#endif
	}
#ifdef DEBUG
	if (prtrealloc) {
		prtrealloc--;
		printf("\n");
	}
#endif
	return (0);

fail:
	if (ssize < len)
		brelse(ebp);
	if (sbap != &ip->i_din2->di_db[0])
		brelse(sbp);
	return (ENOSPC);
}

/*
 * Allocate an inode in the filesystem.
 *
 * If allocating a directory, use myfs_ffs_dirpref to select the inode.
 * If allocating in a directory, the following hierarchy is followed:
 *   1) allocate the preferred inode.
 *   2) allocate an inode in the same cylinder group.
 *   3) quadradically rehash into other cylinder groups, until an
 *      available inode is located.
 * If no inode preference is given the following hierarchy is used
 * to allocate an inode:
 *   1) allocate an inode in cylinder group 0.
 *   2) quadradically rehash into other cylinder groups, until an
 *      available inode is located.
 */
int
myfs_ffs_valloc(pvp, mode, cred, vpp)
	struct vnode *pvp;
	int mode;
	struct ucred *cred;
	struct vnode **vpp;
{
	struct myfs_inode *pip;
	struct myfs_fs *fs;
	struct myfs_inode *ip;
	struct timespec ts;
	struct myfs_ufsmount *ump;
	ino_t ino, ipref;
	int cg, error, error1;
	static struct timeval lastfail;
	static int curfail;

	*vpp = NULL;
	pip = MYFS_VTOI(pvp);
	fs = pip->i_fs;
	ump = pip->i_ump;

	MYFS_LOCK(ump);
	if (fs->fs_cstotal.cs_nifree == 0)
		goto noinodes;

	if ((mode & IFMT) == IFDIR)
		ipref = myfs_ffs_dirpref(pip);
	else
		ipref = pip->i_number;
	if (ipref >= fs->fs_ncg * fs->fs_ipg)
		ipref = 0;
	cg = myfs_ino_to_cg(fs, ipref);
	/*
	 * Track number of dirs created one after another
	 * in a same cg without intervening by files.
	 */
	if ((mode & IFMT) == IFDIR) {
		if (fs->fs_contigdirs[cg] < 255)
			fs->fs_contigdirs[cg]++;
	} else {
		if (fs->fs_contigdirs[cg] > 0)
			fs->fs_contigdirs[cg]--;
	}
	ino = (ino_t)ffs_hashalloc(pip, cg, ipref, mode,
					(allocfcn_t *)ffs_nodealloccg);
	if (ino == 0)
		goto noinodes;
	error = myfs_ffs_vget(pvp->v_mount, ino, LK_EXCLUSIVE, vpp);
	if (error) {
		error1 = myfs_ffs_vgetf(pvp->v_mount, ino, LK_EXCLUSIVE, vpp,
		    MYFS_FFSV_FORCEINSMQ);
		myfs_ffs_vfree(pvp, ino, mode);
		if (error1 == 0) {
			ip = MYFS_VTOI(*vpp);
			if (ip->i_mode)
				goto dup_alloc;
			ip->i_flag |= IN_MODIFIED;
			vput(*vpp);
		}
		return (error);
	}
	ip = MYFS_VTOI(*vpp);
	if (ip->i_mode) {
dup_alloc:
		printf("mode = 0%o, inum = %lu, fs = %s\n",
		    ip->i_mode, (u_long)ip->i_number, fs->fs_fsmnt);
		panic("myfs_ffs_valloc: dup alloc");
	}
	if (MYFS_DIP(ip, i_blocks) && (fs->fs_flags & MYFS_FS_UNCLEAN) == 0) {  /* XXX */
		printf("free inode %s/%lu had %ld blocks\n",
		    fs->fs_fsmnt, (u_long)ino, (long)MYFS_DIP(ip, i_blocks));
		MYFS_DIP_SET(ip, i_blocks, 0);
	}
	ip->i_flags = 0;
	MYFS_DIP_SET(ip, i_flags, 0);
	/*
	 * Set up a new generation number for this inode.
	 */
	if (ip->i_gen == 0 || ++ip->i_gen == 0)
		ip->i_gen = arc4random() / 2 + 1;
	MYFS_DIP_SET(ip, i_gen, ip->i_gen);
	if (fs->fs_magic == MYFS_FS_UFS2_MAGIC) {
		vfs_timestamp(&ts);
		ip->i_din2->di_birthtime = ts.tv_sec;
		ip->i_din2->di_birthnsec = ts.tv_nsec;
	}
	ip->i_flag = 0;
	vnode_destroy_vobject(*vpp);
	(*vpp)->v_type = VNON;
	if (fs->fs_magic == MYFS_FS_UFS2_MAGIC)
		(*vpp)->v_op = &myfs_ffs_vnodeops2;
	else
		(*vpp)->v_op = &myfs_ffs_vnodeops1;
	return (0);
noinodes:
	MYFS_UNLOCK(ump);
	if (ppsratecheck(&lastfail, &curfail, 1)) {
		ffs_fserr(fs, pip->i_number, "out of inodes");
		uprintf("\n%s: create/symlink failed, no inodes free\n",
		    fs->fs_fsmnt);
	}
	return (ENOSPC);
}

/*
 * Find a cylinder group to place a directory.
 *
 * The policy implemented by this algorithm is to allocate a
 * directory inode in the same cylinder group as its parent
 * directory, but also to reserve space for its files inodes
 * and data. Restrict the number of directories which may be
 * allocated one after another in the same cylinder group
 * without intervening allocation of files.
 *
 * If we allocate a first level directory then force allocation
 * in another cylinder group.
 */
static ino_t
myfs_ffs_dirpref(pip)
	struct myfs_inode *pip;
{
	struct myfs_fs *fs;
	int cg, prefcg, dirsize, cgsize;
	int avgifree, avgbfree, avgndir, curdirsize;
	int minifree, minbfree, maxndir;
	int mincg, minndir;
	int maxcontigdirs;

	mtx_assert(MYFS_MTX(pip->i_ump), MA_OWNED);
	fs = pip->i_fs;

	avgifree = fs->fs_cstotal.cs_nifree / fs->fs_ncg;
	avgbfree = fs->fs_cstotal.cs_nbfree / fs->fs_ncg;
	avgndir = fs->fs_cstotal.cs_ndir / fs->fs_ncg;

	/*
	 * Force allocation in another cg if creating a first level dir.
	 */
	ASSERT_VOP_LOCKED(MYFS_ITOV(pip), "myfs_ffs_dirpref");
	if (MYFS_ITOV(pip)->v_vflag & VV_ROOT) {
		prefcg = arc4random() % fs->fs_ncg;
		mincg = prefcg;
		minndir = fs->fs_ipg;
		for (cg = prefcg; cg < fs->fs_ncg; cg++)
			if (fs->fs_cs(fs, cg).cs_ndir < minndir &&
			    fs->fs_cs(fs, cg).cs_nifree >= avgifree &&
			    fs->fs_cs(fs, cg).cs_nbfree >= avgbfree) {
				mincg = cg;
				minndir = fs->fs_cs(fs, cg).cs_ndir;
			}
		for (cg = 0; cg < prefcg; cg++)
			if (fs->fs_cs(fs, cg).cs_ndir < minndir &&
			    fs->fs_cs(fs, cg).cs_nifree >= avgifree &&
			    fs->fs_cs(fs, cg).cs_nbfree >= avgbfree) {
				mincg = cg;
				minndir = fs->fs_cs(fs, cg).cs_ndir;
			}
		return ((ino_t)(fs->fs_ipg * mincg));
	}

	/*
	 * Count various limits which used for
	 * optimal allocation of a directory inode.
	 */
	maxndir = min(avgndir + fs->fs_ipg / 16, fs->fs_ipg);
	minifree = avgifree - avgifree / 4;
	if (minifree < 1)
		minifree = 1;
	minbfree = avgbfree - avgbfree / 4;
	if (minbfree < 1)
		minbfree = 1;
	cgsize = fs->fs_fsize * fs->fs_fpg;
	dirsize = fs->fs_avgfilesize * fs->fs_avgfpdir;
	curdirsize = avgndir ? (cgsize - avgbfree * fs->fs_bsize) / avgndir : 0;
	if (dirsize < curdirsize)
		dirsize = curdirsize;
	if (dirsize <= 0)
		maxcontigdirs = 0;		/* dirsize overflowed */
	else
		maxcontigdirs = min((avgbfree * fs->fs_bsize) / dirsize, 255);
	if (fs->fs_avgfpdir > 0)
		maxcontigdirs = min(maxcontigdirs,
				    fs->fs_ipg / fs->fs_avgfpdir);
	if (maxcontigdirs == 0)
		maxcontigdirs = 1;

	/*
	 * Limit number of dirs in one cg and reserve space for 
	 * regular files, but only if we have no deficit in
	 * inodes or space.
	 */
	prefcg = myfs_ino_to_cg(fs, pip->i_number);
	for (cg = prefcg; cg < fs->fs_ncg; cg++)
		if (fs->fs_cs(fs, cg).cs_ndir < maxndir &&
		    fs->fs_cs(fs, cg).cs_nifree >= minifree &&
	    	    fs->fs_cs(fs, cg).cs_nbfree >= minbfree) {
			if (fs->fs_contigdirs[cg] < maxcontigdirs)
				return ((ino_t)(fs->fs_ipg * cg));
		}
	for (cg = 0; cg < prefcg; cg++)
		if (fs->fs_cs(fs, cg).cs_ndir < maxndir &&
		    fs->fs_cs(fs, cg).cs_nifree >= minifree &&
	    	    fs->fs_cs(fs, cg).cs_nbfree >= minbfree) {
			if (fs->fs_contigdirs[cg] < maxcontigdirs)
				return ((ino_t)(fs->fs_ipg * cg));
		}
	/*
	 * This is a backstop when we have deficit in space.
	 */
	for (cg = prefcg; cg < fs->fs_ncg; cg++)
		if (fs->fs_cs(fs, cg).cs_nifree >= avgifree)
			return ((ino_t)(fs->fs_ipg * cg));
	for (cg = 0; cg < prefcg; cg++)
		if (fs->fs_cs(fs, cg).cs_nifree >= avgifree)
			break;
	return ((ino_t)(fs->fs_ipg * cg));
}

/*
 * Select the desired position for the next block in a file.  The file is
 * logically divided into sections. The first section is composed of the
 * direct blocks. Each additional section contains fs_maxbpg blocks.
 *
 * If no blocks have been allocated in the first section, the policy is to
 * request a block in the same cylinder group as the inode that describes
 * the file. If no blocks have been allocated in any other section, the
 * policy is to place the section in a cylinder group with a greater than
 * average number of free blocks.  An appropriate cylinder group is found
 * by using a rotor that sweeps the cylinder groups. When a new group of
 * blocks is needed, the sweep begins in the cylinder group following the
 * cylinder group from which the previous allocation was made. The sweep
 * continues until a cylinder group with greater than the average number
 * of free blocks is found. If the allocation is for the first block in an
 * indirect block, the information on the previous allocation is unavailable;
 * here a best guess is made based upon the logical block number being
 * allocated.
 *
 * If a section is already partially allocated, the policy is to
 * contiguously allocate fs_maxcontig blocks. The end of one of these
 * contiguous blocks and the beginning of the next is laid out
 * contiguously if possible.
 */
myfs_ufs2_daddr_t
myfs_ffs_blkpref_ufs1(ip, lbn, indx, bap)
	struct myfs_inode *ip;
	myfs_ufs_lbn_t lbn;
	int indx;
	myfs_ufs1_daddr_t *bap;
{
	struct myfs_fs *fs;
	int cg;
	int avgbfree, startcg;

	mtx_assert(MYFS_MTX(ip->i_ump), MA_OWNED);
	fs = ip->i_fs;
	if (indx % fs->fs_maxbpg == 0 || bap[indx - 1] == 0) {
		if (lbn < MYFS_NDADDR + MYFS_NINDIR(fs)) {
			cg = myfs_ino_to_cg(fs, ip->i_number);
			return (myfs_cgbase(fs, cg) + fs->fs_frag);
		}
		/*
		 * Find a cylinder with greater than average number of
		 * unused data blocks.
		 */
		if (indx == 0 || bap[indx - 1] == 0)
			startcg =
			    myfs_ino_to_cg(fs, ip->i_number) + lbn / fs->fs_maxbpg;
		else
			startcg = myfs_dtog(fs, bap[indx - 1]) + 1;
		startcg %= fs->fs_ncg;
		avgbfree = fs->fs_cstotal.cs_nbfree / fs->fs_ncg;
		for (cg = startcg; cg < fs->fs_ncg; cg++)
			if (fs->fs_cs(fs, cg).cs_nbfree >= avgbfree) {
				fs->fs_cgrotor = cg;
				return (myfs_cgbase(fs, cg) + fs->fs_frag);
			}
		for (cg = 0; cg <= startcg; cg++)
			if (fs->fs_cs(fs, cg).cs_nbfree >= avgbfree) {
				fs->fs_cgrotor = cg;
				return (myfs_cgbase(fs, cg) + fs->fs_frag);
			}
		return (0);
	}
	/*
	 * We just always try to lay things out contiguously.
	 */
	return (bap[indx - 1] + fs->fs_frag);
}

/*
 * Same as above, but for MYFS2
 */
myfs_ufs2_daddr_t
myfs_ffs_blkpref_ufs2(ip, lbn, indx, bap)
	struct myfs_inode *ip;
	myfs_ufs_lbn_t lbn;
	int indx;
	myfs_ufs2_daddr_t *bap;
{
	struct myfs_fs *fs;
	int cg;
	int avgbfree, startcg;

	mtx_assert(MYFS_MTX(ip->i_ump), MA_OWNED);
	fs = ip->i_fs;
	if (indx % fs->fs_maxbpg == 0 || bap[indx - 1] == 0) {
		if (lbn < MYFS_NDADDR + MYFS_NINDIR(fs)) {
			cg = myfs_ino_to_cg(fs, ip->i_number);
			return (myfs_cgbase(fs, cg) + fs->fs_frag);
		}
		/*
		 * Find a cylinder with greater than average number of
		 * unused data blocks.
		 */
		if (indx == 0 || bap[indx - 1] == 0)
			startcg =
			    myfs_ino_to_cg(fs, ip->i_number) + lbn / fs->fs_maxbpg;
		else
			startcg = myfs_dtog(fs, bap[indx - 1]) + 1;
		startcg %= fs->fs_ncg;
		avgbfree = fs->fs_cstotal.cs_nbfree / fs->fs_ncg;
		for (cg = startcg; cg < fs->fs_ncg; cg++)
			if (fs->fs_cs(fs, cg).cs_nbfree >= avgbfree) {
				fs->fs_cgrotor = cg;
				return (myfs_cgbase(fs, cg) + fs->fs_frag);
			}
		for (cg = 0; cg <= startcg; cg++)
			if (fs->fs_cs(fs, cg).cs_nbfree >= avgbfree) {
				fs->fs_cgrotor = cg;
				return (myfs_cgbase(fs, cg) + fs->fs_frag);
			}
		return (0);
	}
	/*
	 * We just always try to lay things out contiguously.
	 */
	return (bap[indx - 1] + fs->fs_frag);
}

/*
 * Implement the cylinder overflow algorithm.
 *
 * The policy implemented by this algorithm is:
 *   1) allocate the block in its requested cylinder group.
 *   2) quadradically rehash on the cylinder group number.
 *   3) brute force search for a free block.
 *
 * Must be called with the MYFS lock held.  Will release the lock on success
 * and return with it held on failure.
 */
/*VARARGS5*/
static myfs_ufs2_daddr_t
ffs_hashalloc(ip, cg, pref, size, allocator)
	struct myfs_inode *ip;
	int cg;
	myfs_ufs2_daddr_t pref;
	int size;	/* size for data blocks, mode for inodes */
	allocfcn_t *allocator;
{
	struct myfs_fs *fs;
	myfs_ufs2_daddr_t result;
	int i, icg = cg;

	mtx_assert(MYFS_MTX(ip->i_ump), MA_OWNED);
#ifdef INVARIANTS
	if (MYFS_ITOV(ip)->v_mount->mnt_kern_flag & MNTK_SUSPENDED)
		panic("ffs_hashalloc: allocation on suspended filesystem");
#endif
	fs = ip->i_fs;
	/*
	 * 1: preferred cylinder group
	 */
	result = (*allocator)(ip, cg, pref, size);
	if (result)
		return (result);
	/*
	 * 2: quadratic rehash
	 */
	for (i = 1; i < fs->fs_ncg; i *= 2) {
		cg += i;
		if (cg >= fs->fs_ncg)
			cg -= fs->fs_ncg;
		result = (*allocator)(ip, cg, 0, size);
		if (result)
			return (result);
	}
	/*
	 * 3: brute force search
	 * Note that we start at i == 2, since 0 was checked initially,
	 * and 1 is always checked in the quadratic rehash.
	 */
	cg = (icg + 2) % fs->fs_ncg;
	for (i = 2; i < fs->fs_ncg; i++) {
		result = (*allocator)(ip, cg, 0, size);
		if (result)
			return (result);
		cg++;
		if (cg == fs->fs_ncg)
			cg = 0;
	}
	return (0);
}

/*
 * Determine whether a fragment can be extended.
 *
 * Check to see if the necessary fragments are available, and
 * if they are, allocate them.
 */
static myfs_ufs2_daddr_t
ffs_fragextend(ip, cg, bprev, osize, nsize)
	struct myfs_inode *ip;
	int cg;
	myfs_ufs2_daddr_t bprev;
	int osize, nsize;
{
	struct myfs_fs *fs;
	struct myfs_cg *cgp;
	struct buf *bp;
	struct myfs_ufsmount *ump;
	int nffree;
	long bno;
	int frags, bbase;
	int i, error;
	u_int8_t *blksfree;

	ump = ip->i_ump;
	fs = ip->i_fs;
	if (fs->fs_cs(fs, cg).cs_nffree < myfs_numfrags(fs, nsize - osize))
		return (0);
	frags = myfs_numfrags(fs, nsize);
	bbase = myfs_fragnum(fs, bprev);
	if (bbase > myfs_fragnum(fs, (bprev + frags - 1))) {
		/* cannot extend across a block boundary */
		return (0);
	}
	MYFS_UNLOCK(ump);
	error = bread(ip->i_devvp, myfs_fsbtodb(fs, myfs_cgtod(fs, cg)),
		(int)fs->fs_cgsize, NOCRED, &bp);
	if (error)
		goto fail;
	cgp = (struct myfs_cg *)bp->b_data;
	if (!myfs_cg_chkmagic(cgp))
		goto fail;
	bp->b_xflags |= BX_BKGRDWRITE;
	cgp->cg_old_time = cgp->cg_time = time_second;
	bno = myfs_dtogd(fs, bprev);
	blksfree = myfs_cg_blksfree(cgp);
	for (i = myfs_numfrags(fs, osize); i < frags; i++)
		if (isclr(blksfree, bno + i))
			goto fail;
	/*
	 * the current fragment can be extended
	 * deduct the count on fragment being extended into
	 * increase the count on the remaining fragment (if any)
	 * allocate the extended piece
	 */
	for (i = frags; i < fs->fs_frag - bbase; i++)
		if (isclr(blksfree, bno + i))
			break;
	cgp->cg_frsum[i - myfs_numfrags(fs, osize)]--;
	if (i != frags)
		cgp->cg_frsum[i - frags]++;
	for (i = myfs_numfrags(fs, osize), nffree = 0; i < frags; i++) {
		clrbit(blksfree, bno + i);
		cgp->cg_cs.cs_nffree--;
		nffree++;
	}
	MYFS_LOCK(ump);
	fs->fs_cstotal.cs_nffree -= nffree;
	fs->fs_cs(fs, cg).cs_nffree -= nffree;
	fs->fs_fmod = 1;
	MYFS_ACTIVECLEAR(fs, cg);
	MYFS_UNLOCK(ump);
	if (MYFS_DOINGSOFTDEP(MYFS_ITOV(ip)))
		myfs_softdep_setup_blkmapdep(bp, MYFSTOVFS(ump), bprev);
	bdwrite(bp);
	return (bprev);

fail:
	brelse(bp);
	MYFS_LOCK(ump);
	return (0);

}

/*
 * Determine whether a block can be allocated.
 *
 * Check to see if a block of the appropriate size is available,
 * and if it is, allocate it.
 */
static myfs_ufs2_daddr_t
ffs_alloccg(ip, cg, bpref, size)
	struct myfs_inode *ip;
	int cg;
	myfs_ufs2_daddr_t bpref;
	int size;
{
	struct myfs_fs *fs;
	struct myfs_cg *cgp;
	struct buf *bp;
	struct myfs_ufsmount *ump;
	myfs_ufs1_daddr_t bno;
	myfs_ufs2_daddr_t blkno;
	int i, allocsiz, error, frags;
	u_int8_t *blksfree;

	ump = ip->i_ump;
	fs = ip->i_fs;
	if (fs->fs_cs(fs, cg).cs_nbfree == 0 && size == fs->fs_bsize)
		return (0);
	MYFS_UNLOCK(ump);
	error = bread(ip->i_devvp, myfs_fsbtodb(fs, myfs_cgtod(fs, cg)),
		(int)fs->fs_cgsize, NOCRED, &bp);
	if (error)
		goto fail;
	cgp = (struct myfs_cg *)bp->b_data;
	if (!myfs_cg_chkmagic(cgp) ||
	    (cgp->cg_cs.cs_nbfree == 0 && size == fs->fs_bsize))
		goto fail;
	bp->b_xflags |= BX_BKGRDWRITE;
	cgp->cg_old_time = cgp->cg_time = time_second;
	if (size == fs->fs_bsize) {
		MYFS_LOCK(ump);
		blkno = ffs_alloccgblk(ip, bp, bpref);
		MYFS_ACTIVECLEAR(fs, cg);
		MYFS_UNLOCK(ump);
		bdwrite(bp);
		return (blkno);
	}
	/*
	 * check to see if any fragments are already available
	 * allocsiz is the size which will be allocated, hacking
	 * it down to a smaller size if necessary
	 */
	blksfree = myfs_cg_blksfree(cgp);
	frags = myfs_numfrags(fs, size);
	for (allocsiz = frags; allocsiz < fs->fs_frag; allocsiz++)
		if (cgp->cg_frsum[allocsiz] != 0)
			break;
	if (allocsiz == fs->fs_frag) {
		/*
		 * no fragments were available, so a block will be
		 * allocated, and hacked up
		 */
		if (cgp->cg_cs.cs_nbfree == 0)
			goto fail;
		MYFS_LOCK(ump);
		blkno = ffs_alloccgblk(ip, bp, bpref);
		bno = myfs_dtogd(fs, blkno);
		for (i = frags; i < fs->fs_frag; i++)
			setbit(blksfree, bno + i);
		i = fs->fs_frag - frags;
		cgp->cg_cs.cs_nffree += i;
		fs->fs_cstotal.cs_nffree += i;
		fs->fs_cs(fs, cg).cs_nffree += i;
		fs->fs_fmod = 1;
		cgp->cg_frsum[i]++;
		MYFS_ACTIVECLEAR(fs, cg);
		MYFS_UNLOCK(ump);
		bdwrite(bp);
		return (blkno);
	}
	bno = ffs_mapsearch(fs, cgp, bpref, allocsiz);
	if (bno < 0)
		goto fail;
	for (i = 0; i < frags; i++)
		clrbit(blksfree, bno + i);
	cgp->cg_cs.cs_nffree -= frags;
	cgp->cg_frsum[allocsiz]--;
	if (frags != allocsiz)
		cgp->cg_frsum[allocsiz - frags]++;
	MYFS_LOCK(ump);
	fs->fs_cstotal.cs_nffree -= frags;
	fs->fs_cs(fs, cg).cs_nffree -= frags;
	fs->fs_fmod = 1;
	blkno = myfs_cgbase(fs, cg) + bno;
	MYFS_ACTIVECLEAR(fs, cg);
	MYFS_UNLOCK(ump);
	if (MYFS_DOINGSOFTDEP(MYFS_ITOV(ip)))
		myfs_softdep_setup_blkmapdep(bp, MYFSTOVFS(ump), blkno);
	bdwrite(bp);
	return (blkno);

fail:
	brelse(bp);
	MYFS_LOCK(ump);
	return (0);
}

/*
 * Allocate a block in a cylinder group.
 *
 * This algorithm implements the following policy:
 *   1) allocate the requested block.
 *   2) allocate a rotationally optimal block in the same cylinder.
 *   3) allocate the next available block on the block rotor for the
 *      specified cylinder group.
 * Note that this routine only allocates fs_bsize blocks; these
 * blocks may be fragmented by the routine that allocates them.
 */
static myfs_ufs2_daddr_t
ffs_alloccgblk(ip, bp, bpref)
	struct myfs_inode *ip;
	struct buf *bp;
	myfs_ufs2_daddr_t bpref;
{
	struct myfs_fs *fs;
	struct myfs_cg *cgp;
	struct myfs_ufsmount *ump;
	myfs_ufs1_daddr_t bno;
	myfs_ufs2_daddr_t blkno;
	u_int8_t *blksfree;

	fs = ip->i_fs;
	ump = ip->i_ump;
	mtx_assert(MYFS_MTX(ump), MA_OWNED);
	cgp = (struct myfs_cg *)bp->b_data;
	blksfree = myfs_cg_blksfree(cgp);
	if (bpref == 0 || myfs_dtog(fs, bpref) != cgp->cg_cgx) {
		bpref = cgp->cg_rotor;
	} else {
		bpref = myfs_blknum(fs, bpref);
		bno = myfs_dtogd(fs, bpref);
		/*
		 * if the requested block is available, use it
		 */
		if (myfs_ffs_isblock(fs, blksfree, myfs_fragstoblks(fs, bno)))
			goto gotit;
	}
	/*
	 * Take the next available block in this cylinder group.
	 */
	bno = ffs_mapsearch(fs, cgp, bpref, (int)fs->fs_frag);
	if (bno < 0)
		return (0);
	cgp->cg_rotor = bno;
gotit:
	blkno = myfs_fragstoblks(fs, bno);
	myfs_ffs_clrblock(fs, blksfree, (long)blkno);
	myfs_ffs_clusteracct(ump, fs, cgp, blkno, -1);
	cgp->cg_cs.cs_nbfree--;
	fs->fs_cstotal.cs_nbfree--;
	fs->fs_cs(fs, cgp->cg_cgx).cs_nbfree--;
	fs->fs_fmod = 1;
	blkno = myfs_cgbase(fs, cgp->cg_cgx) + bno;
	/* XXX Fixme. */
	MYFS_UNLOCK(ump);
	if (MYFS_DOINGSOFTDEP(MYFS_ITOV(ip)))
		myfs_softdep_setup_blkmapdep(bp, MYFSTOVFS(ump), blkno);
	MYFS_LOCK(ump);
	return (blkno);
}

/*
 * Determine whether a cluster can be allocated.
 *
 * We do not currently check for optimal rotational layout if there
 * are multiple choices in the same cylinder group. Instead we just
 * take the first one that we find following bpref.
 */
static myfs_ufs2_daddr_t
ffs_clusteralloc(ip, cg, bpref, len)
	struct myfs_inode *ip;
	int cg;
	myfs_ufs2_daddr_t bpref;
	int len;
{
	struct myfs_fs *fs;
	struct myfs_cg *cgp;
	struct buf *bp;
	struct myfs_ufsmount *ump;
	int i, run, bit, map, got;
	myfs_ufs2_daddr_t bno;
	u_char *mapp;
	int32_t *lp;
	u_int8_t *blksfree;

	fs = ip->i_fs;
	ump = ip->i_ump;
	if (fs->fs_maxcluster[cg] < len)
		return (0);
	MYFS_UNLOCK(ump);
	if (bread(ip->i_devvp, myfs_fsbtodb(fs, myfs_cgtod(fs, cg)), (int)fs->fs_cgsize,
	    NOCRED, &bp))
		goto fail_lock;
	cgp = (struct myfs_cg *)bp->b_data;
	if (!myfs_cg_chkmagic(cgp))
		goto fail_lock;
	bp->b_xflags |= BX_BKGRDWRITE;
	/*
	 * Check to see if a cluster of the needed size (or bigger) is
	 * available in this cylinder group.
	 */
	lp = &myfs_cg_clustersum(cgp)[len];
	for (i = len; i <= fs->fs_contigsumsize; i++)
		if (*lp++ > 0)
			break;
	if (i > fs->fs_contigsumsize) {
		/*
		 * This is the first time looking for a cluster in this
		 * cylinder group. Update the cluster summary information
		 * to reflect the true maximum sized cluster so that
		 * future cluster allocation requests can avoid reading
		 * the cylinder group map only to find no clusters.
		 */
		lp = &myfs_cg_clustersum(cgp)[len - 1];
		for (i = len - 1; i > 0; i--)
			if (*lp-- > 0)
				break;
		MYFS_LOCK(ump);
		fs->fs_maxcluster[cg] = i;
		goto fail;
	}
	/*
	 * Search the cluster map to find a big enough cluster.
	 * We take the first one that we find, even if it is larger
	 * than we need as we prefer to get one close to the previous
	 * block allocation. We do not search before the current
	 * preference point as we do not want to allocate a block
	 * that is allocated before the previous one (as we will
	 * then have to wait for another pass of the elevator
	 * algorithm before it will be read). We prefer to fail and
	 * be recalled to try an allocation in the next cylinder group.
	 */
	if (myfs_dtog(fs, bpref) != cg)
		bpref = 0;
	else
		bpref = myfs_fragstoblks(fs, myfs_dtogd(fs, myfs_blknum(fs, bpref)));
	mapp = &myfs_cg_clustersfree(cgp)[bpref / NBBY];
	map = *mapp++;
	bit = 1 << (bpref % NBBY);
	for (run = 0, got = bpref; got < cgp->cg_nclusterblks; got++) {
		if ((map & bit) == 0) {
			run = 0;
		} else {
			run++;
			if (run == len)
				break;
		}
		if ((got & (NBBY - 1)) != (NBBY - 1)) {
			bit <<= 1;
		} else {
			map = *mapp++;
			bit = 1;
		}
	}
	if (got >= cgp->cg_nclusterblks)
		goto fail_lock;
	/*
	 * Allocate the cluster that we have found.
	 */
	blksfree = myfs_cg_blksfree(cgp);
	for (i = 1; i <= len; i++)
		if (!myfs_ffs_isblock(fs, blksfree, got - run + i))
			panic("ffs_clusteralloc: map mismatch");
	bno = myfs_cgbase(fs, cg) + myfs_blkstofrags(fs, got - run + 1);
	if (myfs_dtog(fs, bno) != cg)
		panic("ffs_clusteralloc: allocated out of group");
	len = myfs_blkstofrags(fs, len);
	MYFS_LOCK(ump);
	for (i = 0; i < len; i += fs->fs_frag)
		if (ffs_alloccgblk(ip, bp, bno + i) != bno + i)
			panic("ffs_clusteralloc: lost block");
	MYFS_ACTIVECLEAR(fs, cg);
	MYFS_UNLOCK(ump);
	bdwrite(bp);
	return (bno);

fail_lock:
	MYFS_LOCK(ump);
fail:
	brelse(bp);
	return (0);
}

/*
 * Determine whether an inode can be allocated.
 *
 * Check to see if an inode is available, and if it is,
 * allocate it using the following policy:
 *   1) allocate the requested inode.
 *   2) allocate the next available inode after the requested
 *      inode in the specified cylinder group.
 */
static myfs_ufs2_daddr_t
ffs_nodealloccg(ip, cg, ipref, mode)
	struct myfs_inode *ip;
	int cg;
	myfs_ufs2_daddr_t ipref;
	int mode;
{
	struct myfs_fs *fs;
	struct myfs_cg *cgp;
	struct buf *bp, *ibp;
	struct myfs_ufsmount *ump;
	u_int8_t *inosused;
	struct myfs_ufs2_dinode *dp2;
	int error, start, len, loc, map, i;

	fs = ip->i_fs;
	ump = ip->i_ump;
	if (fs->fs_cs(fs, cg).cs_nifree == 0)
		return (0);
	MYFS_UNLOCK(ump);
	error = bread(ip->i_devvp, myfs_fsbtodb(fs, myfs_cgtod(fs, cg)),
		(int)fs->fs_cgsize, NOCRED, &bp);
	if (error) {
		brelse(bp);
		MYFS_LOCK(ump);
		return (0);
	}
	cgp = (struct myfs_cg *)bp->b_data;
	if (!myfs_cg_chkmagic(cgp) || cgp->cg_cs.cs_nifree == 0) {
		brelse(bp);
		MYFS_LOCK(ump);
		return (0);
	}
	bp->b_xflags |= BX_BKGRDWRITE;
	cgp->cg_old_time = cgp->cg_time = time_second;
	inosused = myfs_cg_inosused(cgp);
	if (ipref) {
		ipref %= fs->fs_ipg;
		if (isclr(inosused, ipref))
			goto gotit;
	}
	start = cgp->cg_irotor / NBBY;
	len = howmany(fs->fs_ipg - cgp->cg_irotor, NBBY);
	loc = skpc(0xff, len, &inosused[start]);
	if (loc == 0) {
		len = start + 1;
		start = 0;
		loc = skpc(0xff, len, &inosused[0]);
		if (loc == 0) {
			printf("cg = %d, irotor = %ld, fs = %s\n",
			    cg, (long)cgp->cg_irotor, fs->fs_fsmnt);
			panic("ffs_nodealloccg: map corrupted");
			/* NOTREACHED */
		}
	}
	i = start + len - loc;
	map = inosused[i];
	ipref = i * NBBY;
	for (i = 1; i < (1 << NBBY); i <<= 1, ipref++) {
		if ((map & i) == 0) {
			cgp->cg_irotor = ipref;
			goto gotit;
		}
	}
	printf("fs = %s\n", fs->fs_fsmnt);
	panic("ffs_nodealloccg: block not in map");
	/* NOTREACHED */
gotit:
	/*
	 * Check to see if we need to initialize more inodes.
	 */
	ibp = NULL;
	if (fs->fs_magic == MYFS_FS_UFS2_MAGIC &&
	    ipref + MYFS_INOPB(fs) > cgp->cg_initediblk &&
	    cgp->cg_initediblk < cgp->cg_niblk) {
		ibp = getblk(ip->i_devvp, myfs_fsbtodb(fs,
		    myfs_ino_to_fsba(fs, cg * fs->fs_ipg + cgp->cg_initediblk)),
		    (int)fs->fs_bsize, 0, 0, 0);
		bzero(ibp->b_data, (int)fs->fs_bsize);
		dp2 = (struct myfs_ufs2_dinode *)(ibp->b_data);
		for (i = 0; i < MYFS_INOPB(fs); i++) {
			dp2->di_gen = arc4random() / 2 + 1;
			dp2++;
		}
		cgp->cg_initediblk += MYFS_INOPB(fs);
	}
	MYFS_LOCK(ump);
	MYFS_ACTIVECLEAR(fs, cg);
	setbit(inosused, ipref);
	cgp->cg_cs.cs_nifree--;
	fs->fs_cstotal.cs_nifree--;
	fs->fs_cs(fs, cg).cs_nifree--;
	fs->fs_fmod = 1;
	if ((mode & IFMT) == IFDIR) {
		cgp->cg_cs.cs_ndir++;
		fs->fs_cstotal.cs_ndir++;
		fs->fs_cs(fs, cg).cs_ndir++;
	}
	MYFS_UNLOCK(ump);
	if (MYFS_DOINGSOFTDEP(MYFS_ITOV(ip)))
		myfs_softdep_setup_inomapdep(bp, ip, cg * fs->fs_ipg + ipref);
	bdwrite(bp);
	if (ibp != NULL)
		bawrite(ibp);
	return (cg * fs->fs_ipg + ipref);
}

/*
 * check if a block is free
 */
static int
ffs_isfreeblock(struct myfs_fs *fs, u_char *cp, myfs_ufs1_daddr_t h)
{

	switch ((int)fs->fs_frag) {
	case 8:
		return (cp[h] == 0);
	case 4:
		return ((cp[h >> 1] & (0x0f << ((h & 0x1) << 2))) == 0);
	case 2:
		return ((cp[h >> 2] & (0x03 << ((h & 0x3) << 1))) == 0);
	case 1:
		return ((cp[h >> 3] & (0x01 << (h & 0x7))) == 0);
	default:
		panic("ffs_isfreeblock");
	}
	return (0);
}

/*
 * Free a block or fragment.
 *
 * The specified block or fragment is placed back in the
 * free map. If a fragment is deallocated, a possible
 * block reassembly is checked.
 */
void
myfs_ffs_blkfree(ump, fs, devvp, bno, size, inum)
	struct myfs_ufsmount *ump;
	struct myfs_fs *fs;
	struct vnode *devvp;
	myfs_ufs2_daddr_t bno;
	long size;
	ino_t inum;
{
	struct myfs_cg *cgp;
	struct buf *bp;
	myfs_ufs1_daddr_t fragno, cgbno;
	myfs_ufs2_daddr_t cgblkno;
	int i, cg, blk, frags, bbase;
	u_int8_t *blksfree;
	struct cdev *dev;

	cg = myfs_dtog(fs, bno);
	if (devvp->v_type == VREG) {
		/* devvp is a snapshot */
		dev = MYFS_VTOI(devvp)->i_devvp->v_rdev;
		cgblkno = myfs_fragstoblks(fs, myfs_cgtod(fs, cg));
	} else {
		/* devvp is a normal disk device */
		dev = devvp->v_rdev;
		cgblkno = myfs_fsbtodb(fs, myfs_cgtod(fs, cg));
		ASSERT_VOP_LOCKED(devvp, "myfs_ffs_blkfree");
		if ((devvp->v_vflag & VV_COPYONWRITE) &&
		    myfs_ffs_snapblkfree(fs, devvp, bno, size, inum))
			return;
	}
#ifdef INVARIANTS
	if ((u_int)size > fs->fs_bsize || myfs_fragoff(fs, size) != 0 ||
	    myfs_fragnum(fs, bno) + myfs_numfrags(fs, size) > fs->fs_frag) {
		printf("dev=%s, bno = %jd, bsize = %ld, size = %ld, fs = %s\n",
		    devtoname(dev), (intmax_t)bno, (long)fs->fs_bsize,
		    size, fs->fs_fsmnt);
		panic("myfs_ffs_blkfree: bad size");
	}
#endif
	if ((u_int)bno >= fs->fs_size) {
		printf("bad block %jd, ino %lu\n", (intmax_t)bno,
		    (u_long)inum);
		ffs_fserr(fs, inum, "bad block");
		return;
	}
	if (bread(devvp, cgblkno, (int)fs->fs_cgsize, NOCRED, &bp)) {
		brelse(bp);
		return;
	}
	cgp = (struct myfs_cg *)bp->b_data;
	if (!myfs_cg_chkmagic(cgp)) {
		brelse(bp);
		return;
	}
	bp->b_xflags |= BX_BKGRDWRITE;
	cgp->cg_old_time = cgp->cg_time = time_second;
	cgbno = myfs_dtogd(fs, bno);
	blksfree = myfs_cg_blksfree(cgp);
	MYFS_LOCK(ump);
	if (size == fs->fs_bsize) {
		fragno = myfs_fragstoblks(fs, cgbno);
		if (!ffs_isfreeblock(fs, blksfree, fragno)) {
			if (devvp->v_type == VREG) {
				MYFS_UNLOCK(ump);
				/* devvp is a snapshot */
				brelse(bp);
				return;
			}
			printf("dev = %s, block = %jd, fs = %s\n",
			    devtoname(dev), (intmax_t)bno, fs->fs_fsmnt);
			panic("myfs_ffs_blkfree: freeing free block");
		}
		myfs_ffs_setblock(fs, blksfree, fragno);
		myfs_ffs_clusteracct(ump, fs, cgp, fragno, 1);
		cgp->cg_cs.cs_nbfree++;
		fs->fs_cstotal.cs_nbfree++;
		fs->fs_cs(fs, cg).cs_nbfree++;
	} else {
		bbase = cgbno - myfs_fragnum(fs, cgbno);
		/*
		 * decrement the counts associated with the old frags
		 */
		blk = myfs_blkmap(fs, blksfree, bbase);
		myfs_ffs_fragacct(fs, blk, cgp->cg_frsum, -1);
		/*
		 * deallocate the fragment
		 */
		frags = myfs_numfrags(fs, size);
		for (i = 0; i < frags; i++) {
			if (isset(blksfree, cgbno + i)) {
				printf("dev = %s, block = %jd, fs = %s\n",
				    devtoname(dev), (intmax_t)(bno + i),
				    fs->fs_fsmnt);
				panic("myfs_ffs_blkfree: freeing free frag");
			}
			setbit(blksfree, cgbno + i);
		}
		cgp->cg_cs.cs_nffree += i;
		fs->fs_cstotal.cs_nffree += i;
		fs->fs_cs(fs, cg).cs_nffree += i;
		/*
		 * add back in counts associated with the new frags
		 */
		blk = myfs_blkmap(fs, blksfree, bbase);
		myfs_ffs_fragacct(fs, blk, cgp->cg_frsum, 1);
		/*
		 * if a complete block has been reassembled, account for it
		 */
		fragno = myfs_fragstoblks(fs, bbase);
		if (myfs_ffs_isblock(fs, blksfree, fragno)) {
			cgp->cg_cs.cs_nffree -= fs->fs_frag;
			fs->fs_cstotal.cs_nffree -= fs->fs_frag;
			fs->fs_cs(fs, cg).cs_nffree -= fs->fs_frag;
			myfs_ffs_clusteracct(ump, fs, cgp, fragno, 1);
			cgp->cg_cs.cs_nbfree++;
			fs->fs_cstotal.cs_nbfree++;
			fs->fs_cs(fs, cg).cs_nbfree++;
		}
	}
	fs->fs_fmod = 1;
	MYFS_ACTIVECLEAR(fs, cg);
	MYFS_UNLOCK(ump);
	bdwrite(bp);
}

#ifdef INVARIANTS
/*
 * Verify allocation of a block or fragment. Returns true if block or
 * fragment is allocated, false if it is free.
 */
static int
myfs_ffs_checkblk(ip, bno, size)
	struct myfs_inode *ip;
	myfs_ufs2_daddr_t bno;
	long size;
{
	struct myfs_fs *fs;
	struct myfs_cg *cgp;
	struct buf *bp;
	myfs_ufs1_daddr_t cgbno;
	int i, error, frags, free;
	u_int8_t *blksfree;

	fs = ip->i_fs;
	if ((u_int)size > fs->fs_bsize || myfs_fragoff(fs, size) != 0) {
		printf("bsize = %ld, size = %ld, fs = %s\n",
		    (long)fs->fs_bsize, size, fs->fs_fsmnt);
		panic("myfs_ffs_checkblk: bad size");
	}
	if ((u_int)bno >= fs->fs_size)
		panic("myfs_ffs_checkblk: bad block %jd", (intmax_t)bno);
	error = bread(ip->i_devvp, myfs_fsbtodb(fs, myfs_cgtod(fs, myfs_dtog(fs, bno))),
		(int)fs->fs_cgsize, NOCRED, &bp);
	if (error)
		panic("myfs_ffs_checkblk: cg bread failed");
	cgp = (struct myfs_cg *)bp->b_data;
	if (!myfs_cg_chkmagic(cgp))
		panic("myfs_ffs_checkblk: cg magic mismatch");
	bp->b_xflags |= BX_BKGRDWRITE;
	blksfree = myfs_cg_blksfree(cgp);
	cgbno = myfs_dtogd(fs, bno);
	if (size == fs->fs_bsize) {
		free = myfs_ffs_isblock(fs, blksfree, myfs_fragstoblks(fs, cgbno));
	} else {
		frags = myfs_numfrags(fs, size);
		for (free = 0, i = 0; i < frags; i++)
			if (isset(blksfree, cgbno + i))
				free++;
		if (free != 0 && free != frags)
			panic("myfs_ffs_checkblk: partially free fragment");
	}
	brelse(bp);
	return (!free);
}
#endif /* INVARIANTS */

/*
 * Free an inode.
 */
int
myfs_ffs_vfree(pvp, ino, mode)
	struct vnode *pvp;
	ino_t ino;
	int mode;
{
	struct myfs_inode *ip;

	if (MYFS_DOINGSOFTDEP(pvp)) {
		myfs_softdep_freefile(pvp, ino, mode);
		return (0);
	}
	ip = MYFS_VTOI(pvp);
	return (myfs_ffs_freefile(ip->i_ump, ip->i_fs, ip->i_devvp, ino, mode));
}

/*
 * Do the actual free operation.
 * The specified inode is placed back in the free map.
 */
int
myfs_ffs_freefile(ump, fs, devvp, ino, mode)
	struct myfs_ufsmount *ump;
	struct myfs_fs *fs;
	struct vnode *devvp;
	ino_t ino;
	int mode;
{
	struct myfs_cg *cgp;
	struct buf *bp;
	myfs_ufs2_daddr_t cgbno;
	int error, cg;
	u_int8_t *inosused;
	struct cdev *dev;

	cg = myfs_ino_to_cg(fs, ino);
	if (devvp->v_type == VREG) {
		/* devvp is a snapshot */
		dev = MYFS_VTOI(devvp)->i_devvp->v_rdev;
		cgbno = myfs_fragstoblks(fs, myfs_cgtod(fs, cg));
	} else {
		/* devvp is a normal disk device */
		dev = devvp->v_rdev;
		cgbno = myfs_fsbtodb(fs, myfs_cgtod(fs, cg));
	}
	if ((u_int)ino >= fs->fs_ipg * fs->fs_ncg)
		panic("myfs_ffs_freefile: range: dev = %s, ino = %lu, fs = %s",
		    devtoname(dev), (u_long)ino, fs->fs_fsmnt);
	if ((error = bread(devvp, cgbno, (int)fs->fs_cgsize, NOCRED, &bp))) {
		brelse(bp);
		return (error);
	}
	cgp = (struct myfs_cg *)bp->b_data;
	if (!myfs_cg_chkmagic(cgp)) {
		brelse(bp);
		return (0);
	}
	bp->b_xflags |= BX_BKGRDWRITE;
	cgp->cg_old_time = cgp->cg_time = time_second;
	inosused = myfs_cg_inosused(cgp);
	ino %= fs->fs_ipg;
	if (isclr(inosused, ino)) {
		printf("dev = %s, ino = %lu, fs = %s\n", devtoname(dev),
		    (u_long)ino + cg * fs->fs_ipg, fs->fs_fsmnt);
		if (fs->fs_ronly == 0)
			panic("myfs_ffs_freefile: freeing free inode");
	}
	clrbit(inosused, ino);
	if (ino < cgp->cg_irotor)
		cgp->cg_irotor = ino;
	cgp->cg_cs.cs_nifree++;
	MYFS_LOCK(ump);
	fs->fs_cstotal.cs_nifree++;
	fs->fs_cs(fs, cg).cs_nifree++;
	if ((mode & IFMT) == IFDIR) {
		cgp->cg_cs.cs_ndir--;
		fs->fs_cstotal.cs_ndir--;
		fs->fs_cs(fs, cg).cs_ndir--;
	}
	fs->fs_fmod = 1;
	MYFS_ACTIVECLEAR(fs, cg);
	MYFS_UNLOCK(ump);
	bdwrite(bp);
	return (0);
}

/*
 * Check to see if a file is free.
 */
int
myfs_ffs_checkfreefile(fs, devvp, ino)
	struct myfs_fs *fs;
	struct vnode *devvp;
	ino_t ino;
{
	struct myfs_cg *cgp;
	struct buf *bp;
	myfs_ufs2_daddr_t cgbno;
	int ret, cg;
	u_int8_t *inosused;

	cg = myfs_ino_to_cg(fs, ino);
	if (devvp->v_type == VREG) {
		/* devvp is a snapshot */
		cgbno = myfs_fragstoblks(fs, myfs_cgtod(fs, cg));
	} else {
		/* devvp is a normal disk device */
		cgbno = myfs_fsbtodb(fs, myfs_cgtod(fs, cg));
	}
	if ((u_int)ino >= fs->fs_ipg * fs->fs_ncg)
		return (1);
	if (bread(devvp, cgbno, (int)fs->fs_cgsize, NOCRED, &bp)) {
		brelse(bp);
		return (1);
	}
	cgp = (struct myfs_cg *)bp->b_data;
	if (!myfs_cg_chkmagic(cgp)) {
		brelse(bp);
		return (1);
	}
	inosused = myfs_cg_inosused(cgp);
	ino %= fs->fs_ipg;
	ret = isclr(inosused, ino);
	brelse(bp);
	return (ret);
}

/*
 * Find a block of the specified size in the specified cylinder group.
 *
 * It is a panic if a request is made to find a block if none are
 * available.
 */
static myfs_ufs1_daddr_t
ffs_mapsearch(fs, cgp, bpref, allocsiz)
	struct myfs_fs *fs;
	struct myfs_cg *cgp;
	myfs_ufs2_daddr_t bpref;
	int allocsiz;
{
	myfs_ufs1_daddr_t bno;
	int start, len, loc, i;
	int blk, field, subfield, pos;
	u_int8_t *blksfree;

	/*
	 * find the fragment by searching through the free block
	 * map for an appropriate bit pattern
	 */
	if (bpref)
		start = myfs_dtogd(fs, bpref) / NBBY;
	else
		start = cgp->cg_frotor / NBBY;
	blksfree = myfs_cg_blksfree(cgp);
	len = howmany(fs->fs_fpg, NBBY) - start;
	loc = scanc((u_int)len, (u_char *)&blksfree[start],
		myfs_fragtbl[fs->fs_frag],
		(u_char)(1 << (allocsiz - 1 + (fs->fs_frag % NBBY))));
	if (loc == 0) {
		len = start + 1;
		start = 0;
		loc = scanc((u_int)len, (u_char *)&blksfree[0],
			myfs_fragtbl[fs->fs_frag],
			(u_char)(1 << (allocsiz - 1 + (fs->fs_frag % NBBY))));
		if (loc == 0) {
			printf("start = %d, len = %d, fs = %s\n",
			    start, len, fs->fs_fsmnt);
			panic("ffs_alloccg: map corrupted");
			/* NOTREACHED */
		}
	}
	bno = (start + len - loc) * NBBY;
	cgp->cg_frotor = bno;
	/*
	 * found the byte in the map
	 * sift through the bits to find the selected frag
	 */
	for (i = bno + NBBY; bno < i; bno += fs->fs_frag) {
		blk = myfs_blkmap(fs, blksfree, bno);
		blk <<= 1;
		field = myfs_around[allocsiz];
		subfield = myfs_inside[allocsiz];
		for (pos = 0; pos <= fs->fs_frag - allocsiz; pos++) {
			if ((blk & field) == subfield)
				return (bno + pos);
			field <<= 1;
			subfield <<= 1;
		}
	}
	printf("bno = %lu, fs = %s\n", (u_long)bno, fs->fs_fsmnt);
	panic("ffs_alloccg: block not in map");
	return (-1);
}

/*
 * Update the cluster map because of an allocation or free.
 *
 * Cnt == 1 means free; cnt == -1 means allocating.
 */
void
myfs_ffs_clusteracct(ump, fs, cgp, blkno, cnt)
	struct myfs_ufsmount *ump;
	struct myfs_fs *fs;
	struct myfs_cg *cgp;
	myfs_ufs1_daddr_t blkno;
	int cnt;
{
	int32_t *sump;
	int32_t *lp;
	u_char *freemapp, *mapp;
	int i, start, end, forw, back, map, bit;

	mtx_assert(MYFS_MTX(ump), MA_OWNED);

	if (fs->fs_contigsumsize <= 0)
		return;
	freemapp = myfs_cg_clustersfree(cgp);
	sump = myfs_cg_clustersum(cgp);
	/*
	 * Allocate or clear the actual block.
	 */
	if (cnt > 0)
		setbit(freemapp, blkno);
	else
		clrbit(freemapp, blkno);
	/*
	 * Find the size of the cluster going forward.
	 */
	start = blkno + 1;
	end = start + fs->fs_contigsumsize;
	if (end >= cgp->cg_nclusterblks)
		end = cgp->cg_nclusterblks;
	mapp = &freemapp[start / NBBY];
	map = *mapp++;
	bit = 1 << (start % NBBY);
	for (i = start; i < end; i++) {
		if ((map & bit) == 0)
			break;
		if ((i & (NBBY - 1)) != (NBBY - 1)) {
			bit <<= 1;
		} else {
			map = *mapp++;
			bit = 1;
		}
	}
	forw = i - start;
	/*
	 * Find the size of the cluster going backward.
	 */
	start = blkno - 1;
	end = start - fs->fs_contigsumsize;
	if (end < 0)
		end = -1;
	mapp = &freemapp[start / NBBY];
	map = *mapp--;
	bit = 1 << (start % NBBY);
	for (i = start; i > end; i--) {
		if ((map & bit) == 0)
			break;
		if ((i & (NBBY - 1)) != 0) {
			bit >>= 1;
		} else {
			map = *mapp--;
			bit = 1 << (NBBY - 1);
		}
	}
	back = start - i;
	/*
	 * Account for old cluster and the possibly new forward and
	 * back clusters.
	 */
	i = back + forw + 1;
	if (i > fs->fs_contigsumsize)
		i = fs->fs_contigsumsize;
	sump[i] += cnt;
	if (back > 0)
		sump[back] -= cnt;
	if (forw > 0)
		sump[forw] -= cnt;
	/*
	 * Update cluster summary information.
	 */
	lp = &sump[fs->fs_contigsumsize];
	for (i = fs->fs_contigsumsize; i > 0; i--)
		if (*lp-- > 0)
			break;
	fs->fs_maxcluster[cgp->cg_cgx] = i;
}

/*
 * Fserr prints the name of a filesystem with an error diagnostic.
 *
 * The form of the error message is:
 *	fs: error message
 */
static void
ffs_fserr(fs, inum, cp)
	struct myfs_fs *fs;
	ino_t inum;
	char *cp;
{
	struct thread *td = curthread;	/* XXX */
	struct proc *p = td->td_proc;

	log(LOG_ERR, "pid %d (%s), uid %d inumber %d on %s: %s\n",
	    p->p_pid, p->p_comm, td->td_ucred->cr_uid, inum, fs->fs_fsmnt, cp);
}

/*
 * This function provides the capability for the fsck program to
 * update an active filesystem. Eleven operations are provided:
 *
 * adjrefcnt(inode, amt) - adjusts the reference count on the
 *	specified inode by the specified amount. Under normal
 *	operation the count should always go down. Decrementing
 *	the count to zero will cause the inode to be freed.
 * adjblkcnt(inode, amt) - adjust the number of blocks used to
 *	by the specifed amount.
 * adjndir, adjbfree, adjifree, adjffree, adjnumclusters(amt) -
 *	adjust the superblock summary.
 * freedirs(inode, count) - directory inodes [inode..inode + count - 1]
 *	are marked as free. Inodes should never have to be marked
 *	as in use.
 * freefiles(inode, count) - file inodes [inode..inode + count - 1]
 *	are marked as free. Inodes should never have to be marked
 *	as in use.
 * freeblks(blockno, size) - blocks [blockno..blockno + size - 1]
 *	are marked as free. Blocks should never have to be marked
 *	as in use.
 * setflags(flags, set/clear) - the fs_flags field has the specified
 *	flags set (second parameter +1) or cleared (second parameter -1).
 */

static int sysctl_myfs_fsck(SYSCTL_HANDLER_ARGS);

SYSCTL_PROC(_vfs_myfs, MYFS_FFS_ADJ_REFCNT, adjrefcnt, CTLFLAG_WR|CTLTYPE_STRUCT,
	0, 0, sysctl_myfs_fsck, "S,fsck", "Adjust Inode Reference Count");

static SYSCTL_NODE(_vfs_myfs, MYFS_FFS_ADJ_BLKCNT, adjblkcnt, CTLFLAG_WR,
	sysctl_myfs_fsck, "Adjust Inode Used Blocks Count");

static SYSCTL_NODE(_vfs_myfs, MYFS_FFS_ADJ_NDIR, adjndir, CTLFLAG_WR,
	sysctl_myfs_fsck, "Adjust number of directories");

static SYSCTL_NODE(_vfs_myfs, MYFS_FFS_ADJ_NBFREE, adjnbfree, CTLFLAG_WR,
	sysctl_myfs_fsck, "Adjust number of free blocks");

static SYSCTL_NODE(_vfs_myfs, MYFS_FFS_ADJ_NIFREE, adjnifree, CTLFLAG_WR,
	sysctl_myfs_fsck, "Adjust number of free inodes");

static SYSCTL_NODE(_vfs_myfs, MYFS_FFS_ADJ_NFFREE, adjnffree, CTLFLAG_WR,
	sysctl_myfs_fsck, "Adjust number of free frags");

static SYSCTL_NODE(_vfs_myfs, MYFS_FFS_ADJ_NUMCLUSTERS, adjnumclusters, CTLFLAG_WR,
	sysctl_myfs_fsck, "Adjust number of free clusters");

static SYSCTL_NODE(_vfs_myfs, MYFS_FFS_DIR_FREE, freedirs, CTLFLAG_WR,
	sysctl_myfs_fsck, "Free Range of Directory Inodes");

static SYSCTL_NODE(_vfs_myfs, MYFS_FFS_FILE_FREE, freefiles, CTLFLAG_WR,
	sysctl_myfs_fsck, "Free Range of File Inodes");

static SYSCTL_NODE(_vfs_myfs, MYFS_FFS_BLK_FREE, freeblks, CTLFLAG_WR,
	sysctl_myfs_fsck, "Free Range of Blocks");

static SYSCTL_NODE(_vfs_myfs, MYFS_FFS_SET_FLAGS, setflags, CTLFLAG_WR,
	sysctl_myfs_fsck, "Change Filesystem Flags");

#ifdef DEBUG
static int fsckcmds = 0;
SYSCTL_INT(_debug, OID_AUTO, fsckcmds, CTLFLAG_RW, &fsckcmds, 0, "");
#endif /* DEBUG */

static int
sysctl_myfs_fsck(SYSCTL_HANDLER_ARGS)
{
	struct myfs_fsck_cmd cmd;
	struct myfs_ufsmount *ump;
	struct vnode *vp;
	struct myfs_inode *ip;
	struct mount *mp;
	struct myfs_fs *fs;
	myfs_ufs2_daddr_t blkno;
	long blkcnt, blksize;
	struct file *fp;
	int filetype, error;

	if (req->newlen > sizeof cmd)
		return (EBADRPC);
	if ((error = SYSCTL_IN(req, &cmd, sizeof cmd)) != 0)
		return (error);
	if (cmd.version != MYFS_FFS_CMD_VERSION)
		return (ERPCMISMATCH);
	if ((error = getvnode(curproc->p_fd, cmd.handle, CAP_FSCK, &fp)) != 0)
		return (error);
	vn_start_write(fp->f_data, &mp, V_WAIT);
	if (mp == 0 || strncmp(mp->mnt_stat.f_fstypename, "ufs", MFSNAMELEN)) {
		vn_finished_write(mp);
		fdrop(fp, curthread);
		return (EINVAL);
	}
	if (mp->mnt_flag & MNT_RDONLY) {
		vn_finished_write(mp);
		fdrop(fp, curthread);
		return (EROFS);
	}
	ump = VFSTOMYFS(mp);
	fs = ump->um_fs;
	filetype = IFREG;

	switch (oidp->oid_number) {

	case MYFS_FFS_SET_FLAGS:
#ifdef DEBUG
		if (fsckcmds)
			printf("%s: %s flags\n", mp->mnt_stat.f_mntonname,
			    cmd.size > 0 ? "set" : "clear");
#endif /* DEBUG */
		if (cmd.size > 0)
			fs->fs_flags |= (long)cmd.value;
		else
			fs->fs_flags &= ~(long)cmd.value;
		break;

	case MYFS_FFS_ADJ_REFCNT:
#ifdef DEBUG
		if (fsckcmds) {
			printf("%s: adjust inode %jd count by %jd\n",
			    mp->mnt_stat.f_mntonname, (intmax_t)cmd.value,
			    (intmax_t)cmd.size);
		}
#endif /* DEBUG */
		if ((error = myfs_ffs_vget(mp, (ino_t)cmd.value, LK_EXCLUSIVE, &vp)))
			break;
		ip = MYFS_VTOI(vp);
		ip->i_nlink += cmd.size;
		MYFS_DIP_SET(ip, i_nlink, ip->i_nlink);
		ip->i_effnlink += cmd.size;
		ip->i_flag |= IN_CHANGE;
		if (MYFS_DOINGSOFTDEP(vp))
			myfs_softdep_change_linkcnt(ip);
		vput(vp);
		break;

	case MYFS_FFS_ADJ_BLKCNT:
#ifdef DEBUG
		if (fsckcmds) {
			printf("%s: adjust inode %jd block count by %jd\n",
			    mp->mnt_stat.f_mntonname, (intmax_t)cmd.value,
			    (intmax_t)cmd.size);
		}
#endif /* DEBUG */
		if ((error = myfs_ffs_vget(mp, (ino_t)cmd.value, LK_EXCLUSIVE, &vp)))
			break;
		ip = MYFS_VTOI(vp);
		if (ip->i_flag & IN_SPACECOUNTED) {
			MYFS_LOCK(ump);
			fs->fs_pendingblocks += cmd.size;
			MYFS_UNLOCK(ump);
		}
		MYFS_DIP_SET(ip, i_blocks, MYFS_DIP(ip, i_blocks) + cmd.size);
		ip->i_flag |= IN_CHANGE;
		vput(vp);
		break;

	case MYFS_FFS_DIR_FREE:
		filetype = IFDIR;
		/* fall through */

	case MYFS_FFS_FILE_FREE:
#ifdef DEBUG
		if (fsckcmds) {
			if (cmd.size == 1)
				printf("%s: free %s inode %d\n",
				    mp->mnt_stat.f_mntonname,
				    filetype == IFDIR ? "directory" : "file",
				    (ino_t)cmd.value);
			else
				printf("%s: free %s inodes %d-%d\n",
				    mp->mnt_stat.f_mntonname,
				    filetype == IFDIR ? "directory" : "file",
				    (ino_t)cmd.value,
				    (ino_t)(cmd.value + cmd.size - 1));
		}
#endif /* DEBUG */
		while (cmd.size > 0) {
			if ((error = myfs_ffs_freefile(ump, fs, ump->um_devvp,
			    cmd.value, filetype)))
				break;
			cmd.size -= 1;
			cmd.value += 1;
		}
		break;

	case MYFS_FFS_BLK_FREE:
#ifdef DEBUG
		if (fsckcmds) {
			if (cmd.size == 1)
				printf("%s: free block %jd\n",
				    mp->mnt_stat.f_mntonname,
				    (intmax_t)cmd.value);
			else
				printf("%s: free blocks %jd-%jd\n",
				    mp->mnt_stat.f_mntonname, 
				    (intmax_t)cmd.value,
				    (intmax_t)cmd.value + cmd.size - 1);
		}
#endif /* DEBUG */
		blkno = cmd.value;
		blkcnt = cmd.size;
		blksize = fs->fs_frag - (blkno % fs->fs_frag);
		while (blkcnt > 0) {
			if (blksize > blkcnt)
				blksize = blkcnt;
			myfs_ffs_blkfree(ump, fs, ump->um_devvp, blkno,
			    blksize * fs->fs_fsize, MYFS_ROOTINO);
			blkno += blksize;
			blkcnt -= blksize;
			blksize = fs->fs_frag;
		}
		break;

	/*
	 * Adjust superblock summaries.  fsck(8) is expected to
	 * submit deltas when necessary.
	 */
	case MYFS_FFS_ADJ_NDIR:
#ifdef DEBUG
		if (fsckcmds) {
			printf("%s: adjust number of directories by %jd\n",
			    mp->mnt_stat.f_mntonname, (intmax_t)cmd.value);
		}
#endif /* DEBUG */
		fs->fs_cstotal.cs_ndir += cmd.value;
		break;
	case MYFS_FFS_ADJ_NBFREE:
#ifdef DEBUG
		if (fsckcmds) {
			printf("%s: adjust number of free blocks by %+jd\n",
			    mp->mnt_stat.f_mntonname, (intmax_t)cmd.value);
		}
#endif /* DEBUG */
		fs->fs_cstotal.cs_nbfree += cmd.value;
		break;
	case MYFS_FFS_ADJ_NIFREE:
#ifdef DEBUG
		if (fsckcmds) {
			printf("%s: adjust number of free inodes by %+jd\n",
			    mp->mnt_stat.f_mntonname, (intmax_t)cmd.value);
		}
#endif /* DEBUG */
		fs->fs_cstotal.cs_nifree += cmd.value;
		break;
	case MYFS_FFS_ADJ_NFFREE:
#ifdef DEBUG
		if (fsckcmds) {
			printf("%s: adjust number of free frags by %+jd\n",
			    mp->mnt_stat.f_mntonname, (intmax_t)cmd.value);
		}
#endif /* DEBUG */
		fs->fs_cstotal.cs_nffree += cmd.value;
		break;
	case MYFS_FFS_ADJ_NUMCLUSTERS:
#ifdef DEBUG
		if (fsckcmds) {
			printf("%s: adjust number of free clusters by %+jd\n",
			    mp->mnt_stat.f_mntonname, (intmax_t)cmd.value);
		}
#endif /* DEBUG */
		fs->fs_cstotal.cs_numclusters += cmd.value;
		break;

	default:
#ifdef DEBUG
		if (fsckcmds) {
			printf("Invalid request %d from fsck\n",
			    oidp->oid_number);
		}
#endif /* DEBUG */
		error = EINVAL;
		break;

	}
	fdrop(fp, curthread);
	vn_finished_write(mp);
	return (error);
}
