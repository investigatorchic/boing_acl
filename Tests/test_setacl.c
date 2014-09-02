#include <stdio.h>

int
main(int argc, char *argv[])
{
	int result = 1;
	if (argc != 5) {
		printf("correct use: setacl [file_name] [0|1] [uid|gid] [perms]\n\tperms(mask) = 1=exec 2=read 4=write\n\n");
	}
	else {
		result = setacl(argv[1], atoi(argv[2]), atoi(argv[3]), atoi(argv[4]));
		printf("%d\n", result);
	}
	return (0);
}
