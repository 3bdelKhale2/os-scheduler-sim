#include <gtk/gtk.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

// Constants
#define MEMORY_SIZE 60
#define MAX_LINES 10
#define MAX_LINE_LENGTH 256
#define MAX_QUEUE_SIZE 3
#define MAX_PROCESSES 3

// Function prototypes
void on_input_entered(GtkEntry *entry, gpointer data);
gboolean refresh_gui(gpointer data);
gboolean run_simulation_step(gpointer data);
void on_file_button_clicked(GtkButton *button, gpointer data);
void on_start_clicked(GtkButton *button, gpointer data);
void on_stop_clicked(GtkButton *button, gpointer data);
void on_reset_clicked(GtkButton *button, gpointer data);
void on_step_clicked(GtkButton *button, gpointer data);
void on_auto_clicked(GtkButton *button, gpointer data);

// Global variables
int clck = 0;
int program_arrival_times[MAX_PROCESSES] = {-1, -1, -1};
int RR_quantum = 2; // Default value
int Desired_scheduler = 1; // Default: FIFO
char *program_files[MAX_PROCESSES] = {NULL, NULL, NULL};
char *program_names[MAX_PROCESSES] = {"Program_1.txt", "Program_2.txt", "Program_3.txt"};

// New globals for handling user input
gboolean waiting_for_input = FALSE;
char pending_input[256] = "";
int input_requesting_pid = -1;
char input_variable[256] = "";

// Struct definitions
typedef struct {
    char name[256];
    int acquired;
    int owner_pid;
    int blocked[MAX_QUEUE_SIZE];
    int blocked_priorities[MAX_QUEUE_SIZE];
    int blocked_count;
} Resource;

typedef struct {
    char name[20];
    char data[256];
} MemoryWord;

typedef struct {
    MemoryWord words[MEMORY_SIZE];
    int allocated[MEMORY_SIZE];
} Memory;

typedef struct {
    int pids[MAX_QUEUE_SIZE];
    int priorities[MAX_QUEUE_SIZE];
    int size;
} ArrayQueue;

// GUI state
typedef struct {
    GtkWidget *window;
    GtkWidget *process_list_view;
    GtkWidget *ready_queue_view;
    GtkWidget *blocked_queue_view;
    GtkWidget *resource_view;
    GtkWidget *memory_view;
    GtkWidget *log_text_view;
    GtkTextBuffer *log_buffer;
    GtkWidget *output_text_view; // Output panel
    GtkTextBuffer *output_buffer; // Output buffer
    GtkWidget *scheduler_combo;
    GtkWidget *quantum_entry;
    GtkWidget *arrival_entries[MAX_PROCESSES];
    GtkWidget *file_buttons[MAX_PROCESSES];
    GtkWidget *start_button;
    GtkWidget *stop_button;
    GtkWidget *reset_button;
    GtkWidget *step_button;
    GtkWidget *auto_button;
    GtkWidget *overview_label;
    gboolean running;
    guint timer_id;
} GuiState;

// Global system state
Resource UserInput, UserOutput, File;
Memory memory;
ArrayQueue ready_queues[4];
ArrayQueue blocked_queue;
int running_pid = -1;
int time_in_quantum = 0;
GuiState gui_state;

// Initialize memory
void initializeMemory(Memory* memory) {
    for (int i = 0; i < MEMORY_SIZE; i++) {
        memory->allocated[i] = 0;
        strcpy(memory->words[i].data, "");
        if (i % 20 == 0) strcpy(memory->words[i].name, "ProcessID");
        else if (i % 20 == 1) strcpy(memory->words[i].name, "ProcessState");
        else if (i % 20 == 2) strcpy(memory->words[i].name, "ProcessPriority");
        else if (i % 20 == 3) strcpy(memory->words[i].name, "ProgramCounter");
        else if (i % 20 == 4) strcpy(memory->words[i].name, "MemoryStart");
        else if (i % 20 == 5) strcpy(memory->words[i].name, "MemoryEnd");
        else if (i % 20 >= 6 && i % 20 <= 15) strcpy(memory->words[i].name, "Instruction");
        else strcpy(memory->words[i].name, "Variable");
    }
}

// Initialize resource
void initializeResource(Resource* resource, const char* name) {
    strcpy(resource->name, name);
    resource->acquired = 0;
    resource->owner_pid = -1;
    resource->blocked_count = 0;
    for (int i = 0; i < MAX_QUEUE_SIZE; i++) {
        resource->blocked[i] = -1;
        resource->blocked_priorities[i] = -1;
    }
}

// Initialize all resources
void initializeAllResources() {
    initializeResource(&UserInput, "userInput");
    initializeResource(&UserOutput, "userOutput");
    initializeResource(&File, "file");
}

// Initialize queue
void initializeArrayQueue(ArrayQueue* q) {
    q->size = 0;
    for (int i = 0; i < MAX_QUEUE_SIZE; i++) {
        q->pids[i] = -1;
        q->priorities[i] = -1;
    }
}

// Enqueue with priority
void enqueueArray(ArrayQueue* q, int pid, int priority) {
    if (q->size >= MAX_QUEUE_SIZE) {
        char msg[512];
        snprintf(msg, sizeof(msg), "Queue full! Cannot enqueue PID %d.\n", pid);
        gtk_text_buffer_insert_at_cursor(gui_state.log_buffer, msg, -1);
        return;
    }
    int i = q->size - 1;
    while (i >= 0 && q->priorities[i] < priority) {
        q->pids[i + 1] = q->pids[i];
        q->priorities[i + 1] = q->priorities[i];
        i--;
    }
    q->pids[i + 1] = pid;
    q->priorities[i + 1] = priority;
    q->size++;
    char msg[512];
    snprintf(msg, sizeof(msg), "PID %d enqueued with priority %d.\n", pid, priority);
    gtk_text_buffer_insert_at_cursor(gui_state.log_buffer, msg, -1);
}

// Dequeue
int dequeueArray(ArrayQueue* q) {
    if (q->size == 0) return -1;
    int pid = q->pids[0];
    for (int i = 1; i < q->size; i++) {
        q->pids[i - 1] = q->pids[i];
        q->priorities[i - 1] = q->priorities[i];
    }
    q->size--;
    char msg[512];
    snprintf(msg, sizeof(msg), "PID %d dequeued.\n", pid);
    gtk_text_buffer_insert_at_cursor(gui_state.log_buffer, msg, -1);
    return pid;
}

// Peek
int peekArray(ArrayQueue* q) {
    if (q->size == 0) return -1;
    return q->pids[0];
}

// Read program file
char** readFileToArray(const char* fileName) {
    FILE* file = fopen(fileName, "r");
    if (!file) {
        char msg[512];
        snprintf(msg, sizeof(msg), "Error: Cannot open file %s. Ensure it exists.\n", fileName);
        gtk_text_buffer_insert_at_cursor(gui_state.log_buffer, msg, -1);
        return NULL;
    }
    char** lines = (char**)calloc(MAX_LINES, sizeof(char*));
    if (!lines) {
        fclose(file);
        char msg[512];
        snprintf(msg, sizeof(msg), "Error: Memory allocation failed for %s.\n", fileName);
        gtk_text_buffer_insert_at_cursor(gui_state.log_buffer, msg, -1);
        return NULL;
    }
    char buffer[MAX_LINE_LENGTH];
    int lineCount = 0;
    while (lineCount < MAX_LINES && fgets(buffer, MAX_LINE_LENGTH, file)) {
        if (ferror(file)) {
            char msg[512];
            snprintf(msg, sizeof(msg), "Error: Failed to read from %s.\n", fileName);
            gtk_text_buffer_insert_at_cursor(gui_state.log_buffer, msg, -1);
            for (int i = 0; i < lineCount; i++) free(lines[i]);
            free(lines);
            fclose(file);
            return NULL;
        }
        buffer[strcspn(buffer, "\n")] = '\0';
        if (strlen(buffer) > 0) { // Only store non-empty lines
            lines[lineCount] = strdup(buffer);
            if (!lines[lineCount]) {
                char msg[512];
                snprintf(msg, sizeof(msg), "Error: Memory allocation failed for line %d in %s.\n", lineCount + 1, fileName);
                gtk_text_buffer_insert_at_cursor(gui_state.log_buffer, msg, -1);
                for (int i = 0; i < lineCount; i++) free(lines[i]);
                free(lines);
                fclose(file);
                return NULL;
            }
            lineCount++;
        }
    }
    fclose(file);
    return lines;
}

// Find process index
int findProcessIndex(int pid) {
    for (int i = 0; i < MEMORY_SIZE; i += 20) {
        if (memory.allocated[i] && atoi(memory.words[i].data) == pid) {
            return i;
        }
    }
    return -1;
}

// Assign variable
void assign(int pid, char* variable, char* value) {
    int idx = findProcessIndex(pid);
    if (idx == -1) {
        char msg[512];
        snprintf(msg, sizeof(msg), "Process %d not found for assign.\n", pid);
        gtk_text_buffer_insert_at_cursor(gui_state.log_buffer, msg, -1);
        return;
    }
    if (strcmp(value, "input") == 0) {
        if (UserInput.acquired && UserInput.owner_pid != pid) {
            UserInput.blocked[UserInput.blocked_count] = pid;
            UserInput.blocked_priorities[UserInput.blocked_count] = atoi(memory.words[idx + 2].data);
            UserInput.blocked_count++;
            strcpy(memory.words[idx + 1].data, "Blocked");
            enqueueArray(&blocked_queue, pid, atoi(memory.words[idx + 2].data));
            running_pid = -1;
            char msg[512];
            snprintf(msg, sizeof(msg), "Process %d blocked on userInput.\n", pid);
            gtk_text_buffer_insert_at_cursor(gui_state.log_buffer, msg, -1);
            return;
        }
        UserInput.acquired = 1;
        UserInput.owner_pid = pid;
        waiting_for_input = TRUE;
        input_requesting_pid = pid;
        strcpy(input_variable, variable);
        char msg[512];
        snprintf(msg, sizeof(msg), "Process %d: Please enter a value for %s.\n", pid, variable);
        gtk_text_buffer_insert_at_cursor(gui_state.log_buffer, msg, -1);
        gui_state.running = FALSE;
        if (gui_state.timer_id) {
            g_source_remove(gui_state.timer_id);
            gui_state.timer_id = 0;
        }
        gtk_widget_set_sensitive(gui_state.start_button, TRUE);
        gtk_widget_set_sensitive(gui_state.stop_button, FALSE);
        return;
    }
    int assigned = 0;
    for (int i = idx + 16; i < idx + 19; i++) {
        if (!memory.allocated[i] || strcmp(memory.words[i].name, variable) == 0) {
            strcpy(memory.words[i].name, variable);
            strcpy(memory.words[i].data, value);
            memory.allocated[i] = 1;
            char msg[512];
            snprintf(msg, sizeof(msg), "Process %d assigned %s = %s.\n", pid, variable, value);
            gtk_text_buffer_insert_at_cursor(gui_state.log_buffer, msg, -1);
            assigned = 1;
            break;
        }
    }
    if (!assigned) {
        char msg[512];
        snprintf(msg, sizeof(msg), "Process %d: No memory slots available for variable %s.\n", pid, variable);
        gtk_text_buffer_insert_at_cursor(gui_state.log_buffer, msg, -1);
    }
}

// Write file
void writeFile(int pid, char* filename, char* data) {
    int idx = findProcessIndex(pid);
    if (idx == -1) return;
    if (File.acquired && File.owner_pid != pid) {
        File.blocked[File.blocked_count] = pid;
        File.blocked_priorities[File.blocked_count] = atoi(memory.words[idx + 2].data);
        File.blocked_count++;
        strcpy(memory.words[idx + 1].data, "Blocked");
        enqueueArray(&blocked_queue, pid, atoi(memory.words[idx + 2].data));
        running_pid = -1;
        char msg[512];
        snprintf(msg, sizeof(msg), "Process %d blocked on file.\n", pid);
        gtk_text_buffer_insert_at_cursor(gui_state.log_buffer, msg, -1);
        return;
    }
    FILE* file = fopen(filename, "w");
    if (file) {
        fprintf(file, "%s", data);
        if (ferror(file)) {
            char msg[512];
            snprintf(msg, sizeof(msg), "Process %d: Error writing to %s.\n", pid, filename);
            gtk_text_buffer_insert_at_cursor(gui_state.log_buffer, msg, -1);
        } else {
            char msg[512];
            snprintf(msg, sizeof(msg), "Process %d wrote '%s' to %s.\n", pid, data, filename);
            gtk_text_buffer_insert_at_cursor(gui_state.log_buffer, msg, -1);
        }
        fclose(file);
    } else {
        char msg[512];
        snprintf(msg, sizeof(msg), "Process %d: Error opening %s.\n", pid, filename);
        gtk_text_buffer_insert_at_cursor(gui_state.log_buffer, msg, -1);
    }
}

// Read file
void readFile(int pid, char* filename) {
    int idx = findProcessIndex(pid);
    if (idx == -1) return;

    if (File.acquired && File.owner_pid != pid) {
        File.blocked[File.blocked_count] = pid;
        File.blocked_priorities[File.blocked_count] = atoi(memory.words[idx + 2].data);
        File.blocked_count++;
        strcpy(memory.words[idx + 1].data, "Blocked");
        enqueueArray(&blocked_queue, pid, atoi(memory.words[idx + 2].data));
        running_pid = -1;
        char msg[512];
        snprintf(msg, sizeof(msg), "Process %d blocked on file.\n", pid);
        gtk_text_buffer_insert_at_cursor(gui_state.log_buffer, msg, -1);
        return;
    }

    FILE* file = fopen(filename, "r");
    if (file) {
        char buffer[MAX_LINE_LENGTH];
        while (fgets(buffer, MAX_LINE_LENGTH, file)) {
            if (ferror(file)) {
                char msg[512];
                snprintf(msg, sizeof(msg), "Process %d: Error reading %s.\n", pid, filename);
                gtk_text_buffer_insert_at_cursor(gui_state.log_buffer, msg, -1);
                break;
            }
            char msg[512];
            snprintf(msg, sizeof(msg), "Process %d read: %s", pid, buffer);
            gtk_text_buffer_insert_at_cursor(gui_state.log_buffer, msg, -1);
        }
        fclose(file);
    } else {
        char msg[512];
        snprintf(msg, sizeof(msg), "Process %d: Error opening %s.\n", pid, filename);
        gtk_text_buffer_insert_at_cursor(gui_state.log_buffer, msg, -1);
    }
}

// Print from-to
void printFromTo(int pid, char* var1, char* var2) {
    int idx = findProcessIndex(pid);
    if (idx == -1) return;
    if (UserOutput.acquired && UserOutput.owner_pid != pid) {
        UserOutput.blocked[UserOutput.blocked_count] = pid;
        UserOutput.blocked_priorities[UserOutput.blocked_count] = atoi(memory.words[idx + 2].data);
        UserOutput.blocked_count++;
        strcpy(memory.words[idx + 1].data, "Blocked");
        enqueueArray(&blocked_queue, pid, atoi(memory.words[idx + 2].data));
        running_pid = -1;
        char msg[512];
        snprintf(msg, sizeof(msg), "Process %d blocked on userOutput.\n", pid);
        gtk_text_buffer_insert_at_cursor(gui_state.log_buffer, msg, -1);
        return;
    }
    int start = -1, end = -1;
    for (int i = idx + 16; i < idx + 19; i++) {
        if (memory.allocated[i] && strcmp(memory.words[i].name, var1) == 0) {
            start = atoi(memory.words[i].data);
        }
        if (memory.allocated[i] && strcmp(memory.words[i].name, var2) == 0) {
            end = atoi(memory.words[i].data);
        }
    }
    if (start != -1 && end != -1) {
        char buffer[512] = "";
        size_t buffer_len = 0;
        const size_t max_buffer = sizeof(buffer) - 10;
        int count = 0;
        const int max_count = 50;
        if (start <= end) {
            for (int i = start; i <= end && buffer_len < max_buffer && count < max_count; i++) {
                char num[12];
                snprintf(num, sizeof(num), "%d ", i);
                size_t num_len = strlen(num);
                if (buffer_len + num_len < max_buffer) {
                    strcat(buffer, num);
                    buffer_len += num_len;
                    count++;
                } else {
                    strcat(buffer, "...");
                    break;
                }
            }
        } else {
            for (int i = start; i >= end && buffer_len < max_buffer && count < max_count; i--) {
                char num[12];
                snprintf(num, sizeof(num), "%d ", i);
                size_t num_len = strlen(num);
                if (buffer_len + num_len < max_buffer) {
                    strcat(buffer, num);
                    buffer_len += num_len;
                    count++;
                } else {
                    strcat(buffer, "...");
                    break;
                }
            }
        }
        char msg[1536];
        snprintf(msg, sizeof(msg), "[PID %d] %s\n", pid, buffer);
        gtk_text_buffer_insert_at_cursor(gui_state.output_buffer, msg, -1);
        snprintf(msg, sizeof(msg), "Process %d output: %s\n", pid, buffer);
        gtk_text_buffer_insert_at_cursor(gui_state.log_buffer, msg, -1);
    } else {
        char msg[512];
        snprintf(msg, sizeof(msg), "Process %d: Variables %s or %s not found.\n", pid, var1, var2);
        gtk_text_buffer_insert_at_cursor(gui_state.log_buffer, msg, -1);
    }
}

// Print variable
void print(int pid, char* variable) {
    int idx = findProcessIndex(pid);
    if (idx == -1) return;
    if (UserOutput.acquired && UserOutput.owner_pid != pid) {
        UserOutput.blocked[UserOutput.blocked_count] = pid;
        UserOutput.blocked_priorities[UserOutput.blocked_count] = atoi(memory.words[idx + 2].data);
        UserOutput.blocked_count++;
        strcpy(memory.words[idx + 1].data, "Blocked");
        enqueueArray(&blocked_queue, pid, atoi(memory.words[idx + 2].data));
        running_pid = -1;
        char msg[512];
        snprintf(msg, sizeof(msg), "Process %d blocked on userOutput.\n", pid);
        gtk_text_buffer_insert_at_cursor(gui_state.log_buffer, msg, -1);
        return;
    }
    for (int i = idx + 16; i < idx + 19; i++) {
        if (memory.allocated[i] && strcmp(memory.words[i].name, variable) == 0) {
            char msg[512];
            snprintf(msg, sizeof(msg), "[PID %d] %s\n", pid, memory.words[i].data);
            gtk_text_buffer_insert_at_cursor(gui_state.output_buffer, msg, -1);
            snprintf(msg, sizeof(msg), "Process %d printed: %s\n", pid, memory.words[i].data);
            gtk_text_buffer_insert_at_cursor(gui_state.log_buffer, msg, -1);
            break;
        }
    }
}

// Case-insensitive string comparison
int strcasecmp(const char *s1, const char *s2) {
    while (*s1 && *s2) {
        if (tolower((unsigned char)*s1) != tolower((unsigned char)*s2)) {
            return tolower((unsigned char)*s1) - tolower((unsigned char)*s2);
        }
        s1++;
        s2++;
    }
    return tolower((unsigned char)*s1) - tolower((unsigned char)*s2);
}

// SemWait
void semWait(int pid, char* resource) {
    Resource* res = NULL;
    if (strcasecmp(resource, "userInput") == 0) res = &UserInput;
    else if (strcasecmp(resource, "userOutput") == 0) res = &UserOutput;
    else if (strcasecmp(resource, "file") == 0) res = &File;
    if (!res) {
        char msg[512];
        snprintf(msg, sizeof(msg), "Process %d: Invalid resource %s.\n", pid, resource);
        gtk_text_buffer_insert_at_cursor(gui_state.log_buffer, msg, -1);
        return;
    }
    int idx = findProcessIndex(pid);
    if (res->acquired && res->owner_pid != pid) {
        res->blocked[res->blocked_count] = pid;
        res->blocked_priorities[res->blocked_count] = atoi(memory.words[idx + 2].data);
        res->blocked_count++;
        strcpy(memory.words[idx + 1].data, "Blocked");
        enqueueArray(&blocked_queue, pid, atoi(memory.words[idx + 2].data));
        running_pid = -1;
        char msg[512];
        snprintf(msg, sizeof(msg), "Process %d blocked on %s.\n", pid, res->name);
        gtk_text_buffer_insert_at_cursor(gui_state.log_buffer, msg, -1);
    } else {
        res->acquired = 1;
        res->owner_pid = pid;
        char msg[512];
        snprintf(msg, sizeof(msg), "Process %d acquired %s.\n", pid, res->name);
        gtk_text_buffer_insert_at_cursor(gui_state.log_buffer, msg, -1);
    }
}

// SemSignal
void semSignal(int pid, char* resource) {
    Resource* res = NULL;
    if (strcasecmp(resource, "userInput") == 0) res = &UserInput;
    else if (strcasecmp(resource, "userOutput") == 0) res = &UserOutput;
    else if (strcasecmp(resource, "file") == 0) res = &File;
    if (!res || res->owner_pid != pid) {
        char msg[512];
        if (!res) {
            snprintf(msg, sizeof(msg), "Process %d: Invalid resource %s.\n", pid, resource);
        } else {
            snprintf(msg, sizeof(msg), "Process %d: Cannot signal %s (not owner).\n", pid, resource);
        }
        gtk_text_buffer_insert_at_cursor(gui_state.log_buffer, msg, -1);
        return;
    }
    res->acquired = 0;
    res->owner_pid = -1;
    char msg[512];
    snprintf(msg, sizeof(msg), "Process %d released %s.\n", pid, res->name);
    gtk_text_buffer_insert_at_cursor(gui_state.log_buffer, msg, -1);
    if (res->blocked_count > 0) {
        int max_priority_idx = 0;
        for (int i = 1; i < res->blocked_count; i++) {
            if (res->blocked_priorities[i] > res->blocked_priorities[max_priority_idx]) {
                max_priority_idx = i;
            }
        }
        int unblocked_pid = res->blocked[max_priority_idx];
        int priority = res->blocked_priorities[max_priority_idx];
        for (int i = max_priority_idx; i < res->blocked_count - 1; i++) {
            res->blocked[i] = res->blocked[i + 1];
            res->blocked_priorities[i] = res->blocked_priorities[i + 1];
        }
        res->blocked_count--;
        int unblocked_idx = findProcessIndex(unblocked_pid);
        strcpy(memory.words[unblocked_idx + 1].data, "Ready");
        enqueueArray(&ready_queues[priority - 1], unblocked_pid, priority);
        snprintf(msg, sizeof(msg), "Process %d unblocked on %s.\n", unblocked_pid, res->name);
        gtk_text_buffer_insert_at_cursor(gui_state.log_buffer, msg, -1);
    }
}

// Execute command
void executeCommand(int pid, char* command) {
    char cmd_copy[MAX_LINE_LENGTH];
    strcpy(cmd_copy, command);
    char* token = strtok(cmd_copy, " ");
    if (!token) return;

    char msg[512];
    snprintf(msg, sizeof(msg), "Process %d executing: %s\n", pid, command);
    gtk_text_buffer_insert_at_cursor(gui_state.log_buffer, msg, -1);

    if (strcmp(token, "assign") == 0) {
        char* var = strtok(NULL, " ");
        char* val = strtok(NULL, " ");
        if (var && val) {
            if (strcmp(val, "readFile") == 0) {
                char* file = NULL;
                char* file_var = strtok(NULL, " ");
                for (int i = 0; i < MEMORY_SIZE; i += 20) {
                    for (int j = i + 16; j < i + 19; j++) {
                        if (memory.allocated[j] && strcmp(memory.words[j].name, file_var) == 0) {
                            file = memory.words[j].data;
                        }
                    }
                }
                if (file) {
                    char** lines = readFileToArray(file);
                    if (lines) {
                        char file_content[MAX_LINE_LENGTH * MAX_LINES] = "";
                        for (int i = 0; i < MAX_LINES && lines[i]; i++) {
                            strcat(file_content, lines[i]);
                            strcat(file_content, "\n");
                            free(lines[i]);
                        }
                        free(lines);
                        assign(pid, var, file_content);
                    }
                } else {
                    char msg[512];
                    snprintf(msg, sizeof(msg), "Process %d: Missing file name for readFile in assign.\n", pid);
                    gtk_text_buffer_insert_at_cursor(gui_state.log_buffer, msg, -1);
                }
            } else {
                assign(pid, var, val);
            }
        }
    } else if (strcmp(token, "print") == 0) {
        char* var = strtok(NULL, " ");
        if (var) print(pid, var);
    } else if (strcmp(token, "writeFile") == 0) {
        char* file = strtok(NULL, " ");
        char* data = strtok(NULL, " ");
        if (file && data) writeFile(pid, file, data);
    } else if (strcmp(token, "readFile") == 0) {
        char* file = strtok(NULL, " ");
        if (file) readFile(pid, file);
    } else if (strcmp(token, "printFromTo") == 0) {
        char* var1 = strtok(NULL, " ");
        char* var2 = strtok(NULL, " ");
        if (var1 && var2) printFromTo(pid, var1, var2);
    } else if (strcmp(token, "semWait") == 0) {
        char* res = strtok(NULL, " ");
        if (res) semWait(pid, res);
    } else if (strcmp(token, "semSignal") == 0) {
        char* res = strtok(NULL, " ");
        if (res) semSignal(pid, res);
    }
}

// Load process
void fill_memory(int pid, char* filename, int priority, int arrival_time) {
    char** instructions = readFileToArray(filename);
    if (!instructions) return;
    int start = -1;
    for (int i = 0; i < MEMORY_SIZE; i += 20) {
        if (!memory.allocated[i]) {
            start = i;
            break;
        }
    }
    if (start == -1) {
        char msg[512];
        snprintf(msg, sizeof(msg), "Memory full! Cannot load process %d.\n", pid);
        gtk_text_buffer_insert_at_cursor(gui_state.log_buffer, msg, -1);
        for (int i = 0; i < MAX_LINES; i++) {
            if (instructions[i]) free(instructions[i]);
        }
        free(instructions);
        return;
    }
    // Initialize process metadata
    sprintf(memory.words[start].data, "%d", pid);
    memory.allocated[start] = 1;
    strcpy(memory.words[start + 1].data, "Ready");
    memory.allocated[start + 1] = 1;
    sprintf(memory.words[start + 2].data, "%d", priority);
    memory.allocated[start + 2] = 1;
    sprintf(memory.words[start + 3].data, "%d", start + 6);
    memory.allocated[start + 3] = 1;
    sprintf(memory.words[start + 4].data, "%d", start);
    memory.allocated[start + 4] = 1;
    sprintf(memory.words[start + 5].data, "%d", start + 19);
    memory.allocated[start + 5] = 1;
    // Load instructions
    int instruction_count = 0;
    for (int i = 0; i < MAX_LINES && instructions[i]; i++) {
        strcpy(memory.words[start + 6 + i].data, instructions[i]);
        memory.allocated[start + 6 + i] = 1;
        instruction_count++;
    }
    // Clear remaining instruction slots
    for (int i = start + 6 + instruction_count; i < start + 16; i++) {
        strcpy(memory.words[i].data, "");
        memory.allocated[i] = 0;
    }
    // Clear variable slots
    for (int i = start + 16; i < start + 20; i++) {
        strcpy(memory.words[i].data, "");
        memory.allocated[i] = 0;
    }
    for (int i = 0; i < MAX_LINES; i++) {
        if (instructions[i]) free(instructions[i]);
    }
    free(instructions);
    if (clck >= arrival_time) {
        enqueueArray(&ready_queues[priority - 1], pid, priority);
        char msg[512];
        snprintf(msg, sizeof(msg), "Process %d loaded at clock %d.\n", pid, clck);
        gtk_text_buffer_insert_at_cursor(gui_state.log_buffer, msg, -1);
    }
}

// FIFO scheduler
void FIFO() {
    if (running_pid != -1) {
        int idx = findProcessIndex(running_pid);
        int pc = atoi(memory.words[idx + 3].data);
        if (pc < idx + 6 || pc > idx + 15 || !memory.allocated[pc]) {
            char msg[512];
            snprintf(msg, sizeof(msg), "Process %d terminated.\n", running_pid);
            gtk_text_buffer_insert_at_cursor(gui_state.log_buffer, msg, -1);
            strcpy(memory.words[idx + 1].data, "Terminated");
            running_pid = -1;
        } else {
            executeCommand(running_pid, memory.words[pc].data);
            if (running_pid != -1) {
                sprintf(memory.words[idx + 3].data, "%d", pc + 1);
            }
        }
    }
    if (running_pid == -1) {
        int pid = dequeueArray(&ready_queues[0]);
        if (pid != -1) {
            running_pid = pid;
            int idx = findProcessIndex(pid);
            strcpy(memory.words[idx + 1].data, "Running");
            char msg[512];
            snprintf(msg, sizeof(msg), "Process %d started (FIFO).\n", pid);
            gtk_text_buffer_insert_at_cursor(gui_state.log_buffer, msg, -1);
        }
    }
}

// Round Robin scheduler
void RR() {
    if (running_pid != -1) {
        int idx = findProcessIndex(running_pid);
        int pc = atoi(memory.words[idx + 3].data);
        if (pc < idx + 6 || pc > idx + 15 || !memory.allocated[pc]) {
            char msg[512];
            snprintf(msg, sizeof(msg), "Process %d terminated.\n", running_pid);
            gtk_text_buffer_insert_at_cursor(gui_state.log_buffer, msg, -1);
            strcpy(memory.words[idx + 1].data, "Terminated");
            running_pid = -1;
            time_in_quantum = 0;
        } else {
            executeCommand(running_pid, memory.words[pc].data);
            if (running_pid != -1) {
                sprintf(memory.words[idx + 3].data, "%d", pc + 1);
                time_in_quantum++;
                if (time_in_quantum >= RR_quantum) {
                    strcpy(memory.words[idx + 1].data, "Ready");
                    int priority = atoi(memory.words[idx + 2].data);
                    enqueueArray(&ready_queues[0], running_pid, priority);
                    running_pid = -1;
                    time_in_quantum = 0;
                    char msg[512];
                    snprintf(msg, sizeof(msg), "Process %d quantum expired (RR).\n", running_pid);
                    gtk_text_buffer_insert_at_cursor(gui_state.log_buffer, msg, -1);
                }
            } else {
                time_in_quantum = 0;
            }
        }
    }
    if (running_pid == -1) {
        int pid = dequeueArray(&ready_queues[0]);
        if (pid != -1) {
            running_pid = pid;
            int idx = findProcessIndex(pid);
            strcpy(memory.words[idx + 1].data, "Running");
            char msg[512];
            snprintf(msg, sizeof(msg), "Process %d started (RR).\n", pid);
            gtk_text_buffer_insert_at_cursor(gui_state.log_buffer, msg, -1);
        }
    }
}

// MLFQ scheduler
void MLFQ() {
    if (running_pid != -1) {
        int idx = findProcessIndex(running_pid);
        int pc = atoi(memory.words[idx + 3].data);
        int priority = atoi(memory.words[idx + 2].data);
        int quantum = (priority == 4) ? 8 : (1 << (priority - 1));
        if (pc < idx + 6 || pc > idx + 15 || !memory.allocated[pc]) {
            char msg[512];
            snprintf(msg, sizeof(msg), "Process %d terminated.\n", running_pid);
            gtk_text_buffer_insert_at_cursor(gui_state.log_buffer, msg, -1);
            strcpy(memory.words[idx + 1].data, "Terminated");
            running_pid = -1;
            time_in_quantum = 0;
        } else {
            executeCommand(running_pid, memory.words[pc].data);
            if (running_pid != -1) {
                sprintf(memory.words[idx + 3].data, "%d", pc + 1);
                time_in_quantum++;
                if (time_in_quantum >= quantum) {
                    strcpy(memory.words[idx + 1].data, "Ready");
                    if (priority < 4) {
                        sprintf(memory.words[idx + 2].data, "%d", priority + 1);
                        enqueueArray(&ready_queues[priority], running_pid, priority + 1);
                    } else {
                        enqueueArray(&ready_queues[3], running_pid, 4);
                    }
                    char msg[512];
                    snprintf(msg, sizeof(msg), "Process %d quantum expired, moved to priority %d (MLFQ).\n", running_pid, priority < 4 ? priority + 1 : 4);
                    gtk_text_buffer_insert_at_cursor(gui_state.log_buffer, msg, -1);
                    running_pid = -1;
                    time_in_quantum = 0;
                }
            } else {
                time_in_quantum = 0;
            }
        }
    }
    if (running_pid == -1) {
        for (int i = 0; i < 4; i++) {
            int pid = dequeueArray(&ready_queues[i]);
            if (pid != -1) {
                running_pid = pid;
                int idx = findProcessIndex(pid);
                strcpy(memory.words[idx + 1].data, "Running");
                char msg[512];
                snprintf(msg, sizeof(msg), "Process %d started, priority %d (MLFQ).\n", pid, i + 1);
                gtk_text_buffer_insert_at_cursor(gui_state.log_buffer, msg, -1);
                break;
            }
        }
    }
}

// Run one simulation step
gboolean run_simulation_step(gpointer data) {
    if (waiting_for_input) {
        return G_SOURCE_CONTINUE;
    }

    if (!gui_state.running && data == NULL) {
    } else if (!gui_state.running) {
        return G_SOURCE_REMOVE;
    }

    int processes_to_load = 0;
    for (int i = 0; i < MAX_PROCESSES; i++) {
        if (program_files[i] && program_arrival_times[i] >= 0) {
            processes_to_load++;
            if (program_arrival_times[i] == clck) {
                fill_memory(i + 1, program_files[i], 1, program_arrival_times[i]);
            }
        }
    }

    if (Desired_scheduler == 1) FIFO();
    else if (Desired_scheduler == 2) RR();
    else if (Desired_scheduler == 3) MLFQ();

    clck++;
    char msg[512];
    snprintf(msg, sizeof(msg), "Clock incremented to %d.\n", clck);
    gtk_text_buffer_insert_at_cursor(gui_state.log_buffer, msg, -1);

    int active_processes = running_pid != -1;
    for (int i = 0; i < 4; i++) active_processes |= ready_queues[i].size > 0;
    active_processes |= blocked_queue.size > 0;
    int loaded_processes = 0;
    for (int i = 0; i < MEMORY_SIZE; i += 20) {
        if (memory.allocated[i]) loaded_processes++;
    }
    if (loaded_processes >= processes_to_load && !active_processes && !waiting_for_input) {
        gui_state.running = FALSE;
        gtk_widget_set_sensitive(gui_state.start_button, TRUE);
        gtk_widget_set_sensitive(gui_state.stop_button, FALSE);
        char msg[512];
        snprintf(msg, sizeof(msg), "Simulation completed at clock %d.\n", clck);
        gtk_text_buffer_insert_at_cursor(gui_state.log_buffer, msg, -1);
        return G_SOURCE_REMOVE;
    }

    g_idle_add((GSourceFunc)refresh_gui, NULL);
    return G_SOURCE_CONTINUE;
}

// Input entered callback
void on_input_entered(GtkEntry *entry, gpointer data) {
    const char *input = gtk_entry_get_text(entry);
    if (strlen(input) > 0) {
        if (waiting_for_input && input_requesting_pid != -1) {
            strncpy(pending_input, input, sizeof(pending_input) - 1);
            pending_input[sizeof(pending_input) - 1] = '\0';
            if (UserInput.acquired && UserInput.owner_pid == input_requesting_pid) {
                assign(input_requesting_pid, input_variable, pending_input);
                waiting_for_input = FALSE;
                input_requesting_pid = -1;
                strcpy(input_variable, "");
                strcpy(pending_input, "");
                gtk_entry_set_text(entry, "");
                g_idle_add((GSourceFunc)refresh_gui, NULL);
            } else {
                char msg[512];
                snprintf(msg, sizeof(msg), "Error: UserInput not acquired by PID %d.\n", input_requesting_pid);
                gtk_text_buffer_insert_at_cursor(gui_state.log_buffer, msg, -1);
            }
        } else {
            char msg[512];
            snprintf(msg, sizeof(msg), "User input received: %s (no process waiting).\n", input);
            gtk_text_buffer_insert_at_cursor(gui_state.log_buffer, msg, -1);
            gtk_entry_set_text(entry, "");
        }
    }
}

// Refresh GUI
gboolean refresh_gui(gpointer data) {
    char overview[512];
    snprintf(overview, sizeof(overview),
             "Clock: %d\nTotal Processes: %d\nScheduler: %s",
             clck, MAX_PROCESSES,
             Desired_scheduler == 1 ? "FIFO" : Desired_scheduler == 2 ? "RR" : "MLFQ");
    gtk_label_set_text(GTK_LABEL(gui_state.overview_label), overview);

    GtkListStore *process_store = GTK_LIST_STORE(gtk_tree_view_get_model(GTK_TREE_VIEW(gui_state.process_list_view)));
    gtk_list_store_clear(process_store);
    for (int i = 0; i < MEMORY_SIZE; i += 20) {
        if (memory.allocated[i]) {
            int pid = atoi(memory.words[i].data);
            GtkTreeIter iter;
            gtk_list_store_append(process_store, &iter);
            gtk_list_store_set(process_store, &iter,
                               0, pid,
                               1, memory.words[i + 1].data,
                               2, atoi(memory.words[i + 2].data),
                               3, atoi(memory.words[i + 3].data),
                               4, atoi(memory.words[i + 4].data),
                               5, atoi(memory.words[i + 5].data),
                               -1);
        }
    }

    GtkListStore *ready_store = GTK_LIST_STORE(gtk_tree_view_get_model(GTK_TREE_VIEW(gui_state.ready_queue_view)));
    gtk_list_store_clear(ready_store);
    for (int i = 0; i < 4; i++) {
        for (int j = 0; j < ready_queues[i].size; j++) {
            int pid = ready_queues[i].pids[j];
            int idx = findProcessIndex(pid);
            if (idx != -1) {
                char instruction[256] = "N/A";
                int pc = atoi(memory.words[idx + 3].data);
                if (pc >= idx + 6 && pc <= idx + 15 && memory.allocated[pc]) {
                    strcpy(instruction, memory.words[pc].data);
                }
                GtkTreeIter iter;
                gtk_list_store_append(ready_store, &iter);
                gtk_list_store_set(ready_store, &iter,
                                   0, pid,
                                   1, instruction,
                                   2, i + 1,
                                   -1);
            }
        }
    }

    GtkListStore *blocked_store = GTK_LIST_STORE(gtk_tree_view_get_model(GTK_TREE_VIEW(gui_state.blocked_queue_view)));
    gtk_list_store_clear(blocked_store);
    for (int i = 0; i < blocked_queue.size; i++) {
        int pid = blocked_queue.pids[i];
        int idx = findProcessIndex(pid);
        if (idx != -1) {
            char instruction[256] = "N/A";
            int pc = atoi(memory.words[idx + 3].data);
            if (pc >= idx + 6 && pc <= idx + 15 && memory.allocated[pc]) {
                strcpy(instruction, memory.words[pc].data);
            }
            GtkTreeIter iter;
            gtk_list_store_append(blocked_store, &iter);
            gtk_list_store_set(blocked_store, &iter,
                               0, pid,
                               1, instruction,
                               -1);
        }
    }

    GtkListStore *resource_store = GTK_LIST_STORE(gtk_tree_view_get_model(GTK_TREE_VIEW(gui_state.resource_view)));
    gtk_list_store_clear(resource_store);
    Resource* resources[] = {&UserInput, &UserOutput, &File};
    for (int i = 0; i < 3; i++) {
        char blocked_pids[256] = "";
        for (int j = 0; j < resources[i]->blocked_count; j++) {
            char pid_str[10];
            snprintf(pid_str, sizeof(pid_str), "%d ", resources[i]->blocked[j]);
            strcat(blocked_pids, pid_str);
        }
        GtkTreeIter iter;
        gtk_list_store_append(resource_store, &iter);
        gtk_list_store_set(resource_store, &iter,
                           0, resources[i]->name,
                           1, resources[i]->acquired ? "Taken" : "Free",
                           2, resources[i]->owner_pid,
                           3, blocked_pids,
                           4, resources[i]->blocked_count,
                           -1);
    }

    GtkListStore *memory_store = GTK_LIST_STORE(gtk_tree_view_get_model(GTK_TREE_VIEW(gui_state.memory_view)));
    gtk_list_store_clear(memory_store);
    for (int i = 0; i < MEMORY_SIZE; i++) {
        if (memory.allocated[i]) {
            GtkTreeIter iter;
            gtk_list_store_append(memory_store, &iter);
            gtk_list_store_set(memory_store, &iter,
                               0, i,
                               1, memory.words[i].name,
                               2, memory.words[i].data,
                               3, memory.allocated[i],
                               -1);
        }
    }

    return G_SOURCE_REMOVE;
}

// File chooser callback
void on_file_button_clicked(GtkButton *button, gpointer data) {
    int index = GPOINTER_TO_INT(data);
    GtkWidget *dialog = gtk_file_chooser_dialog_new("Select Program File",
                                                   GTK_WINDOW(gui_state.window),
                                                   GTK_FILE_CHOOSER_ACTION_OPEN,
                                                   "_Cancel", GTK_RESPONSE_CANCEL,
                                                   "_Open", GTK_RESPONSE_ACCEPT,
                                                   NULL);
    if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT) {
        char *filename = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(dialog));
        if (program_files[index]) free(program_files[index]);
        program_files[index] = strdup(filename);
        char msg[512];
        snprintf(msg, sizeof(msg), "Loaded program %d: %s\n", index + 1, filename);
        gtk_text_buffer_insert_at_cursor(gui_state.log_buffer, msg, -1);
        g_free(filename);
    }
    gtk_widget_destroy(dialog);
}

// Start simulation
void on_start_clicked(GtkButton *button, gpointer data) {
    if (waiting_for_input) {
        char msg[512];
        snprintf(msg, sizeof(msg), "Waiting for user input for process %d.\n", input_requesting_pid);
        gtk_text_buffer_insert_at_cursor(gui_state.log_buffer, msg, -1);
        return;
    }

    const char *quantum_text = gtk_entry_get_text(GTK_ENTRY(gui_state.quantum_entry));
    RR_quantum = atoi(quantum_text);
    if (RR_quantum <= 0) RR_quantum = 2;

    int valid_files = 0;
    for (int i = 0; i < MAX_PROCESSES; i++) {
        const char *arrival_text = gtk_entry_get_text(GTK_ENTRY(gui_state.arrival_entries[i]));
        program_arrival_times[i] = atoi(arrival_text);
        if (program_arrival_times[i] < 0) program_arrival_times[i] = 0;
        if (!program_files[i]) {
            program_files[i] = strdup(program_names[i]);
        }
        FILE *file = fopen(program_files[i], "r");
        if (file) {
            valid_files++;
            fclose(file);
        } else {
            char msg[512];
            snprintf(msg, sizeof(msg), "Error: Program file %s not found.\n", program_files[i]);
            gtk_text_buffer_insert_at_cursor(gui_state.log_buffer, msg, -1);
            if (program_files[i]) {
                free(program_files[i]);
                program_files[i] = NULL;
            }
        }
    }
    if (valid_files == 0) {
        char msg[512];
        snprintf(msg, sizeof(msg), "Error: No valid program files loaded.\n");
        gtk_text_buffer_insert_at_cursor(gui_state.log_buffer, msg, -1);
        return;
    }

    Desired_scheduler = gtk_combo_box_get_active(GTK_COMBO_BOX(gui_state.scheduler_combo)) + 1;
    gui_state.running = TRUE;
    gtk_widget_set_sensitive(gui_state.start_button, FALSE);
    gtk_widget_set_sensitive(gui_state.stop_button, TRUE);
    gui_state.timer_id = g_timeout_add(1000, run_simulation_step, NULL);
    char msg[512];
    snprintf(msg, sizeof(msg), "Simulation started with %s scheduler.\n", Desired_scheduler == 1 ? "FIFO" : Desired_scheduler == 2 ? "RR" : "MLFQ");
    gtk_text_buffer_insert_at_cursor(gui_state.log_buffer, msg, -1);
}

// Stop simulation
void on_stop_clicked(GtkButton *button, gpointer data) {
    gui_state.running = FALSE;
    gtk_widget_set_sensitive(gui_state.start_button, TRUE);
    gtk_widget_set_sensitive(gui_state.stop_button, FALSE);
    if (gui_state.timer_id) {
        g_source_remove(gui_state.timer_id);
        gui_state.timer_id = 0;
    }
    char msg[512];
    snprintf(msg, sizeof(msg), "Simulation stopped at clock %d.\n", clck);
    gtk_text_buffer_insert_at_cursor(gui_state.log_buffer, msg, -1);
}

// Reset simulation
void on_reset_clicked(GtkButton *button, gpointer data) {
    gui_state.running = FALSE;
    waiting_for_input = FALSE;
    input_requesting_pid = -1;
    strcpy(input_variable, "");
    strcpy(pending_input, "");
    gtk_widget_set_sensitive(gui_state.start_button, TRUE);
    gtk_widget_set_sensitive(gui_state.stop_button, FALSE);
    if (gui_state.timer_id) {
        g_source_remove(gui_state.timer_id);
        gui_state.timer_id = 0;
    }
    clck = 0;
    running_pid = -1;
    time_in_quantum = 0;
    initializeMemory(&memory);
    initializeAllResources();
    for (int i = 0; i < 4; i++) initializeArrayQueue(&ready_queues[i]);
    initializeArrayQueue(&blocked_queue);
    gtk_text_buffer_set_text(gui_state.log_buffer, "", -1);
    gtk_text_buffer_set_text(gui_state.output_buffer, "", -1);
    for (int i = 0; i < MAX_PROCESSES; i++) {
        gtk_entry_set_text(GTK_ENTRY(gui_state.arrival_entries[i]), "");
        if (program_files[i]) free(program_files[i]);
        program_files[i] = NULL;
        char msg[512];
        snprintf(msg, sizeof(msg), "Reset: Please load %s for Program %d.\n", program_names[i], i + 1);
        gtk_text_buffer_insert_at_cursor(gui_state.log_buffer, msg, -1);
    }
    gtk_entry_set_text(GTK_ENTRY(gui_state.quantum_entry), "2");
    g_idle_add((GSourceFunc)refresh_gui, NULL);
    char msg[512];
    snprintf(msg, sizeof(msg), "Simulation reset.\n");
    gtk_text_buffer_insert_at_cursor(gui_state.log_buffer, msg, -1);
}

// Step simulation
void on_step_clicked(GtkButton *button, gpointer data) {
    if (waiting_for_input) {
        char msg[512];
        snprintf(msg, sizeof(msg), "Waiting for user input for process %d.\n", input_requesting_pid);
        gtk_text_buffer_insert_at_cursor(gui_state.log_buffer, msg, -1);
        return;
    }

    int valid_files = 0;
    for (int i = 0; i < MAX_PROCESSES; i++) {
        if (program_files[i]) {
            FILE *file = fopen(program_files[i], "r");
            if (file) {
                valid_files++;
                fclose(file);
            }
        }
    }
    if (valid_files == 0) {
        char msg[512];
        snprintf(msg, sizeof(msg), "Error: No valid program files loaded.\n");
        gtk_text_buffer_insert_at_cursor(gui_state.log_buffer, msg, -1);
        return;
    }

    if (gui_state.timer_id) {
        g_source_remove(gui_state.timer_id);
        gui_state.timer_id = 0;
    }
    gui_state.running = FALSE;
    gtk_widget_set_sensitive(gui_state.start_button, TRUE);
    gtk_widget_set_sensitive(gui_state.stop_button, FALSE);

    run_simulation_step(NULL);
}

// Auto execute
void on_auto_clicked(GtkButton *button, gpointer data) {
    on_start_clicked(NULL, NULL);
}

void create_gui() {
    gui_state.window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(gui_state.window), "Operating System Simulator");
    gtk_window_set_default_size(GTK_WINDOW(gui_state.window), 1200, 800);
    g_signal_connect(gui_state.window, "destroy", G_CALLBACK(gtk_main_quit), NULL);

    GtkWidget *grid = gtk_grid_new();
    gtk_container_add(GTK_CONTAINER(gui_state.window), grid);
    gtk_grid_set_row_spacing(GTK_GRID(grid), 10);
    gtk_grid_set_column_spacing(GTK_GRID(grid), 10);
    gtk_grid_set_row_homogeneous(GTK_GRID(grid), FALSE);
    gtk_grid_set_column_homogeneous(GTK_GRID(grid), TRUE);
    gtk_container_set_border_width(GTK_CONTAINER(gui_state.window), 10);

    // Left Column
    gui_state.overview_label = gtk_label_new("Clock: 0\nTotal Processes: 0\nScheduler: None");
    gtk_widget_set_halign(gui_state.overview_label, GTK_ALIGN_START);
    gtk_grid_attach(GTK_GRID(grid), gui_state.overview_label, 0, 0, 1, 1);

    GtkListStore *process_store = gtk_list_store_new(6, G_TYPE_INT, G_TYPE_STRING, G_TYPE_INT, G_TYPE_INT, G_TYPE_INT, G_TYPE_INT);
    gui_state.process_list_view = gtk_tree_view_new_with_model(GTK_TREE_MODEL(process_store));
    GtkCellRenderer *renderer = gtk_cell_renderer_text_new();
    gtk_tree_view_insert_column_with_attributes(GTK_TREE_VIEW(gui_state.process_list_view), -1, "PID", renderer, "text", 0, NULL);
    gtk_tree_view_insert_column_with_attributes(GTK_TREE_VIEW(gui_state.process_list_view), -1, "State", renderer, "text", 1, NULL);
    gtk_tree_view_insert_column_with_attributes(GTK_TREE_VIEW(gui_state.process_list_view), -1, "Priority", renderer, "text", 2, NULL);
    gtk_tree_view_insert_column_with_attributes(GTK_TREE_VIEW(gui_state.process_list_view), -1, "PC", renderer, "text", 3, NULL);
    gtk_tree_view_insert_column_with_attributes(GTK_TREE_VIEW(gui_state.process_list_view), -1, "Mem Start", renderer, "text", 4, NULL);
    gtk_tree_view_insert_column_with_attributes(GTK_TREE_VIEW(gui_state.process_list_view), -1, "Mem End", renderer, "text", 5, NULL);
    GtkWidget *process_scroll = gtk_scrolled_window_new(NULL, NULL);
    gtk_widget_set_vexpand(process_scroll, TRUE);
    gtk_container_add(GTK_CONTAINER(process_scroll), gui_state.process_list_view);
    gtk_grid_attach(GTK_GRID(grid), process_scroll, 0, 1, 1, 1);

    GtkListStore *ready_store = gtk_list_store_new(3, G_TYPE_INT, G_TYPE_STRING, G_TYPE_INT);
    gui_state.ready_queue_view = gtk_tree_view_new_with_model(GTK_TREE_MODEL(ready_store));
    gtk_tree_view_insert_column_with_attributes(GTK_TREE_VIEW(gui_state.ready_queue_view), -1, "PID", renderer, "text", 0, NULL);
    gtk_tree_view_insert_column_with_attributes(GTK_TREE_VIEW(gui_state.ready_queue_view), -1, "Current Instruction", renderer, "text", 1, NULL);
    gtk_tree_view_insert_column_with_attributes(GTK_TREE_VIEW(gui_state.ready_queue_view), -1, "Priority", renderer, "text", 2, NULL);
    GtkWidget *ready_scroll = gtk_scrolled_window_new(NULL, NULL);
    gtk_widget_set_vexpand(ready_scroll, TRUE);
    gtk_container_add(GTK_CONTAINER(ready_scroll), gui_state.ready_queue_view);
    gtk_grid_attach(GTK_GRID(grid), ready_scroll, 0, 2, 1, 1);

    GtkListStore *blocked_store = gtk_list_store_new(2, G_TYPE_INT, G_TYPE_STRING);
    gui_state.blocked_queue_view = gtk_tree_view_new_with_model(GTK_TREE_MODEL(blocked_store));
    gtk_tree_view_insert_column_with_attributes(GTK_TREE_VIEW(gui_state.blocked_queue_view), -1, "PID", renderer, "text", 0, NULL);
    gtk_tree_view_insert_column_with_attributes(GTK_TREE_VIEW(gui_state.blocked_queue_view), -1, "Current Instruction", renderer, "text", 1, NULL);
    GtkWidget *blocked_scroll = gtk_scrolled_window_new(NULL, NULL);
    gtk_widget_set_vexpand(blocked_scroll, TRUE);
    gtk_container_add(GTK_CONTAINER(blocked_scroll), gui_state.blocked_queue_view);
    gtk_grid_attach(GTK_GRID(grid), blocked_scroll, 0, 3, 1, 1);

    GtkListStore *resource_store = gtk_list_store_new(5, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_INT, G_TYPE_STRING, G_TYPE_INT);
    gui_state.resource_view = gtk_tree_view_new_with_model(GTK_TREE_MODEL(resource_store));
    gtk_tree_view_insert_column_with_attributes(GTK_TREE_VIEW(gui_state.resource_view), -1, "Resource", renderer, "text", 0, NULL);
    gtk_tree_view_insert_column_with_attributes(GTK_TREE_VIEW(gui_state.resource_view), -1, "Status", renderer, "text", 1, NULL);
    gtk_tree_view_insert_column_with_attributes(GTK_TREE_VIEW(gui_state.resource_view), -1, "Owner PID", renderer, "text", 2, NULL);
    gtk_tree_view_insert_column_with_attributes(GTK_TREE_VIEW(gui_state.resource_view), -1, "Blocked PIDs", renderer, "text", 3, NULL);
    gtk_tree_view_insert_column_with_attributes(GTK_TREE_VIEW(gui_state.resource_view), -1, "Blocked Count", renderer, "text", 4, NULL);
    GtkWidget *resource_scroll = gtk_scrolled_window_new(NULL, NULL);
    gtk_widget_set_vexpand(resource_scroll, TRUE);
    gtk_container_add(GTK_CONTAINER(resource_scroll), gui_state.resource_view);
    gtk_grid_attach(GTK_GRID(grid), resource_scroll, 0, 4, 1, 1);

    GtkWidget *input_entry = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(input_entry), "Enter input here");
    g_signal_connect(input_entry, "activate", G_CALLBACK(on_input_entered), NULL);
    gtk_grid_attach(GTK_GRID(grid), input_entry, 0, 5, 1, 1);

    // Right Column
    GtkWidget *right_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
    gtk_widget_set_vexpand(right_box, TRUE);

    GtkListStore *memory_store = gtk_list_store_new(4, G_TYPE_INT, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_INT);
    gui_state.memory_view = gtk_tree_view_new_with_model(GTK_TREE_MODEL(memory_store));
    gtk_tree_view_insert_column_with_attributes(GTK_TREE_VIEW(gui_state.memory_view), -1, "Index", renderer, "text", 0, NULL);
    gtk_tree_view_insert_column_with_attributes(GTK_TREE_VIEW(gui_state.memory_view), -1, "Name", renderer, "text", 1, NULL);
    gtk_tree_view_insert_column_with_attributes(GTK_TREE_VIEW(gui_state.memory_view), -1, "Data", renderer, "text", 2, NULL);
    gtk_tree_view_insert_column_with_attributes(GTK_TREE_VIEW(gui_state.memory_view), -1, "Allocated", renderer, "text", 3, NULL);
    GtkWidget *memory_scroll = gtk_scrolled_window_new(NULL, NULL);
    gtk_widget_set_vexpand(memory_scroll, TRUE);
    gtk_container_add(GTK_CONTAINER(memory_scroll), gui_state.memory_view);
    gtk_box_pack_start(GTK_BOX(right_box), memory_scroll, TRUE, TRUE, 0);

    // Output panel
    gui_state.output_text_view = gtk_text_view_new();
    gui_state.output_buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(gui_state.output_text_view));
    gtk_text_view_set_editable(GTK_TEXT_VIEW(gui_state.output_text_view), FALSE);
    GtkWidget *output_scroll = gtk_scrolled_window_new(NULL, NULL);
    gtk_widget_set_vexpand(output_scroll, TRUE);
    gtk_container_add(GTK_CONTAINER(output_scroll), gui_state.output_text_view);
    gtk_box_pack_start(GTK_BOX(right_box), output_scroll, TRUE, TRUE, 0);

    // Log panel
    gui_state.log_text_view = gtk_text_view_new();
    gui_state.log_buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(gui_state.log_text_view));
    gtk_text_view_set_editable(GTK_TEXT_VIEW(gui_state.log_text_view), FALSE);
    GtkWidget *log_scroll = gtk_scrolled_window_new(NULL, NULL);
    gtk_widget_set_vexpand(log_scroll, TRUE);
    gtk_container_add(GTK_CONTAINER(log_scroll), gui_state.log_text_view);
    gtk_box_pack_start(GTK_BOX(right_box), log_scroll, TRUE, TRUE, 0);

    // Control panel
    GtkWidget *control_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
    gui_state.scheduler_combo = gtk_combo_box_text_new();
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(gui_state.scheduler_combo), "FIFO");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(gui_state.scheduler_combo), "Round Robin");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(gui_state.scheduler_combo), "MLFQ");
    gtk_combo_box_set_active(GTK_COMBO_BOX(gui_state.scheduler_combo), 0);
    gtk_box_pack_start(GTK_BOX(control_box), gtk_label_new("Scheduler:"), FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(control_box), gui_state.scheduler_combo, FALSE, FALSE, 0);

    gui_state.quantum_entry = gtk_entry_new();
    gtk_entry_set_text(GTK_ENTRY(gui_state.quantum_entry), "2");
    gtk_box_pack_start(GTK_BOX(control_box), gtk_label_new("Quantum:"), FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(control_box), gui_state.quantum_entry, FALSE, FALSE, 0);

    gui_state.start_button = gtk_button_new_with_label("Start");
    g_signal_connect(gui_state.start_button, "clicked", G_CALLBACK(on_start_clicked), NULL);
    gtk_box_pack_start(GTK_BOX(control_box), gui_state.start_button, FALSE, FALSE, 0);

    gui_state.stop_button = gtk_button_new_with_label("Stop");
    g_signal_connect(gui_state.stop_button, "clicked", G_CALLBACK(on_stop_clicked), NULL);
    gtk_widget_set_sensitive(gui_state.stop_button, FALSE);
    gtk_box_pack_start(GTK_BOX(control_box), gui_state.stop_button, FALSE, FALSE, 0);

    gui_state.reset_button = gtk_button_new_with_label("Reset");
    g_signal_connect(gui_state.reset_button, "clicked", G_CALLBACK(on_reset_clicked), NULL);
    gtk_box_pack_start(GTK_BOX(control_box), gui_state.reset_button, FALSE, FALSE, 0);

    gui_state.step_button = gtk_button_new_with_label("Step");
    g_signal_connect(gui_state.step_button, "clicked", G_CALLBACK(on_step_clicked), NULL);
    gtk_box_pack_start(GTK_BOX(control_box), gui_state.step_button, FALSE, FALSE, 0);

    gui_state.auto_button = gtk_button_new_with_label("Auto Execute");
    g_signal_connect(gui_state.auto_button, "clicked", G_CALLBACK(on_auto_clicked), NULL);
    gtk_box_pack_start(GTK_BOX(control_box), gui_state.auto_button, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(right_box), control_box, FALSE, FALSE, 0);

    // Process creation
    GtkWidget *process_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
    for (int i = 0; i < MAX_PROCESSES; i++) {
        GtkWidget *hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
        char label[32];
        snprintf(label, sizeof(label), "Program %d Arrival Time:", i + 1);
        gtk_box_pack_start(GTK_BOX(hbox), gtk_label_new(label), FALSE, FALSE, 0);
        gui_state.arrival_entries[i] = gtk_entry_new();
        gtk_box_pack_start(GTK_BOX(hbox), gui_state.arrival_entries[i], FALSE, FALSE, 0);
        gui_state.file_buttons[i] = gtk_button_new_with_label("Load File");
        g_signal_connect(gui_state.file_buttons[i], "clicked", G_CALLBACK(on_file_button_clicked), GINT_TO_POINTER(i));
        gtk_box_pack_start(GTK_BOX(hbox), gui_state.file_buttons[i], FALSE, FALSE, 0);
        gtk_box_pack_start(GTK_BOX(process_box), hbox, FALSE, FALSE, 0);
    }
    gtk_box_pack_start(GTK_BOX(right_box), process_box, FALSE, FALSE, 0);
    gtk_grid_attach(GTK_GRID(grid), right_box, 1, 0, 1, 6);

    // CSS
    GtkCssProvider *provider = gtk_css_provider_new();
    gtk_css_provider_load_from_data(provider,
                                    "window { background-color: #f5f5f5; }"
                                    "label { font-size: 12px; }"
                                    "button { padding: 5px; margin: 5px; }"
                                    "textview { font-family: monospace; font-size: 10px; }",
                                    -1, NULL);
    gtk_style_context_add_provider_for_screen(gdk_screen_get_default(),
                                             GTK_STYLE_PROVIDER(provider),
                                             GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    g_object_unref(provider);

    gtk_widget_show_all(gui_state.window);
}

// Main function
int main(int argc, char *argv[]) {
    gtk_init(&argc, &argv);

    initializeMemory(&memory);
    initializeAllResources();
    for (int i = 0; i < 4; i++) {
        initializeArrayQueue(&ready_queues[i]);
    }
    initializeArrayQueue(&blocked_queue);

    create_gui();

    char msg[512];
    snprintf(msg, sizeof(msg), "Simulator initialized. Please load program files and set arrival times.\n");
    gtk_text_buffer_insert_at_cursor(gui_state.log_buffer, msg, -1);

    gtk_main();
    return 0;
}