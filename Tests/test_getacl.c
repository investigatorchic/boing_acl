#include <stdio.h>
#include <errno.h>

int
main(int argc, char *argv[])
{
	int result = getacl(argv[1], atoi(argv[2]), atoi(argv[3]));
	if (result == -1) {
		printf("%d\n", (int)errno);
	}
	else {
		printf("%d\n", result);
	}
	return (0);
}
