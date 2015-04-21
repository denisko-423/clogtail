#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <glob.h>

#define BUFFERSIZE 4096
#define OFFSETEXT ".offset"
#define OFFSETSZ 7

#define FASSERT(x, y) if ((x) == -1) { perror((y)); return 1; }

typedef struct {
    off_t offset;
    ino_t inode;
} offset_t;

int main (int argc, char *argv[]) {

    char *buf, *input_fn, *offset_fn, *offset_suffix = OFFSETEXT;
    struct stat input_stat, search_stat;
    int offset_file_exists;
    offset_t offset_data;
    glob_t glob_data;
    ssize_t rd, wr;
    off_t start;
    int res;

    if (argc < 2) {
        printf("arguments required:\n\t- path to file (required)\n\t- glob to search for rotated file (optional)\n");
        return 1;
    }

    input_fn = argv[1];
    offset_fn = malloc(strlen(input_fn) + OFFSETSZ);
    strcpy(offset_fn, input_fn);
    strcat(offset_fn, offset_suffix);

    int input_fd = open(input_fn, O_RDONLY);
    FASSERT(input_fd, "file open")

    res = fstat(input_fd, &input_stat);
    FASSERT(res, "file stat");

    offset_file_exists = access(offset_fn, F_OK | R_OK | W_OK );

    int offset_fd = open(offset_fn, O_RDWR | O_CREAT);
    FASSERT(offset_fd, "offset file open");

    if (offset_file_exists == 0) { // we found the ".offset" file

        res = read(offset_fd, &offset_data, sizeof(offset_t));
        FASSERT(res, "offset file read");

        if (offset_data.offset == input_stat.st_size &&
            offset_data.inode == input_stat.st_ino) // no changes
                return 0;

        buf = malloc(BUFFERSIZE);
        if (buf == NULL) {
            perror("malloc");
            return 1;
        }

        if (offset_data.inode != input_stat.st_ino) { // file rotated

            if (argc == 3) { // we have glob to search for rotated file

                int i, found = 0;
                if (!glob(argv[2], GLOB_NOSORT, NULL, &glob_data))
                    for (i = 0; i < glob_data.gl_pathc && !found; i++)
                        if(!stat(glob_data.gl_pathv[i], &search_stat))
                            found = (search_stat.st_ino == offset_data.inode);

                if (found) {

                    fprintf(stderr, "file rotated, found at %s\n", glob_data.gl_pathv[i - 1]);

                    int globfd = open(glob_data.gl_pathv[i - 1], O_RDONLY);
                    FASSERT(globfd, "found file open");

                    res = lseek(globfd, offset_data.offset, SEEK_SET);
                    FASSERT(res, "found file lseek");

                    while((rd = read(globfd, buf, BUFFERSIZE)) != 0) {
                        FASSERT(rd, "found file read");
                        wr = write(STDOUT_FILENO, buf, rd);
                    }

                    close(globfd);
                    globfree(&glob_data);

                } else
                    fputs("warning, file rotated and was not found\n", stderr);

            } else
                fputs("warning, file rotated and no glob specified\n", stderr);

            offset_data.inode = input_stat.st_ino;
            offset_data.offset = 0;

        } else {

            if (offset_data.offset > input_stat.st_size ) {
                fputs("warning, truncated file\n", stderr);
                offset_data.offset = 0;
            }

            start = lseek(input_fd, offset_data.offset, SEEK_SET);
            FASSERT(start, "lseek");
        }

        while((rd = read(input_fd, buf, BUFFERSIZE)) != 0 ) {
            FASSERT(rd, "read");
            wr = write(STDOUT_FILENO, buf, rd);
            offset_data.offset += rd;
        };

        free(buf);

    } else {

        offset_data.offset = input_stat.st_size;
        offset_data.inode = input_stat.st_ino;
    }

    close(input_fd);

    lseek(offset_fd, 0, SEEK_SET);
    write(offset_fd, &offset_data, sizeof(offset_t));
    close(offset_fd);

    return 0;
}
