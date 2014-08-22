/*-
 * Copyright (c) 1999-2001 Robert N. M. Watson
 * All rights reserved.
 *
 * This software was developed by Robert Watson for the TrustedBSD Project.
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
 * $FreeBSD: src/sys/ufs/ufs/extattr.h,v 1.23.2.1.2.1 2009/10/25 01:10:29 kensmith Exp $
 */
/*
 * Developed by the TrustedBSD Project.
 * Support for extended filesystem attributes.
 */

#ifndef _MYFS_UFS_EXTATTR_H_
#define	_MYFS_UFS_EXTATTR_H_

#define	MYFS_EXTATTR_MAGIC		0x00b5d5ec
#define	MYFS_EXTATTR_VERSION		0x00000003
#define	MYFS_EXTATTR_FSROOTSUBDIR	".attribute"
#define	MYFS_EXTATTR_SUBDIR_SYSTEM	"system"
#define	MYFS_EXTATTR_SUBDIR_USER		"user"
#define	MYFS_EXTATTR_MAXEXTATTRNAME	65	/* including null */

#define	MYFS_EXTATTR_ATTR_FLAG_INUSE	0x00000001	/* attr has been set */
#define	MYFS_EXTATTR_PERM_KERNEL		0x00000000
#define	MYFS_EXTATTR_PERM_ROOT		0x00000001
#define	MYFS_EXTATTR_PERM_OWNER		0x00000002
#define	MYFS_EXTATTR_PERM_ANYONE		0x00000003

#define	MYFS_EXTATTR_UEPM_INITIALIZED	0x00000001
#define	MYFS_EXTATTR_UEPM_STARTED	0x00000002

#define	MYFS_EXTATTR_CMD_START		0x00000001
#define	MYFS_EXTATTR_CMD_STOP		0x00000002
#define	MYFS_EXTATTR_CMD_ENABLE		0x00000003
#define	MYFS_EXTATTR_CMD_DISABLE		0x00000004

struct myfs_ufs_extattr_fileheader {
	u_int	uef_magic;	/* magic number for sanity checking */
	u_int	uef_version;	/* version of attribute file */
	u_int	uef_size;	/* size of attributes, w/o header */
};

struct myfs_ufs_extattr_header {
	u_int	ueh_flags;	/* flags for attribute */
	u_int	ueh_len;	/* local defined length; <= uef_size */
	u_int32_t	ueh_i_gen;	/* generation number for sanity */
	/* data follows the header */
};

/*
 * This structure defines the required fields of an extended-attribute header.
 */
struct myfs_extattr {
	int32_t	ea_length;	    /* length of this attribute */
	int8_t	ea_namespace;	    /* name space of this attribute */
	int8_t	ea_contentpadlen;   /* bytes of padding at end of attribute */
	int8_t	ea_namelength;	    /* length of attribute name */
	char	ea_name[1];	    /* null-terminated attribute name */
	/* extended attribute content follows */
};

/*
 * These macros are used to access and manipulate an extended attribute:
 *
 * EXTATTR_NEXT(eap) returns a pointer to the next extended attribute
 *	following eap.
 * EXTATTR_CONTENT(eap) returns a pointer to the extended attribute
 *	content referenced by eap.
 * EXTATTR_CONTENT_SIZE(eap) returns the size of the extended attribute
 *	content referenced by eap.
 * EXTATTR_SET_LENGTHS(eap, contentsize) called after initializing the
 *	attribute name to calculate and set the ea_length, ea_namelength,
 *	and ea_contentpadlen fields of the extended attribute structure.
 */
#define MYFS_EXTATTR_NEXT(eap) \
	((struct myfs_extattr *)(((void *)(eap)) + (eap)->ea_length))
#define MYFS_EXTATTR_CONTENT(eap) (((void *)(eap)) + EXTATTR_BASE_LENGTH(eap))
#define MYFS_EXTATTR_CONTENT_SIZE(eap) \
	((eap)->ea_length - EXTATTR_BASE_LENGTH(eap) - (eap)->ea_contentpadlen)
#define MYFS_EXTATTR_BASE_LENGTH(eap) \
	((sizeof(struct myfs_extattr) + (eap)->ea_namelength + 7) & ~7)
#define MYFS_EXTATTR_SET_LENGTHS(eap, contentsize) do { \
	KASSERT(((eap)->ea_name[0] != 0), \
		("Must initialize name before setting lengths")); \
	(eap)->ea_namelength = strlen((eap)->ea_name); \
	(eap)->ea_contentpadlen = ((contentsize) % 8) ? \
		8 - ((contentsize) % 8) : 0; \
	(eap)->ea_length = MYFS_EXTATTR_BASE_LENGTH(eap) + \
		(contentsize) + (eap)->ea_contentpadlen; \
} while (0)

#ifdef _KERNEL

#include <sys/_sx.h>

#ifdef MALLOC_DECLARE
MALLOC_DECLARE(M_MYFS_EXTATTR);
#endif

struct vnode;
LIST_HEAD(myfs_ufs_extattr_list_head, myfs_ufs_extattr_list_entry);
struct myfs_ufs_extattr_list_entry {
	LIST_ENTRY(myfs_ufs_extattr_list_entry)	uele_entries;
	struct myfs_ufs_extattr_fileheader		uele_fileheader;
	int	uele_attrnamespace;
	char	uele_attrname[MYFS_EXTATTR_MAXEXTATTRNAME];
	struct vnode	*uele_backing_vnode;
};

struct ucred;
struct myfs_ufs_extattr_per_mount {
	struct sx	uepm_lock;
	struct myfs_ufs_extattr_list_head	uepm_list;
	struct ucred	*uepm_ucred;
	int	uepm_flags;
};

void	myfs_ufs_extattr_uepm_init(struct myfs_ufs_extattr_per_mount *uepm);
void	myfs_ufs_extattr_uepm_destroy(struct myfs_ufs_extattr_per_mount *uepm);
int	myfs_ufs_extattr_start(struct mount *mp, struct thread *td);
int	myfs_ufs_extattr_autostart(struct mount *mp, struct thread *td);
int	myfs_ufs_extattr_stop(struct mount *mp, struct thread *td);
int	myfs_ufs_extattrctl(struct mount *mp, int cmd, struct vnode *filename,
	    int attrnamespace, const char *attrname);
int	myfs_ufs_getextattr(struct vop_getextattr_args *ap);
int	myfs_ufs_deleteextattr(struct vop_deleteextattr_args *ap);
int	myfs_ufs_setextattr(struct vop_setextattr_args *ap);
void	myfs_ufs_extattr_vnode_inactive(struct vnode *vp, struct thread *td);

#else

/* User-level definition of KASSERT for macros above */
#define KASSERT(cond, str) do { \
        if (!(cond)) { printf("panic: "); printf(str); printf("\n"); exit(1); }\
} while (0)

#endif /* !_KERNEL */

#endif /* !_MYFS_UFS_EXTATTR_H_ */
