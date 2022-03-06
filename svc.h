#ifndef svc_h
#define svc_h

#include <stdlib.h>

struct helper {
    char * dir;
    struct commit **commits;
    size_t n_commits;
    struct branch **branches;
    size_t n_branches;
    struct branch *current_branch;
};

struct branch {
    char *branch_name;
    struct commit *head;
    struct tracked_file *files;
    size_t n_files;
};

struct commit {
    char id[7];
    struct tracked_file *files;
    size_t n_files;
    struct branch *branch;
    char *message;
    struct commit **parents;
    size_t n_parents;
};

struct tracked_file {
    char *file_name;
    int hash;
    char change;
};

typedef struct resolution {
    // NOTE: DO NOT MODIFY THIS STRUCT
    char *file_name;
    char *resolved_file;
} resolution;

void *svc_init(void);

void cleanup(void *helper);

int hash_file(void *helper, char *file_path);

char *svc_commit(void *helper, char *message);

void *get_commit(void *helper, char *commit_id);

char **get_prev_commits(void *helper, void *commit, int *n_prev);

void print_commit(void *helper, char *commit_id);

int svc_branch(void *helper, char *branch_name);

int svc_checkout(void *helper, char *branch_name);

char **list_branches(void *helper, int *n_branches);

int svc_add(void *helper, char *file_name);

int svc_rm(void *helper, char *file_name);

int svc_reset(void *helper, char *commit_id);

char *svc_merge(void *helper, char *branch_name, resolution *resolutions,
                                                       int n_resolutions);

void set_commit_id(struct commit*);

int compar(const void *a, const void *b);

void remove_tracked_files(struct branch *branch, int *arr, int rem_count);

char *str_concat(char ** arr, size_t n_strings);

int check_changes(struct helper *helper);

void set_to_commit(struct helper *helper, struct branch *branch,
                                          struct commit *commit);

#endif
