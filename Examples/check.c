/*
 * This is a small test program that simply calls the setacl(2) system
 * call.  It does not actually expect setacl(2) to work at this point,
 * as part of the Project a small stub of code was provided that uses
 * the setacl(2) entry point to demonstrate how to figure out whether
 * a file is inside a myfs filesystem or not.  All we expect to happen
 * here is to pass in the pathname to a file, have the kernel obtain a
 * vnode using that pathname, and then have the kernel uprintf(9) whether
 * or not the file came from a myfs filesystem.
 */

#include <stdio.h>

int
main(int argc, char *argv[])
{
        int i;

        for (i = 1; i < argc; i++)
                if (setacl(argv[i], 0, 0, 0))
                        perror(argv[i]);
}
