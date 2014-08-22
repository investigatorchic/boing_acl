/*-
 * Copyright (c) 2001 Ian Dowse.  All rights reserved.
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
 * $FreeBSD: src/sys/ufs/ufs/dirhash.h,v 1.8.2.1.2.1 2009/10/25 01:10:29 kensmith Exp $
 */

#ifndef _MYFS_UFS_DIRHASH_H_
#define _MYFS_UFS_DIRHASH_H_

#include <sys/_lock.h>
#include <sys/_sx.h>

/*
 * For fast operations on large directories, we maintain a hash
 * that maps the file name to the offset of the directory entry within
 * the directory file.
 *
 * The hashing uses a dumb spillover to the next free slot on
 * collisions, so we must keep the utilisation low to avoid
 * long linear searches. Deleted entries that are not the last
 * in a chain must be marked DIRHASH_DEL.
 *
 * We also maintain information about free space in each block
 * to speed up creations.
 */
#define MYFS_DIRHASH_EMPTY	(-1)	/* entry unused */
#define MYFS_DIRHASH_DEL	(-2)	/* deleted entry; may be part of chain */

#define MYFS_DIRALIGN	4
#define MYFS_DH_NFSTATS	(MYFS_DIRECTSIZ(MYFS_MAXNAMLEN + 1) / MYFS_DIRALIGN)
				 /* max MYFS_DIRALIGN words in a directory entry */

/*
 * Dirhash uses a score mechanism to achieve a hybrid between a
 * least-recently-used and a least-often-used algorithm for entry
 * recycling. The score is incremented when a directory is used, and
 * decremented when the directory is a candidate for recycling. When
 * the score reaches zero, the hash is recycled. Hashes are linked
 * together on a TAILQ list, and hashes with higher scores filter
 * towards the tail (most recently used) end of the list.
 *
 * New hash entries are given an inital score of DH_SCOREINIT and are
 * placed at the most-recently-used end of the list. This helps a lot
 * in the worst-case case scenario where every directory access is
 * to a directory that is not hashed (i.e. the working set of hash
 * candidates is much larger than the configured memry limit). In this
 * case it limits the number of hash builds to 1/DH_SCOREINIT of the
 * number of accesses.
 */ 
#define MYFS_DH_SCOREINIT	8	/* initial dh_score when dirhash built */
#define MYFS_DH_SCOREMAX	64	/* max dh_score value */

/*
 * The main hash table has 2 levels. It is an array of pointers to
 * blocks of MYFS_DH_NBLKOFF offsets.
 */
#define MYFS_DH_BLKOFFSHIFT	8
#define MYFS_DH_NBLKOFF	(1 << MYFS_DH_BLKOFFSHIFT)
#define MYFS_DH_BLKOFFMASK	(MYFS_DH_NBLKOFF - 1)

#define MYFS_DH_ENTRY(dh, slot) \
    ((dh)->dh_hash[(slot) >> MYFS_DH_BLKOFFSHIFT][(slot) & MYFS_DH_BLKOFFMASK])

struct myfs_dirhash {
	struct sx dh_lock;	/* protects all fields except list & score */
	int	dh_refcount;

	myfs_doff_t	**dh_hash;	/* the hash array (2-level) */
	int	dh_narrays;	/* number of entries in dh_hash */
	int	dh_hlen;	/* total slots in the 2-level hash array */
	int	dh_hused;	/* entries in use */
	int	dh_memreq;	/* Memory used. */

	/* Free space statistics. XXX assumes MYFS_DIRBLKSIZ is 512. */
	u_int8_t *dh_blkfree;	/* free DIRALIGN words in each dir block */
	int	dh_nblk;	/* size of dh_blkfree array */
	int	dh_dirblks;	/* number of MYFS_DIRBLKSIZ blocks in dir */
	int	dh_firstfree[MYFS_DH_NFSTATS + 1]; /* first blk with N words free */

	int	dh_seqopt;	/* sequential access optimisation enabled */
	myfs_doff_t	dh_seqoff;	/* sequential access optimisation offset */

	int	dh_score;	/* access count for this dirhash */

	int	dh_onlist;	/* true if on the ufsdirhash_list chain */

	time_t	dh_lastused;	/* time the dirhash was last read or written*/

	/* Protected by ufsdirhash_mtx. */
	TAILQ_ENTRY(myfs_dirhash) dh_list;	/* chain of all dirhashes */
};


/*
 * Dirhash functions.
 */
void	myfs_ufsdirhash_init(void);
void	myfs_ufsdirhash_uninit(void);
int	myfs_ufsdirhash_build(struct myfs_inode *);
myfs_doff_t	myfs_ufsdirhash_findfree(struct myfs_inode *, int, int *);
myfs_doff_t	myfs_ufsdirhash_enduseful(struct myfs_inode *);
int	myfs_ufsdirhash_lookup(struct myfs_inode *, char *, int, myfs_doff_t *, struct buf **,
	    myfs_doff_t *);
void	myfs_ufsdirhash_newblk(struct myfs_inode *, myfs_doff_t);
void	myfs_ufsdirhash_add(struct myfs_inode *, struct myfs_direct *, myfs_doff_t);
void	myfs_ufsdirhash_remove(struct myfs_inode *, struct myfs_direct *, myfs_doff_t);
void	myfs_ufsdirhash_move(struct myfs_inode *, struct myfs_direct *, myfs_doff_t, myfs_doff_t);
void	myfs_ufsdirhash_dirtrunc(struct myfs_inode *, myfs_doff_t);
void	myfs_ufsdirhash_free(struct myfs_inode *);

void	myfs_ufsdirhash_checkblock(struct myfs_inode *, char *, myfs_doff_t);

#endif /* !_MYFS_UFS_DIRHASH_H_ */
