#define _GNU_SOURCE
#include <assert.h>
#include <errno.h>
#include <sched.h>
#include <stdio.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <sys/wait.h>
#include <unistd.h>

int main(int argc, char **argv)
{
    assert(argc == 2);
    const char *target = "/dev/konoha-serial-check";
    assert(!unshare(CLONE_NEWNS));
    assert(!mount(NULL, "/", NULL, MS_PRIVATE | MS_REC, NULL));
    assert(!mkdir(target, 0755) || errno == EEXIST);
    assert(!mount(argv[1], target, "none", MS_BIND, NULL));
    struct statvfs fs;
    assert(!statvfs(target, &fs));
    printf("before bind-remount flags=%lx\n", fs.f_flag);
    assert(fs.f_flag & ST_NOSUID);
    assert(!mount(target, target, "none", MS_REMOUNT | MS_BIND | MS_RDONLY, NULL));
    assert(!statvfs(target, &fs));
    printf("after bind-remount flags=%lx\n", fs.f_flag);
    assert((fs.f_flag & ST_RDONLY) && !(fs.f_flag & ST_NOSUID));
    fflush(stdout);
    pid_t pid = fork();
    assert(pid >= 0);
    if (!pid) {
        execl("/system/bin/runcon", "runcon", "u:r:vendor_modprobe:s0",
              "/dev/konoha-serial-check/policy-probe.sh", NULL);
        _exit(127);
    }
    int status;
    assert(waitpid(pid, &status, 0) == pid);
    assert(!umount(target));
    assert(!rmdir(target));
    assert(WIFEXITED(status) && WEXITSTATUS(status) == 0);
    puts("PRIVATE_MOUNT_VALIDATION_PASS");
    return 0;
}
