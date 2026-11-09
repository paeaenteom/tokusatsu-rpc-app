// autostart.c 점검 — 시험용 이름(…TEST)으로 컴파일해 진짜 부팅 항목은 안 건드린다
//  명령: task-on task-off task-exists run-on run-off run-exists installed  → 결과 숫자 한 줄
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include "../src/app/autostart.h"
int main(int argc, char **argv) {
    for (int i = 1; i < argc; i++) {
        const char *c = argv[i];
        int r = !strcmp(c, "task-on") ? as_task_set(1) : !strcmp(c, "task-off") ? as_task_set(0) : !strcmp(c, "task-exists") ? as_task_exists()
              : !strcmp(c, "run-on") ? as_run_set(1) : !strcmp(c, "run-off") ? as_run_set(0) : !strcmp(c, "run-exists") ? as_run_exists()
              : !strcmp(c, "installed") ? as_installed() : -1;
        printf("%s %d\n", c, r);
    }
    return 0;
}
