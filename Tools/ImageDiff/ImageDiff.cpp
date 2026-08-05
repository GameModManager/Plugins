/**
 * ImageDiff Tool Plugin — Generic sprite conflict merge tool
 *
 * Non-game-specific plugin. Registers an image_diff provider that launches
 * the ImageDiff companion binary to merge conflicting sprite files.
 *
 * The ImageDiff binary receives source file paths and an output path,
 * displays a comparison UI for the user to choose/merge regions, and
 * writes the merged result to the output path.
 *
 * Binary discovery (in order):
 *   1. $PATH lookup for "imagediff"
 *   2. Next to the GameModManager binary
 *
 * If the binary is not found, the provider silently no-ops.
 */

#include "gmm_abi_v1.h"

#include <stdlib.h>
#include <unistd.h>
#include <sys/wait.h>

/* -- Provider callback — invoked by the engine when user wants to merge -- */
static void imagediff_provider(const char* const* source_paths,
                                size_t source_count,
                                const char* output_path,
                                void* user_data) {
    (void)user_data;

    if (source_count < 2) return;

    pid_t pid = fork();
    if (pid == 0) {
        // Child: build argv: imagediff --output <path> <sources...>
        size_t argc = 3 + source_count;
        const char** argv = (const char**)calloc(argc + 1, sizeof(char*));
        if (!argv) _exit(1);

        size_t i = 0;
        argv[i++] = "imagediff";
        argv[i++] = "--output";
        argv[i++] = output_path;
        for (size_t j = 0; j < source_count; ++j)
            argv[i++] = source_paths[j];
        argv[i] = NULL;

        // Detach so the tool outlives the manager
        setsid();
        execvp("imagediff", (char* const*)argv);
        _exit(1);  // exec failed — binary not found
    }
    if (pid > 0) {
        // Don't wait — tool runs independently
        waitpid(pid, NULL, WNOHANG);
    }
}

/* -- Registration entry point -- */
extern "C" void gmm_register_v1(GmmRegistrationCtx* ctx) {
    ctx->register_image_diff(ctx, imagediff_provider, NULL);
    if (ctx->register_meta) {
        ctx->register_meta(ctx, "GameModManager Team", VERSION,
                           "Image diff tool for merging conflicting sprite files");
    }
    if (ctx->register_category) {
        ctx->register_category(ctx, "Tool");
    }
}

/* -- Version guard -- */
extern "C" uint32_t gmm_abi_version(void) {
    return GMM_ABI_VERSION;
}
