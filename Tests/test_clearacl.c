#include <stdio.h>

int
main(int argc, char *argv[])
{
	int result = 1;
	if (argc != 4) {
		printf("correct use: clearacl [file name] [0|1] [uid|gid]\n\n");
	}
	else {
		int result = clearacl(argv[1], atoi(argv[2]), atoi(argv[3]));
		printf("%d\n", result);
	}
	return (0);
}
