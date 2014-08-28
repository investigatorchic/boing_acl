#ifndef __ACL_DEF_H__
#define __ACL_DEF_H__

int
validate_type(int type);

int
validate_permissions(int perms);

int
add_to_acl_by_id(struct myfs_acl_entry *ids, int ids_length, id_t id, u_int32_t perms);

int
clear_from_acl_by_id(struct myfs_acl_entry *ids, int ids_length, id_t id);

int 
get_acl_id(struct myfs_acl_entry *ids, int ids_length, id_t id);

int
group_check(struct ucred *ucred, gid_t gid);

int
process_acl_addition(struct thread *td, struct myfs_inode *my_inode, struct setacl_args *uap);

int
process_acl_clear(struct thread *td, struct myfs_inode *my_inode, struct clearacl_args *uap);

int
process_acl_get(struct thread *td, struct myfs_inode *my_inode, struct getacl_args *uap);






# define IAMGROOT (td->td_ucred->cr_uid == 0)
# define UID_NOW td->td_ucred->cr_uid
# define GID_NOW td->td_ucred->cr_groups[0]
# define PID_NOW td->td_proc->p_pid


#endif
