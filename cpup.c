/**
 * @file cpup.c
 *
 * Copyright(c) 2016 大前良介(OHMAE Ryosuke)
 *
 * This software is released under the MIT License.
 * http://opensource.org/licenses/MIT
 *
 * @brief CPU使用率調査コマンド
 *
 * CPU全体及びコアごとの使用率と上位プロセスの表示
 *
 * @author <a href="mailto:ryo@mm2d.net">大前良介(OHMAE Ryosuke)</a>
 * @date 2016/3/21
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <dirent.h>
#include <unistd.h>
#include <errno.h>
#include "def.h"

/**
 * パス名の最大
 */
#define PATH_MAX 4096
/**
 * ラインバッファのサイズ
 */
#define LINE_BUFFER_SIZE 1024
/**
 * プロセスバッファの初期値
 */
#define INIT_PROCS 1024
/**
 * プロセス名の上限
 */
#define PR_NAME_LEN 16
/**
 * 表示するプロセス数
 */
#define DISPLAY_PROCESS_NUM 10
/**
 * 2つのポインタ値の入れ替え
 */
#define SWAP(a, b) {void *tmp = a; a = b; b = tmp;}

/**
 * statのCPUカウンタ記録用構造体
 */
typedef struct cputime_t {
  uint64_t user;       /**< ユーザーモードで消費した時間 */
  uint64_t nice;       /**< 低い優先度のユーザーモードで消費した時間 */
  uint64_t system;     /**< システムモードで消費した時間 */
  uint64_t idle;       /**< タスク待ちで消費した時間 */
  uint64_t iowait;     /**< I/O の完了待ちの時間 */
  uint64_t irq;        /**< 割り込みの処理に使った時間 */
  uint64_t softirq;    /**< ソフト割り込みの処理に使った時間 */
  uint64_t steal;      /**< 仮想環境下で他のOSに消費された時間 */
  uint64_t guest;      /**< ゲストOSの実行に消費された時間 */
  uint64_t guest_nice; /**< 低い優先度のゲストOSの実行に消費された時間 */
} cputime_t;

/**
 * /proc/[pid]/statの情報保持構造体
 */
typedef struct process_t {
  int pid;           /**< PID */
  char comm[PR_NAME_LEN];  /**< プロセス名 */
  char state;        /**< state */
  uint64_t load;     /**< 負荷の合計 */
  uint64_t utime;    /**< ユーザ時間 */
  uint64_t stime;    /**< システム時間 */
  uint64_t cutime;   /**< 子プロセスのユーザ時間 */
  uint64_t cstime;   /**< 子プロセスのシステム時間 */
  int64_t priority;  /**< プライオリティ */
  int64_t nice;      /**< nice値 */
} process_t;

/**
 * 全CPU時間格納構造体
 */
typedef struct cpu_t {
  int cpu_num;        /**< CPUの個数 */
  cputime_t *times;   /**< CPU時間 */
  int proc_num;       /**< 格納済みプロセス数 */
  int alloc_num;      /**< メモリ確保数 */
  int array_num;      /**< 配列の長さ */
  process_t **procs;  /**< プロセス情報格納配列 */
} cpu_t;

static void *xmalloc(size_t size);
static void *xrealloc(void *ptr, size_t size);
static cpu_t *new_cpu_t(int num);
static void delete_cpu_t(cpu_t *cpu);
static int comp_pid(const void *a, const void *b);
static int comp_load(const void *a, const void *b);
static uint64_t get_total(cputime_t *time);
static uint64_t get_load(cputime_t *time);
static uint64_t get_idle(cputime_t *time);
static uint64_t get_iowait(cputime_t *time);
static uint64_t get_system(cputime_t *time);
static uint64_t get_user(cputime_t *time);
static uint64_t get_irq(cputime_t *time);
static uint64_t get_guest(cputime_t *time);
static void get_diff(cputime_t *before, cputime_t *after, cputime_t *diff);
static result_t read_stat(cpu_t *cpu);
static result_t read_process(cpu_t *cpu);
static result_t read_pid_stat(process_t *proc, int pid);
static result_t parse_stat(char *line, process_t *proc);
static void show_result(cpu_t *before, cpu_t *after);
static void show_result_cpus(cpu_t *before, cpu_t *after);
static void show_result_process(uint64_t total, cpu_t *before, cpu_t *after);

/**
 * @brief malloc結果がNULLだった場合にexitする
 *
 * @param[in] size 確保サイズ
 * @return 確保された領域へのポインタ
 */
static void *xmalloc(size_t size) {
  void *p = malloc(size);
  if (p == NULL) {
    ERR("%s\n", strerror(errno));
    exit(EXIT_FAILURE);
  }
  return p;
}

/**
 * @brief realloc結果がNULLだった場合にexitする
 *
 * @param[in] ptr ポインタ
 * @param[in] size 確保サイズ
 * @return 確保された領域へのポインタ
 */
static void *xrealloc(void *ptr, size_t size) {
  void *p = realloc(ptr, size);
  if (p == NULL) {
    ERR("%s\n", strerror(errno));
    exit(EXIT_FAILURE);
  }
  return p;
}

/**
 * @brief 全CPU時間格納構造体の初期化を行う
 *
 * @param[in] num CPUの個数
 * @return 全CPU時間格納構造体
 */
static cpu_t *new_cpu_t(int num) {
  cpu_t *cpu;
  cpu = xmalloc(sizeof(cpu_t));
  cpu->cpu_num = num;
  cpu->times = xmalloc((num + 1) * sizeof(cputime_t));
  cpu->proc_num = 0;
  cpu->array_num = INIT_PROCS;
  cpu->procs = xmalloc(INIT_PROCS * sizeof(process_t*));
  return cpu;
}

/**
 * @brief 全CPU時間格納構造体の開放を行う
 *
 * @param[in] cpu 開放する構造体
 */
static void delete_cpu_t(cpu_t *cpu) {
  int i;
  if (cpu == NULL) {
    return;
  }
  for (i = 0; i < cpu->alloc_num; i++) {
    free(cpu->procs[i]);
  }
  free(cpu->procs);
  free(cpu->times);
  free(cpu);
}

/**
 * @brief 次のプロセス構造体が利用できるようにする
 *
 * proc_numの位置がメモリ確保された状態であることを担保する。
 *
 * @param[out] cpu 確保する構造体
 */
static void ensure_next_proc(cpu_t *cpu) {
  if (cpu->array_num == cpu->proc_num) {
    cpu->array_num *= 2;
    cpu->procs = xrealloc(cpu->procs, sizeof(process_t*) * cpu->array_num);
  }
  if (cpu->alloc_num == cpu->proc_num) {
    cpu->procs[cpu->alloc_num] = xmalloc(sizeof(process_t));
    cpu->alloc_num++;
  }
}

/**
 * @brief pid昇順ソート用比較関数
 *
 * @param[in] a 比較対象
 * @param[in] b 比較対象
 * @return a < b の時負、a == b の時0、a > b の時正
 */
static int comp_pid(const void *a, const void *b) {
  return (*(process_t**)a)->pid - (*(process_t**)b)->pid;
}

/**
 * @brief 負荷降順ソート用比較関数
 *
 * @param[in] a 比較対象
 * @param[in] b 比較対象
 * @return a > b の時負、a == b の時0、a < b の時正
 */
static int comp_load(const void *a, const void *b) {
  return (*(process_t**)b)->load - (*(process_t**)a)->load;
}

/**
 * @brief カウンタの合計を返す
 *
 * @param[in] time cputime_t構造体
 * @return カウンタの合計値
 */
static uint64_t get_total(cputime_t *time) {
  return time->user + time->nice + time->system
      + time->idle + time->iowait + time->irq + time->softirq
      + time->steal + time->guest + time->guest_nice;
}
/**
 * @brief 負荷のカウンタの合計を返す
 *
 * @param[in] time cputime_t構造体
 * @return 負荷のカウンタの合計値
 */
static uint64_t get_load(cputime_t *time) {
  return time->user + time->nice + time->system
      + time->irq + time->softirq
      + time->steal + time->guest + time->guest_nice;
}

/**
 * @brief アイドルのカウンタの合計を返す
 *
 * @param[in] time cputime_t構造体
 * @return アイドルのカウンタの合計値
 */
static uint64_t get_idle(cputime_t *time) {
  return time->idle + time->iowait;
}

/**
 * @brief I/O待ちのカウンタの合計を返す
 *
 * @param[in] time cputime_t構造体
 * @return I/O待ちのカウンタの合計値
 */
static uint64_t get_iowait(cputime_t *time) {
  return time->iowait;
}

/**
 * @brief systemのカウンタの合計を返す
 *
 * @param[in] time cputime_t構造体
 * @return systemのカウンタの合計値
 */
static uint64_t get_system(cputime_t *time) {
  return time->system;
}

/**
 * @brief userのカウンタの合計を返す
 *
 * @param[in] time cputime_t構造体
 * @return userのカウンタの合計値
 */
static uint64_t get_user(cputime_t *time) {
  return time->user + time->nice;
}

/**
 * @brief 割り込みのカウンタの合計を返す
 *
 * @param[in] time cputime_t構造体
 * @return 割り込みのカウンタの合計値
 */
static uint64_t get_irq(cputime_t *time) {
  return time->irq + time->softirq;
}

/**
 * @brief ゲストのカウンタの合計を返す
 *
 * @param[in] time cputime_t構造体
 * @return ゲストのカウンタの合計値
 */
static uint64_t get_guest(cputime_t *time) {
  return time->guest + time->guest_nice;
}

/**
 * @brief cputime_t間の差分を取る
 *
 * @param[in] before 時間的に前の値
 * @param[in] after 時間的に後の値
 * @param[out] diff 差分の格納先
 */
static void get_diff(cputime_t *before, cputime_t *after, cputime_t *diff) {
  uint64_t tmp;
  // 負の値の場合は0とする
#define DIFF(x) {tmp = after->x - before->x; diff->x = tmp < 0 ? 0 : tmp;}
  DIFF(user);
  DIFF(nice);
  DIFF(system);
  DIFF(idle);
  DIFF(iowait);
  DIFF(irq);
  DIFF(softirq);
  DIFF(steal);
  DIFF(guest);
  DIFF(guest_nice);
#undef DIFF
}

/**
 * @brief statの値を読みだす
 *
 * @param[out] cpu 結果の書き込み先
 * @return 成功：SUCCESS / 失敗：FAILURE
 */
static result_t read_stat(cpu_t *cpu) {
  result_t result = FAILURE;
  cputime_t *work;
  FILE *file;
  char line[LINE_BUFFER_SIZE];
  file = fopen("/proc/stat", "rb");
  if (file == NULL) {
    ERR("%s\n", strerror(errno));
    return FAILURE;
  }
  if (fgets(line, sizeof(line), file) == NULL) {
    ERR("%s\n", strerror(errno));
    goto error;
  }
  memset(cpu->times, 0, sizeof(cputime_t) * cpu->cpu_num);
  work = &cpu->times[cpu->cpu_num];
  if (sscanf(line, "cpu %lu %lu %lu %lu %lu %lu %lu %lu %lu %lu",
             &work->user,
             &work->nice,
             &work->system,
             &work->idle,
             &work->iowait,
             &work->irq,
             &work->softirq,
             &work->steal,
             &work->guest,
             &work->guest_nice) < 4) {
    ERR("%s\n", strerror(errno));
    goto error;
  }
  if (cpu->cpu_num > 1) {
    int i;
    for (i = 0; i < cpu->cpu_num; i++) {
      if (fgets(line, sizeof(line), file) == NULL) {
        ERR("%s\n", strerror(errno));
        goto error;
      }
      work = &cpu->times[i];
      if (sscanf(line, "cpu%*u %lu %lu %lu %lu %lu %lu %lu %lu %lu %lu",
                 &work->user,
                 &work->nice,
                 &work->system,
                 &work->idle,
                 &work->iowait,
                 &work->irq,
                 &work->softirq,
                 &work->steal,
                 &work->guest,
                 &work->guest_nice) < 4) {
        ERR("%s\n", strerror(errno));
        goto error;
      }
    }
  }
  result = SUCCESS;
  error:
  fclose(file);
  return result;
}

/**
 * @brief プロセス情報の読み出し
 *
 * @param[out] cpu 結果の書き込み先
 * @return 成功：SUCCESS / 失敗：FAILURE
 */
static result_t read_process(cpu_t *cpu) {
  DIR *dir;
  struct dirent *dent;
  cpu->proc_num = 0;
  dir = opendir("/proc");
  if (dir == NULL) {
    ERR("%s\n", strerror(errno));
    return FAILURE;
  }
  while ((dent = readdir(dir)) != NULL) {
    if(dent->d_name[0] >= '0' && dent->d_name[0] <= '9') {
      int pid = atoi(dent->d_name);
      ensure_next_proc(cpu);
      process_t *process = cpu->procs[cpu->proc_num];
      if (read_pid_stat(process, pid) == SUCCESS) {
        cpu->proc_num++;
      }
    }
  }
  qsort(cpu->procs, cpu->proc_num, sizeof(process_t*), comp_pid);
  closedir(dir);
  return SUCCESS;
}

/**
 * @brief 指定PIDのプロセス情報を読みだす
 *
 * @param[out] proc 結果の書き込み先
 * @param[in]  pid  PID
 * @return 成功：SUCCESS / 失敗：FAILURE
 */
static result_t read_pid_stat(process_t *proc, int pid) {
  result_t result = FAILURE;
  char path[PATH_MAX];
  FILE *file;
  char line[LINE_BUFFER_SIZE];
  snprintf(path, sizeof(path), "/proc/%d/stat", pid);
  file = fopen(path, "rb");
  if (file == NULL) {
    ERR("%s: %s\n", path, strerror(errno));
    goto error;
  }
  if (fgets(line, sizeof(line), file) == NULL) {
    ERR("%s\n", strerror(errno));
    goto error;
  }
  memset(proc, 0, sizeof(process_t));
  proc->pid = pid;
  result = parse_stat(line, proc);
  error:
  fclose(file);
  return result;
}

/**
 * @brief statの情報をパースする
 *
 * @param[in]  line statを読みだした内容
 * @param[out] proc 結果の書き込み先
 * @return 成功：SUCCESS / 失敗：FAILURE
 */
static result_t parse_stat(char *line, process_t *proc) {
  char *tmp;
  int len;
  int n;
  line = strchr(line, '(') + 1;
  tmp = strrchr(line, ')');
  len = tmp - line;
  if (len >= sizeof(proc->comm)) {
    len = sizeof(proc->comm) - 1;
  }
  memcpy(proc->comm, line, len);
  proc->comm[len] = 0;
  line = tmp + 2;
  n = sscanf(line,
             "%c" // state
             "%*d %*d %*d %*d %*d " // ppid pgid sid tty_nr tty_pgrp
             "%*u %*u %*u %*u %*u " // flags min_flt cmin_flt maj_flt cmaj_flt
             "%lu %lu %lu %lu " // utime stime cutime cstime
             "%ld %ld " // priority nice
             //"%ld " // num_threads
             //"%lu %lu %lu %lu %lu " // itrealvalue starttime vsize rss rsslim
             //"%lu %lu %lu " // start_code end_code start_stack
             //"%lu %lu " // kstkesp kstkeip
             //"%lu %lu %lu %lu %lu " // signal blocked sigignore sigcatch wchan
             //"%lu %lu " // nswap cnswap
             //"%ld %ld " // exit_signal processor
             //"%lu %lu %lu " // rt_priority policy delayacct_blkio_ticks
             //"%lu %ld " // guest_time cguest_time
             ,
             &proc->state,
             &proc->utime,
             &proc->stime,
             &proc->cutime,
             &proc->cstime,
             &proc->priority,
             &proc->nice
  );
  if (n != 7) {
    return FAILURE;
  }
  return SUCCESS;
}

/**
 * @brief 結果表示
 *
 * @param[in] before 時間的に前の値
 * @param[in] after  時間的に後の値
 */
static void show_result(cpu_t *before, cpu_t *after) {
  cputime_t diff;
  int num = before->cpu_num;
  get_diff(&before->times[num], &after->times[num], &diff);
  uint64_t total  = get_total(&diff);
  uint64_t load   = get_load(&diff);
  uint64_t idle   = get_idle(&diff);
  uint64_t iowait = get_iowait(&diff);
  uint64_t system = get_system(&diff);
  uint64_t user   = get_user(&diff);
  uint64_t irq    = get_irq(&diff);
  uint64_t guest  = get_guest(&diff);
  if (total == 0) {
    total = 1;
  }
  float usage = (float) load / total * 100;
  printf("%5.1f%% (T:%4lu I:%4lu IO:%4lu S:%4lu U:%4lu IRQ:%4lu G:%4lu)",
         usage, total, idle, iowait, system, user, irq, guest);
  if (num > 1) {
    show_result_cpus(before, after);
  }
  printf("\n");
  show_result_process(total, before, after);
}

/**
 * @brief CPUコアごとの使用率の表示
 *
 * @param[in] before 時間的に前の値
 * @param[in] after  時間的に後の値
 */
static void show_result_cpus(cpu_t *before, cpu_t *after) {
  cputime_t diff;
  int i;
  int num = before->cpu_num;
  uint64_t total;
  uint64_t load;
  for (i = 0; i < num; i++) {
    get_diff(&before->times[i], &after->times[i], &diff);
    total  = get_total(&diff);
    load   = get_load(&diff);
    if (total == 0) {
      total = 1;
    }
    printf("%5.1f%%", (float) load / total * 100);
  }
}

/**
 * @brief プロセス情報を表示する
 *
 * @param[in] total  カウンタの合計値
 * @param[in] before 時間的に前の値
 * @param[in] after  時間的に後の値
 */
static void show_result_process(uint64_t total, cpu_t *before, cpu_t *after) {
  int i, j;
  int num = after->proc_num;
  int display;
  process_t **list = xmalloc(sizeof(process_t*) * num);
  for (i = 0, j = 0; i < num; i++) {
    process_t *a = after->procs[i];
    process_t *b = NULL;
    list[i] = a;
    a->load = a->utime + a->stime;
    for (;j < before->proc_num && before->procs[j]->pid < a->pid; j++);
    if (j < before->proc_num) {
      b = before->procs[j];
      if(a->pid == b->pid) {
        a->load -= (b->utime + b->stime);
      }
    }
  }
  qsort(list, num, sizeof(process_t*), comp_load);
  display = num < DISPLAY_PROCESS_NUM ? num : DISPLAY_PROCESS_NUM;
  printf("%d processes\n", num);
  printf("  PID  PR  NI S    CPU  CNT COMMAND\n");
  for (i = 0; i < display; i++) {
    char prio[4];
    if (list[i]->priority > 999 || list[i]->priority < -99) {
      snprintf(prio, sizeof(prio), " rt");
    } else {
      snprintf(prio, sizeof(prio), "%3ld", list[i]->priority);
    }
    printf("%5d %s %3ld %c %5.1f%% %4lu %s\n",
           list[i]->pid,
           prio,
           list[i]->nice,
           list[i]->state,
           (float) list[i]->load / total * 100,
           list[i]->load,
           list[i]->comm);
  }
  free(list);
  printf("\n");
}

int main(int argc, char **argv) {
  int result = EXIT_FAILURE;
  cpu_t *after = NULL;
  cpu_t *before = NULL;
  int num = sysconf(_SC_NPROCESSORS_ONLN);
  after = new_cpu_t(num);
  before = new_cpu_t(num);
  if (read_stat(before) != SUCCESS
      || read_process(before) != SUCCESS) {
    goto error;
  }
  while (TRUE) {
    sleep(5);
    if (read_stat(after) != SUCCESS
        || read_process(after) != SUCCESS) {
      goto error;
    }
    show_result(before, after);
    SWAP(before, after);
  }
  result = EXIT_SUCCESS;
  error:
  delete_cpu_t(before);
  delete_cpu_t(after);
  return result;
}
