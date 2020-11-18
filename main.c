#include <stdio.h>
#include <math.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <pthread.h>
#include "const.h"

struct write_to_memory_args {
    int    fd;
    size_t start;
    size_t end;
    char   *allocated_memory;
    pthread_t pthread;
    char *bytes;
};

void read_from_fd(int fd, char *chars_from_random_array) {
    int number_of_read_bytes = read(fd, chars_from_random_array, READ_BATCH_SIZE);
    if (number_of_read_bytes == -1) {
        perror("Can't read int from /dev/urandom\n");
        exit(EXIT_FAILURE);
    }
    if (number_of_read_bytes != READ_BATCH_SIZE)
        perror("Wrong read batch size");
}

char read_byte_array(int fd, char *chars_from_random_array, int index) {
    if (index >= READ_BATCH_SIZE || index <= 0) {
        read_from_fd(fd, chars_from_random_array);
//        printf("---\n %d \n ---", index);
        return chars_from_random_array[0];
    } else {
        return chars_from_random_array[index];
    }
}

void* write_to_memory(void *args) {
    struct write_to_memory_args* write_args = (struct write_to_memory_args*) args;
    int bytes_wrote = 0;
    for (size_t i = write_args->start; i < write_args->end; ++i) {
        write_args->allocated_memory[i] = read_byte_array(write_args->fd, write_args->bytes, bytes_wrote);
        bytes_wrote++;
        if (bytes_wrote > READ_BATCH_SIZE) {
            bytes_wrote = 0;
        }
    }
    return args;
}

void clean_files(int files_amount) {
    char filename[10];
    int files[files_amount];
    for (int i = 0; i < files_amount; ++i) {
        sprintf(filename, "file%d.txt", i);
        files[i] = open(filename, O_RDWR + O_CREAT + O_TRUNC, S_IWUSR + S_IRUSR);
        if (files[i] == -1) {
            perror("Can't open a file");
            exit(EXIT_FAILURE);
        }
        close(files[i]);
    }
}

void open_files(int files_amount, int* fds) {
    char filename[10];
    for (int i = 0; i < files_amount; ++i) {
        sprintf(filename, "file%d.txt", i);
        fds[i] = open(filename, O_RDWR + O_CREAT, S_IWUSR + S_IRUSR);
        posix_fadvise(fds[i], 0, 0, POSIX_FADV_DONTNEED);
        if (fds[i] == -1) {
            perror("Can't open file");
            exit(EXIT_FAILURE);
        }
    }
}

struct timespec start;
struct timespec finish;
double elapsed;

int main()
{
    const size_t size_in_bytes = ((A)*pow(1000, 2));

    void *allocated_memory = (void *)malloc(size_in_bytes);

    int urandom_fd = open("/dev/urandom", O_RDONLY);
    if (urandom_fd < 0) {
        perror("could not open file descriptor");
        exit(EXIT_FAILURE);
    }

//    char bytes[READ_BATCH_SIZE];
    char bytes_arr[D][READ_BATCH_SIZE];


//    if(number_of_read_bytes == size_in_bytes)
//        printf("%s", "lol");
//    else
//        printf("%d %zu", number_of_read_bytes, size_in_bytes);

    pthread_t threads[D];
    size_t batch_size = size_in_bytes / D;
//    size_t remained_batch = size_in_bytes % D;
    struct write_to_memory_args* args;

    clock_gettime(CLOCK_MONOTONIC, &start);

    for (int i = 0; i < D; ++i) {
        if ( i != (D-1) ) {
            args = malloc(sizeof(struct write_to_memory_args));
            args->start            = batch_size * i;
            args->end              = batch_size * (i+1);
            args->fd               = urandom_fd;
            args->bytes            = bytes_arr[i];
            args->pthread          = threads[i];
            args->allocated_memory = allocated_memory;
        } else {
            args = malloc(sizeof(struct write_to_memory_args));
            args->start             = batch_size * i;
            args->end               = size_in_bytes;
            args->fd                = urandom_fd;
            args->bytes             = bytes_arr[i];
            args->pthread           = threads[i];
            args->allocated_memory  = allocated_memory;
        }
        if (pthread_create(&threads[i], NULL, write_to_memory, (void*) args)) {
            free(args);
            perror("Can't create thread");
        }
    }

    for (size_t i = 0; i < D; i++) {
        pthread_join(threads[i], NULL);
    }

    clock_gettime(CLOCK_MONOTONIC, &finish);

    elapsed = (finish.tv_sec - start.tv_sec);
    elapsed += (finish.tv_nsec - start.tv_nsec) / 1000000000.0;

    printf("Filling up the memory took: %f seconds\n", elapsed);


    char *ptr = (char *) allocated_memory;

//    for (size_t i = 0; i < size_in_bytes; i++) {
//        printf(" %d ", ptr[i]);
//        if (i % 10000 == 0) {
//            printf("\n\n\n\n");
//        }
//    }
//    printf("\n -------- -------- ------- \n");

    printf("\n -------- -------- ------- \n %d "
           "\n %d \n -------- -------- ------- \n", ptr[0], ptr[size_in_bytes-1]);

    int files_amount = ( A / E ) + 1;
    int *file_descriptors = malloc(sizeof(int) * files_amount);

    clean_files(files_amount);
    open_files(files_amount, file_descriptors);

    // close files
    if(close(urandom_fd) < 0) {
        perror("could not close file descriptor");
        exit(EXIT_FAILURE);
    }
    free(allocated_memory);
    return 0;
}
