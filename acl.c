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
	if ((t & (ACLS_TYPE_MYFS_UID | ACLS_TYPE_MYFS_GID)) == 0)
		return 0; /* invalid */
	return 1; /* valid */
}

int
validate_permissions(int perms)
{
	if ((t & (IEXEC | IREAD | IWRITE)) == 0)
                return 0; /* invalid */
        return 1; /* valid */

}


int
sys_setacl(struct thread *td, struct setacl_args *uap)
{
	int error;
	struct nameidata nd;

	NDINIT(&nd, LOOKUP, FOLLOW, UIO_USERSPACE, uap->name, td);
	if ((error = namei(&nd)) != 0)
		return error;
	NDFREE(&nd, NDF_ONLY_PNBUF);
	if (nd.ni_vp->v_op == &myfs_ffs_vnodeops2)
		uprintf("File was in a myfs filesystem.\n");
	else
		uprintf("File was not in a myfs filesystem.\n");
	vrele(nd.ni_vp);
	return 0;
}

int
sys_clearacl(struct thread *td, struct clearacl_args *uap)
{
	return 0;
}

int
sys_getacl(struct thread *td, struct getacl_args *uap)
{
	return 0;
}
