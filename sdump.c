/* See LICENSE for licence details. */
#include "sdump.h"
#include "util.h"
#include "loader.h"
#include "image.h"
#include "sixel.h"

void usage()
{
	printf("usage:\n"
		"\tsdump [-h] [-f] [-r angle] image\n"
		"\tcat image | sdump\n"
		"\twget -O - image_url | sdump\n"
		"options:\n"
		"\t-h: show this help\n"
		"\t-f: fit image to display\n"
		"\t-r: rotate image (90/180/270)\n"
		);
}

void remove_temp_file()
{
	extern char temp_file[PATH_MAX]; /* global */
	remove(temp_file);
}

char *make_temp_file(const char *template)
{
	extern char temp_file[PATH_MAX]; /* global */
	int fd;
	ssize_t size, file_size = 0;
	char buf[BUFSIZE], *env;

	/* stdin is tty or not */
	if (isatty(STDIN_FILENO)) {
		logging(ERROR, "stdin is neither pipe nor redirect\n");
		return NULL;
	}

	/* prepare temp file */
	memset(temp_file, 0, BUFSIZE);
	if ((env = getenv("TMPDIR")) != NULL) {
		snprintf(temp_file, BUFSIZE, "%s/%s", env, template);
	} else {
		snprintf(temp_file, BUFSIZE, "/tmp/%s", template);
	}

	if ((fd = emkstemp(temp_file)) < 0)
		return NULL;
	logging(DEBUG, "tmp file:%s\n", temp_file);

	/* register cleanup function */
	if (atexit(remove_temp_file))
		logging(ERROR, "atexit() failed\nmaybe temporary file remains...\n");

	/* read data */
	while ((size = read(STDIN_FILENO, buf, BUFSIZE)) > 0) {
		write(fd, buf, size);
		file_size += size;
	}
	eclose(fd);

	if (file_size == 0) {
		logging(ERROR, "stdin is empty\n");
		return NULL;
	}

	return temp_file;
}

void cleanup(struct sixel_t *sixel, struct image *img)
{
	sixel_die(sixel);
	free_image(img);
}

int main(int argc, char **argv)
{
	const char *template = "sdump.XXXXXX";
	char *file;
	bool resize = false;
	int angle = 0, opt;
	struct winsize ws;
	struct image img;
	struct tty_t tty = {
		.cell_width = CELL_WIDTH, .cell_height = CELL_HEIGHT,
	};;
	struct sixel_t sixel = {
		.context = NULL, .dither = NULL,
	};

	/* check arg */
	while ((opt = getopt(argc, argv, "hfr:")) != -1) {
		switch (opt) {
		case 'h':
			usage();
			return EXIT_SUCCESS;
		case 'f':
			resize = true;
			break;
		case 'r':
			angle = str2num(optarg);
			break;
		default:
			break;
		}
	}

	/* open file */
	if (optind < argc)
		file = argv[optind];
	else
		file = make_temp_file(template);

	if (file == NULL) {
		logging(FATAL, "input file not found\n");
		usage();
		return EXIT_FAILURE;
	}

	/* init */
	init_image(&img);

	if (load_image(file, &img) == false) {
		logging(FATAL, "couldn't load image\n");
		return EXIT_FAILURE;
	}

	if (ioctl(STDIN_FILENO, TIOCGWINSZ, &ws)) {
		logging(ERROR, "ioctl: TIOCGWINSZ failed\n");
		tty.width  = tty.cell_width * 80;
		tty.height = tty.cell_height * 24;
	} else {
		tty.width  = tty.cell_width * ws.ws_col;
		tty.height = tty.cell_height * ws.ws_row;
	}

	/* rotate/resize and draw */
	/* TODO: support color reduction for 8bpp mode */
	if (angle != 0)
		rotate_image(&img, angle, true);

	if (resize)
		resize_image(&img, tty.width, tty.height, true);

	/* sixel */
	if (!sixel_init(&tty, &sixel, &img))
		goto error_occured;
	sixel_write(&tty, &sixel, &img);

	/* cleanup resource */
	cleanup(&sixel, &img);
	return EXIT_SUCCESS;

error_occured:
	cleanup(&sixel, &img);
	return EXIT_FAILURE;;
}
