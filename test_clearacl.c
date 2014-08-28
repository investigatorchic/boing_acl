#include <stdio.h>

int
main(int argc, char *argv[])
{
	int result = clearacl(argv[1], atoi(argv[2]), atoi(argv[3]));
	printf("%d\n", result);
	return (0);
}
