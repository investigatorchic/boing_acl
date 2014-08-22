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
