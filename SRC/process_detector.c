
#define _DEFAULT_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <unistd.h>
#include <ctype.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <errno.h>

#define PROC_DIR "/proc"
#define INTERVAL 3

typedef struct {
    int pid;
    int ppid;
    char state;
    char name[256];
} Process;

int is_number(const char *s) {
    if (*s == '\0')
        return 0;

    while (*s) {
        if (!isdigit((unsigned char)*s))
            return 0;
        s++;
    }
    return 1;
}

/* Read process information from /proc/PID/stat */
int read_process(int pid, Process *p) {
    char path[128];
    char buffer[4096];

    snprintf(path, sizeof(path), "/proc/%d/stat", pid);

    FILE *fp = fopen(path, "r");
    if (fp == NULL)
        return 0;

    if (fgets(buffer, sizeof(buffer), fp) == NULL) {
        fclose(fp);
        return 0;
    }
    fclose(fp);

    char *left = strchr(buffer, '(');
    char *right = strrchr(buffer, ')');

    if (left == NULL || right == NULL || right <= left)
        return 0;

    size_t len = (size_t)(right - left - 1);
    if (len >= sizeof(p->name))
        len = sizeof(p->name) - 1;

    memcpy(p->name, left + 1, len);
    p->name[len] = '\0';

    /*
       After ')' the fields are:
       state = field 3
       ppid  = field 4
    */
    char state;
    int ppid;

    if (sscanf(right + 1, " %c %d", &state, &ppid) != 2)
        return 0;

    p->pid = pid;
    p->ppid = ppid;
    p->state = state;

    return 1;
}

void scan_processes(void) {
    DIR *dir = opendir(PROC_DIR);

    if (dir == NULL) {
        perror("Cannot open /proc");
        return;
    }

    struct dirent *entry;
    int total = 0, zombies = 0, possible_orphans = 0;

    printf("\n===============================================================\n");
    printf("       ZOMBIE AND ORPHAN PROCESS DETECTION SYSTEM\n");
    printf("===============================================================\n");
    printf("%-8s %-8s %-8s %-22s %s\n",
           "PID", "PPID", "STATE", "PROCESS", "DETECTION");

    while ((entry = readdir(dir)) != NULL) {
        if (!is_number(entry->d_name))
            continue;

        int pid = atoi(entry->d_name);
        Process p;

        if (!read_process(pid, &p))
            continue;

        total++;

        const char *status = "Normal";

        if (p.state == 'Z') {
            status = "ZOMBIE";
            zombies++;
        } else if (p.pid != getpid() && p.ppid == 1) {
            /*
              PPID 1 often indicates adoption by the system
              reaper, but does not prove orphanhood.
            */
            status = "POSSIBLE ORPHAN";
            possible_orphans++;
        }

        printf("%-8d %-8d %-8c %-22.22s %s\n",
               p.pid, p.ppid, p.state, p.name, status);
    }

    closedir(dir);

    printf("---------------------------------------------------------------\n");
    printf("Processes scanned       : %d\n", total);
    printf("Zombie processes        : %d\n", zombies);
    printf("Possible orphan processes: %d\n", possible_orphans);
    printf("---------------------------------------------------------------\n");
}

void monitor_processes(void) {
    printf("\nLive monitoring started. Press Ctrl+C to stop.\n");

    while (1) {
        scan_processes();
        sleep(INTERVAL);
    }
}

/* Create a zombie process for demonstration */
void create_zombie_demo(void) {
    pid_t pid = fork();

    if (pid < 0) {
        perror("fork");
        return;
    }

    if (pid == 0) {
        _exit(0);
    }

    /*
      The parent deliberately does not call wait().
      The child should remain a zombie while this program
      continues running.
    */
    printf("\nZombie demo created.\n");
    printf("Child PID: %d\n", pid);
    printf("Choose Scan Processes to detect it.\n");
}

/*
  Create a child whose original parent exits.
  The child sleeps so there is time to scan /proc.
*/
void create_orphan_demo(void) {
    pid_t helper = fork();

    if (helper < 0) {
        perror("fork");
        return;
    }

    if (helper == 0) {
        pid_t child = fork();

        if (child < 0)
            _exit(1);

        if (child == 0) {
            printf("Orphan demo child PID: %d\n", getpid());
            fflush(stdout);
            sleep(60);
            _exit(0);
        }

        /* Helper exits without waiting for its child. */
        _exit(0);
    }

    /*
      Reap the helper, not the grandchild.
      The grandchild becomes adopted when helper exits.
    */
    while (waitpid(helper, NULL, 0) < 0 && errno == EINTR) {
    }

    printf("\nOrphan demo initiated.\n");
    printf("The child may be adopted by PID 1 or a subreaper.\n");
    printf("Scan processes to inspect its current PPID.\n");
}

int main(void) {
    int choice;

    while (1) {
        printf("\n========================================\n");
        printf(" ZOMBIE AND ORPHAN PROCESS DETECTOR\n");
        printf("========================================\n");
        printf("1. Scan processes\n");
        printf("2. Start live monitoring\n");
        printf("3. Create zombie test process\n");
        printf("4. Create orphan test process\n");
        printf("5. Exit\n");
        printf("Enter your choice: ");

        if (scanf("%d", &choice) != 1) {
            printf("Invalid input. Enter a number.\n");

            int ch;
            while ((ch = getchar()) != '\n' && ch != EOF) {
            }
            continue;
        }

        switch (choice) {
            case 1:
                scan_processes();
                break;

            case 2:
                monitor_processes();
                break;

            case 3:
                create_zombie_demo();
                break;

            case 4:
                create_orphan_demo();
                break;

            case 5:
                printf("Exiting detector. Goodbye!\n");
                return 0;

            default:
                printf("Invalid choice. Select 1 to 5.\n");
        }
    }

    return 0;
}
