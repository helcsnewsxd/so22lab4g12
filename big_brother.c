#include "big_brother.h"
#include "fat_file.h"
#include "fat_table.h"
#include "fat_types.h"
#include "fat_util.h"
#include "fat_volume.h"
#include <stdio.h>
#include <string.h>

int bb_is_log_file_dentry(fat_dir_entry dir_entry) {
    return strncmp(LOG_FILE_BASENAME, (char *)(dir_entry->base_name), 3) == 0 &&
           strncmp(LOG_FILE_EXTENSION, (char *)(dir_entry->extension), 3) == 0;
}

int bb_is_log_filepath(char *filepath) {
    return strncmp(BB_LOG_FILE, filepath, 8) == 0;
}

int bb_is_log_dirpath(char *filepath) {
    return strncmp(BB_DIRNAME, filepath, 15) == 0;
}

/* Searches for a cluster that could correspond to the bb directory and returns
 * its index. If the cluster is not found, returns 0.
 */
static u32 get_cluster_content(fat_table table, u32 cluster) {
    return le32_to_cpu(((const le32 *)table->fat_map)[cluster]);
}

static fat_dir_entry get_first_dentry_from_cluster(fat_table table,
                                                   u32 cluster) {
    int cluster_data_fd = table[cluster].fd;
    void *buf = NULL;
    // skip two entries (first entries are are . and ..)
    off_t bytes_to_skip = FAT_DIR_ENTRY_BYTE_SIZE * 2;

    full_pread(cluster_data_fd, buf, FAT_DIR_ENTRY_BYTE_SIZE, bytes_to_skip);

    return (fat_dir_entry)buf; // turn buf into fat_dir_entry
}

static bool bb_has_log_file_as_first_entry(fat_table table, u32 cluster) {
    fat_dir_entry dir_entry = get_first_dentry_from_cluster(table, cluster);
    return bb_is_log_file_dentry(dir_entry);
}

u32 search_bb_orphan_dir_cluster(fat_table table) {
    u32 bb_dir_start_cluster = 0;
    u32 cluster = 2;
    u32 cluster_content;
    bool cluster_not_found = true;
    bool is_bad, correct_first_entry;

    while (fat_table_is_valid_cluster_number(table, cluster) &&
           cluster_not_found) {
        cluster_content = get_cluster_content(table, cluster);
        is_bad = fat_table_cluster_is_bad_sector(cluster_content);
        correct_first_entry = bb_has_log_file_as_first_entry(table, cluster);

        if (is_bad && correct_first_entry) {
            bb_dir_start_cluster = cluster;
            cluster_not_found = false;
        }
        cluster++;
    }

    return bb_dir_start_cluster;
}

/* Creates the /bb directory as an orphan and adds it to the file tree as
 * child of root dir.
 */
static int bb_init_orphan_dir(fat_volume vol, u32 bb_cluster) {
    errno = 0;
    fat_tree_node root_node = NULL;

    // Create orphan dir
    fat_file loaded_bb_dir =
        fat_file_init_orphan_dir(BB_DIRNAME, vol->table, bb_cluster);

    // Add directory to file tree. It's entries will be like any other dir.
    root_node = fat_tree_node_search(vol->file_tree, "/");
    vol->file_tree = fat_tree_insert(vol->file_tree, root_node, loaded_bb_dir);

    return -errno;
}

int bb_init_log_dir() {
    errno = 0;

    fat_volume vol = NULL;

    vol = get_fat_volume();

    u32 bb_cluster = search_bb_orphan_dir_cluster(vol->table);

    if (!bb_cluster) { // orphan dir does not exist
        bb_cluster = fat_table_get_next_free_cluster(vol->table);
        fat_table_set_next_cluster(vol->table, bb_cluster,
                                   FAT_CLUSTER_BAD_SECTOR);
    }

    bb_init_orphan_dir(vol, bb_cluster);

    return -errno;
}
