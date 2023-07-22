
static int read_fd(FILE *fd, unsigned char *buf, int buflen, int *written)
{
	unsigned char *p = buf;
	int len = 0;
	*written = 0;

	do {
		len = fread(p, 1, 4096, fd);
		*written += len;
		p += len;
		if (p > buf + buflen)
			return 0;
	} while (len == 4096);

	return 1;
}

static int read_file(const char *filename, unsigned char *buf, int buflen, int *written)
{
	FILE *file = NULL;
	int ok;

	file = fopen(filename, "r");
	if (file == NULL) {
		*written = strlen(filename)+1;
		memcpy(buf, filename, *written);
		buf[*written-1] = '\n';
		return 1;
	}

	ok = read_fd(file, buf, buflen, written);
	fclose(file);
	return ok;
}

