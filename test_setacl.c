#include <stdio.h>

int
main(int argc, char *argv[])
{
	int result = setacl(argv[1], atoi(argv[2]), atoi(argv[3]), atoi(argv[4]));
	printf("%d\n", result);
	return (0);
}
