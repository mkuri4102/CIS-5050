#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <string.h>
#include <errno.h>
#include <pthread.h>
#include <vector>
#include <algorithm>

#define DEFAULT_N 4
#define MAX_NUMBERS 1000000
#define NUMSIZE 256  // Define buffer size for reading numbers from pipes

//dont' need
typedef struct {
    long *arr;
    int size;
} ThreadData;

int use_threads = 0;

// Bubble Sort Algorithm
void bubble_sort(std::vector<long long>& arr) {
    int size = arr.size();
    for (int i = 0; i < size - 1; i++) {
        for (int j = 0; j < size - i - 1; j++) {
            if (arr[j] > arr[j + 1]) {
                long long temp = arr[j];
                arr[j] = arr[j + 1];
                arr[j + 1] = temp;
            }
        }
    }
}

// Merge
std::vector<long long> merge_sorted_arrays(std::vector<std::vector<long long>>& sorted_arrays) {
    std::vector<long long> result;
    for (const auto& arr : sorted_arrays) {
        result.insert(result.end(), arr.begin(), arr.end());
    }
    std::sort(result.begin(), result.end());  // Final sorting
    return result;
}

// Child thread function
void* thread_kid(void* arg) {
    int* pipeInfo = (int*) arg;
    int childRead = pipeInfo[0];
    int childWrite = pipeInfo[1];

    // Read vector from parent via pipe
    std::vector<long long> piece;
    char line[NUMSIZE];
    long long num;
    FILE* input = fdopen(childRead, "r");
    while (fgets(line, NUMSIZE, input) != NULL) {
        num = atoll(line);
        piece.push_back(num);
    }
    fclose(input);

    // do bubble sort on the vector
    bubble_sort(piece);

    // Write sorted vector back to parent
    FILE* output = fdopen(childWrite, "w");
    for (long long num : piece) {
        fprintf(output, "%lld\n", num);
    }
    fclose(output);

    pthread_exit(NULL);
}

int main(int argc, char *argv[]) {
    int N = DEFAULT_N;
    char **input_files;
    int num_files;

    if (argc == 1) {
        fprintf(stderr, "Author: Minna Kuriakose, SEAS login: mkuri\n");
        return 1;
    }

    // Parse command-line arguments
    int opt;
    while ((opt = getopt(argc, argv, "n:t")) != -1) {
        switch (opt) {
            case 'n':
                N = atoi(optarg);
                if (N < 0) {
                    fprintf(stderr, "Error: N must be 0 or a positive integer.\n");
                    exit(1);
                }
                break;
            case 't':
                use_threads = 1;
                break;
            default:
                fprintf(stderr, "Usage: %s [-t] [-n num_processes] file1 file2 ...\n", argv[0]);
                return 1;
        }
    }

    // Parse input files
    input_files = argv + optind;
    num_files = argc - optind;
    if (num_files == 0) {
        fprintf(stderr, "Error: No input files provided.\n");
        return 1;
    }

    // Read numbers from input files
    std::vector<long long> numbers;
    for (int i = 0; i < num_files; i++) {
        FILE* file = fopen(input_files[i], "r");
        if (!file) {
            perror("Error opening file");
            return 1;
        }
        long long num;
        while (fscanf(file, "%lld", &num) == 1) {
            numbers.push_back(num);
        }
        fclose(file);
    }

    if (N <= 1) {
        bubble_sort(numbers);
        for (long long num : numbers) {
            printf("%lld\n", num);
        }
        return 0;
    }

    // Split into chunks for parallel sorting
    int chunk_size = numbers.size() / N;
    int remainder = numbers.size() % N;
    std::vector<std::vector<long long>> split(N);

    int start = 0;
    for (int i = 0; i < N; i++) {
        int size = chunk_size + (i < remainder ? 1 : 0);
        split[i].insert(split[i].end(), numbers.begin() + start, numbers.begin() + start + size);
        start += size;
    }

    if (use_threads) {
        // Thread-based sorting
        pthread_t threads[N];
        int upload[N][2], download[N][2];
        int Info[N][2];

        std::vector<std::vector<long long>> sorted;

        for (int i = 0; i < N; i++) {
            pipe(upload[i]);
            pipe(download[i]);

            Info[i][0] = upload[i][0];  // Read end
            Info[i][1] = download[i][1];  // Write end

            pthread_create(&threads[i], NULL, thread_kid, &(Info[i]));

            // Write to child
            FILE* output = fdopen(upload[i][1], "w");
            for (long long num : split[i]) {
                fprintf(output, "%lld\n", num);
            }
            fclose(output);
        }

        // Collect sorted vectors from children
        for (int i = 0; i < N; i++) {
            std::vector<long long> piece;
            char line[NUMSIZE];
            long long num;
            FILE* input = fdopen(download[i][0], "r");
            while (fgets(line, NUMSIZE, input) != NULL) {
                num = atoll(line);
                piece.push_back(num);
            }
            sorted.push_back(piece);
            fclose(input);
        }

        // Wait for all threads to finish
        for (int i = 0; i < N; i++) {
            pthread_join(threads[i], NULL);
        }

        // Merge
        std::vector<long long> result = merge_sorted_arrays(sorted);
        for (long long num : result) {
            printf("%lld\n", num);
        }
    } else {
    	//test remove
    	//printf("Entering process-based sorting...\n");
        int down_pipes[N][2], up_pipes[N][2];
        for (int i = 0; i < N; i++) {
            if (pipe(down_pipes[i]) == -1 || pipe(up_pipes[i]) == -1) {
                perror("Error creating pipes");
                exit(1);
            }
        }

        int start = 0;
        for (int i = 0; i < N; i++) {
            int size = chunk_size + (i < remainder ? 1 : 0);
            if (fork() == 0) { // Child process
                close(down_pipes[i][1]);  // Close write down pipe
                close(up_pipes[i][0]);    // Close read up pipe

                long *subarray = new long[size];
                read(down_pipes[i][0], subarray, size * sizeof(long));
                close(down_pipes[i][0]);

                std::vector<long long> temp(subarray, subarray + size);  // Convert to vector
                //printf("Child %d is sorting %zu elements.\n", i, temp.size());
                bubble_sort(temp);

                //printf("Child %d has sorted %zu elements. Writing to pipe.\n", i, temp.size());
                write(up_pipes[i][1], temp.data(), size * sizeof(long));
                close(up_pipes[i][1]);
                delete[] subarray;
                exit(0);
            }
            close(down_pipes[i][0]);
            close(up_pipes[i][1]);
            write(down_pipes[i][1], &numbers[start], size * sizeof(long));
            close(down_pipes[i][1]);
            start += size;
        }

        std::vector<std::vector<long long>> sorted_arrays(N);

        for (int i = 0; i < N; i++) {
            int size = chunk_size + (i < remainder ? 1 : 0);
            std::vector<long long> temp(size);
            ssize_t bytes_read = read(up_pipes[i][0], temp.data(), size * sizeof(long long));
           /* if (bytes_read == -1) {
                perror("Read failed");
            } else {
                printf("Parent read %zu elements from pipe %d.\n", bytes_read / sizeof(long long), i);
            }*/
            sorted_arrays[i] = std::move(temp);

            close(up_pipes[i][0]);  // Close read pipe
        }

        // Merge sorted arrays
        std::vector<long long> final_sorted = merge_sorted_arrays(sorted_arrays);
        //printf("Size of final_sorted: %zu\n", final_sorted.size());  // Check size of final_sorted
        for (long long num : final_sorted) {
            printf("%lld\n", num);
        }
    }

    return 0;
}
