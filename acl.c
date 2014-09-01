#include <sys/param.h>
#include <sys/systm.h>
#include <sys/bio.h>
#include <sys/buf.h>
#include <sys/sysproto.h>
#include <sys/fcntl.h>
#include <sys/namei.h>
#include <sys/mount.h>
#include <sys/vnode.h>
#include <sys/dirent.h>
#include <sys/extattr.h>

#include "../../../local/myfs/myfs_quota.h"
#include "../../../local/myfs/myfs_inode.h"
#include "../../../local/myfs/myfs_dir.h"
#include "../../../local/myfs/myfs_extattr.h"
#include "../../../local/myfs/myfs_ufsmount.h"
#include "../../../local/myfs/myfs_ufs_extern.h"
#include "../../../local/myfs/myfs_ffs_extern.h"

#include "acl-def.h"

/**
 *  This new entry should
 *  be added to the current list of ACLs for the file.  If the id number
 *  passed in to this system call is zero then the id number of the
 *  currently running process should be used
 *  
 * @param char* name 
 * @param int   type  ( 0 if the id is a uid, 1 if id is a gid )
 * @param int   idnum 
 * @param int   perms ( bit 2 for read, bit 1 for write, bit 0 for execute )
 * @return int 0 if successful, non 0 if not. 
 */

int
validate_type(int type)
{
	return type == ACLS_TYPE_MYFS_UID || type == ACLS_TYPE_MYFS_GID;	
}

int
validate_permissions(int perms)
{
	if ((perms & (ACL_EXEC_MYFS | ACL_READ_MYFS | ACL_WRITE_MYFS)) == 0)
                return 0; /* invalid */
        return 1; /* valid */

}

int
add_to_acl_by_id(struct myfs_acl_entry *ids, int ids_length, id_t id, u_int32_t perms)
{
	int i;
	int available = -1;

	for(i = 0 ; i < ids_length ; i++) {
		struct myfs_acl_entry entry = ids[i];
		if (entry.id == 0) available = i;
		if (entry.id == id) {
			entry.perms = perms; 
			return 0; /*update successful*/
		}	
	}
	if(available != -1) {
		ids[available].id = id; 
		ids[available].perms = perms;
		return 0;
	}
	return 1;
}

int
clear_from_acl_by_id(struct myfs_acl_entry *ids, int ids_length, id_t id)
{

	int i;
	if(id == 0) return EINVAL;

	for(i = 0 ; i < ids_length ; i++) {
                struct myfs_acl_entry entry = ids[i];
                if (entry.id == id) {
                        entry.perms = 0;
			entry.id = 0;
                        return 0; /*clear successful*/
                }
	}
	return 0;
}

int
group_check(struct ucred *ucred, gid_t gid)
{
	if (ucred) {
		int i;
		for (i = 0; i < ucred->cr_ngroups ; i++)
			if (ucred->cr_groups[i] == gid) return 1;
	}
	return 0;
}


int 
get_acl_id(struct myfs_acl_entry *ids, int ids_length, id_t id)
{

	int i;
	printf("108\n");
	for(i = 0 ; i < ids_length ; i++) {
		struct myfs_acl_entry entry = ids[i];
		printf("%d == %d: %d | %d\n", (int) id, (int) entry.id, entry.id == id, ((int) entry.id) == ((int) id) );
		if (entry.id == id) {
			printf("before entry.perms\n");
			return entry.perms;
		}
	}	
	return 1;
}

int
process_acl_addition(struct thread *td, struct myfs_inode *my_inode, struct setacl_args *uap)
{
	int result = EPERM;
	id_t idnum = uap->idnum;
	switch(uap->type) {
               	case ACLS_TYPE_MYFS_UID:
			printf("%d\n" , (!IAMGROOT));
			if ((IAMGROOT) || (UID_NOW == my_inode->dinode_u.din2->di_uid)) {
				 if (idnum == 0) idnum = UID_NOW;
				 result = add_to_acl_by_id(my_inode->dinode_u.din2->myfs_acl_uid, sizeof(my_inode->dinode_u.din2->myfs_acl_uid) / sizeof(struct myfs_acl_entry), idnum, uap->perms);		
			}
			else {
				result = EPERM;
			}
			break;
		case ACLS_TYPE_MYFS_GID:
			if (idnum == 0) idnum = GID_NOW;
			if (IAMGROOT || group_check(td->td_ucred, idnum)) {
				result = add_to_acl_by_id(my_inode->dinode_u.din2->myfs_acl_gid, sizeof(my_inode->dinode_u.din2->myfs_acl_gid) / sizeof(struct myfs_acl_entry), idnum, uap->perms);	
			}
			else {
				result = EACCES;
			}
			break;
	}
	return result;
}


int
process_acl_clear(struct thread *td, struct myfs_inode *my_inode, struct clearacl_args *uap)
{
        int result = EPERM;
        id_t idnum = uap->idnum;
 		switch(uap->type) {
                        case ACLS_TYPE_MYFS_UID:
				if (IAMGROOT || (UID_NOW == my_inode->dinode_u.din2->di_uid)) {	
                                	if (idnum == 0) idnum = UID_NOW;
                                	result = clear_from_acl_by_id(my_inode->dinode_u.din2->myfs_acl_uid, sizeof(my_inode->dinode_u.din2->myfs_acl_uid) / sizeof(struct myfs_acl_entry), idnum);
                       		}
				else {
					result = EPERM;
				}	         
				break;
                        case ACLS_TYPE_MYFS_GID:
				if (IAMGROOT || (UID_NOW == my_inode->dinode_u.din2->di_uid)) {
                                	if (idnum == 0) idnum = GID_NOW;
                                	result = clear_from_acl_by_id(my_inode->dinode_u.din2->myfs_acl_gid, sizeof(my_inode->dinode_u.din2->myfs_acl_gid) / sizeof(struct myfs_acl_entry), idnum);
 				}
				else {
					result = EPERM;
				}                               
				break;
                }
        return result;
}

int
process_acl_get(struct thread *td, struct myfs_inode *my_inode, struct getacl_args *uap)
{
	printf("179\n");
	int result = EPERM;
	printf("181\n");
        id_t idnum = uap->idnum;
	printf("183\n");
 		switch(uap->type) {
                        case ACLS_TYPE_MYFS_UID:
				if (IAMGROOT || (UID_NOW == my_inode->dinode_u.din2->di_uid)) {
                                	if (idnum == 0) idnum = UID_NOW;
                                	result = get_acl_id(my_inode->dinode_u.din2->myfs_acl_uid, sizeof(my_inode->dinode_u.din2->myfs_acl_uid) / sizeof(struct myfs_acl_entry), idnum);
                                	printf("%d\n" , result);
				}
				else {
					result = EPERM;
				}
                                break;
                        case ACLS_TYPE_MYFS_GID:
				if (IAMGROOT || (UID_NOW == my_inode->dinode_u.din2->di_uid) || group_check(td->td_ucred, idnum)) {
                                	if (idnum == 0) idnum = GID_NOW;
                                	result = get_acl_id(my_inode->dinode_u.din2->myfs_acl_gid, sizeof(my_inode->dinode_u.din2->myfs_acl_gid) / sizeof(struct myfs_acl_entry), idnum);
                                }
				else {
					result = EPERM;
				}
				break;
		}              
        return result;
}
	
int
sys_setacl(struct thread *td, struct setacl_args *uap)
{
	int error = -1;
	struct nameidata nd;
	char fname[256];
	size_t actual = 0;

	printf("pre-copyinstr\n");
	copyinstr(uap->name, &fname, 255, &actual);
	printf("first if\n");
	printf("%d\n", uap->type);
	printf("%d\n", uap->perms);
	if ( (validate_type(uap->type) == 0 ) && ( validate_permissions( uap->perms ) == 0) ) {
		printf("second if\n");
		return EINVAL;
	}

	NDINIT(&nd, LOOKUP, FOLLOW, UIO_USERSPACE, uap->name, td);
	if ((error = namei(&nd)) != 0){
		printf("221\n");
		return error;
	}
	NDFREE(&nd, NDF_ONLY_PNBUF);

	if (nd.ni_vp->v_op == &myfs_ffs_vnodeops2) {
 		struct myfs_inode *my_inode;
		VI_LOCK(nd.ni_vp);
		uprintf("File was in a myfs filesystem.\n");
		my_inode = MYFS_VTOI(nd.ni_vp);
	 	error = process_acl_addition(td, my_inode, uap);
		VI_UNLOCK(nd.ni_vp);	
	}
	else {
		uprintf("File was not in a myfs filesystem.\n");
		error = EPERM;
	}
	vrele(nd.ni_vp);
	return error;
}

int
sys_clearacl(struct thread *td, struct clearacl_args *uap)
{
	int error;
	struct nameidata nd;
	char fname[256];
	size_t actual = 0;
	printf("pre-copyinstr\n");
	copyinstr(uap->name, &fname, 255, &actual);
	printf("first if\n");
        printf("%d\n", uap->type);
	if ( (validate_type(uap->type) == 0 )) {
		return EINVAL;
	}

	NDINIT(&nd, LOOKUP, FOLLOW, UIO_USERSPACE, uap->name, td);
	if ((error = namei(&nd)) != 0)
		return error;
	NDFREE(&nd, NDF_ONLY_PNBUF);

	if (nd.ni_vp->v_op == &myfs_ffs_vnodeops2) {
 		struct myfs_inode *my_inode;
		VI_LOCK(nd.ni_vp);
		uprintf("File was in a myfs filesystem.\n");
		my_inode = MYFS_VTOI(nd.ni_vp);
	 	error = process_acl_clear(td, my_inode, uap);
		VI_UNLOCK(nd.ni_vp);	
	}
	else {
		uprintf("File was not in a myfs filesystem.\n");
		error = EPERM;
	}
	vrele(nd.ni_vp);
	return error;
}

int
sys_getacl(struct thread *td, struct getacl_args *uap)
{
	int error;
	struct nameidata nd;
	char fname[256];
	size_t actual = 0;
	printf("pre-copyinstr\n");
	copyinstr(uap->name, &fname, 255, &actual);
	printf("first if\n");
        printf("%d\n", uap->type);
	if ( (validate_type(uap->type) == 0 )) {
		return EINVAL;
	}

	NDINIT(&nd, LOOKUP, FOLLOW, UIO_USERSPACE, uap->name, td);
	if ((error = namei(&nd)) != 0)
		return error;
	NDFREE(&nd, NDF_ONLY_PNBUF);

	if (nd.ni_vp->v_op == &myfs_ffs_vnodeops2) {
 		struct myfs_inode *my_inode;
		VI_LOCK(nd.ni_vp);
		uprintf("File was in a myfs filesystem.\n");
		my_inode = MYFS_VTOI(nd.ni_vp);
	 	error = process_acl_get(td, my_inode, uap);
		printf("%d\n" , error);
		VI_UNLOCK(nd.ni_vp);	
	}
	else {
		uprintf("File was not in a myfs filesystem.\n");
		error = EPERM;
	}
	vrele(nd.ni_vp);
	printf("%d\n" , error);
	return error;
}
