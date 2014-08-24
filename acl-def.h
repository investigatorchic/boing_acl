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




















#endif
