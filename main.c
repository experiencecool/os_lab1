#include <stdio.h>
#include <math.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <pthread.h>
#include <semaphore.h>
#include "const.h"

struct timespec start;
struct timespec finish;
double elapsed;
static sem_t file_sync;

struct write_to_memory_args {
    int    fd;
    size_t start;
    size_t end;
    unsigned char *allocated_memory;
    pthread_t pthread;
    unsigned char *bytes;
};

struct write_to_files_args {
    int files_amount;
    size_t file_batch_size_bytes;
    size_t file_batch_size_bytes_remained;
    unsigned char *allocated_memory;
    int *fds;
};

struct ReadFromFileArgs {
    int fd;
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
    char filename[5];
    for (int i = 0; i < files_amount; ++i) {
        sprintf(filename, "%d", i);
        fds[i] = open(filename, O_RDWR + O_CREAT, S_IWUSR + S_IRUSR);
        posix_fadvise(fds[i], 0, 0, POSIX_FADV_DONTNEED);
        if (fds[i] == -1) {
            perror("Can't open file");
            exit(EXIT_FAILURE);
        }
    }
}

void write_to_file(unsigned char *allocated_memory, int fd, int file_number, size_t bytes_count) {
    unsigned char io_block[WRITE_BATCH_SIZE];
    int io_block_byte = 0;
    size_t bytes_written;
    int totalBytesWrittenToFile = 0;
    for (size_t i = file_number * E; i < file_number * E + bytes_count; i++) {
        io_block[io_block_byte] = allocated_memory[i];
        io_block_byte += 1;
        if (io_block_byte >= WRITE_BATCH_SIZE) {
            io_block_byte = 0;
            bytes_written = write(fd, &io_block, WRITE_BATCH_SIZE);
            if (bytes_written == -1) {
                perror("can't write to file");
                exit(EXIT_FAILURE);
            }
            totalBytesWrittenToFile += bytes_written;
        }
        if (io_block_byte > 0) {
            bytes_written = write(fd, &io_block, WRITE_BATCH_SIZE - io_block_byte);
            if (bytes_written == -1) {
                perror("can't write to file");
                exit(EXIT_FAILURE);
            }
            totalBytesWrittenToFile += bytes_written;
        }
    }
    printf("Total bytes written to file: %d\n", totalBytesWrittenToFile);
}

_Noreturn void* write_to_files(void* args) {
    printf("%s", "Started writing\n");
    struct write_to_files_args* write_to_files_args = (struct write_to_files_args*) args;
    while (1) {
        sem_wait(&file_sync);
        printf("%s", "Locked on write\n");
        for (int i = 0; i < write_to_files_args->files_amount; i++) {
            if (write_to_files_args->file_batch_size_bytes_remained != 0 &&
            i == write_to_files_args->files_amount - 1){
                write_to_file(write_to_files_args->allocated_memory,
                                    write_to_files_args->fds[i],
                                    i,
                                    write_to_files_args->file_batch_size_bytes_remained);
            } else {
               write_to_file(write_to_files_args->allocated_memory,
                             write_to_files_args->fds[i],
                             i,
                             write_to_files_args->file_batch_size_bytes);
            }
        }
        sem_post(&file_sync);
        printf("%s", "Unlocked on write\n");
    }
}

void* ReadFile(void* args) {
    size_t totalBytesRead;
    unsigned char max;
    struct ReadFromFileArgs* fileArgs = (struct ReadFromFileArgs*) args;
    unsigned char readBlock[WRITE_BATCH_SIZE];
    int readBytes;
    printf("Started read from file thread\n");
    sem_wait(&file_sync);
    printf("Locked on read\n");
    if (lseek(fileArgs->fd, 0, SEEK_SET) == -1) {
        return NULL;
    }
    while (1) {
        readBytes = read(fileArgs->fd, &readBlock, WRITE_BATCH_SIZE);
        if (readBytes == -1) {
            perror("Error reading from file");
            break;
        }

        totalBytesRead += readBytes;
        if (readBytes < WRITE_BATCH_SIZE) {
            if (readBytes == 0) {
                printf("Zero\n");
                break;
            }
        } else {
            for (int i = 0; i < WRITE_BATCH_SIZE; i++) {
                if (readBlock[i] > max) {
                    max = readBlock[i];
                }
            }
        }
    }
    sem_post(&file_sync);
    printf("Unlocked on read\n");
    sleep(1);

    return NULL;
}

int main()
{
    const size_t size_in_bytes = ((A)*pow(1000, 2));

    void *allocated_memory = (void *)malloc(size_in_bytes);

    if(sem_init(&file_sync, 0, 1) < 0){
        perror("could not initialize a semaphore");
        exit(EXIT_FAILURE);
    }

    pthread_t write_to_files_thread_id;


    int urandom_fd = open("/dev/urandom", O_RDONLY);
    if (urandom_fd < 0) {
        perror("could not open file descriptor");
        exit(EXIT_FAILURE);
    }

//    char bytes[READ_BATCH_SIZE];
    unsigned char bytes_arr[D][READ_BATCH_SIZE];


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

    elapsed = (double)(finish.tv_sec - start.tv_sec);
    elapsed += (double)(finish.tv_nsec - start.tv_nsec) / 1000000000.0;

    printf("Filling up the memory took: %f seconds\n", elapsed);


//    char *ptr = (char *) allocated_memory;

//    for (size_t i = 0; i < size_in_bytes; i++) {
//        printf(" %d ", ptr[i]);
//        if (i % 10000 == 0) {
//            printf("\n\n\n\n");
//        }
//    }
//    printf("\n -------- -------- ------- \n");

//    printf("\n -------- -------- ------- \n %d "
//           "\n %d \n -------- -------- ------- \n", ptr[0], ptr[size_in_bytes-1]);

    int files_amount = ( A / E ) + 1;
    int *file_descriptors = malloc(sizeof(int) * files_amount);

    clean_files(files_amount);
    open_files(files_amount, file_descriptors);

    struct write_to_files_args *to_file_args = malloc(sizeof(struct write_to_files_args));

    to_file_args->file_batch_size_bytes             = size_in_bytes / files_amount;
    to_file_args->file_batch_size_bytes_remained    = size_in_bytes % files_amount;
    to_file_args->files_amount                      = files_amount;
    to_file_args->fds                               = file_descriptors;
    to_file_args->allocated_memory                  = allocated_memory;

    if (pthread_create(&write_to_files_thread_id, NULL, write_to_files, to_file_args)){
        free(to_file_args);
        perror("Can't create write to files thread");
    }

    pthread_t readFromFileThreads[I];

    int fileIndex = 0;

    clock_gettime(CLOCK_MONOTONIC, &start);

    for (size_t i = 0; i < I; i++) {
        struct ReadFromFileArgs* fileArgs = malloc(sizeof(struct ReadFromFileArgs));
        if (fileIndex >= files_amount) fileIndex = 0;
        fileArgs->fd = file_descriptors[fileIndex];
        fileIndex += 1;
        if (pthread_create(&readFromFileThreads[i], NULL, ReadFile, (void*) fileArgs)) {
            free(fileArgs);
            perror("Can't create thread");
        }
    }

    for (int i = 0; i < I; i++) {
        pthread_join(readFromFileThreads[i], NULL);
    }

    clock_gettime(CLOCK_MONOTONIC, &finish);

    elapsed = (finish.tv_sec - start.tv_sec);
    elapsed += (finish.tv_nsec - start.tv_nsec) / 1000000000.0;

    printf("Reading from files: %f seconds\n", elapsed);

    pthread_cancel(write_to_files_thread_id);

    // close files
    if(close(urandom_fd) < 0) {
        perror("could not close file descriptor");
        exit(EXIT_FAILURE);
    }
    free(allocated_memory);
    return 0;
}
