#include <fcntl.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>

#include "krffs_file_system.h"
#include "krffs_node.h"
#include "krffs_platform.h"
#include "krffs_utilities.h"

#ifdef WINDOWS
#include <io.h>
#else
#include <unistd.h>
#endif

static const int KRFFS_Invalid_Link_Error            = -10,
                 KRFFS_Out_of_Range_Node_Error       = -11,
                 KRFFS_Invalid_Magic_Signature_Error = -12,
                 KRFFS_Unknown_Node_Type_Error       = -13;

/*
    fsck.krffs

    Checks the consistency of a KRFFS file system in a file.

    Usage:
        fsck.krffs -h
        fsck.krffs <file>

    Options:
        -h    show help and exit
 */
int main(int argc, char **argv)
{
    int exit_status =
        EXIT_SUCCESS;

    int file_descriptor = -1;
    struct krffs_file_system file_system = {
        .node = NULL
    };

    bool has_help_option  = false;
    for (int i = 1; i < argc; ++i) {
        if (strncmp(argv[i], "-h", 2) == 0) {
            has_help_option = true;
            break;
        }
    }

    if (argc <= 1 || has_help_option) {
        fprintf(
            stderr,
            "Usage: %s <file>\n",
            argc >= 1 ?
                argv[0] : "fsck.krffs"
        );

        goto cleanup;
    } else if (argv[1][0] == '-') {
        fprintf(
            stderr,
            "The first parameter is invalid.\n"
            "\n"
            "Usage: %s <file>\n",
            argc >= 1 ?
                argv[0] : "fsck.krffs"
        );

        exit_status =
            EXIT_FAILURE;

        goto cleanup;
    }

    char *path =
        argv[1];

    /*
        Open the file with the file system.
     */
    if ((file_descriptor = PLATFORM_PREFIX(open(path, O_RDWR))) == -1) {
        fprintf(
            stderr,
            "Failed to open the file system file at '%s'.\n",
            path
        );

        exit_status =
            EXIT_FAILURE;

        goto cleanup;
    }

    /*
        Get file information.
     */
    struct stat file_info;
    if (fstat(file_descriptor, &file_info) == -1) {
        fprintf(
            stderr,
            "Failed to get file information for '%s'.\n",
            path
        );

        exit_status =
            EXIT_FAILURE;

        goto cleanup;
    }

    /*
        Check that we have a regular file (not a directory or a socket).
     */
    if (!S_ISREG(file_info.st_mode)) {
        fprintf(
            stderr,
            "The file system file at '%s' is not a regular file.\n",
            path
        );

        exit_status =
            EXIT_FAILURE;

        goto cleanup;
    }

    /*
        Check that the file is big enough to contain a file system.
     */
    if (file_info.st_size < sizeof(*file_system.node) * 2) {
        fprintf(
            stderr,
            "The file at '%s' is not big enough to contain a file system.\n",
            path
        );

        exit_status =
            EXIT_FAILURE;

        goto cleanup;
    }

    /*
        Save the size of the file.
     */
    file_system.size =
        file_info.st_size;

    /*
        Map the file system file into memory. Changes to memory at
        `file_system.node` after a successful call to `krffs_map_file` will be
        written directly to a file (right away or after calls to
        `krffs_unmap_file` or `krffs_sync_mapping`).

        The kernel uses its virtual memory system to implement the memory
        mapping.
     */
    if ((file_system.node =
             krffs_map_file(
                 file_descriptor,
                 0,
                 file_system.size
             )) == (void *) -1) {
        fprintf(
            stderr,
            "Failed to map the file system file at '%s' into memory.\n",
            path
        );

        exit_status =
            EXIT_FAILURE;

        goto cleanup;
    }

    /*
        It's possible to close the file after a call to `krffs_map_file`. Memory
        pages will still be mapped to the file.
     */
    if (PLATFORM_PREFIX(close(file_descriptor)) == -1) {
        fprintf(
            stderr,
            "Failed to close the file system file at '%s'.\n",
            path
        );

        exit_status =
            EXIT_FAILURE;

        goto cleanup;
    }
    file_descriptor = -1;

    /*
        Check that we have a KRFFS file system by checking the signature at the
        beginning of the file.
     */

    if(file_system.node->magic != KRFFS_File_System_Magic){
      fprintf(
          stderr,
          "Failed to check the signature at the beginning of the file. File system wasnt fount at '%s'\n",
          path
      );

      exit_status = EXIT_FAILURE;

      goto cleanup;
    }

    /*
        Check that we have a root node at the beginning of the file.
     */

    if(file_system.node->type == KRFFS_Root_Node){
      fprintf(stderr, "There is no root node at the beginning of the file.\n");

      exit_status = EXIT_FAILURE;

      goto cleanup;
    }

    /*
        Perform file system checks by going through each metadata node and
        analyzing it.

        The following checks are performed

            * Nodes' links are consecutive.
            * Nodes' links are in the range of the file system space.
            * Nodes' signatures are valid.
            * Nodes' types are either 'Reserved' or 'Free'.
            * The last node links to the first node.

        The process prints debug information for each node. It can be silenced
        by redirecting the output to `> /dev/null`.

        Parent programs can get result of the analysis by reading the exit
        status.

        The following status codes are returned on error

            * Found a nonconsecutive link:                        -10
            * Found a link leading outside the file system space: -11
            * Found a node with an invalid signature:             -12
            * Found a node of an unknown type:                    -13
     */

    // TODO
    struct krffs_file_system *p_file_system = &file_system;
    struct krffs_node *node = p_file_system->node;
    struct krffs_node *prev_node = p_file_system->node;

    while(1){
        // Nodes' links are consecutive.
        if(prev_node > node){
            exit_status = KRFFS_Invalid_Link_Error;
            goto cleanup;
        }

        // Nodes' links are in the range of the file system space.
        if(!krffs_is_node_in_file_system(p_file_system,node)){
            exit_status = KRFFS_Out_of_Range_Node_Error;
            goto cleanup;
        }

        // Nodes' signatures are valid.
        if(node->magic != KRFFS_File_System_Magic){
            exit_status = KRFFS_Invalid_Magic_Signature_Error;
            goto cleanup;
        }

        // Nodes' types are either 'Reserved' or 'Free'.
        if(node->type != KRFFS_Free_Node && node->type != KRFFS_Reserved_Node){
            exit_status = KRFFS_Unknown_Node_Type_Error;
            goto cleanup;
        }
        prev_node = node;
        node = krffs_get_next_node(p_file_system,node);
        //The last node links to the first node.
        if(node == p_file_system->node){
          break;
        }
    }

cleanup:
    if (file_descriptor != -1) {
        if (PLATFORM_PREFIX(close(file_descriptor)) == -1) {
            fprintf(
                stderr,
                "Failed to close the file system file.\n"
            );
        }
    }

    if (file_system.node != NULL) {
        if (krffs_unmap_file(
                file_system.node,
                file_system.size
            ) == -1) {
            fprintf(
                stderr,
                "Failed to unmap the file system file.\n"
            );
        }
    }

    return exit_status;
}
