#include "svc.h"
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include <sys/types.h>
#include <sys/stat.h>

void *svc_init(void) {
    // Create the helper
    struct helper *h = malloc(sizeof(struct helper));

    // Make the directory where the commits will be stored
    char address[14] = "svc_commits_a";
    while(mkdir(address, 0777)) {
        // Changing the address until it reaches one that doesn't already exist
        address[12] = address[12] + 1;
    }
    // Store the directory in helper
    h->dir = malloc(sizeof(char)*14);
    if(h->dir == NULL) {
        exit(1); // An error has occurred
    }
    strcpy(h->dir, address);

    // Initialise rest of fields
    h->commits = NULL;
    h->n_commits = 0;
    h->n_branches = 1;

    // Setup the master branch
    h->branches = malloc(sizeof(struct branch*));
    if(h->branches == NULL) {
        exit(1); // An error has occurred
    }
    struct branch *master = malloc(sizeof(struct branch));
    if(master == NULL){
        exit(1); // An error has occurred
    }
    master->branch_name = malloc(sizeof(char)*7);
    if(master->branch_name == NULL) {
        exit(1); // An error has occurred
    }
    strcpy(master->branch_name, "master");
    master->head = NULL;
    master->files = NULL;
    master->n_files = 0;
    h->branches[0] = master;
    // Set current branch to master
    h->current_branch = h->branches[0];
    return h;
}

void cleanup(void *helper) {
    struct helper *h = (struct helper *)helper;

    // Remove files that were created
    char *t_arr[] = {"rm -r ", h->dir};
    char *command = str_concat(t_arr, 2);
    if(command == NULL) {
        return; // An error has occurred
    }
    if(system(command) != 0) {
        return; // An error has occurred
    }
    free(command);

    // Free the commits
    for(size_t i = 0; i < h->n_commits; i++) {
        // Free the commit message
        free(h->commits[i]->message);
        // Free the files in each commit
        for(size_t j = 0; j < h->commits[i]->n_files; j++) {
            free(h->commits[i]->files[j].file_name);
        }
        free(h->commits[i]->files);
        // Free parents array
        free(h->commits[i]->parents);
        free(h->commits[i]);
    }
    free(h->commits);

    // Free the branches
    for(size_t i = 0; i < h->n_branches; i++) {
        free(h->branches[i]->branch_name);
        // Free the files
        for(size_t j = 0; j < h->branches[i]->n_files; j++) {
            free(h->branches[i]->files[j].file_name);
        }
        free(h->branches[i]->files);
        free(h->branches[i]);
    }
    free(h->branches);

    // Free the directory string
    free(h->dir);
    // Free h
    free(h);
}

int hash_file(void *helper, char *file_path) {
    if(helper == NULL) {
        return -1; // Error
    }
    if(file_path == NULL) {
        return -1; // Error
    }
    if(access(file_path, F_OK) == -1) {
        return -2; // Cannot access == file does not exist
    }
    //File I/O
    FILE *f_ptr;
    f_ptr = fopen(file_path, "rb");
    if(f_ptr == NULL) {
        return -1; // Error occurred when opening file
    }

    // Begin the hash algorithm
    int hash = 0;
    char buff;
    // Add up the bytes in the name
    for(unsigned int i = 0; i < strlen(file_path); i++) {
        hash += (unsigned char) file_path[i];
    }
    //Calculating modulus once is same as doing each time but faster
    hash %= 1000;
    // Add up the bytes in the file
    // While we have not reached EOF or an error
    while(fread(&buff, 1, 1, f_ptr)) {
        hash += (unsigned char) buff;
    }
    hash %= 2000000000;

    fclose(f_ptr);
    return hash;
}

char *svc_commit(void *helper, char *message) {
    if(helper == NULL || message == NULL) {
        return NULL; // An error has occurred
    }
    struct helper *h = (struct helper *)helper;
    struct branch *branch = h->current_branch;

    // Update files being tracked but are no longer accessible
    check_changes(helper); // Check for files that are no longer accessible
    for(size_t i = 0; i < branch->n_files; i++) {
        char c = branch->files[i].change;
        // These files were added but are no longer accessible
        if(c == 'a') {
            branch->files[i].change = 'c'; // Mark for removal
        }
        // These files were being tracked but are no longer accessible
        if(c == 'd') {
            branch->files[i].change = 'D'; // Mark as a deletion
        }
    }

    // Check if there are changes to be committed
    if(!check_changes(helper)){
        return NULL; // No changes to be committed
    }

    // Create a new commit
    struct commit *commit = malloc(sizeof(struct commit));
    if(commit == NULL) {
        return NULL; // An error has occurred
    }
    // Copy the commit message
    commit->message = malloc(sizeof(char)*(strlen(message) + 1));
    if(commit->message == NULL) {
        return NULL; // An error has occurred
    }
    strcpy(commit->message, message);
    // Set the commit's branch to the current branch
    commit->branch = branch;

    // Need to remove files marked as deleted from tracked files after commit
    // An array to mark files for removal from tracked files
    int *rem_list = malloc(sizeof(int) * branch->n_files);
    if(rem_list == NULL) {
        return NULL; // An error has occurred
    }
    int rem_count = 0;
    for(size_t i = 0; i < branch->n_files; i++) {
        rem_list[i] = 0;
    }

    // Copy the tracked files
    commit->files = malloc(sizeof(struct tracked_file) * branch->n_files);
    if(commit->files == NULL) {
        return NULL; //An error has occurred
    }
    // For each file...
    for(size_t i = 0; i < branch->n_files; i++) {
        // ...copy the file name
        commit->files[i].file_name = malloc(sizeof(char) *
                                     (strlen(branch->files[i].file_name) + 1));
        if(commit->files[i].file_name == NULL) {
            return NULL; // An error has occurred
        }
        strcpy(commit->files[i].file_name, branch->files[i].file_name);

        // Set the change made and hash
        if(branch->files[i].change == 'A') {
            // If change is addition, find the hash
            commit->files[i].change = 'A';
            commit->files[i].hash = hash_file(helper,
                                    commit->files[i].file_name);
            // Update the tracked file to show no change
            branch->files[i].change = 'N';
            // Update the hash to the most recent one
            branch->files[i].hash = commit->files[i].hash;
        } else if(branch->files[i].change == 'D') {
            // If change is deletion, set hash to -2
            commit->files[i].change = 'D';
            commit->files[i].hash = -2;
            // Mark for removal from tracked files
            rem_list[i] = 1;
            rem_count++;
        } else if(branch->files[i].change == 'M') {
            // If change is modification, copy hash
            commit->files[i].change = 'M';
            commit->files[i].hash = branch->files[i].hash;
            // Update the tracked file to show no change
            branch->files[i].change = 'N';
        } else {
            // Otherwise file is set to no changes currently
            commit->files[i].change = 'N';
            commit->files[i].hash = branch->files[i].hash;
        }
    }

    // Set the file count
    commit->n_files = branch->n_files;

    // Remove files marked for deletion
    remove_tracked_files(branch, rem_list, rem_count);
    free(rem_list);

    // Set parents
    if(branch->head == NULL) {
        commit->parents = NULL; // No parents if no previous commits
        commit->n_parents = 0;
    } else {
        // Set parent to current commit
        commit->parents = malloc(sizeof(struct commit*));
        commit->parents[0] = branch->head;
        commit->n_parents = 1;
    }
    // Set the commit id
    set_commit_id(commit);
    // Update the branch's current commit to this one
    branch->head = commit;
    // Add commit to the commit list in helper
    struct commit **temp = realloc(h->commits,
                           sizeof(struct commit *) * (h->n_commits + 1));
    if(temp == NULL) {
        return NULL; // An error has occurred
    }
    h->commits = temp;
    h->commits[h->n_commits] = commit;
    h->n_commits++;

    // Create a snapshot of the files
    // Make a directory for the commit
    char *temp_arr[] = {h->dir, "/", commit->id};
    char *address = str_concat(temp_arr, 3);
    if(address == NULL) {
        return NULL; // An error has occurred
    }
    if(mkdir(address, 0777) != 0) {
        return NULL; // An error has occurred
    }

    // Make a copy of each file in the commit that has been changed
    for(size_t i = 0; i < commit->n_files; i++) {
        if(commit->files[i].change == 'A' || commit->files[i].change == 'M') {
            // Create a string for the shell command that copies the file...
            // ... to the new directory
            char *t_arr[] = {"cp --parents \"", commit->files[i].file_name,
                                                            "\" ", address};
            char *command = str_concat(t_arr, 4);
            if(command == NULL) {
                return NULL; // An error has occurred
            }
            // Execute the command
            if(system(command) != 0) {
                return NULL; // An error has occurred
            }
            free(command);
        }
    }
    free(address);
    return commit->id;
}

void *get_commit(void *helper, char *commit_id) {
    if(helper == NULL || commit_id == NULL) {
        return NULL; // Defensive checks
    }
    struct helper *h = (struct helper*)helper;
    // Look for the commit
    for(size_t i = 0; i < h->n_commits; i++) {
        if(strcmp(h->commits[i]->id, commit_id) == 0) {
            // Found the commit
            return h->commits[i];
        }
    }
    // Otherwise, not found
    return NULL;
}

char **get_prev_commits(void *helper, void *commit, int *n_prev) {
    if(n_prev == NULL || helper == NULL) {
        return NULL; // An error has occurred
    }
    if(commit == NULL) {
        *n_prev = 0;
        return NULL;
    }
    struct commit *c = (struct commit *)commit;
    if( c->n_parents == 0) {
        *n_prev = 0;
        return NULL;
    }
    *n_prev = c->n_parents;
    // Create the array
    char **arr = malloc(sizeof(char*)*c->n_parents);
    if(arr == NULL) {
        return NULL; // An error has occurred
    }
    // Copy a pointer to the parents id to the array
    for(size_t i = 0; i < c->n_parents; i++) {
        arr[i] = c->parents[i]->id;
    }
    return arr;
}

void print_commit(void *helper, char *commit_id) {
    struct commit *commit = get_commit(helper, commit_id);
    // Defensive checks
    if(commit == NULL) {
        puts("Invalid commit id");
        return;
    }
    printf("%s [%s]: %s\n",
            commit->id, commit->branch->branch_name, commit->message);
    char c;
    // Keep track of how many files are being tracked and not removed
    int count = 0;
    for(size_t i = 0; i < commit->n_files; i++) {
        c = commit->files[i].change;
        if(c != 'D') {
            count++;
        }
        // Substitute the change with the correct symbol
        if(c != 'N') {
            if(c == 'A') {
                c = '+';
            } else if (c == 'D') {
                c = '-';
            } else if (c == 'M') {
                c = '/';
            }
            // If change was a modification
            if(c == '/') {
                // Find previous hash
                int old_hash = 0;
                for(size_t j = 0; j < commit->parents[0]->n_files; j++) {
                    if(strcmp(commit->parents[0]->files[j].file_name,
                              commit->files[i].file_name) == 0) {
                        old_hash = commit->parents[0]->files[j].hash;
                        break;
                    }
                }
                printf("    %c %s [%10d -> %10d]\n",
                c, commit->files[i].file_name, old_hash, commit->files[i].hash);
            } else {
                printf("    %c %s\n", c, commit->files[i].file_name);
            }
        }
    }
    printf("\n    Tracked files (%d):\n", count);
    for(size_t i = 0; i < commit->n_files; i++) {
        c = commit->files[i].change;
        if(c != 'D') {
            printf("    [%10d] %s\n",
            commit->files[i].hash, commit->files[i].file_name);
        }
    }
}

int svc_branch(void *helper, char *branch_name) {
    if(branch_name == NULL) {
        return -1;
    }
    // Check if branch name is valid
    for(size_t i = 0; i < strlen(branch_name); i++) {
        char c = branch_name[i];
        // Using ascii codes to check valid characters
        if(!((c >= 97 && c <= 122) || (c >= 65 && c <= 90) ||
              (c >= 48 && c <= 57) || c == '_' || c == '/' || c == '-')) {
            return -1; // Invalid name
        }
    }
    struct helper *h = (struct helper *)helper;
    // Check if name already exists
    for(size_t i = 0; i < h->n_branches; i++) {
        if(strcmp(h->branches[i]->branch_name, branch_name) == 0) {
            return -2; // Name already exists
        }
    }
    // Check for changes to be committed
    if(check_changes(helper)) {
        return -3; // There are uncommitted changes
    }

    // Create the new branch
    struct branch *new_branch = malloc(sizeof(struct branch));
    if(new_branch == NULL) {
        return -1; // An error has occurred
    }
    // Copy the branch name
    char *name = malloc(sizeof(char) * (strlen(branch_name) + 1));
    if(name == NULL) {
        return -1; // An error has occurred
    }
    strcpy(name, branch_name);
    new_branch->branch_name = name;
    // Set the head of the new branch to the current branch's head
    new_branch->head = h->current_branch->head;
    // Create space for the tracked files
    struct tracked_file *files = malloc(sizeof(struct tracked_file)
                                        * h->current_branch->n_files);
    if(files == NULL) {
        return -1; // An error has occurred
    }
    new_branch->files = files;
    // Copy the tracked files from current branch
    for(size_t i = 0; i < h->current_branch->n_files; i++) {
        // Copy filename of each
        char *f_name = malloc(sizeof(char) *
                       (strlen(h->current_branch->files[i].file_name) + 1));
        if(f_name == NULL) {
            return -1; // An error has occurred
        }
        new_branch->files[i].file_name = f_name;
        strcpy(new_branch->files[i].file_name,
               h->current_branch->files[i].file_name);
        // Copy the hash
        new_branch->files[i].hash = h->current_branch->files[i].hash;
        // Copy the change
        new_branch->files[i].change = h->current_branch->files[i].change;
    }
    // Set number of files being tracked
    new_branch->n_files = h->current_branch->n_files;

    // Create space for the new branch
    struct branch **branches = realloc(h->branches,
                                sizeof(struct branch*) * (h->n_branches + 1));
    if(branches == NULL) {
        return -1; // An error has occurred
    }
    h->branches = branches;
    // Put the new branch into the list of branches
    h->branches[h->n_branches] = new_branch;
    h->n_branches++;
    return 0;
}

int svc_checkout(void *helper, char *branch_name) {
    if(branch_name == NULL) {
        return -1; // An error has occurred
    }
    struct helper *h = (struct helper*)helper;
    // Look for the branch
    struct branch* branch = NULL;
    for(size_t i = 0; i < h->n_branches; i++) {
        if(strcmp(h->branches[i]->branch_name, branch_name) == 0) {
            branch = h->branches[i];
            break;
        }
    }
    if(branch == NULL) {
        return -1; // Branch does not exist
    }
    if(check_changes(helper)) {
        return -2; // uncommitted changes
    }
    h->current_branch = branch;
    // Set the workspace to the last commit of the branch
    set_to_commit(helper, branch, branch->head);
    return 0;
}

char **list_branches(void *helper, int *n_branches) {
    if(n_branches == NULL) {
        return NULL; // An error has occurred
    }
    struct helper *h = (struct helper*)helper;
    *n_branches = h->n_branches;
    // Create an array to store the branch names
    char **arr = malloc(sizeof(char *)*h->n_branches);
    if(arr == NULL) {
        return NULL; // An error has occurred
    }
    // Print out each name and copy into the array
    for(size_t i = 0; i < h->n_branches; i++) {
        printf("%s\n", h->branches[i]->branch_name);
        arr[i] = h->branches[i]->branch_name;
    }
    return arr;
}

int svc_add(void *helper, char *file_name) {
    if(file_name == NULL) {
        return -1;
    }
    struct helper *h = (struct helper *)helper;
    struct branch *branch = h->current_branch;
    // Check if file is already being tracked
    for(size_t i = 0; i < branch->n_files; i++) {
        if(strcmp(branch->files[i].file_name, file_name) == 0) {
            // If marked for deletion then set to addition
            if(branch->files[i].change == 'D') {
                branch->files[i].change = 'A';
                branch->files[i].hash = hash_file(helper,
                                        branch->files[i].file_name);
                return branch->files[i].hash;
            } else {
                return -2; // Otherwise cannot add again
            }
        }
    }
    // Check if file exists
    if(access(file_name, F_OK) == -1) {
        return -3; // Cannot access == file does not exist
    }
    // Add file to list
    struct tracked_file *temp = realloc(branch->files,
                        sizeof(struct tracked_file) * (branch->n_files + 1));
    if(temp == NULL) {
        return -1; // An error has occurred
    }
    branch->files = temp;
    // Copy the file name
    char *temp_str = malloc(sizeof(char) * (strlen(file_name) + 1));
    if(temp_str == NULL) {
        return -1; // An error has occurred
    }
    branch->files[branch->n_files].file_name = temp_str;
    strcpy(branch->files[branch->n_files].file_name, file_name);
    // Set the change to add
    branch->files[branch->n_files].change = 'A';
    // Set hash
    branch->files[branch->n_files].hash = hash_file(helper, file_name);
    // Increment number of files
    branch->n_files++;
    return branch->files[branch->n_files - 1].hash;
}

int svc_rm(void *helper, char *file_name) {
    if(file_name == NULL) {
        return -1; // An error has occurred
    }
    struct helper *h = (struct helper *)helper;
    struct branch *branch = h->current_branch;
    // Check if file is already being tracked
    int found = 0;
    size_t index = 0;
    for(size_t i = 0; i < branch->n_files; i++) {
        // If it is not already being deleted and it matches the name
        if(branch->files[i].change != 'D'
        && strcmp(branch->files[i].file_name, file_name) == 0) {
            found = 1; // Found the file
            index = i;
            break;
        }
    }
    if(!found) {
        return -2; // File not currently being tracked
    }
    // Set file to be deleted
    branch->files[index].change = 'D';
    return branch->files[index].hash;
}

int svc_reset(void *helper, char *commit_id) {
    if(commit_id == NULL) {
        return -1; // An error has occurred
    }
    struct helper *h = (struct helper *)helper;
    // Check if commit exists
    struct commit *commit = NULL;
    for(size_t i = 0; i < h->n_commits; i++) {
        if(strcmp(h->commits[i]->id, commit_id) == 0) {
            commit = h->commits[i];
            break;
        }
    }
    if(commit == NULL) {
        return -2; // No commit with given id exists
    }
    // Set the workspace to the given commit
    set_to_commit(h, h->current_branch, commit);
    return 0;
}

char *svc_merge(void *helper, char *branch_name, struct resolution *resolutions, int n_resolutions) {
    // Defensive checks
    if(branch_name == NULL) {
        puts("Invalid branch name");
        return NULL;
    }
    struct helper *h = (struct helper *)helper;
    struct branch *merge_branch = NULL;
    // Find the merging branch
    for(size_t i = 0; i < h->n_branches; i++) {
        if(strcmp(h->branches[i]->branch_name, branch_name) == 0) {
            merge_branch = h->branches[i]; // Found the branch
            break;
        }
    }
    if(merge_branch == NULL) {
        puts("Branch not found");
        return NULL;
    }
    // If branches have the same name, they are the same (names are unique)
    if(strcmp(h->current_branch->branch_name, merge_branch->branch_name) == 0) {
        puts("Cannot merge a branch with itself");
        return NULL;
    }
    if(check_changes(h)) {
        puts("Changes must be committed");
        return NULL;
    }

    struct branch *branch = h->current_branch;
    // Merge tracked files list
    // Find the new size by checking how many unique file names there are
    size_t count = 0;
    char **temp_arr = malloc(sizeof(char *) *(branch->n_files +
                            merge_branch->n_files + n_resolutions));
    if(temp_arr == NULL) {
        return NULL; // An error has occurred;
    }
    for(size_t i = 0; i < branch->n_files; i++) {
        temp_arr[i] = branch->files[i].file_name;
        count++;
    }
    int conflict = 0;
    for(size_t i = 0; i < merge_branch->n_files; i++) {
        conflict = 0;
        for(size_t j = 0; j < branch->n_files; j++) {
            if(strcmp(temp_arr[j], merge_branch->files[i].file_name) == 0) {
                conflict = 1;
                break;
            }
        }
        // If the file does not conflict, then add it to thew new list
        if(!conflict) {
            temp_arr[count] = merge_branch->files[i].file_name;
            count++;
        }
    }
    free(temp_arr);
    // Resize the current branch to hold all the new files
    size_t new_size = count;
    struct tracked_file *temp = realloc(branch->files,
                                sizeof(struct tracked_file) * new_size);
    if(temp == NULL) {
        return NULL; // An error has occurred
    }
    branch->files = temp;
    // Set all the files being copied over to null initially
    for(size_t i = branch->n_files; i < new_size; i++) {
        branch->files[i].file_name = NULL;
    }
    // Copy the merging branch files except those that already exist
    int index = 0;
    for(size_t i = 0; i < merge_branch->n_files; i++) {
        char *fname = merge_branch->files[i].file_name;
        int exist = 0;
        // Check already exists
        for(size_t j = 0; j < branch->n_files; j++) {
            if(strcmp(fname, branch->files[j].file_name) == 0) {
                exist = 1;
                break;
            }
        }
        if(!exist) {
            // If does not exist, add to the new tracked files list
            char *temp_str = malloc(sizeof(char)*
                             (strlen(merge_branch->files[i].file_name) + 1));
            branch->files[index + branch->n_files].file_name = temp_str;
            strcpy(branch->files[index + branch->n_files].file_name,
                    merge_branch->files[i].file_name);
            // All changes are addition
            branch->files[index + branch->n_files].change = 'A';
            branch->files[index + branch->n_files].hash =
                merge_branch->files[i].hash;
            index++;
            // Copy the file from last commit it was copied
            struct commit *c = merge_branch->head;
            int found = 0;
            while(c != NULL && !found) {
                // Look for the last commit containing the file
                for(size_t k = 0; k < c->n_files; k++) {
                    // If the file has the same name and it was changed
                    if(strcmp(c->files[k].file_name,
                       merge_branch->files[i].file_name) == 0
                    && c->files[k].change != 'N') {
                        // Found the file
                        found = 1;
                        // Create string for the shell command to copy it
                        char *arr[] = {"cp ", h->dir, "/", c->id, "/\"",
                                       merge_branch->files[i].file_name,"\" \"",
                                       merge_branch->files[i].file_name, "\""};
                        char *command = str_concat(arr, 9);
                        if(command == NULL) {
                            return NULL; // An error has occurred
                        }
                        // Execute command
                        if(system(command) != 0) {
                            return NULL; // An error has occurred
                        }
                        free(command);
                        break;
                    }
                }
                // Otherwise set to next parent
                if(c->n_parents == 0) {
                    c = NULL;
                } else {
                    c = c->parents[0];
                }
            }

        }
    }
    // Set up an array to track files to be removed
    int *r_list = malloc(sizeof(int) * new_size);
    int r_count = 0;
    for(size_t i = 0; i < new_size; i++) {
        r_list[i] = 0;
    }
    // Resolve conflicting files
    for(int j = 0; j < n_resolutions; j++) {
        for(size_t i = 0; i < new_size; i++) {
            char *fname = branch->files[i].file_name;
            // If the file was conflicting
            if(strcmp(fname, resolutions[j].file_name) == 0) {
                // If file has a resolution file
                if(resolutions[j].resolved_file != NULL) {
                    // Replace conflicting file with resolution file
                    char *t_arr[] = {"cp \"", resolutions[j].resolved_file,
                                                      "\" \"", fname, "\""};
                    char *command = str_concat(t_arr, 5);
                    if(command == NULL) {
                        return NULL; // An error has occurred
                    }
                    if(system(command) != 0) {
                        return NULL; // An error has occurred
                    }
                    free(command);
                    // Check if the conflicting file existed in current branch
                    if(i < branch->n_files) {
                        // If it did, then change was modification
                        branch->files[i].change = 'M';
                    } else {
                        // Otherwise change was addition;
                        branch->files[i].change = 'A';
                    }

                } else {
                    // The resolution does not contain a file
                    // If the conflicting file was not being tracked in the...
                    // ... current branch,
                    if(i >= branch->n_files) {
                        r_list[i] = 1; // Mark for removal from track list
                        r_count++;
                    } else {
                        // Otherwise mark the file as deletion
                        branch->files[i].change = 'D';
                    }
                }
                break;
            }
        }
    }
    branch->n_files = new_size;
    // Remove the files that were marked for removal
    remove_tracked_files(branch, r_list, r_count);
    free(r_list);
    // Create the commit message
    char *arr[] = {"Merged branch ", branch_name};
    char *message = str_concat(arr, 2);
    if(message == NULL) {
        return NULL; // An error has occurred
    }
    // Commit the changes
    char *id = svc_commit(helper, message);
    free(message);
    struct commit *commit = (struct commit *)get_commit(helper, id);
    // Update commit parents to include merging branch
    struct commit **temp_parents = realloc(commit->parents,
                                    sizeof(struct commit *)*2);
    if(temp_parents == NULL) {
        return NULL; // An error has occurred
    }
    commit->parents = temp_parents;
    commit->parents[1] = merge_branch->head;
    commit->n_parents = 2;
    puts("Merge successful");
    return id;
}

// Helper function to separate calculating the commit id from commit function
void set_commit_id(struct commit* commit) {
    if(commit == NULL) {
        return; // Defensive checking
    }
    // The algorithm
    int id = 0;
    // Looping for the commit message
    for(size_t i = 0; i < strlen(commit->message); i++) {
        id += (unsigned char) commit->message[i];
    }
    id %= 1000; // Same as doing modulus every loop
    // Sort the array of files
    qsort(commit->files, commit->n_files, sizeof(struct tracked_file), compar);
    // Looping for changes in the commit
    for(size_t i = 0; i < commit->n_files; i++) {
        char c = commit->files[i].change;
        if(c == 'A') {
            id += 376591; // Change is addition
        } else if (c == 'D') {
            id += 85973; // Change is deletion
        } else if (c == 'M') {
            id += 9573681; // Change is modification
        }
        if(c != 'N') {
            // Loop through file name if change is not NONE
            for(size_t j = 0; j < strlen(commit->files[i].file_name); j++) {
                unsigned char k = (unsigned char) commit->files[i].file_name[j];
                id = ((id * (k % 37)) % 15485863) + 1;
            }
        }
    }
    // Put id as a hex in commit
    sprintf(commit->id, "%06x", id);
}

// Comparator for sorting strings alphabetically ignoring upper and lower case
int compar(const void *a, const void *b) {
    // Comparator for 2 tracked files
    char *a_name = ((struct tracked_file *)a)->file_name;
    char *b_name = ((struct tracked_file *)b)->file_name;
    // Find the min length of the two string and set max to it
    size_t max = 0;
    if(strlen(a_name) < strlen(b_name)) {
        max = strlen(a_name);
    } else {
        max = strlen(b_name);
    }
    // Only need to compare up to max (length of shorter string)
    for(size_t i = 0; i < max; i++){
        int a_char = a_name[i];
        int b_char = b_name[i];
        // Convert to lower case
        if(a_char >= 65 && a_char <= 90) {
            a_char += 32;
        }
        if(b_char >= 65 && b_char <= 90) {
            b_char += 32;
        }
        // Compare each character
        if(a_char < b_char) {
            return -1;
        }
        if(a_char > b_char) {
            return 1;
        }
    }
    // The strings are the same for the first (max) letters
    if(max == strlen(a_name) && max == strlen(b_name)) {
        return 0; // Strings are same length so they are equal
    } else if (max == strlen(a_name)) {
        return -1; // A is shorter so it comes first
    } else {
        return 1; // B is shorter
    }
}

// Helper function for removing files from a tracked_file list
void remove_tracked_files(struct branch *branch, int *arr, int rem_count) {
    if(rem_count == 0) {
        return; // Nothing to remove
    }
    int new_size = branch->n_files - rem_count;
    // Case where every element gets removed
    if(new_size == 0) {
        for(size_t i = 0; i < branch->n_files; i++) {
            free(branch->files[i].file_name);
        }
        free(branch->files);
        branch->files = NULL;
        branch->n_files = 0;
        return;
    }
    // Otherwise create a new list
    struct tracked_file *temp = malloc(sizeof(struct tracked_file) * new_size);
    if(temp == NULL) {
        exit(1); // An error has occurred
    }
    int count = 0;
    for(size_t i = 0; i < branch->n_files; i++) {
        if(arr[i] == 1) {
            // Free name if marked for removal
            free(branch->files[i].file_name);
        } else {
            // Copy to new list if not marked for removal
            temp[count].file_name = branch->files[i].file_name;
            temp[count].hash = branch->files[i].hash;
            temp[count].change = branch->files[i].change;
            count++;
        }
    }
    // Free the old list
    free(branch->files);
    // Update the branch to point to the new list
    branch->files = temp;
    branch->n_files = new_size;
}

// Helper function to concatenate two or more strings
char *str_concat(char ** arr, size_t n_strings) {
    if(arr == NULL || n_strings == 0) {
        return NULL; // An error has occurred
    }
    // Figure out how long the new string will be
    size_t size = 0;
    for(size_t i = 0; i < n_strings; i++) {
        size += strlen(arr[i]);
    }
    // Allocate memory for the new string including null terminator
    char *new_string = malloc(sizeof(char)*(size + 1));
    if(new_string == NULL) {
        return NULL; // An error has occurred
    }
    // Copy the old strings into the new one
    int cur = 0;
    for(size_t i = 0; i < n_strings; i++) {
        for(size_t j = 0; j < strlen(arr[i]); j++) {
            new_string[cur] = arr[i][j];
            cur++;
        }
    }
    // Add a null terminator
    new_string[size] = '\0';
    return new_string;
}

// Helper function to check for uncommitted changes
int check_changes(struct helper *helper) {
    struct helper *h = helper;
    struct branch *branch = h->current_branch;
    // If no files are currently being tracked...
    if(branch->files == NULL) {
        return 0; // ...nothing to commit
    }

    // Check if tracked files can still be accessed
    // An array to mark files for removal
    int *r_list = malloc(sizeof(int) * branch->n_files);
    int r_count = 0;
    for(size_t i = 0; i < branch->n_files; i++) {
        r_list[i] = 0;
    }
    // Make sure files have not been deleted
    for(size_t i = 0; i < branch->n_files; i++) {
        if(branch->files[i].change == 'c') {
            // File was previously awaiting addition but a commit is occurring
            r_list[i] = 1; // ... mark it for removal
            r_count++;
        }
        // If file is not already marked for delete, check if was deleted
        if(branch->files[i].change != 'D'){
            // If cannot access
            if(access(branch->files[i].file_name, F_OK) == -1) {
                // If the change was addition and now cannot be accessed...
                if(branch->files[i].change == 'A'){
                    // Mark it as awaiting addition
                    branch->files[i].change = 'a';
                } else {
                    // Otherwise, mark it as pending deletion
                    branch->files[i].change = 'd';
                }
            } else{
                // If it can be accessed
                // If change is awaiting addition
                if(branch->files[i].change == 'a'){
                    // It has now been added again
                    branch->files[i].change = 'A';
                } else if(branch->files[i].change == 'd') {
                    // If it was pending deletion but can now be accessed...
                    // ... set it to no change
                    branch->files[i].change = 'N';
                }
            }
        }
    }
    // Remove the files that were marked for removal
    remove_tracked_files(branch, r_list, r_count);
    free(r_list);
    // Check if there are any files left, awaiting addition does not count
    int no_files = 1;
    for(size_t i = 0; i < branch->n_files; i++) {
        if(branch->files[i].change != 'a') {
            no_files = 0;
            break;
        }
    }
    if(no_files) {
        return 0; // No changes to be committed
    }

    // Check if files marked as no change have been changed...
    // ... and check if files marked as changed have been reverted
    struct commit *prev = branch->head;
    if(prev != NULL) {
        for(size_t i = 0; i < branch->n_files; i++) {
            if(branch->files[i].change == 'N'
            || branch->files[i].change == 'M') {
                // Update the hash
                branch->files[i].hash = hash_file(helper,
                                        branch->files[i].file_name);
                // Compare to previous commit's hash
                for(size_t j = 0; j < prev->n_files; j++) {
                    if(strcmp(prev->files[j].file_name,
                              branch->files[i].file_name) == 0) {
                        if(prev->files[j].hash == branch->files[i].hash) {
                            branch->files[i].change = 'N';
                        } else {
                            branch->files[i].change = 'M';
                        }
                        break;
                    }
                }
            }
        }
    }

    // Check if there are any changes to commit
    int changed = 0;
    for(size_t i = 0; i < branch->n_files; i++) {
        char c = branch->files[i].change;
        // If change is not none and not awaiting addition, then it has changed
        if(c != 'N' && c != 'a') {
            changed = 1;
            break;
        }
    }
    if(!changed) {
        return 0; // No changes to commit
    }
    // Otherwise there are changes
    return 1;
}

// Helper function to set workspace to a given commit
void set_to_commit(struct helper *helper, struct branch *branch,
                                          struct commit *commit) {
    if(commit == NULL) {
        return;
    }

    // Copy the committed files into the workspace
    for(size_t i = 0; i < commit->n_files; i++) {
        // If change was a modification or addition, then restore from commit
        if(commit->files[i].change == 'M' || commit->files[i].change == 'A') {

            char *arr[] = {"cp ", helper->dir, "/", commit->id, "/\"",
                                  commit->files[i].file_name, "\" \"",
                                    commit->files[i].file_name, "\""};
            char *command = str_concat(arr, 9);
            if(command == NULL) {
                return; // An error has occurred
            }
            if(system(command) != 0) {
                return; // An error has occurred
            }
            free(command);
        }
        // If change was none, then look for previous commit to retore from
        if(commit->files[i].change == 'N') {
            // Due to the way the svc is designed, a file that has no change...
            // ... recorded will have a change or addition recorded at some ...
            // ... point in the direct history of the commit, even ...
            // ... accounting for merges and branches
            struct commit *prev;
            if(commit->parents == NULL) {
                prev = NULL;
            } else {
                prev = commit->parents[0];
            }
            int found = 0;
            // Keep looking while there is a previous parent and haven't found
            while(prev != NULL && !found) {
                for(size_t j = 0; j < prev->n_files; j++) {
                    if(strcmp(commit->files[i].file_name,
                                prev->files[j].file_name) == 0) {
                        // Found the file in a previous commit
                        // Check if a change was recorded for the file
                        char change = prev->files[j].change;
                        if(change == 'A' || change == 'M') {
                            // If an addition or modification was recorded,
                            // then a copy was stored. Restore the copy
                            char *arr[] = {"cp ", helper->dir, "/", prev->id,
                                            "/\"", prev->files[j].file_name,
                                    "\" \"", prev->files[j].file_name, "\""};
                            char *command = str_concat(arr, 9);
                            if(command == NULL) {
                                return; // An error has occurred
                            }
                            if(system(command) != 0) {
                                return; // An error has occurred
                            }
                            free(command);
                            found = 1; // Break out of the loop
                            break;
                        }
                    }
                }
                // Set prev to next
                if(prev->parents == NULL) {
                    prev = NULL;
                } else {
                    prev = prev->parents[0];
                }
            }
        }
    }

    // Restore the commit's tracked files to the branch
    int count = 0;
    for(size_t i = 0; i < commit->n_files; i++) {
        // Count number of files that were not removed after this commit
        if(commit->files[i].change != 'D') {
            count++;
        }
    }
    // Free the current files
    for(size_t i = 0; i < branch->n_files; i++) {
        free(branch->files[i].file_name);
    }
    struct tracked_file *temp = realloc(branch->files,
                                sizeof(struct tracked_file) * count);
    if(temp == NULL) {
        return; // An error has occurred
    }
    branch->files = temp;
    // Copy the files over
    int index = 0; // Track index of files being copied
    for(size_t i = 0; i < commit->n_files; i++) {
        if(commit->files[i].change != 'D') {
            // Copy the name
            char *temp_str = malloc(sizeof(char) *
                                    (strlen(commit->files[i].file_name) + 1));
            if(temp_str == NULL) {
                return; // An error has occurred
            }
            branch->files[index].file_name = temp_str;
            strcpy(branch->files[index].file_name, commit->files[i].file_name);
            // Copy the hash
            branch->files[index].hash = commit->files[i].hash;
            // Set change to none, since no changes after a commit
            branch->files[index].change = 'N';
            index++;
        }
    }
    // Update the branch and current branch
    branch->n_files = count;
    branch->head = commit;
}
