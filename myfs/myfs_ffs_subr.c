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
 *	@(#)ffs_subr.c	8.5 (Berkeley) 3/21/95
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD: src/sys/ufs/ffs/ffs_subr.c,v 1.40.22.1.2.1 2009/10/25 01:10:29 kensmith Exp $");

#include <sys/param.h>

#ifndef _KERNEL
#include <local/myfs/myfs_dinode.h>
#include <local/myfs/myfs_fs.h>
#include "fsck.h"
#else
#include <sys/systm.h>
#include <sys/lock.h>
#include <sys/malloc.h>
#include <sys/mount.h>
#include <sys/vnode.h>
#include <sys/bio.h>
#include <sys/buf.h>
#include <sys/ucred.h>

#include <local/myfs/myfs_quota.h>
#include <local/myfs/myfs_inode.h>
#include <local/myfs/myfs_extattr.h>
#include <local/myfs/myfs_ufsmount.h>
#include <local/myfs/myfs_ufs_extern.h>
#include <local/myfs/myfs_ffs_extern.h>
#include <local/myfs/myfs_fs.h>

#ifdef KDB
void	myfs_ffs_checkoverlap(struct buf *, struct myfs_inode *);
#endif

/*
 * Return buffer with the contents of block "offset" from the beginning of
 * directory "ip".  If "res" is non-zero, fill it in with a pointer to the
 * remaining space in the directory.
 */
int
myfs_ffs_blkatoff(vp, offset, res, bpp)
	struct vnode *vp;
	off_t offset;
	char **res;
	struct buf **bpp;
{
	struct myfs_inode *ip;
	struct myfs_fs *fs;
	struct buf *bp;
	myfs_ufs_lbn_t lbn;
	int bsize, error;

	ip = MYFS_VTOI(vp);
	fs = ip->i_fs;
	lbn = myfs_lblkno(fs, offset);
	bsize = myfs_blksize(fs, ip, lbn);

	*bpp = NULL;
	error = bread(vp, lbn, bsize, NOCRED, &bp);
	if (error) {
		brelse(bp);
		return (error);
	}
	if (res)
		*res = (char *)bp->b_data + myfs_blkoff(fs, offset);
	*bpp = bp;
	return (0);
}

/*
 * Load up the contents of an inode and copy the appropriate pieces
 * to the incore copy.
 */
void
myfs_ffs_load_inode(bp, ip, fs, ino)
	struct buf *bp;
	struct myfs_inode *ip;
	struct myfs_fs *fs;
	ino_t ino;
{

	if (ip->i_ump->um_fstype == MYFS1) {
		*ip->i_din1 =
		    *((struct myfs_ufs1_dinode *)bp->b_data + myfs_ino_to_fsbo(fs, ino));
		ip->i_mode = ip->i_din1->di_mode;
		ip->i_nlink = ip->i_din1->di_nlink;
		ip->i_size = ip->i_din1->di_size;
		ip->i_flags = ip->i_din1->di_flags;
		ip->i_gen = ip->i_din1->di_gen;
		ip->i_uid = ip->i_din1->di_uid;
		ip->i_gid = ip->i_din1->di_gid;
	} else {
		*ip->i_din2 =
		    *((struct myfs_ufs2_dinode *)bp->b_data + myfs_ino_to_fsbo(fs, ino));
		ip->i_mode = ip->i_din2->di_mode;
		ip->i_nlink = ip->i_din2->di_nlink;
		ip->i_size = ip->i_din2->di_size;
		ip->i_flags = ip->i_din2->di_flags;
		ip->i_gen = ip->i_din2->di_gen;
		ip->i_uid = ip->i_din2->di_uid;
		ip->i_gid = ip->i_din2->di_gid;
	}
}
#endif /* KERNEL */

/*
 * Update the frsum fields to reflect addition or deletion
 * of some frags.
 */
void
myfs_ffs_fragacct(fs, fragmap, fraglist, cnt)
	struct myfs_fs *fs;
	int fragmap;
	int32_t fraglist[];
	int cnt;
{
	int inblk;
	int field, subfield;
	int siz, pos;

	inblk = (int)(myfs_fragtbl[fs->fs_frag][fragmap]) << 1;
	fragmap <<= 1;
	for (siz = 1; siz < fs->fs_frag; siz++) {
		if ((inblk & (1 << (siz + (fs->fs_frag % NBBY)))) == 0)
			continue;
		field = myfs_around[siz];
		subfield = myfs_inside[siz];
		for (pos = siz; pos <= fs->fs_frag; pos++) {
			if ((fragmap & field) == subfield) {
				fraglist[siz] += cnt;
				pos += siz;
				field <<= siz;
				subfield <<= siz;
			}
			field <<= 1;
			subfield <<= 1;
		}
	}
}

#ifdef KDB
void
myfs_ffs_checkoverlap(bp, ip)
	struct buf *bp;
	struct myfs_inode *ip;
{
	struct buf *ebp, *ep;
	myfs_ufs2_daddr_t start, last;
	struct vnode *vp;

	ebp = &buf[nbuf];
	start = bp->b_blkno;
	last = start + btodb(bp->b_bcount) - 1;
	for (ep = buf; ep < ebp; ep++) {
		if (ep == bp || (ep->b_flags & B_INVAL) ||
		    ep->b_vp == NULLVP)
			continue;
		vp = ip->i_devvp;
		/* look for overlap */
		if (ep->b_bcount == 0 || ep->b_blkno > last ||
		    ep->b_blkno + btodb(ep->b_bcount) <= start)
			continue;
		vprint("Disk overlap", vp);
		printf("\tstart %jd, end %jd overlap start %jd, end %jd\n",
		    (intmax_t)start, (intmax_t)last, (intmax_t)ep->b_blkno,
		    (intmax_t)(ep->b_blkno + btodb(ep->b_bcount) - 1));
		panic("myfs_ffs_checkoverlap: Disk buffer overlap");
	}
}
#endif /* KDB */

/*
 * block operations
 *
 * check if a block is available
 */
int
myfs_ffs_isblock(fs, cp, h)
	struct myfs_fs *fs;
	unsigned char *cp;
	myfs_ufs1_daddr_t h;
{
	unsigned char mask;

	switch ((int)fs->fs_frag) {
	case 8:
		return (cp[h] == 0xff);
	case 4:
		mask = 0x0f << ((h & 0x1) << 2);
		return ((cp[h >> 1] & mask) == mask);
	case 2:
		mask = 0x03 << ((h & 0x3) << 1);
		return ((cp[h >> 2] & mask) == mask);
	case 1:
		mask = 0x01 << (h & 0x7);
		return ((cp[h >> 3] & mask) == mask);
	default:
		panic("myfs_ffs_isblock");
	}
	return (0);
}

/*
 * take a block out of the map
 */
void
myfs_ffs_clrblock(fs, cp, h)
	struct myfs_fs *fs;
	u_char *cp;
	myfs_ufs1_daddr_t h;
{

	switch ((int)fs->fs_frag) {
	case 8:
		cp[h] = 0;
		return;
	case 4:
		cp[h >> 1] &= ~(0x0f << ((h & 0x1) << 2));
		return;
	case 2:
		cp[h >> 2] &= ~(0x03 << ((h & 0x3) << 1));
		return;
	case 1:
		cp[h >> 3] &= ~(0x01 << (h & 0x7));
		return;
	default:
		panic("myfs_ffs_clrblock");
	}
}

/*
 * put a block into the map
 */
void
myfs_ffs_setblock(fs, cp, h)
	struct myfs_fs *fs;
	unsigned char *cp;
	myfs_ufs1_daddr_t h;
{

	switch ((int)fs->fs_frag) {

	case 8:
		cp[h] = 0xff;
		return;
	case 4:
		cp[h >> 1] |= (0x0f << ((h & 0x1) << 2));
		return;
	case 2:
		cp[h >> 2] |= (0x03 << ((h & 0x3) << 1));
		return;
	case 1:
		cp[h >> 3] |= (0x01 << (h & 0x7));
		return;
	default:
		panic("myfs_ffs_setblock");
	}
}
